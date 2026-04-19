#include "MT_Game_Phrases.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Morse.h"
#include "MT_Sidetone.h"
#include "MT_NeoPixel.h"
#include "MT_Score.h"
#include "MT_Settings.h"
#include <lvgl.h>

static inline uint16_t ditMs(void) { return 1200 / Keyer_GetWPM(); }

// ── Phrase database: abbreviation + meaning ──
typedef struct { const char* code; const char* meaning; } phrase_t;
static const phrase_t phraseDB[] = {
  {"CQ",  "Calling any station"},
  {"QSL", "Acknowledged / Confirmed"},
  {"QRT", "Stop transmitting"},
  {"QRS", "Send more slowly"},
  {"QRZ", "Who is calling me?"},
  {"QTH", "My location is..."},
  {"QRM", "Interference from stations"},
  {"QRN", "Static / Atmospheric noise"},
  {"QSB", "Signal is fading"},
  {"DE",  "From / This is"},
  {"K",   "Go ahead / Over"},
  {"SK",  "End of contact"},
  {"AR",  "End of message"},
  {"BT",  "Break / Separator"},
  {"73",  "Best regards"},
  {"88",  "Love and kisses"},
  {"TU",  "Thank you"},
  {"R",   "Roger / Received"},
  {"RST", "Readability Strength Tone"},
  {"GM",  "Good morning"},
  {"GA",  "Good afternoon"},
  {"GE",  "Good evening"},
  {"UR",  "Your"},
  {"HR",  "Here"},
  {"ES",  "And"},
};
#define NUM_PHRASES (sizeof(phraseDB)/sizeof(phraseDB[0]))

#define MAX_TARGET 32

enum Mode { PH_CALLSIGN, PH_GUIDED, PH_PRACTICE };

static lv_obj_t*   scr        = NULL;
static lv_obj_t*   menuScr    = NULL;
static lv_obj_t*   phraseLbl  = NULL;  // target text
static lv_obj_t*   meaningLbl = NULL;  // meaning / hint
static lv_obj_t*   inputLbl   = NULL;  // user input
static lv_obj_t*   scoreLbl   = NULL;
static lv_obj_t*   statusLbl  = NULL;
static lv_obj_t*   progressLbl= NULL;
static lv_obj_t*   overPanel  = NULL;
static lv_timer_t* tickTmr    = NULL;

static char     target[MAX_TARGET];
static char     inputBuf[MAX_TARGET];
static uint8_t  inputPos   = 0;
static uint32_t score      = 0;
static uint16_t roundNum   = 0;
static uint16_t streak     = 0;
static bool     active     = false;
static Mode     mode       = PH_CALLSIGN;
static volatile char lastChar = 0;

// Callsign progressive state
static char     callsign[12];
static uint8_t  callLen    = 0;   // total callsign length
static uint8_t  callLevel  = 1;   // current level (1=first letter, 2=first 2, etc)
static uint8_t  callCorrect= 0;   // consecutive correct at this level

// Phrase mode state
static int      lastPhraseIdx = -1;

// Audio playback
static char     playBuf[MAX_TARGET * 8];
static int      playPos   = 0;
static bool     playTone  = false;
static uint16_t playCtr   = 0;
static bool     playing   = false;
static bool     playDone  = false;

static void game_char(char c) { lastChar = c; UI_PushDecodedChar(c); }
static void exit_cb(lv_event_t* e);

// ── Audio playback builder ──
static void buildPlayback(const char* text) {
  int p = 0;
  for (int i = 0; text[i] && p < (int)sizeof(playBuf) - 8; i++) {
    if (text[i] == ' ') { playBuf[p++] = 'W'; continue; }
    const char* code = Morse_Encode(text[i]);
    if (!code) continue;
    if (i > 0 && text[i-1] != ' ') playBuf[p++] = 'G';
    for (int j = 0; code[j]; j++) {
      if (j > 0) playBuf[p++] = 'g';
      playBuf[p++] = code[j];
    }
  }
  playBuf[p] = '\0';
  playPos = 0; playCtr = 0; playTone = false; playing = true; playDone = false;
}

