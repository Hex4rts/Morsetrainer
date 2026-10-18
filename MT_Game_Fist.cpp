#include "MT_Game_Fist.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Score.h"
#include <lvgl.h>
#include <math.h>

// ============================================================================
//  Fist Check — send anything, get a live read-out of your keying timing.
//
//  Reads every key edge from the keyer's timestamped edge log, then measures:
//    straight key: dit steadiness, dah:dit ratio, element / letter / word gaps
//    iambic:       letter / word gaps only (elements are machine-timed)
//  All gaps are expressed in dit units against the ideal 1 / 3 / 7.
// ============================================================================

#define C_BG       lv_color_hex(0x000000)
#define C_SURFACE  lv_color_hex(0x1A1A1A)
#define C_BORDER   lv_color_hex(0x333333)
#define C_GREEN    lv_color_hex(0x00E676)
#define C_AMBER    lv_color_hex(0xFFB300)
#define C_RED      lv_color_hex(0xFF3D00)
#define C_WHITE    lv_color_hex(0xFFFFFF)
#define C_MUTED    lv_color_hex(0x666666)

#define FC_N          64      // samples kept (rolling window)
#define FC_TICK_MS    100
#define FC_MIN_ELEMS  8       // elements needed before scoring
#define FC_SAVE_ELEMS 40      // elements needed for a score to count as a best
#define FC_MAX_GAP_MS 4000    // longer pauses are not spacing, just a break
#define DEV_RANGE     0.7f    // |log2(measured/ideal)| at which a metric scores 0

enum { R_SPEED, R_DIT, R_RATIO, R_EGAP, R_CGAP, R_WGAP, R_COUNT };
static const char* const rowName[R_COUNT]  = {"SPEED", "DIT EVEN", "DAH : DIT", "ELEMENT GAP", "LETTER GAP", "WORD GAP"};
static const char* const rowIdeal[R_COUNT] = {"", "< 10%", "3.0", "1.0", "3.0", "7.0"};

static lv_obj_t*   scr       = NULL;
static lv_obj_t*   textLbl   = NULL;
static lv_obj_t*   valLbl[R_COUNT]  = {};
static lv_obj_t*   gaugeDot[R_COUNT] = {};
static lv_obj_t*   scoreLbl  = NULL;
static lv_obj_t*   tipLbl    = NULL;
static lv_obj_t*   countLbl  = NULL;
static lv_timer_t* tickTmr   = NULL;
static bool        active    = false;

// Samples
static uint16_t downs[FC_N], gaps[FC_N];
static uint8_t  downN, downW, gapN, gapW;
static uint32_t elemTotal;
static bool     haveDown, haveUp;
static uint32_t downT, upT;

// Decoded text (keyer callback -> ring -> tick)
static volatile char chRing[16];
static volatile uint8_t chW = 0, chR = 0;
static char text[40];

static int lastScore = -1;

static void game_char(char c) {
  chRing[chW % 16] = c; chW = chW + 1;
  UI_PushDecodedChar(c);
}

static void resetSamples(void) {
  downN = downW = gapN = gapW = 0;
  elemTotal = 0;
  haveDown = haveUp = false;
  text[0] = '\0';
  lastScore = -1;
  Keyer_FlushEdges();
}

static void push(uint16_t* buf, uint8_t* n, uint8_t* w, uint16_t v) {
  buf[*w] = v;
  *w = (*w + 1) % FC_N;
  if (*n < FC_N) (*n)++;
}

static void drainEdges(void) {
  bool dn; uint32_t t;
  while (Keyer_PopEdge(&dn, &t)) {
    if (dn) {
      if (haveUp) {
        uint32_t g = (t - upT) & 0x7FFFFFFF;
        if (g > 0 && g < FC_MAX_GAP_MS) push(gaps, &gapN, &gapW, g);
      }
      haveUp = false; haveDown = true; downT = t;
    } else {
      if (haveDown) {
        uint32_t d = (t - downT) & 0x7FFFFFFF;
        if (d >= 8 && d < 3000) { push(downs, &downN, &downW, d); elemTotal++; }
      }
      haveDown = false; haveUp = true; upT = t;
    }
  }
}

