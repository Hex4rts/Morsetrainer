// =============================================================================
//  MT_Game_Phrases.cpp — CW Essentials
//
//  Sequence-based Morse learning (Morse Trace style) with fixed curricula:
//
//   MY CALLSIGN (2-phase curriculum — e.g. for VE2HXR):
//     Phase 1 (single letters, one per stage):
//       Stage 0: V    Stage 1: E    Stage 2: 2
//       Stage 3: H    Stage 4: X    Stage 5: R
//     Phase 2 (growing sequences):
//       Stage 6: VE           Stage 7: VE2
//       Stage 8: VE2H         Stage 9: VE2HX
//       Stage 10: VE2HXR
//     Total stages = 2*callLen - 1.
//     User must correctly key the full current target to advance; N=2
//     consecutive complete rounds promote to the next stage.
//
//   PHRASES (GUIDED / PRACTICE):
//     Target = full phrase (e.g. "CQ", "QSL", "QTH"). User keys the whole
//     sequence. GUIDED plays the phrase through the sidetone first, then
//     waits for input. PRACTICE just shows + waits.
//
//  Display: full target shown big with per-letter coloring (done=green,
//  current=amber, remaining=gray). Reference Morse bars for the CURRENT
//  letter. Live user bars below for what's being keyed.
// =============================================================================

#include "MT_Game_Phrases.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Morse.h"
#include "MT_Sidetone.h"
#include "MT_NeoPixel.h"
#include "MT_Score.h"
#include "MT_Settings.h"
#include <lvgl.h>

// ── Timing helpers ──
#define TICK_MS 10
static inline uint16_t ditMs(void)     { return 1200 / Keyer_GetWPM(); }
static inline uint16_t ditTicks(void)  { uint16_t t = ditMs() / TICK_MS; return t ? t : 1; }

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

// ── Sizing ──
#define MAX_TARGET 12
#define MAX_BARS   16
#define BAR_DIT_W   7
#define BAR_DAH_W  19
#define BAR_H      10
#define BAR_GAP     2

// Target text layout
#define CHAR_W     22   // approximate pixel width for MONTSERRAT_24 with spacing

// Screen Y coords
#define Y_TARGET   40
#define Y_REFPAT   80
#define Y_REFBARS 100
#define Y_DIV     122
#define Y_INPLBL  128
#define Y_USRBARS 148
#define Y_STATUS  178

// ── Colors (match Morse Trace) ──
#define COL_DIT   lv_color_hex(0x42A5F5)
#define COL_DAH   lv_color_hex(0xFFB300)
#define COL_DONE  lv_color_hex(0x00E676)
#define COL_GRAY  lv_color_hex(0x555555)
#define COL_RED   lv_color_hex(0xFF3D00)

enum Mode { PH_CALLSIGN, PH_GUIDED, PH_PRACTICE };

// Stage advancement requirement
#define STAGE_ADVANCE 2   // consecutive complete rounds needed to advance stage

// ── UI objects ──
static lv_obj_t*   scr         = NULL;
static lv_obj_t*   menuScr     = NULL;
static lv_obj_t*   charLbls[MAX_TARGET] = {};   // per-letter labels (for color coding)
static lv_obj_t*   refPatLbl   = NULL;          // "- . -" text next to bars
static lv_obj_t*   hintLbl     = NULL;          // callsign/meaning hint
static lv_obj_t*   inputLbl    = NULL;          // "YOUR INPUT"
static lv_obj_t*   statusLbl   = NULL;
static lv_obj_t*   scoreLbl    = NULL;
static lv_obj_t*   progressLbl = NULL;
static lv_obj_t*   overPanel   = NULL;
static lv_obj_t*   refBars [MAX_BARS] = {};
static lv_obj_t*   userBars[MAX_BARS] = {};
static lv_timer_t* tickTmr     = NULL;

// ── State ──
static char     target[MAX_TARGET + 1];
static uint8_t  tLen      = 0;
static uint8_t  tPos      = 0;
static uint32_t score     = 0;
static uint16_t roundNum  = 0;
static uint16_t streak    = 0;
static bool     active    = false;
static Mode     mode      = PH_CALLSIGN;
static volatile char lastChar = 0;