// ── Set up next round ──
static void setupCallsignRound(void) {
  // Build target from callsign[0..callLevel-1]
  memset(target, 0, MAX_TARGET);
  strncpy(target, callsign, callLevel);
  inputBuf[0] = '\0'; inputPos = 0;

  if (phraseLbl) lv_label_set_text(phraseLbl, target);
  if (inputLbl) lv_label_set_text(inputLbl, "_");

  // Show progress
  char prog[32];
  if (callLevel < callLen) {
    snprintf(prog, sizeof(prog), "Learning: %d/%d letters", callLevel, callLen);
  } else {
    snprintf(prog, sizeof(prog), "Full callsign!");
  }
  if (progressLbl) lv_label_set_text(progressLbl, prog);

  // Meaning line shows the full callsign grayed out
  if (meaningLbl) {
    char hint[24];
    snprintf(hint, sizeof(hint), "( %s )", callsign);
    lv_label_set_text(meaningLbl, hint);
  }

  if (statusLbl) {
    lv_label_set_text(statusLbl, "KEY IT");
    lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x42A5F5), 0);
  }
  playDone = true; playing = false;
}

static void setupPhraseRound(void) {
  int idx;
  do { idx = random(0, NUM_PHRASES); } while (idx == lastPhraseIdx && NUM_PHRASES > 1);
  lastPhraseIdx = idx;

  strncpy(target, phraseDB[idx].code, MAX_TARGET - 1);
  target[MAX_TARGET - 1] = '\0';
  inputBuf[0] = '\0'; inputPos = 0;

  if (phraseLbl) lv_label_set_text(phraseLbl, target);
  if (meaningLbl) lv_label_set_text(meaningLbl, phraseDB[idx].meaning);
  if (inputLbl) lv_label_set_text(inputLbl, "_");
  if (progressLbl) lv_label_set_text(progressLbl, "");

  if (mode == PH_GUIDED) {
    if (statusLbl) {
      lv_label_set_text(statusLbl, "LISTEN...");
      lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x42A5F5), 0);
    }
    buildPlayback(target);
  } else {
    if (statusLbl) {
      lv_label_set_text(statusLbl, "KEY IT");
      lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x42A5F5), 0);
    }
    playDone = true; playing = false;
  }
}

// ── Tick ──
static void tick_cb(lv_timer_t* t) {
  if (!active) return;

  // Audio playback (guided mode only)
  if (playing) {
    uint16_t dm = ditMs();
    if (--playCtr > 0) return;
    if (playBuf[playPos] == '\0') {
      Sidetone_Off(); playing = false; playDone = true;
      if (statusLbl) {
        lv_label_set_text(statusLbl, "YOUR TURN");
        lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);
      }
      return;
    }
    char elem = playBuf[playPos];
    if (elem == '.' || elem == '-') {
      if (!playTone) {
        Sidetone_On(); playTone = true;
        playCtr = (elem == '-') ? dm * 3 : dm;
      } else {
        Sidetone_Off(); playTone = false;
        playCtr = dm;
        playPos++;
      }
    } else if (elem == 'g') { playCtr = dm;     playPos++; }
      else if (elem == 'G') { playCtr = dm * 3; playPos++; }
      else if (elem == 'W') { playCtr = dm * 7; playPos++; }
    return;
  }

  if (!playDone) return;

  // Process decoded chars
  if (lastChar) {
    char c = lastChar;
    lastChar = 0;

    if (c == ' ') {
      if (inputPos > 0 && inputBuf[inputPos-1] != ' ') {
        inputBuf[inputPos++] = ' ';
        inputBuf[inputPos] = '\0';
      }
    } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      inputBuf[inputPos++] = c;
      inputBuf[inputPos] = '\0';
    }
    if (inputLbl) lv_label_set_text(inputLbl, inputPos > 0 ? inputBuf : "_");

    // Compare (trim trailing spaces)
    int tLen = strlen(target);
    int iLen = inputPos;
    while (iLen > 0 && inputBuf[iLen-1] == ' ') iLen--;

    if (iLen >= tLen && strncmp(inputBuf, target, tLen) == 0) {
      // Correct!
      score += tLen * 10;
      streak++;
      roundNum++;
      NeoPixel_Correct();

      char sb[16]; snprintf(sb, sizeof(sb), "%lu", score);
      if (scoreLbl) lv_label_set_text(scoreLbl, sb);

      if (mode == PH_CALLSIGN) {
        callCorrect++;
        if (callCorrect >= 3 && callLevel < callLen) {
          // Level up!
          callLevel++;
          callCorrect = 0;
          NeoPixel_LevelUp();
          if (statusLbl) {
            lv_label_set_text(statusLbl, "LEVEL UP!");
            lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);
          }
          lv_timer_handler(); delay(800);
        } else {
          if (statusLbl) {
            char fb[16]; snprintf(fb, sizeof(fb), "GOOD! %d/3", callCorrect);
            lv_label_set_text(statusLbl, fb);
            lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);
          }
          lv_timer_handler(); delay(500);
        }
        setupCallsignRound();
      } else {
        if (statusLbl) {
          lv_label_set_text(statusLbl, "CORRECT!");
          lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);
        }
        lv_timer_handler(); delay(600);
        setupPhraseRound();
      }
    } else if (inputPos >= MAX_TARGET - 1) {
      // Buffer full, wrong
      NeoPixel_Wrong();
      streak = 0;
      if (mode == PH_CALLSIGN) callCorrect = 0;
      inputBuf[0] = '\0'; inputPos = 0;
      if (inputLbl) lv_label_set_text(inputLbl, "_");
      if (statusLbl) {
        lv_label_set_text(statusLbl, "TRY AGAIN");
        lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFF3D00), 0);
      }
    }
  }
}

