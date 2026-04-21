#include "MT_Game_CallsignRush.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Morse.h"
#include "MT_Sidetone.h"
#include "MT_NeoPixel.h"
#include "MT_Score.h"
#include <lvgl.h>

static inline uint16_t ditMs(void) { return 1200 / Keyer_GetWPM(); }

#define CR_MAX_CALL   8
#define CR_TICK_MS    10   // 10ms tick for accurate morse playback

// Visual bars for beginner
#define CR_MAX_BARS   40
#define CR_BAR_DIT_W  5
#define CR_BAR_DAH_W  13
#define CR_BAR_H      8
#define CR_BAR_GAP    2
#define CR_LETTER_GAP 6

enum CRDiff { CR_BGN, CR_INT, CR_EXP };
// Beginner:     show callsign text + play audio + visual bars + 15s
// Intermediate: show callsign text + play audio + 12s
// Expert:       play audio only + 12s (must decode by ear)

static const char* prefixes[] = {
  "W","K","N","WA","KB","KD","WB","VE","VA","G","F","DL","JA","VK","ZL","EA"
};
#define NUM_PFX (sizeof(prefixes)/sizeof(prefixes[0]))

static lv_obj_t*   scr       = NULL;
static lv_obj_t*   menuScr   = NULL;
static lv_obj_t*   callLbl   = NULL;
static lv_obj_t*   inputLbl  = NULL;
static lv_obj_t*   timerBar  = NULL;
static lv_obj_t*   scoreLbl  = NULL;
static lv_obj_t*   roundLbl  = NULL;
static lv_obj_t*   overPanel = NULL;
static lv_obj_t*   statusLbl = NULL;
static lv_obj_t*   barObjs[CR_MAX_BARS];
static lv_timer_t* tickTmr   = NULL;

static char     target[CR_MAX_CALL + 1];
static char     inputBuf[CR_MAX_CALL + 1];
static uint8_t  inputPos  = 0;
static uint32_t score     = 0;
static uint16_t roundNum  = 0;
static uint32_t timeLeft  = 0;
static uint32_t timeTotal = 0;
static bool     active    = false;
static CRDiff   crdiff    = CR_BGN;
static volatile char lastChar = 0;

// Audio playback for expert mode
static char     playBuf[CR_MAX_CALL * 8 * 2];
static int      playPos   = 0;
static bool     playTone  = false;
static uint16_t playCtr   = 0;
static bool     playing   = false;

static void game_char(char c) { lastChar = c; UI_PushDecodedChar(c); }
static void exit_cb(lv_event_t* e) { Game_CallsignRush_Stop(); }

static void genCallsign(void) {
  const char* pfx = prefixes[random(0, NUM_PFX)];
  int digit = random(0, 10);
  int slen = random(1, 4);
  char sfx[4];
  for (int i = 0; i < slen; i++) sfx[i] = 'A' + random(0, 26);
  sfx[slen] = '\0';
  snprintf(target, sizeof(target), "%s%d%s", pfx, digit, sfx);
}

static void buildPlayStr(void) {
  playBuf[0] = '\0';
  for (int i = 0; target[i]; i++) {
    char c = toupper(target[i]);
    if (i > 0) strcat(playBuf, " ");
    const char* code = Morse_Encode(c);
    if (code) strcat(playBuf, code);
  }
}

// Visual dit/dah bars for beginner mode
static void clearCallBars(void) {
  for (int i = 0; i < CR_MAX_BARS; i++)
    if (barObjs[i]) lv_obj_add_flag(barObjs[i], LV_OBJ_FLAG_HIDDEN);
}

static void drawCallBars(int16_t baseY) {
  clearCallBars();
  // Build full morse string with spaces between letters
  char pat[CR_MAX_CALL * 8];
  pat[0] = '\0';
  for (int i = 0; target[i]; i++) {
    if (i > 0) strcat(pat, " ");
    const char* code = Morse_Encode(toupper(target[i]));
    if (code) strcat(pat, code);
  }
  // Draw bars
  int16_t x = (320 - strlen(pat) * 6) / 2;  // rough center
  if (x < 4) x = 4;
  int idx = 0;
  for (int i = 0; pat[i] && idx < CR_MAX_BARS; i++) {
    if (pat[i] == ' ') { x += CR_LETTER_GAP; continue; }
    bool dah = (pat[i] == '-');
    int16_t w = dah ? CR_BAR_DAH_W : CR_BAR_DIT_W;
    if (barObjs[idx]) {
      lv_obj_set_size(barObjs[idx], w, CR_BAR_H);
      lv_obj_set_pos(barObjs[idx], x, baseY);
      lv_obj_set_style_bg_color(barObjs[idx], dah ? lv_color_hex(0xFFB300) : lv_color_hex(0x42A5F5), 0);
      lv_obj_set_style_bg_opa(barObjs[idx], LV_OPA_COVER, 0);
      lv_obj_set_style_radius(barObjs[idx], 2, 0);
      lv_obj_clear_flag(barObjs[idx], LV_OBJ_FLAG_HIDDEN);
    }
    x += w + CR_BAR_GAP;
    idx++;
  }
}