// Callsign state
static char     callsign[12];
static uint8_t  callLen     = 0;     // number of chars in callsign
static uint8_t  callStage   = 0;     // 0..2*callLen-2
static uint8_t  callCorrect = 0;     // consecutive completions at current stage

// Phrase state
static int         lastPhraseIdx = -1;
static const char* curMeaning    = "";

// User visual pattern (bars), fed from element callback (iambic)
static char     userVisPat[MAX_BARS * 2 + 1];
static uint8_t  userVisLen = 0;
static volatile bool userVisDirty = false;

// Audio playback (guided mode + callsign demo)
static char     playBuf[MAX_TARGET * 8];
static int      playPos   = 0;
static bool     playTone  = false;
static uint16_t playCtr   = 0;        // counts down in ticks
static bool     playing   = false;
static bool     playDone  = false;
static int8_t   lastPlayedStage = -1; // callsign: last stage we auto-played audio for

// Pause state machine (replaces the old pauseCtr + pauseIsComplete bool)
//   PS_NONE   - normal play/input processing
//   PS_ROUND  - round just completed, wait then check stage advance
//   PS_BANNER - "NEW LETTER!/LEVEL UP!" shown, wait then load next stage
enum PauseState { PS_NONE, PS_ROUND, PS_BANNER };
static PauseState pauseState = PS_NONE;
static uint16_t   pauseCtr   = 0;

// ── Forward decls ──
static void exit_cb(lv_event_t* e);
static void startGame(Mode m);
static void setupNextTarget(void);
static void updateTargetDisplay(void);
static void updateCurrentLetter(void);
static void drawBars(lv_obj_t* bars[], const char* pat, int16_t x0, int16_t y);
static void clearBars(lv_obj_t* bars[]);

// ── Keyer callbacks ──
static void game_char(char c) { lastChar = c; UI_PushDecodedChar(c); }

static void game_element(bool state, bool isDah) {
  // Iambic elements arrive pre-classified on element start
  if (state && Keyer_GetMode() != KEYER_STRAIGHT) {
    if (userVisLen < (int)sizeof(userVisPat) - 1) {
      userVisPat[userVisLen++] = isDah ? '-' : '.';
      userVisPat[userVisLen]   = '\0';
      userVisDirty = true;
    }
    NeoPixel_KeyFlash(isDah);
  }
}