// ── Game over ──
static void showGameOver(void) {
  active = false;
  Keyer_OnChar(NULL);
  Sidetone_Off();
  if (tickTmr) lv_timer_pause(tickTmr);

  Score_Submit("phrases", score, streak);

  overPanel = lv_obj_create(scr);
  lv_obj_set_size(overPanel, 220, 120);
  lv_obj_center(overPanel);
  lv_obj_set_style_bg_color(overPanel, lv_color_hex(0x0D0D0D), 0);
  lv_obj_set_style_bg_opa(overPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(overPanel, 8, 0);
  lv_obj_set_style_border_color(overPanel, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_border_width(overPanel, 2, 0);

  char buf[64];
  if (mode == PH_CALLSIGN) {
    snprintf(buf, sizeof(buf), "SCORE: %lu\nLETTERS: %d/%d\nROUNDS: %d",
             score, callLevel, callLen, roundNum);
  } else {
    snprintf(buf, sizeof(buf), "SCORE: %lu\nPHRASES: %d\nSTREAK: %d",
             score, roundNum, streak);
  }
  lv_obj_t* lbl = lv_label_create(overPanel);
  lv_label_set_text(lbl, buf);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t* btn = lv_button_create(overPanel);
  lv_obj_set_size(btn, 100, 28);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_t* bl = lv_label_create(btn);
  lv_label_set_text(bl, "DONE");
  lv_obj_set_style_text_color(bl, lv_color_hex(0x000000), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(btn, exit_cb, LV_EVENT_CLICKED, NULL);
}

// ── Start game ──
static void startGame(Mode m) {
  mode = m;
  score = 0; roundNum = 0; streak = 0;
  active = true;
  inputBuf[0] = '\0'; inputPos = 0;
  lastChar = 0;
  lastPhraseIdx = -1;

  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }

  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x050510), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // HUD: score + exit
  scoreLbl = lv_label_create(scr);
  lv_label_set_text(scoreLbl, "0");
  lv_obj_set_style_text_color(scoreLbl, lv_color_hex(0x00E676), 0);
  lv_obj_align(scoreLbl, LV_ALIGN_TOP_LEFT, 8, 4);

  lv_obj_t* eb = lv_button_create(scr);
  lv_obj_set_size(eb, 36, 18);
  lv_obj_align(eb, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(eb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(eb, 0, 0);
  lv_obj_set_style_radius(eb, 4, 0);
  lv_obj_t* ebl = lv_label_create(eb);
  lv_label_set_text(ebl, "EXIT");
  lv_obj_set_style_text_color(ebl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(ebl);
  lv_obj_add_event_cb(eb, [](lv_event_t* e) { showGameOver(); }, LV_EVENT_CLICKED, NULL);

  // Progress (callsign mode)
  progressLbl = lv_label_create(scr);
  lv_label_set_text(progressLbl, "");
  lv_obj_set_style_text_color(progressLbl, lv_color_hex(0x666666), 0);
  lv_obj_align(progressLbl, LV_ALIGN_TOP_MID, 0, 28);

  // Target phrase (large)
  phraseLbl = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(phraseLbl, &lv_font_montserrat_28, 0);
#endif
  lv_obj_set_style_text_color(phraseLbl, lv_color_hex(0x42A5F5), 0);
  lv_label_set_text(phraseLbl, "");
  lv_obj_align(phraseLbl, LV_ALIGN_CENTER, 0, -40);

  // Meaning line (below target)
  meaningLbl = lv_label_create(scr);
  lv_label_set_text(meaningLbl, "");
  lv_obj_set_style_text_color(meaningLbl, lv_color_hex(0x666666), 0);
  lv_obj_align(meaningLbl, LV_ALIGN_CENTER, 0, -8);

  // Player input
  inputLbl = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_20
  lv_obj_set_style_text_font(inputLbl, &lv_font_montserrat_20, 0);
#endif
  lv_obj_set_style_text_color(inputLbl, lv_color_hex(0xFFB300), 0);
  lv_label_set_text(inputLbl, "_");
  lv_obj_align(inputLbl, LV_ALIGN_CENTER, 0, 30);

  // Status
  statusLbl = lv_label_create(scr);
  lv_label_set_text(statusLbl, "");
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x42A5F5), 0);
  lv_obj_align(statusLbl, LV_ALIGN_CENTER, 0, 60);

  // Initialize based on mode
  if (mode == PH_CALLSIGN) {
    const mt_settings_t* s = Settings_Get();
    strncpy(callsign, s->callsign, sizeof(callsign) - 1);
    callsign[sizeof(callsign) - 1] = '\0';
    callLen = strlen(callsign);
    if (callLen < 1) { strcpy(callsign, "N0CALL"); callLen = 6; }
    callLevel = 1;
    callCorrect = 0;
    setupCallsignRound();
  } else {
    setupPhraseRound();
  }

  Keyer_OnChar(game_char);
  Keyer_SKFlush(true);

  tickTmr = lv_timer_create(tick_cb, 10, NULL);
  lv_screen_load(scr);
}

// ── Menu ──
void Game_Phrases_Start(void) {
  menuScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(menuScr, lv_color_hex(0x050510), 0);
  lv_obj_set_style_bg_opa(menuScr, LV_OPA_COVER, 0);

  lv_obj_t* title = lv_label_create(menuScr);
  lv_label_set_text(title, "CW ESSENTIALS");
  lv_obj_set_style_text_color(title, lv_color_hex(0x00E676), 0);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
#endif
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

  // Helper for menu buttons
  auto mkBtn = [](lv_obj_t* p, const char* t, const char* sub, uint32_t col, int16_t y) -> lv_obj_t* {
    lv_obj_t* b = lv_button_create(p);
    lv_obj_set_size(b, 220, 40);
    lv_obj_align(b, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(b, lv_color_hex(col), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 4, -7);
    lv_obj_t* s = lv_label_create(b);
    lv_label_set_text(s, sub);
    lv_obj_set_style_text_color(s, lv_color_hex(0x666666), 0);
    lv_obj_align(s, LV_ALIGN_LEFT_MID, 4, 8);
    return b;
  };

  lv_obj_t* b1 = mkBtn(menuScr, "MY CALLSIGN",
    "Build it up letter by letter", 0x42A5F5, 50);
  lv_obj_add_event_cb(b1, [](lv_event_t* e) { startGame(PH_CALLSIGN); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* b2 = mkBtn(menuScr, "PHRASES - GUIDED",
    "Hear it, learn meaning, copy it", 0x00E676, 100);
  lv_obj_add_event_cb(b2, [](lv_event_t* e) { startGame(PH_GUIDED); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* b3 = mkBtn(menuScr, "PHRASES - PRACTICE",
    "See phrase + meaning, key it", 0xFFB300, 150);
  lv_obj_add_event_cb(b3, [](lv_event_t* e) { startGame(PH_PRACTICE); }, LV_EVENT_CLICKED, NULL);

  // Back button
  lv_obj_t* bb = lv_button_create(menuScr);
  lv_obj_set_size(bb, 80, 24);
  lv_obj_align(bb, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_bg_color(bb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(bb, 0, 0);
  lv_obj_set_style_radius(bb, 4, 0);
  lv_obj_t* bl = lv_label_create(bb);
  lv_label_set_text(bl, "BACK");
  lv_obj_set_style_text_color(bl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(bb, exit_cb, LV_EVENT_CLICKED, NULL);

  lv_screen_load(menuScr);
}

static void exit_cb(lv_event_t* e) { Game_Phrases_Stop(); }

void Game_Phrases_Stop(void) {
  active = false;
  playing = false;
  Sidetone_Off();
  Keyer_OnChar([](char c) { UI_PushDecodedChar(c); });
  if (tickTmr) { lv_timer_delete(tickTmr); tickTmr = NULL; }
  if (scr) { lv_obj_delete(scr); scr = NULL; }
  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }
  overPanel = NULL; phraseLbl = NULL; meaningLbl = NULL;
  inputLbl = NULL; scoreLbl = NULL; statusLbl = NULL; progressLbl = NULL;
  UI_ShowMain();
}

void Game_Phrases_Update(void) {}
