#include "MT_UI_Ref.h"
#include "MT_UI.h"
#include "MT_Morse.h"
#include "MT_Keyer.h"
#include "MT_Sidetone.h"
#include "MT_NeoPixel.h"
#include "MT_Game_Phrases.h"

// ============================================================================
//  ABC tab — Morse reference
//
//  Two pages, picked with the toggle at the top:
//    CHARACTERS  A-Z, 0-9, punctuation as dit/dah bars
//    Q-CODES     Q-codes, prosigns and abbreviations (the CW Essentials
//                phrase list) with meaning and bars
//  Tap any entry to hear it at the keyer WPM.
//
//  Each page is ONE lvgl object with a custom draw callback. Building it
//  from per-cell labels + per-element bar objects would cost hundreds of
//  objects, which does not fit in the 48 KB LVGL heap. Taps are hit-tested
//  by hand against the same layout walk that draws.
// ============================================================================

#define C_SURFACE  lv_color_hex(0x1A1A1A)
#define C_BORDER   lv_color_hex(0x333333)
#define C_GREEN    lv_color_hex(0x00E676)
#define C_AMBER    lv_color_hex(0xFFB300)
#define C_BLUE     lv_color_hex(0x42A5F5)
#define C_WHITE    lv_color_hex(0xFFFFFF)
#define C_MUTED    lv_color_hex(0x666666)
#define C_MEANING  lv_color_hex(0xAAAAAA)

// Layout (px)
#define HDR_H      18
#define ROW_H      26
#define GAP        3
#define LETTER_X   5     // character inset from cell left
#define BARS_X     22    // bar region inset from cell left
#define DIT_W      5
#define DAH_W      12
#define BAR_H      5
#define BAR_GAP    3

// Q-code page: smaller bars so a 3-letter code fits beside its meaning
#define Q_CODE_X   6
#define Q_MEAN_X   44
#define Q_DIT_W    4
#define Q_DAH_W    10
#define Q_BAR_GAP  2
#define Q_CHAR_GAP 6

// Item ids: a character is its own ASCII code, a phrase is ID_PHRASE + index
#define ID_PHRASE  1000

typedef struct {
  const char* title;
  const char* chars;
  uint8_t     cols;   // letters max 4 elements -> 4 cols; digits/punct up to 6 -> 3 cols
} ref_section_t;

