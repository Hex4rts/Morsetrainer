#include "MT_Game_MorseMemory.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Morse.h"
#include "MT_Sidetone.h"
#include "MT_NeoPixel.h"
#include "MT_Score.h"
#include <lvgl.h>

// ── Morse Memory (Simon-says for CW) ───────────────────────────────────────
// The device plays a growing sequence of letters by ear. Each round it appends
// one new random letter and replays the WHOLE sequence; the player keys it back
// from the start. A single wrong letter ends the run — score is built from how
// long a sequence you can hold and send back.

static inline uint16_t ditMs(void) { return 1200 / Keyer_GetWPM(); }

#define MM_TICK_MS   10    // 10ms tick for accurate morse playback
#define MM_MAX       30    // sequence cap (practically unreachable)
#define MM_FEEDBACK  1200  // ms the verdict banner lingers

enum MMDiff { MM_BGN, MM_INT, MM_EXP };
// Beginner:     sequence shown while it plays (read-along), input shown on recall
// Intermediate: audio only on playback, your keyed input shown on recall
// Expert:       audio only, input hidden — fully blind until the verdict

enum MMPhase { MM_PLAY, MM_RECALL, MM_FEEDBACK_PH, MM_OVER };

static lv_obj_t*   scr       = NULL;
static lv_obj_t*   menuScr   = NULL;
static lv_obj_t*   seqLbl    = NULL;   // big: the sequence / "???"
static lv_obj_t*   inputLbl  = NULL;   // what the player has keyed
static lv_obj_t*   statusLbl = NULL;
static lv_obj_t*   scoreLbl  = NULL;
static lv_obj_t*   roundLbl  = NULL;
static lv_obj_t*   overPanel = NULL;
static lv_timer_t* tickTmr   = NULL;

static char     seq[MM_MAX + 1];        // the target sequence
static uint8_t  seqLen     = 0;
static uint8_t  recallPos  = 0;         // how many letters keyed correctly so far
static uint32_t score      = 0;
static uint8_t  bestLen    = 0;         // longest sequence fully recalled
static bool     active     = false;
static MMDiff   mmdiff     = MM_BGN;
static MMPhase  phase      = MM_PLAY;
static volatile char lastChar = 0;

// Audio playback of the whole sequence
static char     playBuf[MM_MAX * 8];
static int      playPos    = 0;
static bool     playTone   = false;
static uint16_t playCtr    = 0;
static uint32_t phaseStart = 0;

static inline bool showInput(void)   { return mmdiff != MM_EXP; }
static inline bool revealOnPlay(void){ return mmdiff == MM_BGN; }

static void game_char(char c) { lastChar = c; UI_PushDecodedChar(c); }
static void exit_cb(lv_event_t* e) { Game_MorseMemory_Stop(); }

// Render the recall progress: real letters when shown, anonymous dots when blind
static void drawInput(void) {
  if (!inputLbl) return;
  if (recallPos == 0) { lv_label_set_text(inputLbl, "_"); return; }
  if (showInput()) {
    char buf[MM_MAX + 1];
    for (int i = 0; i < recallPos && i < MM_MAX; i++) buf[i] = seq[i];
    buf[recallPos] = '\0';
    lv_label_set_text(inputLbl, buf);
  } else {
    // Blind: one bullet per keyed letter, no content revealed
    char buf[MM_MAX * 2 + 1]; int j = 0;
    for (int i = 0; i < recallPos && i < MM_MAX; i++) {
      if (i) buf[j++] = ' ';
      buf[j++] = '*';
    }
    buf[j] = '\0';
    lv_label_set_text(inputLbl, buf);
  }
}

static void buildPlayStr(void) {
  playBuf[0] = '\0';
  for (int i = 0; i < seqLen; i++) {
    if (i > 0) strcat(playBuf, " ");
    const char* code = Morse_Encode(seq[i]);
    if (code) strcat(playBuf, code);
  }
}

// Begin a round: append a new letter, then replay the whole sequence
static void newRound(void) {
  if (seqLen < MM_MAX) seq[seqLen++] = 'A' + random(0, 26);
  seq[seqLen] = '\0';
  recallPos = 0;

  buildPlayStr();
  playPos = 0; playCtr = ditMs() / MM_TICK_MS; playTone = false;
  phase = MM_PLAY;
  Keyer_SetInputBlocked(true);

  char buf[16];
  snprintf(buf, sizeof(buf), "LEN %d", seqLen);
  lv_label_set_text(roundLbl, buf);

  lv_label_set_text(inputLbl, "_");
  if (revealOnPlay()) {
    lv_label_set_text(seqLbl, seq);
    lv_obj_set_style_text_color(seqLbl, lv_color_hex(0x42A5F5), 0);
  } else {
    lv_label_set_text(seqLbl, "???");
    lv_obj_set_style_text_color(seqLbl, lv_color_hex(0x555555), 0);
  }
  if (statusLbl) {
    lv_label_set_text(statusLbl, "LISTEN...");
    lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFFB300), 0);
  }
}