// ── Analysis ────────────────────────────────────────────────────────────────
typedef struct {
  bool  ok;
  bool  straight;
  float u;                     // dit unit (ms)
  float val[R_COUNT];          // < 0 = not available
  float dev[R_COUNT];          // signed log2 deviation from ideal, for gauges
  bool  hasDev[R_COUNT];
  int   score;                 // -1 = not enough data
  const char* tip;
} fist_t;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static fist_t analyse(void) {
  fist_t f = {};
  f.straight = (Keyer_GetMode() == KEYER_STRAIGHT);
  for (int i = 0; i < R_COUNT; i++) f.val[i] = -1;
  f.score = -1;
  f.tip = "Send a few words, e.g. PARIS PARIS";
  if (downN < FC_MIN_ELEMS) return f;

  if (f.straight) {
    // Split key-down times into dits and dahs with a 1-D two-means
    float lo = 1e9, hi = 0, sum = 0;
    for (int i = 0; i < downN; i++) { lo = fminf(lo, downs[i]); hi = fmaxf(hi, downs[i]); sum += downs[i]; }
    float thr;
    if (hi < lo * 1.8f) {
      // One cluster only: decide which with the keyer's own reference dit
      thr = (sum / downN < Keyer_GetSKDitAvg() * 2.0f) ? hi + 1 : lo - 1;
    } else {
      thr = (lo + hi) / 2;
      for (int it = 0; it < 8; it++) {
        float sd = 0, sh = 0; int nd = 0, nh = 0;
        for (int i = 0; i < downN; i++) {
          if (downs[i] < thr) { sd += downs[i]; nd++; } else { sh += downs[i]; nh++; }
        }
        if (!nd || !nh) break;
        thr = (sd / nd + sh / nh) / 2;
      }
    }
    float sd = 0, sh = 0, sq = 0; int nd = 0, nh = 0;
    for (int i = 0; i < downN; i++) {
      if (downs[i] < thr) { sd += downs[i]; nd++; } else { sh += downs[i]; nh++; }
    }
    float ditMean = nd ? sd / nd : 0, dahMean = nh ? sh / nh : 0;
    for (int i = 0; i < downN; i++)
      if (downs[i] < thr) sq += (downs[i] - ditMean) * (downs[i] - ditMean);
    f.u = nd ? ditMean : dahMean / 3.0f;
    if (nd >= 3) {
      float cv = sqrtf(sq / nd) / ditMean;
      f.val[R_DIT] = cv;
      f.dev[R_DIT] = fmaxf(0, cv - 0.05f) * 2.0f;   // 10% -> 0.1, 25% -> 0.4
      f.hasDev[R_DIT] = true;
    }
    if (nd && nh) {
      f.val[R_RATIO] = dahMean / ditMean;
      f.dev[R_RATIO] = log2f(f.val[R_RATIO] / 3.0f);
      f.hasDev[R_RATIO] = true;
    }
  } else {
    f.u = Keyer_GetDitMs();
  }
  if (f.u < 5) return f;
  f.ok = true;
  f.val[R_SPEED] = 1200.0f / f.u;

  // Gaps in dit units, sorted into element / letter / word with a 3-means in
  // log space seeded at the ideal 1 / 3 / 7. Fixed cut-offs misread a cramped
  // fist: letter gaps of 1.9 dits would count as element gaps and the read-out
  // would claim the letters were too FAR apart. Gaps over 14 dits are pauses.
  float lg[FC_N]; int nl = 0;
  for (int i = 0; i < gapN; i++) {
    float g = gaps[i] / f.u;
    if (g > 0 && g < 14.0f) lg[nl++] = logf(g);
  }
  float ctr[3] = {0.0f, logf(3.0f), logf(7.0f)};
  float csum[3]; int cnt[3];
  for (int it = 0; it < 10; it++) {
    for (int k = 0; k < 3; k++) { csum[k] = 0; cnt[k] = 0; }
    for (int i = 0; i < nl; i++) {
      int best = 0;
      for (int k = 1; k < 3; k++) if (fabsf(lg[i] - ctr[k]) < fabsf(lg[i] - ctr[best])) best = k;
      csum[best] += lg[i]; cnt[best]++;
    }
    for (int k = 0; k < 3; k++) if (cnt[k]) ctr[k] = csum[k] / cnt[k];
  }
  const float ideal[R_COUNT] = {0, 0, 3.0f, 1.0f, 3.0f, 7.0f};
  if (f.straight && cnt[0] >= 3) f.val[R_EGAP] = expf(ctr[0]);   // iambic: machine-timed
  if (cnt[1] >= 2)               f.val[R_CGAP] = expf(ctr[1]);
  if (cnt[2] >= 1)               f.val[R_WGAP] = expf(ctr[2]);
  for (int r = R_EGAP; r <= R_WGAP; r++) {
    if (f.val[r] < 0) continue;
    f.dev[r] = log2f(f.val[r] / ideal[r]);
    f.hasDev[r] = true;
  }

  // Score: mean of per-metric scores, worst metric drives the tip
  float total = 0; int n = 0; int worst = -1; float worstS = 2;
  for (int r = R_DIT; r < R_COUNT; r++) {
    if (!f.hasDev[r]) continue;
    float s = clampf(1.0f - fabsf(f.dev[r]) / DEV_RANGE, 0, 1);
    total += s; n++;
    if (s < worstS) { worstS = s; worst = r; }
  }
  if (n >= 2) f.score = (int)lroundf(100.0f * total / n);

  if (worst < 0 || worstS > 0.85f) {
    f.tip = (n >= 2) ? "Clean fist - nice sending!" : "Keep sending...";
  } else {
    bool high = f.dev[worst] > 0;
    switch (worst) {
      case R_DIT:   f.tip = "Keep your dits an even length"; break;
      case R_RATIO: f.tip = high ? "Dahs too long - aim for 3 dits" : "Hold your dahs longer (3 dits)"; break;
      case R_EGAP:  f.tip = high ? "Tighten the gaps inside letters" : "Leave a dit of space between elements"; break;
      case R_CGAP:  f.tip = high ? "Letters are too far apart" : "Leave more space between letters"; break;
      case R_WGAP:  f.tip = high ? "Words are too far apart" : "Leave more space between words"; break;
    }
  }
  return f;
}