// ── Bar helpers ──
static void makeBars(lv_obj_t* parent, lv_obj_t* bars[]) {
  for (int i = 0; i < MAX_BARS; i++) {
    bars[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(bars[i]);
    lv_obj_set_size(bars[i], BAR_DIT_W, BAR_H);
    lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(bars[i], (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }
}

static void clearBars(lv_obj_t* bars[]) {
  for (int i = 0; i < MAX_BARS; i++) {
    if (bars[i]) lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
  }
}

static void drawBars(lv_obj_t* bars[], const char* pat, int16_t x0, int16_t y) {
  int16_t x = x0;
  int barIdx = 0;
  for (int i = 0; i < MAX_BARS; i++) {
    if (bars[i]) lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; pat[i] && barIdx < MAX_BARS; i++) {
    if (pat[i] == ' ') { x += 8; continue; }
    bool dah = (pat[i] == '-');
    int16_t w = dah ? BAR_DAH_W : BAR_DIT_W;
    if (!bars[barIdx]) continue;
    lv_obj_set_size(bars[barIdx], w, BAR_H);
    lv_obj_set_pos(bars[barIdx], x, y);
    lv_obj_set_style_bg_color(bars[barIdx], dah ? COL_DAH : COL_DIT, 0);
    lv_obj_set_style_bg_opa(bars[barIdx], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bars[barIdx], 2, 0);
    lv_obj_clear_flag(bars[barIdx], LV_OBJ_FLAG_HIDDEN);
    x += w + BAR_GAP;
    barIdx++;
  }
}

// ── Audio playback builder (guided mode) ──
// playBuf chars:  '.' dit   '-' dah   'g' intra-char gap (1 dit)
//                 'G' inter-char gap (3 dits)   'W' word gap (7 dits)
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

// ── Show full target with per-letter coloring ──
// Playback mode: current letter amber, others gray (follow the audio)
// User-turn mode: done=green, current=amber, remaining=gray
static void updateTargetDisplay(void) {
  // Total width, centered
  int16_t totalW  = tLen * CHAR_W;
  int16_t startX  = (320 - totalW) / 2;
  if (startX < 4) startX = 4;

  for (int i = 0; i < MAX_TARGET; i++) {
    if (!charLbls[i]) continue;
    if (i < tLen) {
      char s[2] = { target[i], '\0' };
      lv_label_set_text(charLbls[i], s);
      lv_obj_set_pos(charLbls[i], startX + i * CHAR_W, Y_TARGET);

      lv_color_t col;
      if (playing) {
        // Playback: follow the cursor, others dim
        col = (i == tPos) ? COL_DAH : COL_GRAY;
      } else if (tPos >= tLen) {
        col = COL_DONE;                        // round complete: all green
      } else if (i < tPos) {
        col = COL_DONE;                        // done letter
      } else if (i == tPos) {
        col = COL_DAH;                         // current letter
      } else {
        col = COL_GRAY;                        // remaining letter
      }
      lv_obj_set_style_text_color(charLbls[i], col, 0);
      lv_obj_clear_flag(charLbls[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(charLbls[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// ── Show reference pattern + bars for current letter ──
static void updateCurrentLetter(void) {
  // Clear user pattern for the new letter
  userVisLen = 0; userVisPat[0] = '\0';
  userVisDirty = true;

  if (tPos >= tLen) {
    if (refPatLbl) lv_label_set_text(refPatLbl, "");
    clearBars(refBars);
    return;
  }

  const char* pat = Morse_Encode(target[tPos]);
  if (!pat) pat = "";
  if (refPatLbl) lv_label_set_text(refPatLbl, pat);

  // Center the reference bars under target area
  int16_t w = 0;
  for (int i = 0; pat[i]; i++) {
    if (pat[i] == '-') w += BAR_DAH_W + BAR_GAP;
    else if (pat[i] == '.') w += BAR_DIT_W + BAR_GAP;
  }
  int16_t x0 = (320 - w) / 2;
  if (x0 < 4) x0 = 4;
  drawBars(refBars, pat, x0, Y_REFBARS);
}

// ── Set up target for current round ──
static void setupCallsignRound(void) {
  memset(target, 0, sizeof(target));

  // Phase 1: single letter (stages 0..callLen-1)
  // Phase 2: growing sequence (stages callLen..2*callLen-2)
  if (callStage < callLen) {
    target[0] = callsign[callStage];
    target[1] = '\0';
    tLen = 1;
  } else {
    uint8_t seqLen = (callStage - callLen) + 2;   // 2, 3, ..., callLen
    if (seqLen > callLen) seqLen = callLen;
    strncpy(target, callsign, seqLen);
    target[seqLen] = '\0';
    tLen = seqLen;
  }
  tPos = 0;

  char prog[40];
  if (callStage < callLen) {
    snprintf(prog, sizeof(prog), "Letter %d/%d  (%d/%d ok)",
             callStage + 1, callLen, callCorrect, STAGE_ADVANCE);
  } else {
    snprintf(prog, sizeof(prog), "Sequence %d/%d  (%d/%d ok)",
             callStage - callLen + 1, callLen - 1, callCorrect, STAGE_ADVANCE);
  }
  if (progressLbl) lv_label_set_text(progressLbl, prog);

  char hint[24];
  snprintf(hint, sizeof(hint), "( %s )", callsign);
  if (hintLbl) lv_label_set_text(hintLbl, hint);

  updateTargetDisplay();
  updateCurrentLetter();

  // Audio demo: play the target when entering a new stage (phase 1 single
  // letters AND phase 2 combos). Skip replay on repeat rounds of same stage.
  bool firstEntry = (callStage != lastPlayedStage);
  lastPlayedStage = callStage;
  if (firstEntry) {
    if (statusLbl) {
      lv_label_set_text(statusLbl, "LISTEN...");
      lv_obj_set_style_text_color(statusLbl, COL_DIT, 0);
    }
    buildPlayback(target);
    updateTargetDisplay();  // now playing=true, use playback colors
  } else {
    if (statusLbl) {
      lv_label_set_text(statusLbl, "KEY IT");
      lv_obj_set_style_text_color(statusLbl, COL_DIT, 0);
    }
    playDone = true; playing = false;
  }
}

static void setupPhraseRound(void) {
  int idx;
  do { idx = random(0, NUM_PHRASES); } while (idx == lastPhraseIdx && NUM_PHRASES > 1);
  lastPhraseIdx = idx;

  strncpy(target, phraseDB[idx].code, MAX_TARGET);
  target[MAX_TARGET] = '\0';
  tLen = strlen(target);
  tPos = 0;
  curMeaning = phraseDB[idx].meaning;

  char prog[24];
  snprintf(prog, sizeof(prog), "Phrase %d", roundNum + 1);
  if (progressLbl) lv_label_set_text(progressLbl, prog);
  if (hintLbl)     lv_label_set_text(hintLbl, curMeaning);

  updateTargetDisplay();
  updateCurrentLetter();

  if (mode == PH_GUIDED) {
    if (statusLbl) {
      lv_label_set_text(statusLbl, "LISTEN...");
      lv_obj_set_style_text_color(statusLbl, COL_DIT, 0);
    }
    buildPlayback(target);
    updateTargetDisplay();   // now playing=true, use playback colors
  } else {
    if (statusLbl) {
      lv_label_set_text(statusLbl, "KEY IT");
      lv_obj_set_style_text_color(statusLbl, COL_DIT, 0);
    }
    playDone = true; playing = false;
  }
}

static void setupNextTarget(void) {
  if (mode == PH_CALLSIGN) setupCallsignRound();
  else                     setupPhraseRound();
}

// ── Tick: pause SM + audio playback + user bars + input processing ──
static void tick_cb(lv_timer_t* t) {
  if (!active) return;

  // Refresh user bars if changed
  if (userVisDirty) {
    drawBars(userBars, userVisPat, 12, Y_USRBARS);
    userVisDirty = false;
  }

  // Pause state machine — replaces old delay() pattern and is the ONE place
  // where setupNextTarget() is called on pause expiry. This was the bug:
  // previously the banner pause never called setupNextTarget.
  if (pauseState != PS_NONE) {
    if (pauseCtr > 0) { pauseCtr--; return; }

    PauseState s = pauseState;
    pauseState   = PS_NONE;

    if (s == PS_ROUND) {
      // Round complete — decide if we advance stage (callsign mode)
      if (mode == PH_CALLSIGN) {
        callCorrect++;
        if (callCorrect >= STAGE_ADVANCE) {
          callCorrect = 0;
          uint8_t maxStage = (callLen * 2) - 2;
          if (callStage < maxStage) {
            callStage++;
            // lastPlayedStage kept: setupCallsignRound will detect new stage
            if (statusLbl) {
              lv_label_set_text(statusLbl,
                callStage < callLen ? "NEW LETTER!" : "LEVEL UP!");
              lv_obj_set_style_text_color(statusLbl, COL_DONE, 0);
            }
            NeoPixel_LevelUp();
            pauseCtr   = 50;
            pauseState = PS_BANNER;     // banner phase — next expiry loads next stage
            return;
          }
          // Already at final stage: loop on full callsign
        }
      }
      // Clear any stray input that arrived during the pause
      lastChar = 0;
      userVisLen = 0; userVisPat[0] = '\0'; userVisDirty = true;
      setupNextTarget();
    } else {  // PS_BANNER
      // Banner pause ended — NOW load the new stage (this was missing before)
      lastChar = 0;
      userVisLen = 0; userVisPat[0] = '\0'; userVisDirty = true;
      setupNextTarget();
    }
    return;
  }

  // Audio playback (guided mode + callsign demo)
  if (playing) {
    if (playCtr > 0) { playCtr--; return; }
    if (playBuf[playPos] == '\0') {
      // Playback finished — reset for user turn
      Sidetone_Off();
      playing = false; playDone = true;
      tPos = 0;
      lastChar = 0;
      userVisLen = 0; userVisPat[0] = '\0'; userVisDirty = true;
      updateTargetDisplay();    // now in user-turn colors (first letter amber)
      updateCurrentLetter();    // ref bars for letter 0
      if (statusLbl) {
        lv_label_set_text(statusLbl, "YOUR TURN");
        lv_obj_set_style_text_color(statusLbl, COL_DONE, 0);
      }
      return;
    }
    char elem = playBuf[playPos];
    if (elem == '.' || elem == '-') {
      if (!playTone) {
        Sidetone_On(); playTone = true;
        playCtr = (elem == '-') ? ditTicks() * 3 : ditTicks();
      } else {
        Sidetone_Off(); playTone = false;
        playCtr = ditTicks();
        playPos++;
      }
    } else if (elem == 'g') {
      playCtr = ditTicks();
      playPos++;
    } else if (elem == 'G') {
      // Inter-character gap — advance the highlighted letter so the user
      // SEES which char is being played next. This fixes "second char
      // animation failed to present".
      playCtr = ditTicks() * 3;
      playPos++;
      if (tPos + 1 < tLen) {
        tPos++;
        updateTargetDisplay();  // new letter gets amber highlight
        updateCurrentLetter();  // new ref bars for the letter about to play
      }
    } else if (elem == 'W') {
      playCtr = ditTicks() * 7;
      playPos++;
    }
    return;
  }

  if (!playDone) return;

  // Process decoded character from the keyer
  if (lastChar) {
    char c = lastChar;
    lastChar = 0;

    if (c == ' ') return;  // ignore word-space at letter level

    if (tPos < tLen && c == target[tPos]) {
      // Correct letter
      tPos++;
      score += 10;
      NeoPixel_Correct();

      char sb[16]; snprintf(sb, sizeof(sb), "%lu", score);
      if (scoreLbl) lv_label_set_text(scoreLbl, sb);

      if (tPos >= tLen) {
        // Round complete — full sequence keyed correctly
        streak++;
        roundNum++;
        updateTargetDisplay();                // all green
        if (refPatLbl) lv_label_set_text(refPatLbl, "");
        clearBars(refBars);
        clearBars(userBars);
        if (statusLbl) {
          lv_label_set_text(statusLbl, "COMPLETE!");
          lv_obj_set_style_text_color(statusLbl, COL_DONE, 0);
        }
        NeoPixel_LevelUp();
        pauseCtr   = 60;
        pauseState = PS_ROUND;
      } else {
        // More letters to go
        updateTargetDisplay();
        updateCurrentLetter();
        if (statusLbl) {
          lv_label_set_text(statusLbl, "NEXT");
          lv_obj_set_style_text_color(statusLbl, COL_DONE, 0);
        }
      }
    } else {
      // Wrong letter — reset the attempt, stay on same target
      // NOTE: we do NOT reset callCorrect here. A mid-round mistake shouldn't
      // wipe earned stage-progress. User can recover and complete the round.
      NeoPixel_Wrong();
      streak = 0;
      tPos = 0;
      userVisLen = 0; userVisPat[0] = '\0';
      clearBars(userBars);
      updateTargetDisplay();
      updateCurrentLetter();
      if (statusLbl) {
        lv_label_set_text(statusLbl, "TRY AGAIN");
        lv_obj_set_style_text_color(statusLbl, COL_RED, 0);
      }
    }
  }
}

// ── Game over screen ──
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
  lv_obj_set_style_border_color(overPanel, COL_DONE, 0);
  lv_obj_set_style_border_width(overPanel, 2, 0);

  char buf[80];
  if (mode == PH_CALLSIGN) {
    snprintf(buf, sizeof(buf), "SCORE: %lu\nSTAGE: %d/%d\nROUNDS: %d",
             score, callStage + 1, (callLen * 2) - 1, roundNum);
  } else {
    snprintf(buf, sizeof(buf), "SCORE: %lu\nPHRASES: %d\nSTREAK: %d",
             score, roundNum, streak);
  }
  lv_obj_t* lbl = lv_label_create(overPanel);
  lv_label_set_text(lbl, buf);
  lv_obj_set_style_text_color(lbl, COL_DONE, 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t* btn = lv_button_create(overPanel);
  lv_obj_set_size(btn, 100, 28);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(btn, COL_DAH, 0);
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
  userVisLen = 0; userVisPat[0] = '\0'; userVisDirty = false;
  lastChar = 0;
  lastPhraseIdx = -1;
  pauseCtr = 0; pauseState = PS_NONE;
  lastPlayedStage = -1;    // force audio demo for first stage

  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }

  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // Header: score | progress | EXIT
  scoreLbl = lv_label_create(scr);
  lv_label_set_text(scoreLbl, "0");
  lv_obj_set_style_text_color(scoreLbl, COL_DONE, 0);
  lv_obj_align(scoreLbl, LV_ALIGN_TOP_LEFT, 8, 4);

  progressLbl = lv_label_create(scr);
  lv_label_set_text(progressLbl, "");
  lv_obj_set_style_text_color(progressLbl, lv_color_hex(0x888888), 0);
  lv_obj_align(progressLbl, LV_ALIGN_TOP_MID, 0, 4);

  lv_obj_t* eb = lv_button_create(scr);
  lv_obj_set_size(eb, 42, 18);
  lv_obj_align(eb, LV_ALIGN_TOP_RIGHT, -4, 2);
  lv_obj_set_style_bg_color(eb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(eb, 0, 0);
  lv_obj_set_style_radius(eb, 4, 0);
  lv_obj_t* ebl = lv_label_create(eb);
  lv_label_set_text(ebl, "EXIT");
  lv_obj_set_style_text_color(ebl, COL_RED, 0);
  lv_obj_center(ebl);
  lv_obj_add_event_cb(eb, [](lv_event_t* e) { showGameOver(); }, LV_EVENT_CLICKED, NULL);

  // Hint line — callsign or phrase meaning
  hintLbl = lv_label_create(scr);
  lv_label_set_text(hintLbl, "");
  lv_obj_set_style_text_color(hintLbl, lv_color_hex(0x666666), 0);
  lv_obj_align(hintLbl, LV_ALIGN_TOP_MID, 0, 22);

  // Per-letter target labels (color-coded for sequence progress)
  for (int i = 0; i < MAX_TARGET; i++) {
    charLbls[i] = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(charLbls[i], &lv_font_montserrat_24, 0);
#endif
    lv_label_set_text(charLbls[i], "");
    lv_obj_set_style_text_color(charLbls[i], COL_GRAY, 0);
    lv_obj_add_flag(charLbls[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Reference pattern text (below target)
  refPatLbl = lv_label_create(scr);
  lv_label_set_text(refPatLbl, "");
  lv_obj_set_style_text_color(refPatLbl, lv_color_hex(0x555555), 0);
  lv_obj_align(refPatLbl, LV_ALIGN_TOP_MID, 0, Y_REFPAT);

  // Reference bars (positioned dynamically in updateCurrentLetter)
  makeBars(scr, refBars);

  // Divider
  lv_obj_t* div = lv_obj_create(scr);
  lv_obj_remove_style_all(div);
  lv_obj_set_size(div, 304, 1);
  lv_obj_set_pos(div, 8, Y_DIV);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

  // YOUR INPUT label
  inputLbl = lv_label_create(scr);
  lv_label_set_text(inputLbl, "YOUR INPUT");
  lv_obj_set_style_text_color(inputLbl, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(inputLbl, 8, Y_INPLBL);

  // User bars (drawn at Y_USRBARS by tick_cb)
  makeBars(scr, userBars);

  // Status
  statusLbl = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_16
  lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_16, 0);
#endif
  lv_label_set_text(statusLbl, "");
  lv_obj_set_style_text_color(statusLbl, COL_DIT, 0);
  lv_obj_align(statusLbl, LV_ALIGN_BOTTOM_MID, 0, -10);

  // Init mode
  if (mode == PH_CALLSIGN) {
    const mt_settings_t* s = Settings_Get();
    strncpy(callsign, s->callsign, sizeof(callsign) - 1);
    callsign[sizeof(callsign) - 1] = '\0';
    // Uppercase it
    for (int i = 0; callsign[i]; i++) {
      if (callsign[i] >= 'a' && callsign[i] <= 'z') callsign[i] -= 32;
    }
    callLen = strlen(callsign);
    if (callLen < 1 || callLen > MAX_TARGET) {
      strcpy(callsign, "N0CALL"); callLen = 6;
    }
    callStage = 0;
    callCorrect = 0;
    setupCallsignRound();
  } else {
    setupPhraseRound();
  }

  Keyer_OnChar(game_char);
  Keyer_OnElement(game_element);
  Keyer_SKFlush(true);

  tickTmr = lv_timer_create(tick_cb, TICK_MS, NULL);
  lv_screen_load(scr);
}

// ── Menu ──
void Game_Phrases_Start(void) {
  menuScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(menuScr, lv_color_hex(0x050510), 0);
  lv_obj_set_style_bg_opa(menuScr, LV_OPA_COVER, 0);

  lv_obj_t* title = lv_label_create(menuScr);
  lv_label_set_text(title, "CW ESSENTIALS");
  lv_obj_set_style_text_color(title, COL_DONE, 0);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
#endif
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

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
    "Letters, then sequences", 0x42A5F5, 50);
  lv_obj_add_event_cb(b1, [](lv_event_t* e) { startGame(PH_CALLSIGN); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* b2 = mkBtn(menuScr, "PHRASES - GUIDED",
    "Hear it, then key the sequence", 0x00E676, 100);
  lv_obj_add_event_cb(b2, [](lv_event_t* e) { startGame(PH_GUIDED); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* b3 = mkBtn(menuScr, "PHRASES - PRACTICE",
    "See phrase + meaning, key it", 0xFFB300, 150);
  lv_obj_add_event_cb(b3, [](lv_event_t* e) { startGame(PH_PRACTICE); }, LV_EVENT_CLICKED, NULL);

  lv_obj_t* bb = lv_button_create(menuScr);
  lv_obj_set_size(bb, 80, 24);
  lv_obj_align(bb, LV_ALIGN_BOTTOM_MID, 0, -12);
  lv_obj_set_style_bg_color(bb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(bb, 0, 0);
  lv_obj_set_style_radius(bb, 4, 0);
  lv_obj_t* bl = lv_label_create(bb);
  lv_label_set_text(bl, "BACK");
  lv_obj_set_style_text_color(bl, COL_RED, 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(bb, exit_cb, LV_EVENT_CLICKED, NULL);

  lv_screen_load(menuScr);
}

static void exit_cb(lv_event_t* e) { Game_Phrases_Stop(); }

void Game_Phrases_Stop(void) {
  active = false;
  playing = false;
  Sidetone_Off();
  // Restore default keyer handlers (matches Trainer/QSO)
  Keyer_OnChar([](char c) { UI_PushDecodedChar(c); });
  Keyer_OnElement([](bool s, bool d) { if (s) NeoPixel_KeyFlash(d); });
  if (tickTmr) { lv_timer_delete(tickTmr); tickTmr = NULL; }
  if (scr)     { lv_obj_delete(scr); scr = NULL; }
  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }
  overPanel = NULL; refPatLbl = NULL; hintLbl = NULL;
  inputLbl = NULL; statusLbl = NULL; scoreLbl = NULL; progressLbl = NULL;
  for (int i = 0; i < MAX_TARGET; i++) charLbls[i] = NULL;
  for (int i = 0; i < MAX_BARS; i++)   { refBars[i] = NULL; userBars[i] = NULL; }
  UI_ShowMain();
}

void Game_Phrases_Update(void) {}