static void newRound(void) {
  roundNum++;
  genCallsign();
  inputPos = 0; inputBuf[0] = '\0';

  if (crdiff == CR_BGN) {
    timeTotal = 0;  // no timer for beginner
    timeLeft = 0;
    lv_obj_add_flag(timerBar, LV_OBJ_FLAG_HIDDEN);
  } else {
    uint32_t baseTime = 12000;
    timeTotal = baseTime - (roundNum - 1) * 500;
    if (timeTotal < 4000) timeTotal = 4000;
    timeLeft = timeTotal;
    lv_obj_clear_flag(timerBar, LV_OBJ_FLAG_HIDDEN);
  }

  // Show based on difficulty
  // Beginner + Intermediate: show callsign text + play audio
  // Expert: play audio only
  buildPlayStr();
  playPos = 0; playCtr = ditMs() / CR_TICK_MS; playTone = false; playing = true;

  if (crdiff == CR_EXP) {
    lv_label_set_text(callLbl, "???");
    clearCallBars();
    if (statusLbl) {
      lv_label_set_text(statusLbl, "LISTEN...");
      lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFFB300), 0);
    }
  } else {
    lv_label_set_text(callLbl, target);
    if (statusLbl) lv_label_set_text(statusLbl, "");
    // Beginner: show visual dit/dah bars
    if (crdiff == CR_BGN) {
      drawCallBars(130);  // below the callsign text
    } else {
      clearCallBars();
    }
  }

  lv_label_set_text(inputLbl, "_");
  char buf[16]; snprintf(buf, sizeof(buf), "ROUND %d", roundNum);
  lv_label_set_text(roundLbl, buf);
  if (crdiff != CR_BGN) {
    lv_bar_set_range(timerBar, 0, timeTotal / CR_TICK_MS);
    lv_bar_set_value(timerBar, timeTotal / CR_TICK_MS, LV_ANIM_OFF);
  }
}