static void showGameOver(void) {
  active = false;
  phase = MM_OVER;
  Sidetone_Off();
  Keyer_SetInputBlocked(false);
  if (tickTmr) lv_timer_pause(tickTmr);

  overPanel = lv_obj_create(scr);
  lv_obj_set_size(overPanel, 230, 140);
  lv_obj_center(overPanel);
  lv_obj_set_style_bg_color(overPanel, lv_color_hex(0x0D0D0D), 0);
  lv_obj_set_style_bg_opa(overPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(overPanel, 8, 0);
  lv_obj_set_style_border_color(overPanel, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_border_width(overPanel, 2, 0);

  char buf[80];
  snprintf(buf, sizeof(buf), "MEMORY LOST\nSCORE: %lu\nBEST RUN: %d\nWAS: %s",
           score, bestLen, seq);
  lv_obj_t* lbl = lv_label_create(overPanel);
  lv_label_set_text(lbl, buf);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t* btn = lv_button_create(overPanel);
  lv_obj_set_size(btn, 120, 30);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_t* bl = lv_label_create(btn);
  lv_label_set_text(bl, "RTB");
  lv_obj_set_style_text_color(bl, lv_color_hex(0x000000), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(btn, exit_cb, LV_EVENT_CLICKED, NULL);

  Score_Submit("memory", score, bestLen);
}

static void tick_cb(lv_timer_t* t) {
  if (!active) return;

  switch (phase) {

    // ── Device plays the whole sequence ──
    case MM_PLAY: {
      if (playCtr > 0) { playCtr--; break; }
      int plen = strlen(playBuf);
      if (playPos >= plen) {
        Sidetone_Off();
        phase = MM_RECALL;
        recallPos = 0;
        Keyer_SetInputBlocked(false);
        Keyer_FlushInput();
        lastChar = 0;
        phaseStart = millis();
        // Hide the sequence for recall even in beginner — copy it from memory
        lv_label_set_text(seqLbl, "???");
        lv_obj_set_style_text_color(seqLbl, lv_color_hex(0x555555), 0);
        lv_label_set_text(inputLbl, "_");
        if (statusLbl) {
          lv_label_set_text(statusLbl, "SEND IT BACK");
          lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);
        }
        break;
      }
      char e = playBuf[playPos];
      if (!playTone) {
        // Inter-character gap: 3-dit standard = 1 (tone-off) + 2 here.
        if (e == ' ') { playPos++; playCtr = (ditMs() * 2) / MM_TICK_MS; }
        else {
          Sidetone_On();
          playCtr = (e == '.') ? (ditMs() / MM_TICK_MS) : ((ditMs() * 3) / MM_TICK_MS);
          if (playCtr < 1) playCtr = 1;
          playTone = true;
        }
      } else {
        Sidetone_Off(); playTone = false; playPos++;
        playCtr = ditMs() / MM_TICK_MS; if (playCtr < 1) playCtr = 1;
      }
      break;
    }

    // ── Player keys the sequence back ──
    case MM_RECALL: {
      char c = lastChar; lastChar = 0;
      if (c) {
        c = toupper(c);
        if (c != ' ') {
          if (c == seq[recallPos]) {
            recallPos++;
            drawInput();
            if (recallPos == seqLen) {
              // Whole sequence reproduced — bank it and grow
              score += seqLen * 10;
              if (seqLen > bestLen) bestLen = seqLen;
              NeoPixel_Correct();
              char buf[16]; snprintf(buf, sizeof(buf), "%lu", score);
              lv_label_set_text(scoreLbl, buf);
              if (statusLbl) {
                lv_label_set_text(statusLbl, "NICE!");
                lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);
              }
              phase = MM_FEEDBACK_PH;
              phaseStart = millis();
              Keyer_SetInputBlocked(true);
            }
          } else {
            // One wrong letter ends the run
            NeoPixel_Wrong();
            if (statusLbl) {
              lv_label_set_text(statusLbl, "WRONG!");
              lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFF3D00), 0);
            }
            lv_label_set_text(seqLbl, seq);  // reveal the answer on the way out
            lv_obj_set_style_text_color(seqLbl, lv_color_hex(0x42A5F5), 0);
            showGameOver();
          }
        }
      }
      break;
    }

    // ── Brief success banner, then next (longer) round ──
    case MM_FEEDBACK_PH: {
      if (millis() - phaseStart > MM_FEEDBACK) newRound();
      break;
    }

    default: break;
  }
}