static const ref_section_t sections[] = {
  {"LETTERS",     "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 4},
  {"NUMBERS",     "0123456789",                 3},
  {"PUNCTUATION", ".,?/=+-()@!",                3},
};
#define SECTION_COUNT (sizeof(sections) / sizeof(sections[0]))

// Prosigns are sent as one run-together symbol, no gap between the letters
static const char* const prosigns[] = {"AR", "SK", "BT", "KN", "AS"};

enum { GRP_Q, GRP_PROSIGN, GRP_ABBR, GRP_COUNT };
static const char* const groupTitle[GRP_COUNT] = {"Q-CODES", "PROSIGNS", "ABBREVIATIONS"};

#define MAX_PHRASES 96
static uint8_t phraseOrder[MAX_PHRASES];   // phrase indices grouped, then A-Z
static uint8_t phraseN = 0;
static uint8_t groupStart[GRP_COUNT + 1];

static lv_obj_t*   chart     = NULL;
static lv_obj_t*   pageBtn[2] = {};
static uint8_t     page      = 0;          // 0 = characters, 1 = Q-codes
static lv_timer_t* playTmr   = NULL;
static int         playId    = 0;          // item being played (highlighted)
static char        playSeq[48];            // '.', '-', ' ' (= letter gap)
static uint8_t     playPos   = 0;
static bool        playTone  = false;

static bool isProsign(const char* code) {
  for (size_t i = 0; i < sizeof(prosigns) / sizeof(prosigns[0]); i++)
    if (strcmp(code, prosigns[i]) == 0) return true;
  return false;
}

static uint8_t groupOf(const char* code) {
  if (isProsign(code)) return GRP_PROSIGN;
  if (code[0] == 'Q' && strlen(code) == 3) return GRP_Q;
  return GRP_ABBR;
}

// Group the phrase list (Q-codes / prosigns / abbreviations), A-Z inside each
static void buildPhraseOrder(void) {
  phraseN = 0;
  uint8_t total = Phrases_Count();
  if (total > MAX_PHRASES) total = MAX_PHRASES;
  for (uint8_t g = 0; g < GRP_COUNT; g++) {
    groupStart[g] = phraseN;
    for (uint8_t i = 0; i < total; i++)
      if (groupOf(Phrases_Code(i)) == g) phraseOrder[phraseN++] = i;
    // insertion sort, ~40 items at most
    for (uint8_t a = groupStart[g] + 1; a < phraseN; a++) {
      uint8_t v = phraseOrder[a]; int b = a - 1;
      while (b >= groupStart[g] && strcmp(Phrases_Code(phraseOrder[b]), Phrases_Code(v)) > 0) {
        phraseOrder[b + 1] = phraseOrder[b]; b--;
      }
      phraseOrder[b + 1] = v;
    }
  }
  groupStart[GRP_COUNT] = phraseN;
}

// ── Layout ──────────────────────────────────────────────────────────────────
// Walks every header and item of the current page in chart-relative
// coordinates. visit() returns true to stop the walk early (hit test).
typedef bool (*ref_visit_t)(const lv_area_t* a, int id, const char* hdr, void* ctx);

static int32_t walkLayout(int32_t width, ref_visit_t visit, void* ctx) {
  int32_t y = 0;
  if (page == 0) {
    for (size_t s = 0; s < SECTION_COUNT; s++) {
      const ref_section_t* sec = &sections[s];
      lv_area_t h = {0, y, width - 1, y + HDR_H - 1};
      if (visit && visit(&h, 0, sec->title, ctx)) return y;
      y += HDR_H;

      int32_t cellW = (width - (sec->cols - 1) * GAP) / sec->cols;
      int n = strlen(sec->chars);
      for (int i = 0; i < n; i++) {
        int col = i % sec->cols, row = i / sec->cols;
        lv_area_t a;
        a.x1 = col * (cellW + GAP);
        a.y1 = y + row * (ROW_H + GAP);
        a.x2 = a.x1 + cellW - 1;
        a.y2 = a.y1 + ROW_H - 1;
        if (visit && visit(&a, sec->chars[i], NULL, ctx)) return y;
      }
      int rows = (n + sec->cols - 1) / sec->cols;
      y += rows * (ROW_H + GAP) + GAP;
    }
  } else {
    for (uint8_t g = 0; g < GRP_COUNT; g++) {
      if (groupStart[g] == groupStart[g + 1]) continue;
      lv_area_t h = {0, y, width - 1, y + HDR_H - 1};
      if (visit && visit(&h, 0, groupTitle[g], ctx)) return y;
      y += HDR_H;
      for (uint8_t k = groupStart[g]; k < groupStart[g + 1]; k++) {
        lv_area_t a = {0, y, width - 1, y + ROW_H - 1};
        if (visit && visit(&a, ID_PHRASE + phraseOrder[k], NULL, ctx)) return y;
        y += ROW_H + GAP;
      }
      y += GAP;
    }
  }
  return y;  // total height
}

// ── Drawing ─────────────────────────────────────────────────────────────────
typedef struct { lv_layer_t* layer; int32_t ox, oy; } draw_ctx_t;

static void drawBar(lv_layer_t* layer, int32_t x, int32_t y, int32_t w, int32_t h, bool dah) {
  lv_draw_rect_dsc_t bd;
  lv_draw_rect_dsc_init(&bd);
  bd.radius   = 2;
  bd.bg_opa   = LV_OPA_COVER;
  bd.bg_color = dah ? C_AMBER : C_BLUE;
  lv_area_t b = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(layer, &bd, &b);
}

static void drawCellBg(lv_layer_t* layer, const lv_area_t* a, bool active) {
  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_color     = C_SURFACE;
  rd.bg_opa       = LV_OPA_COVER;
  rd.radius       = 4;
  rd.border_color = active ? C_GREEN : C_BORDER;
  rd.border_width = active ? 2 : 1;
  lv_draw_rect(layer, &rd, a);
}

static void drawLabel(lv_layer_t* layer, const char* txt, bool local, const lv_font_t* font,
                      lv_color_t color, int32_t x, int32_t y, int32_t x2, lv_text_align_t align) {
  lv_draw_label_dsc_t ld;
  lv_draw_label_dsc_init(&ld);
  ld.font       = font;
  ld.color      = color;
  ld.text       = txt;
  ld.text_local = local ? 1 : 0;   // stack text must be copied by the draw task
  ld.align      = align;
  lv_area_t t = {x, y, x2, y + lv_font_get_line_height(font)};
  lv_draw_label(layer, &ld, &t);
}

// Width of a phrase drawn with the small Q-page bars
static int32_t phraseBarsWidth(const char* code, bool prosign) {
  int32_t w = 0;
  for (const char* c = code; *c; c++) {
    const char* m = Morse_Encode(*c);
    if (!m) continue;
    if (c != code) w += prosign ? Q_BAR_GAP : Q_CHAR_GAP;
    for (const char* e = m; *e; e++) w += (*e == '-' ? Q_DAH_W : Q_DIT_W) + (e[1] ? Q_BAR_GAP : 0);
  }
  return w;
}

static bool drawVisit(const lv_area_t* rel, int id, const char* hdr, void* p) {
  draw_ctx_t* d = (draw_ctx_t*)p;
  lv_area_t a = *rel;
  lv_area_move(&a, d->ox, d->oy);

  if (hdr) {
    drawLabel(d->layer, hdr, false, &lv_font_montserrat_12, C_MUTED, a.x1, a.y1 + 2, a.x2, LV_TEXT_ALIGN_LEFT);
    if (a.y1 == d->oy)  // first header of the page
      drawLabel(d->layer, "TAP TO HEAR", false, &lv_font_montserrat_12, C_MUTED, a.x1, a.y1 + 2, a.x2, LV_TEXT_ALIGN_RIGHT);
    return false;
  }

  bool active = (id == playId);
  drawCellBg(d->layer, &a, active);

  if (id < ID_PHRASE) {
    // ── Character cell ──
    char ch = (char)id;
    char txt[2] = {ch, '\0'};
    const lv_font_t* f = &lv_font_montserrat_16;
    drawLabel(d->layer, txt, true, f, active ? C_GREEN : C_WHITE,
              a.x1 + LETTER_X, a.y1 + (ROW_H - lv_font_get_line_height(f)) / 2, a.x1 + BARS_X, LV_TEXT_ALIGN_LEFT);

    const char* code = Morse_Encode(ch);
    if (!code) return false;
    int32_t w = 0;
    for (const char* c = code; *c; c++) w += (*c == '-' ? DAH_W : DIT_W) + (c[1] ? BAR_GAP : 0);
    int32_t rx1 = a.x1 + BARS_X, rx2 = a.x2 - 3;
    int32_t x = rx1 + ((rx2 - rx1 + 1) - w) / 2;
    int32_t y = a.y1 + (ROW_H - BAR_H) / 2;
    for (const char* c = code; *c; c++) {
      bool dah = (*c == '-');
      drawBar(d->layer, x, y, dah ? DAH_W : DIT_W, BAR_H, dah);
      x += (dah ? DAH_W : DIT_W) + BAR_GAP;
    }
  } else {
    // ── Phrase row: code | meaning | bars (right-aligned) ──
    uint8_t idx = id - ID_PHRASE;
    const char* code = Phrases_Code(idx);
    bool pro = isProsign(code);
    const lv_font_t* fc = &lv_font_montserrat_14;
    const lv_font_t* fm = &lv_font_montserrat_12;
    drawLabel(d->layer, code, false, fc, active ? C_GREEN : C_WHITE,
              a.x1 + Q_CODE_X, a.y1 + (ROW_H - lv_font_get_line_height(fc)) / 2, a.x1 + Q_MEAN_X, LV_TEXT_ALIGN_LEFT);
    int32_t barsW = phraseBarsWidth(code, pro);
    int32_t bx = a.x2 - 6 - barsW;
    drawLabel(d->layer, Phrases_Meaning(idx), false, fm, C_MEANING,
              a.x1 + Q_MEAN_X, a.y1 + (ROW_H - lv_font_get_line_height(fm)) / 2, bx - 4, LV_TEXT_ALIGN_LEFT);
    int32_t y = a.y1 + (ROW_H - BAR_H) / 2;
    for (const char* c = code; *c; c++) {
      const char* m = Morse_Encode(*c);
      if (!m) continue;
      if (c != code) bx += pro ? Q_BAR_GAP : Q_CHAR_GAP;
      for (const char* e = m; *e; e++) {
        bool dah = (*e == '-');
        drawBar(d->layer, bx, y, dah ? Q_DAH_W : Q_DIT_W, BAR_H, dah);
        bx += (dah ? Q_DAH_W : Q_DIT_W) + (e[1] ? Q_BAR_GAP : 0);
      }
    }
  }
  return false;
}

static void draw_cb(lv_event_t* e) {
  lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  draw_ctx_t d = {lv_event_get_layer(e), coords.x1, coords.y1};
  walkLayout(lv_area_get_width(&coords), drawVisit, &d);
}

// ── Playback ────────────────────────────────────────────────────────────────
static uint32_t ditMs(void) { return 1200 / Keyer_GetWPM(); }

static void stopPlayback(bool ownsAudio) {
  if (playTmr) { lv_timer_delete(playTmr); playTmr = NULL; }
  if (ownsAudio) {
    Sidetone_Off();
    Keyer_SetInputBlocked(false);
    Keyer_FlushInput();
  }
  playTone = false;
  playId   = 0;
  if (chart) lv_obj_invalidate(chart);
}

static void play_cb(lv_timer_t* t) {
  // A game took over the screen mid-playback: it now owns the sidetone and
  // the input gate, so bow out without touching either.
  if (lv_screen_active() != UI_GetMainScreen()) { stopPlayback(false); return; }

  if (playTone) {
    Sidetone_Off();
    playTone = false;
    playPos++;
    if (!playSeq[playPos]) { stopPlayback(true); return; }
    if (playSeq[playPos] == ' ') {                   // letter gap: 3 dits total
      playPos++;
      lv_timer_set_period(t, ditMs() * 3);
    } else {
      lv_timer_set_period(t, ditMs());               // element gap
    }
  } else {
    bool dah = (playSeq[playPos] == '-');
    Sidetone_On();
    NeoPixel_KeyFlash(dah);
    playTone = true;
    lv_timer_set_period(t, dah ? ditMs() * 3 : ditMs());
  }
}

// Build the element sequence for an item. Letters of a word are separated by
// ' ' (letter gap); a prosign runs its letters together.
static bool buildSeq(int id) {
  const char* text;
  char one[2] = {0, 0};
  bool pro = false;
  if (id < ID_PHRASE) { one[0] = (char)id; text = one; }
  else { text = Phrases_Code(id - ID_PHRASE); pro = isProsign(text); }

  size_t p = 0;
  for (const char* c = text; *c; c++) {
    const char* m = Morse_Encode(*c);
    if (!m) continue;
    if (p > 0 && !pro && p < sizeof(playSeq) - 1) playSeq[p++] = ' ';
    for (const char* e = m; *e && p < sizeof(playSeq) - 1; e++) playSeq[p++] = *e;
  }
  playSeq[p] = '\0';
  return p > 0;
}

static void startPlayback(int id) {
  if (playTmr) stopPlayback(true);
  if (!buildSeq(id)) return;

  // Block the paddles so keying can't mix with the reference tone
  Keyer_SetInputBlocked(true);
  playId   = id;
  playPos  = 0;
  playTone = false;
  playTmr  = lv_timer_create(play_cb, ditMs(), NULL);
  lv_timer_ready(playTmr);                           // first tone right away
  lv_obj_invalidate(chart);
}

// ── Touch ───────────────────────────────────────────────────────────────────
typedef struct { lv_point_t p; int hit; } hit_ctx_t;

static bool hitVisit(const lv_area_t* a, int id, const char* hdr, void* p) {
  hit_ctx_t* h = (hit_ctx_t*)p;
  if (hdr) return false;
  if (h->p.x >= a->x1 && h->p.x <= a->x2 && h->p.y >= a->y1 && h->p.y <= a->y2) {
    h->hit = id;
    return true;
  }
  return false;
}

static void click_cb(lv_event_t* e) {
  lv_obj_t* obj = (lv_obj_t*)lv_event_get_target(e);
  lv_indev_t* indev = lv_indev_active();
  if (!indev) return;
  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  hit_ctx_t h;
  lv_indev_get_point(indev, &h.p);
  h.p.x -= coords.x1;
  h.p.y -= coords.y1;
  h.hit = 0;
  walkLayout(lv_area_get_width(&coords), hitVisit, &h);
  if (h.hit) startPlayback(h.hit);
}

// ── Page toggle ─────────────────────────────────────────────────────────────
static void styleToggle(void) {
  for (int i = 0; i < 2; i++) {
    bool on = (i == page);
    lv_obj_set_style_bg_color(pageBtn[i], on ? C_GREEN : C_SURFACE, 0);
    lv_obj_set_style_border_color(pageBtn[i], on ? C_GREEN : C_BORDER, 0);
    lv_obj_t* lb = lv_obj_get_child(pageBtn[i], 0);
    if (lb) lv_obj_set_style_text_color(lb, on ? lv_color_hex(0x000000) : C_WHITE, 0);
  }
}

static void setPage(uint8_t p) {
  if (playTmr) stopPlayback(true);
  page = p;
  int32_t w = lv_obj_get_width(chart);
  lv_obj_set_height(chart, walkLayout(w, NULL, NULL));
  lv_obj_scroll_to_y(lv_obj_get_parent(chart), 0, LV_ANIM_OFF);
  lv_obj_invalidate(chart);
  styleToggle();
}

static void page_cb(lv_event_t* e) {
  uint8_t p = (uint8_t)(intptr_t)lv_event_get_user_data(e);
  if (p != page) setPage(p);
}

// ── Create ──────────────────────────────────────────────────────────────────
void UI_Ref_Create(lv_obj_t* parent) {
  buildPhraseOrder();

  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 4, 0);
  lv_obj_set_scroll_dir(parent, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_ACTIVE);

  // Page toggle
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, lv_pct(100), 26);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  static const char* const names[2] = {"A-Z  0-9", "Q-CODES & ABBR"};
  for (int i = 0; i < 2; i++) {
    pageBtn[i] = lv_button_create(row);
    lv_obj_set_size(pageBtn[i], lv_pct(49), 24);
    lv_obj_set_style_border_width(pageBtn[i], 1, 0);
    lv_obj_set_style_radius(pageBtn[i], 6, 0);
    lv_obj_set_style_shadow_width(pageBtn[i], 0, 0);
    lv_obj_t* lb = lv_label_create(pageBtn[i]);
    lv_label_set_text(lb, names[i]);
    lv_obj_center(lb);
    lv_obj_add_event_cb(pageBtn[i], page_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }

  chart = lv_obj_create(parent);
  lv_obj_remove_style_all(chart);
  lv_obj_update_layout(parent);
  int32_t w = lv_obj_get_content_width(parent);
  if (w <= 0) w = 312;  // 320 px screen minus the 4 px tab padding each side
  lv_obj_set_size(chart, w, walkLayout(w, NULL, NULL));
  lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(chart, draw_cb, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(chart, click_cb, LV_EVENT_CLICKED, NULL);

  styleToggle();
}