// ── Display ─────────────────────────────────────────────────────────────────
#define GAUGE_X   224
#define GAUGE_W   88
#define ROW_Y0    64
#define ROW_DY    20

static lv_color_t devColor(float d) {
  d = fabsf(d);
  return d < 0.2f ? C_GREEN : (d < 0.4f ? C_AMBER : C_RED);
}

static void refresh(void) {
  fist_t f = analyse();
  char buf[48];

  for (int r = 0; r < R_COUNT; r++) {
    float v = f.val[r];
    if (r == R_SPEED)            snprintf(buf, sizeof(buf), v < 0 ? "--" : "%.0f wpm", v);
    else if (!f.straight && r <= R_EGAP) snprintf(buf, sizeof(buf), "auto");
    else if (v < 0)              snprintf(buf, sizeof(buf), "--");
    else if (r == R_DIT)         snprintf(buf, sizeof(buf), "%.0f%%", v * 100);
    else                         snprintf(buf, sizeof(buf), "%.1f", v);
    lv_label_set_text(valLbl[r], buf);

    if (!gaugeDot[r]) continue;
    if (f.hasDev[r]) {
      float d = clampf(f.dev[r], -DEV_RANGE, DEV_RANGE);
      int32_t cx = GAUGE_X + GAUGE_W / 2 + (int32_t)(d / DEV_RANGE * (GAUGE_W / 2 - 4));
      lv_obj_set_pos(gaugeDot[r], cx - 4, ROW_Y0 + r * ROW_DY + 5);
      lv_obj_set_style_bg_color(gaugeDot[r], devColor(f.dev[r]), 0);
      lv_obj_clear_flag(gaugeDot[r], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(gaugeDot[r], LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (f.score >= 0) {
    snprintf(buf, sizeof(buf), "%d", f.score);
    lv_obj_set_style_text_color(scoreLbl, f.score >= 80 ? C_GREEN : (f.score >= 60 ? C_AMBER : C_RED), 0);
  } else {
    snprintf(buf, sizeof(buf), "--");
    lv_obj_set_style_text_color(scoreLbl, C_MUTED, 0);
  }
  lv_label_set_text(scoreLbl, buf);
  lv_label_set_text(tipLbl, f.tip);
  snprintf(buf, sizeof(buf), "%lu elements   gaps in dits", (unsigned long)elemTotal);
  lv_label_set_text(countLbl, buf);
  lastScore = f.score;
}

static void tick_cb(lv_timer_t* t) {
  if (!active) return;
  drainEdges();

  bool textChanged = false;
  while (chR != chW) {
    char c = chRing[chR % 16]; chR = chR + 1;
    size_t n = strlen(text);
    if (c == ' ' && (n == 0 || text[n - 1] == ' ')) continue;
    if (n >= sizeof(text) - 1) { memmove(text, text + 1, n); n--; }
    text[n] = c; text[n + 1] = '\0';
    textChanged = true;
  }
  if (textChanged) lv_label_set_text(textLbl, text);

  static uint8_t div = 0;
  if (++div >= 3) { div = 0; refresh(); }   // analysis at ~3 Hz
}

// ── Screen ──────────────────────────────────────────────────────────────────
static void exit_cb(lv_event_t* e)  { Game_Fist_Stop(); }
static void reset_cb(lv_event_t* e) {
  resetSamples();
  lv_label_set_text(textLbl, "");
  refresh();
}

static lv_obj_t* mkLabel(lv_obj_t* p, const char* t, lv_color_t c, int32_t x, int32_t y) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

static lv_obj_t* mkBox(lv_obj_t* p, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t c, int32_t radius) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

static lv_obj_t* mkButton(lv_obj_t* p, const char* t, lv_color_t c, int32_t w, int32_t h) {
  lv_obj_t* b = lv_button_create(p);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, C_BORDER, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, 4, 0);
  lv_obj_set_ext_click_area(b, 8);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, c, 0);
  lv_obj_center(l);
  return b;
}

void Game_Fist_Start(void) {
  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, C_BG, 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(scr, C_WHITE, 0);
  lv_obj_set_style_text_font(scr, &lv_font_montserrat_12, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // HUD
  lv_obj_t* hud = mkBox(scr, 0, 0, 320, 22, C_SURFACE, 0);
  lv_obj_t* l = lv_label_create(hud);
  lv_label_set_text(l, "FIST CHECK");
  lv_obj_set_style_text_color(l, C_AMBER, 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
  l = lv_label_create(hud);
  lv_label_set_text(l, Keyer_ModeName(Keyer_GetMode()));
  lv_obj_set_style_text_color(l, C_MUTED, 0);
  lv_obj_align(l, LV_ALIGN_CENTER, 20, 0);
  lv_obj_t* eb = mkButton(hud, "EXIT", C_RED, 36, 18);
  lv_obj_align(eb, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(eb, exit_cb, LV_EVENT_CLICKED, NULL);

  // Decoded text, newest on the right
  textLbl = lv_label_create(scr);
  lv_label_set_text(textLbl, "");
  lv_label_set_long_mode(textLbl, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(textLbl, 304, 20);
  lv_obj_set_pos(textLbl, 8, 26);
  lv_obj_set_style_text_align(textLbl, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_font(textLbl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(textLbl, C_GREEN, 0);

  // Metric rows: name | value | ideal | gauge
  for (int r = 0; r < R_COUNT; r++) {
    int32_t y = ROW_Y0 + r * ROW_DY;
    mkLabel(scr, rowName[r], C_MUTED, 8, y + 2);
    valLbl[r] = mkLabel(scr, "--", C_WHITE, 104, y + 2);
    mkLabel(scr, rowIdeal[r], C_MUTED, 168, y + 2);
    gaugeDot[r] = NULL;
    if (r == R_SPEED) continue;
    mkBox(scr, GAUGE_X, y + 8, GAUGE_W, 2, C_BORDER, 1);                 // track
    mkBox(scr, GAUGE_X + GAUGE_W / 2 - 1, y + 3, 2, 12, C_MUTED, 0);    // ideal mark
    gaugeDot[r] = mkBox(scr, GAUGE_X + GAUGE_W / 2 - 4, y + 5, 8, 8, C_GREEN, 4);
    lv_obj_add_flag(gaugeDot[r], LV_OBJ_FLAG_HIDDEN);
  }
  mkLabel(scr, "IDEAL", C_BORDER, 168, ROW_Y0 - 14);
  mkLabel(scr, "SHORT", C_BORDER, GAUGE_X, ROW_Y0 - 14);
  lv_obj_t* lg = mkLabel(scr, "LONG", C_BORDER, 0, ROW_Y0 - 14);
  lv_obj_set_pos(lg, GAUGE_X + GAUGE_W - 30, ROW_Y0 - 14);

  // Score + tip + reset
  int32_t by = ROW_Y0 + R_COUNT * ROW_DY + 4;
  mkBox(scr, 8, by - 4, 304, 1, C_BORDER, 0);
  scoreLbl = mkLabel(scr, "--", C_MUTED, 8, by);
#if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(scoreLbl, &lv_font_montserrat_28, 0);
#endif
  mkLabel(scr, "FIST", C_MUTED, 58, by + 2);
  tipLbl = mkLabel(scr, "", C_WHITE, 58, by + 16);
  lv_label_set_long_mode(tipLbl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(tipLbl, 186);
  countLbl = mkLabel(scr, "", C_BORDER, 58, by + 31);
  lv_obj_t* rb = mkButton(scr, "RESET", C_AMBER, 60, 26);
  lv_obj_set_pos(rb, 252, by + 6);
  lv_obj_add_event_cb(rb, reset_cb, LV_EVENT_CLICKED, NULL);

  resetSamples();
  chR = chW;
  Keyer_OnChar(game_char);
  active = true;
  tickTmr = lv_timer_create(tick_cb, FC_TICK_MS, NULL);
  refresh();
  lv_screen_load(scr);
}

void Game_Fist_Stop(void) {
  if (active && lastScore >= 0 && elemTotal >= FC_SAVE_ELEMS)
    Score_Submit("fist", lastScore, Keyer_GetWPM());
  active = false;
  if (tickTmr) { lv_timer_delete(tickTmr); tickTmr = NULL; }
  Keyer_OnChar([](char c) { UI_PushDecodedChar(c); });
  if (scr) {
    UI_ShowMain();
    lv_obj_delete_async(scr);   // EXIT lives on this screen — don't free it mid-event
    scr = NULL;
  }
}