// ── Menu ──
static void startMM(MMDiff d);
static void menu_exit(lv_event_t* e) { if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; } UI_ShowMain(); }
static void bgn_cb(lv_event_t* e) { startMM(MM_BGN); }
static void int_cb(lv_event_t* e) { startMM(MM_INT); }
static void exp_cb(lv_event_t* e) { startMM(MM_EXP); }

static lv_obj_t* mkBtn(lv_obj_t* p, const char* t, const char* d, lv_color_t c, int16_t y) {
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

void Game_MorseMemory_Start(void) {
  menuScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(menuScr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(menuScr, LV_OPA_COVER, 0);

  lv_obj_t* t = lv_label_create(menuScr);
  lv_label_set_text(t, "MORSE MEMORY");
  lv_obj_set_style_text_color(t, lv_color_hex(0xAB47BC), 0);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
#endif
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t* sub = lv_label_create(menuScr);
  lv_label_set_text(sub, "Copy the growing sequence back");
  lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 42);

  lv_obj_t* b;
  b = mkBtn(menuScr, "BEGINNER", "see + hear it", lv_color_hex(0x00E676), 80);
  lv_obj_add_event_cb(b, bgn_cb, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "INTERMEDIATE", "head copy", lv_color_hex(0xFFB300), 118);
  lv_obj_add_event_cb(b, int_cb, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "EXPERT", "blind recall", lv_color_hex(0xFF3D00), 156);
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

static void startMM(MMDiff d) {
  mmdiff = d;
  score = 0; seqLen = 0; recallPos = 0; bestLen = 0; lastChar = 0;
  active = true; overPanel = NULL;

  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }

  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x050510), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // HUD
  roundLbl = lv_label_create(scr);
  lv_label_set_text(roundLbl, "LEN 1");
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

  // Hint
  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "REMEMBER THE SEQUENCE:");
  lv_obj_set_style_text_color(hint, lv_color_hex(0x666666), 0);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 28);

  // Sequence (big)
  seqLbl = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(seqLbl, &lv_font_montserrat_28, 0);
#endif
  lv_obj_set_style_text_color(seqLbl, lv_color_hex(0x42A5F5), 0);
  // Sequences grow long — wrap and center so they never run off the edges.
  lv_obj_set_width(seqLbl, 300);
  lv_label_set_long_mode(seqLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(seqLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(seqLbl, "???");
  lv_obj_align(seqLbl, LV_ALIGN_CENTER, 0, -28);

  // Player input
  inputLbl = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(inputLbl, &lv_font_montserrat_24, 0);
#endif
  lv_obj_set_style_text_color(inputLbl, lv_color_hex(0xFFB300), 0);
  lv_obj_set_width(inputLbl, 300);
  lv_label_set_long_mode(inputLbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(inputLbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(inputLbl, "_");
  lv_obj_align(inputLbl, LV_ALIGN_CENTER, 0, 28);

  // Status
  statusLbl = lv_label_create(scr);
  lv_label_set_text(statusLbl, "");
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x666666), 0);
  lv_obj_align(statusLbl, LV_ALIGN_CENTER, 0, 62);

  Keyer_OnChar(game_char);
  newRound();
  tickTmr = lv_timer_create(tick_cb, MM_TICK_MS, NULL);
  lv_screen_load(scr);
}

void Game_MorseMemory_Stop(void) {
  // Save on manual EXIT too — showGameOver() handles the natural loss (and
  // clears `active`), so this only banks a run the player quit mid-stream.
  if (active && score > 0) Score_Submit("memory", score, bestLen);
  active = false;
  Sidetone_Off();
  Keyer_SetInputBlocked(false);
  if (tickTmr) { lv_timer_del(tickTmr); tickTmr = NULL; }
  Keyer_OnChar([](char c) { UI_PushDecodedChar(c); });
  if (scr) { UI_ShowMain(); lv_obj_delete(scr); scr = NULL; }
  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }
}