static void showGameOver(void) {
  active = false;
  Sidetone_Off();
  if (tickTmr) lv_timer_pause(tickTmr);

  overPanel = lv_obj_create(scr);
  lv_obj_set_size(overPanel, 220, 130);
  lv_obj_center(overPanel);
  lv_obj_set_style_bg_color(overPanel, lv_color_hex(0x0D0D0D), 0);
  lv_obj_set_style_bg_opa(overPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(overPanel, 8, 0);
  lv_obj_set_style_border_color(overPanel, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_border_width(overPanel, 2, 0);

  char buf[48];
  snprintf(buf, sizeof(buf), "COMMS LOST\nSCORE: %lu\nROUNDS: %d", score, roundNum - 1);
  lv_obj_t* lbl = lv_label_create(overPanel);
  lv_label_set_text(lbl, buf);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t* btn = lv_button_create(overPanel);
  lv_obj_set_size(btn, 120, 32);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_t* bl = lv_label_create(btn);
  lv_label_set_text(bl, "RTB");
  lv_obj_set_style_text_color(bl, lv_color_hex(0x000000), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(btn, exit_cb, LV_EVENT_CLICKED, NULL);

  Score_Submit("callrush", score, roundNum - 1);
}

static void tick_cb(lv_timer_t* t) {
  if (!active) return;

  // Audio playback (beginner + expert)
  if (playing) {
    if (playCtr > 0) { playCtr--; }
    else {
      int plen = strlen(playBuf);
      if (playPos >= plen) {
        Sidetone_Off(); playing = false;
        // Expert: reveal callsign after playback
        if (crdiff == CR_EXP) {
          lv_label_set_text(callLbl, target);
          if (statusLbl) lv_label_set_text(statusLbl, "");
        }
      } else {
        char e = playBuf[playPos];
        if (!playTone) {
          // Inter-character gap: standard 3-dit = 1 (tone-off) + 2 here.
          if (e == ' ') { playPos++; playCtr = (ditMs() * 2) / (CR_TICK_MS); }
          else { Sidetone_On(); playCtr = (e == '.') ? (ditMs() / CR_TICK_MS) : ((ditMs() * 3) / CR_TICK_MS);
                 if (playCtr < 1) playCtr = 1; playTone = true; }
        } else { Sidetone_Off(); playTone = false; playPos++;
                 playCtr = ditMs() / CR_TICK_MS; if (playCtr < 1) playCtr = 1; }
      }
    }
  }

  // User input
  char c = lastChar; lastChar = 0;
  if (c) {
    c = toupper(c);
    if (inputPos < CR_MAX_CALL) {
      inputBuf[inputPos++] = c;
      inputBuf[inputPos] = '\0';
      lv_label_set_text(inputLbl, inputBuf);

      if (inputPos == strlen(target)) {
        if (strcmp(inputBuf, target) == 0) {
          score += (crdiff == CR_BGN) ? 100 : (100 + timeLeft / 100);
          NeoPixel_Correct();
          char buf[16]; snprintf(buf, sizeof(buf), "%lu", score);
          lv_label_set_text(scoreLbl, buf);
          Sidetone_Off(); playing = false;
          newRound();
          return;
        } else {
          NeoPixel_Wrong();
          inputPos = 0; inputBuf[0] = '\0';
          lv_label_set_text(inputLbl, "_");
          if (crdiff != CR_BGN) {
            if (timeLeft > 2000) timeLeft -= 2000; else timeLeft = 0;
          }
        }
      }
    }
  }

  if (crdiff != CR_BGN) {
    if (timeLeft > CR_TICK_MS) timeLeft -= CR_TICK_MS; else timeLeft = 0;
    lv_bar_set_value(timerBar, timeLeft / CR_TICK_MS, LV_ANIM_ON);
    if (timeLeft == 0) showGameOver();
  }
}

// ── Menu ──
static void startCR(CRDiff d);
static void menu_exit(lv_event_t* e) { if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; } UI_ShowMain(); }
static void bgn_cb(lv_event_t* e) { startCR(CR_BGN); }
static void int_cb(lv_event_t* e) { startCR(CR_INT); }
static void exp_cb(lv_event_t* e) { startCR(CR_EXP); }

static lv_obj_t* mmkBtn(lv_obj_t* p, const char* t, const char* d, lv_color_t c, int16_t y) {
  lv_obj_t* b = lv_button_create(p);
  lv_obj_set_size(b, 280, 32);
  lv_obj_set_pos(b, 20, y);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(b, c, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 6, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* tl = lv_label_create(b);
  lv_label_set_text(tl, t); lv_obj_set_style_text_color(tl, c, 0);
  lv_obj_align(tl, LV_ALIGN_LEFT_MID, 4, 0);
  lv_obj_t* dl = lv_label_create(b);
  lv_label_set_text(dl, d); lv_obj_set_style_text_color(dl, lv_color_hex(0x666666), 0);
  lv_obj_align(dl, LV_ALIGN_RIGHT_MID, -4, 0);
  return b;
}

void Game_CallsignRush_Start(void) {
  menuScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(menuScr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(menuScr, LV_OPA_COVER, 0);

  lv_obj_t* t = lv_label_create(menuScr);
  lv_label_set_text(t, "CALLSIGN RUSH");
  lv_obj_set_style_text_color(t, lv_color_hex(0xFFB300), 0);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
#endif
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t* sub = lv_label_create(menuScr);
  lv_label_set_text(sub, "Send callsigns as fast as you can");
  lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 42);

  lv_obj_t* b;
  b = mmkBtn(menuScr, "BEGINNER", "text+visual+audio", lv_color_hex(0x00E676), 70);
  lv_obj_add_event_cb(b, bgn_cb, LV_EVENT_CLICKED, NULL);
  b = mmkBtn(menuScr, "INTERMEDIATE", "text+audio 12s", lv_color_hex(0xFFB300), 108);
  lv_obj_add_event_cb(b, int_cb, LV_EVENT_CLICKED, NULL);
  b = mmkBtn(menuScr, "EXPERT", "audio only 12s", lv_color_hex(0xFF3D00), 146);
  lv_obj_add_event_cb(b, exp_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* bb = lv_button_create(menuScr);
  lv_obj_set_size(bb, 80, 26);
  lv_obj_align(bb, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(bb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(bb, 0, 0);
  lv_obj_set_style_radius(bb, 4, 0);
  lv_obj_set_ext_click_area(bb, 10);
  lv_obj_t* bl = lv_label_create(bb);
  lv_label_set_text(bl, "BACK"); lv_obj_set_style_text_color(bl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(bb, menu_exit, LV_EVENT_CLICKED, NULL);

  lv_screen_load(menuScr);
}

static void startCR(CRDiff d) {
  crdiff = d;
  score = 0; roundNum = 0; active = true; lastChar = 0;
  overPanel = NULL; playing = false;

  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }

  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x050510), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // HUD
  roundLbl = lv_label_create(scr);
  lv_label_set_text(roundLbl, "ROUND 1");
  lv_obj_set_style_text_color(roundLbl, lv_color_hex(0x42A5F5), 0);
  lv_obj_align(roundLbl, LV_ALIGN_TOP_LEFT, 8, 4);

  scoreLbl = lv_label_create(scr);
  lv_label_set_text(scoreLbl, "0");
  lv_obj_set_style_text_color(scoreLbl, lv_color_hex(0x00E676), 0);
  lv_obj_align(scoreLbl, LV_ALIGN_TOP_RIGHT, -50, 4);

  // Exit button
  lv_obj_t* eb = lv_button_create(scr);
  lv_obj_set_size(eb, 36, 18);
  lv_obj_align(eb, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(eb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(eb, 0, 0);
  lv_obj_set_style_radius(eb, 4, 0);
  lv_obj_set_ext_click_area(eb, 10);
  lv_obj_t* ebl = lv_label_create(eb);
  lv_label_set_text(ebl, "EXIT");
  lv_obj_set_style_text_color(ebl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(ebl);
  lv_obj_add_event_cb(eb, exit_cb, LV_EVENT_CLICKED, NULL);

  // "SEND THIS CALLSIGN"
  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "SEND THIS CALLSIGN:");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 28);

  // Target callsign (big)
  callLbl = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(callLbl, &lv_font_montserrat_28, 0);
#endif
  lv_obj_set_style_text_color(callLbl, lv_color_hex(0x42A5F5), 0);
  lv_label_set_text(callLbl, "");
  lv_obj_align(callLbl, LV_ALIGN_CENTER, 0, -30);

  // Player input
  inputLbl = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(inputLbl, &lv_font_montserrat_24, 0);
#endif
  lv_obj_set_style_text_color(inputLbl, lv_color_hex(0xFFB300), 0);
  lv_label_set_text(inputLbl, "_");
  lv_obj_align(inputLbl, LV_ALIGN_CENTER, 0, 24);

  // Morse visual bars (beginner only — created but hidden by default)
  for (int i = 0; i < CR_MAX_BARS; i++) {
    barObjs[i] = lv_obj_create(scr);
    lv_obj_remove_style_all(barObjs[i]);
    lv_obj_set_size(barObjs[i], CR_BAR_DIT_W, CR_BAR_H);
    lv_obj_add_flag(barObjs[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(barObjs[i], (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }

  // Status (for expert: "LISTEN...")
  statusLbl = lv_label_create(scr);
  lv_label_set_text(statusLbl, "");
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x666666), 0);
  lv_obj_align(statusLbl, LV_ALIGN_CENTER, 0, 56);

  // Timer bar
  timerBar = lv_bar_create(scr);
  lv_obj_set_size(timerBar, 280, 10);
  lv_obj_align(timerBar, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_color(timerBar, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_color(timerBar, lv_color_hex(0xFFB300), LV_PART_INDICATOR);
  lv_obj_set_style_radius(timerBar, 2, 0);
  lv_obj_set_style_radius(timerBar, 2, LV_PART_INDICATOR);

  Keyer_OnChar(game_char);
  newRound();
  tickTmr = lv_timer_create(tick_cb, CR_TICK_MS, NULL);
  lv_screen_load(scr);
}

void Game_CallsignRush_Stop(void) {
  active = false;
  Sidetone_Off();
  if (tickTmr) { lv_timer_del(tickTmr); tickTmr = NULL; }
  Keyer_OnChar([](char c) { UI_PushDecodedChar(c); });
  if (scr) { UI_ShowMain(); lv_obj_delete(scr); scr = NULL; }
  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }
}
