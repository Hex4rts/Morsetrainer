#include "MT_Game_Trainer.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Morse.h"
#include "MT_Sidetone.h"
#include "MT_NeoPixel.h"
#include "MT_Score.h"
#include <SD_MMC.h>
#include <lvgl.h>

// ── Learning order: simplest morse first ──
static const char LEARN[] = "ETIANMSURWDKGOHVFLPJBXCZYQ0123456789";
#define LEARN_COUNT  35
#define MASTER_NEED  3   // correct streak to master a char
#define REVIEW_EVERY 3   // review old chars every N rounds
#define MAX_COMBO    6   // max letters per challenge

// Playback speed from keyer
static inline uint16_t ditMs(void) { return 1200 / Keyer_GetWPM(); }

// ── Bar drawing ──
#define MAX_BARS  30
#define BAR_DIT_W  7
#define BAR_DAH_W  19
#define BAR_H      10
#define BAR_GAP    2
#define LETTER_GAP 8

// ── Phases ──
enum Phase { PH_MENU, PH_INTRO, PH_LISTEN, PH_WAITING, PH_FEEDBACK };

// ── Difficulty (what reference shows) ──
enum Difficulty { DIFF_BEGINNER, DIFF_INTERMEDIATE, DIFF_EXPERT, DIFF_LEARN };

// ── Per-character tracking ──
typedef struct {
  uint8_t streak;     // consecutive correct
  uint8_t attempts;   // total attempts
  bool    mastered;   // streak >= MASTER_NEED
  bool    introduced; // has been shown to user
} char_progress_t;

// ── LEARN mode state ──
#define DRILL_COUNT       4   // assisted rounds before testing
#define LEARN_STREAK_NEED 3   // unassisted correct before introducing next letter
#define LEARN_MAX_TRIES   3   // attempts per appearance before re-drilling
static uint8_t learnDrill[LEARN_COUNT];  // >0 = remaining assisted rounds, 0 = learned
static uint8_t learnNextIdx = 0;         // next new letter to introduce
static uint8_t learnStreak  = 0;         // consecutive unassisted correct
static bool    learnAssisted = false;    // is current round assisted?
static uint8_t learnCurrentChar = 0;     // which char index current round tests
static uint8_t learnTriesLeft = LEARN_MAX_TRIES;  // tries remaining THIS appearance


// ── State ──
static char_progress_t progress[LEARN_COUNT];
static uint8_t  curCharIdx   = 0;     // index into LEARN[] being taught
static uint8_t  comboLen     = 1;     // 1=single, 2=pairs, 3=triples...
static uint8_t  roundCount   = 0;     // rounds since last review
static uint32_t score        = 0;
static bool     active       = false;
static Difficulty diff       = DIFF_BEGINNER;

// Should reference bars be shown during playback?
static inline bool showRefBars(void) {
  return (diff == DIFF_BEGINNER) || (diff == DIFF_LEARN && learnAssisted);
}

// Current challenge
static char challenge[MAX_COMBO + 1];
static char fullRef[MAX_COMBO * 8];

// Playback
static int      playPos    = 0;
static bool     playTone   = false;
static uint16_t playCtr    = 0;
static Phase    phase      = PH_MENU;
static uint32_t phaseStart = 0;

// Char/element buffers
#define CBUF 8
static volatile char cbuf[CBUF];
static volatile uint8_t cbW = 0, cbR = 0;
#define EBUF 16
static volatile uint8_t ebuf[EBUF];
static volatile uint8_t ebW = 0, ebR = 0;

// User visual pattern
static char    userVisPat[MAX_BARS * 2 + 1];
static uint8_t userVisLen = 0;
static char    userInput[MAX_COMBO + 1];
static uint8_t userLen = 0;

// Persistence
#define SAVE_FILE "/trainer2.txt"
static uint32_t bestScore = 0;

// ── UI objects ──
static lv_obj_t*   scr        = NULL;
static lv_obj_t*   menuScr    = NULL;
static lv_obj_t*   titleLbl   = NULL;
static lv_obj_t*   challengeLbl = NULL;
static lv_obj_t*   refPatLbl  = NULL;
static lv_obj_t*   promptLbl  = NULL;
static lv_obj_t*   userCharLbl= NULL;
static lv_obj_t*   feedbackLbl= NULL;
static lv_obj_t*   progressLbl= NULL;
static lv_obj_t*   refBars[MAX_BARS]  = {};
static lv_obj_t*   userBars[MAX_BARS] = {};
static lv_timer_t* tickTmr    = NULL;

// ── Callbacks ──
static void game_char(char c) {
  cbuf[cbW % CBUF] = c; cbW++;
  UI_PushDecodedChar(c);
}
static void game_element(bool state, bool isDah) {
  if (state && Keyer_GetMode() != KEYER_STRAIGHT) {
    // Iambic: elements are pre-classified
    ebuf[ebW % EBUF] = isDah ? 1 : 0; ebW++;
    NeoPixel_KeyFlash(isDah);
  }
  // Straight key: elements handled by timing in tick_cb
}
static char popChar(void) {
  if (cbR == cbW) return 0;
  return cbuf[cbR++ % CBUF];
}

// ── Colors ──
#define COL_DIT  lv_color_hex(0x42A5F5)  // light blue
#define COL_DAH  lv_color_hex(0xFFB300)  // amber
#define COL_LIVE lv_color_hex(0x888888)

// Straight key bar storage — fed from ISR event log, never misses events
#define SK_MAX_BARS  20
#define COL_SK  lv_color_hex(0x00E676)
typedef struct { int16_t x; int16_t w; } sk_bar_t;
static sk_bar_t skBars[SK_MAX_BARS];
static uint8_t  skBarCount = 0;
static int16_t  skNextX    = 2;

static void skClearBars(void) {
  skBarCount = 0; skNextX = 2;
  Keyer_SKFlush(true);
  for (int i = 0; i < MAX_BARS; i++) lv_obj_add_flag(userBars[i], LV_OBJ_FLAG_HIDDEN);
}

// Read completed elements from ISR log, add to bar array
static void skReadElements(void) {
  uint16_t dur;
  while (Keyer_SKPopElement(&dur, true)) {
    if (skBarCount < SK_MAX_BARS) {
      int16_t w = dur / 2;  // 2ms per pixel — fixed scale
      if (w < 2) w = 2;
      skBars[skBarCount].x = skNextX;
      skBars[skBarCount].w = w;
      skBarCount++;
      skNextX += w + BAR_GAP;
    }
  }
}

// Draw frozen bars + live growing bar
static void skDrawAll(int16_t y) {
  for (int i = 0; i < MAX_BARS; i++) lv_obj_add_flag(userBars[i], LV_OBJ_FLAG_HIDDEN);
  int idx = 0;
  for (int i = 0; i < (int)skBarCount && idx < MAX_BARS; i++) {
    lv_obj_set_size(userBars[idx], skBars[i].w, BAR_H);
    lv_obj_set_pos(userBars[idx], skBars[i].x, y);
    lv_obj_set_style_bg_color(userBars[idx], COL_SK, 0);
    lv_obj_set_style_bg_opa(userBars[idx], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(userBars[idx], 2, 0);
    lv_obj_clear_flag(userBars[idx], LV_OBJ_FLAG_HIDDEN);
    idx++;
  }
  // Live growing bar — reads ISR counter directly (updates every 1ms)
  if (Keyer_SKIsDown() && idx < MAX_BARS) {
    int16_t w = Keyer_SKHeldMs() / 2;
    if (w < 2) w = 2;
    lv_obj_set_size(userBars[idx], w, BAR_H);
    lv_obj_set_pos(userBars[idx], skNextX, y);
    lv_obj_set_style_bg_color(userBars[idx], COL_SK, 0);
    lv_obj_set_style_bg_opa(userBars[idx], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(userBars[idx], 2, 0);
    lv_obj_clear_flag(userBars[idx], LV_OBJ_FLAG_HIDDEN);
  }
}

// ── Bar drawing ──
static void drawBars(lv_obj_t* bars[], const char* pat, int16_t y) {
  int16_t x = 8;
  int barIdx = 0;
  for (int i = 0; i < MAX_BARS; i++) lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; pat[i] && barIdx < MAX_BARS; i++) {
    if (pat[i] == ' ') { x += LETTER_GAP; continue; }
    bool dah = (pat[i] == '-');
    int16_t w = dah ? BAR_DAH_W : BAR_DIT_W;
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

// (drawLiveBar replaced by skDrawAll for straight key)
static void clearBars(lv_obj_t* bars[]) {
  for (int i = 0; i < MAX_BARS; i++) lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
}
static void makeBars(lv_obj_t* parent, lv_obj_t* bars[]) {
  for (int i = 0; i < MAX_BARS; i++) {
    bars[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(bars[i]);
    lv_obj_set_size(bars[i], BAR_DIT_W, BAR_H);
    lv_obj_add_flag(bars[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(bars[i], (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }
}

// ── Build morse string ──
static void buildRef(void) {
  fullRef[0] = '\0';
  for (int i = 0; challenge[i]; i++) {
    if (i > 0) strcat(fullRef, " ");
    const char* c = Morse_Encode(challenge[i]);
    if (c) strcat(fullRef, c);
  }
}

// ── Count mastered chars ──
static uint8_t countMastered(void) {
  uint8_t n = 0;
  for (int i = 0; i < LEARN_COUNT; i++) if (progress[i].mastered) n++;
  return n;
}

// ── Find next char to teach ──
static uint8_t findNextUnmastered(void) {
  for (int i = 0; i < LEARN_COUNT; i++) {
    if (!progress[i].mastered) return i;
  }
  return 0; // all mastered — wrap
}

// ── Pick a review char (random from mastered pool) ──
static char pickReviewChar(void) {
  uint8_t mastered[LEARN_COUNT];
  uint8_t cnt = 0;
  for (int i = 0; i < LEARN_COUNT; i++) {
    if (progress[i].mastered) mastered[cnt++] = i;
  }
  if (cnt == 0) return LEARN[0];
  return LEARN[mastered[random(0, cnt)]];
}

// ── Pick a weak char (mastered but lowest streak, for review mix) ──
static char pickWeakChar(void) {
  // Find char with attempts but not strong mastery
  uint8_t best = 0;
  uint8_t bestStreak = 255;
  bool found = false;
  for (int i = 0; i < LEARN_COUNT; i++) {
    if (progress[i].introduced && progress[i].streak < bestStreak) {
      best = i; bestStreak = progress[i].streak; found = true;
    }
  }
  return found ? LEARN[best] : LEARN[0];
}

// ── Save/Load progress to SD ──
static void saveProgress(void) {
  File f = SD_MMC.open(SAVE_FILE, FILE_WRITE);
  if (!f) return;
  f.printf("combo=%d\n", comboLen);
  f.printf("score=%lu\n", score > bestScore ? score : bestScore);
  f.printf("curidx=%d\n", curCharIdx);
  f.printf("lnext=%d\n", learnNextIdx);
  for (int i = 0; i < LEARN_COUNT; i++) {
    f.printf("c%d=%d,%d,%d,%d\n", i, progress[i].streak,
             progress[i].attempts, progress[i].mastered ? 1 : 0,
             progress[i].introduced ? 1 : 0);
  }
  for (int i = 0; i < LEARN_COUNT; i++) {
    f.printf("d%d=%d\n", i, learnDrill[i]);
  }
  f.close();
  if (score > bestScore) bestScore = score;
}

static void loadProgress(void) {
  memset(progress, 0, sizeof(progress));
  memset(learnDrill, 0, sizeof(learnDrill));
  curCharIdx = 0; comboLen = 1; bestScore = 0; learnNextIdx = 0;
  if (!SD_MMC.exists(SAVE_FILE)) return;
  File f = SD_MMC.open(SAVE_FILE, FILE_READ);
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    if (key == "combo") comboLen = val.toInt();
    else if (key == "score") bestScore = val.toInt();
    else if (key == "curidx") curCharIdx = val.toInt();
    else if (key == "lnext") learnNextIdx = val.toInt();
    else if (key.startsWith("d")) {
      int idx = key.substring(1).toInt();
      if (idx >= 0 && idx < LEARN_COUNT) learnDrill[idx] = val.toInt();
    }
    else if (key.startsWith("c")) {
      int idx = key.substring(1).toInt();
      if (idx >= 0 && idx < LEARN_COUNT) {
        int v[4] = {};
        int p = 0;
        for (int i = 0; i < 4 && p >= 0; i++) {
          int comma = val.indexOf(',', p);
          if (comma < 0) { v[i] = val.substring(p).toInt(); break; }
          v[i] = val.substring(p, comma).toInt();
          p = comma + 1;
        }
        progress[idx].streak = v[0];
        progress[idx].attempts = v[1];
        progress[idx].mastered = v[2] != 0;
        progress[idx].introduced = v[3] != 0;
      }
    }
  }
  f.close();
}

// ── Update progress display ──
static void updateProgressBar(void) {
  if (!progressLbl) return;
  char buf[48];

  if (diff == DIFF_LEARN) {
    // Count learned letters in LEARN mode
    uint8_t learned = 0;
    for (int i = 0; i < LEARN_COUNT; i++)
      if (i < (int)learnNextIdx && learnDrill[i] == 0) learned++;
    snprintf(buf, sizeof(buf), "%d/%d learned", learned, LEARN_COUNT);
  } else {
    uint8_t m = countMastered();
    if (comboLen == 1) {
      snprintf(buf, sizeof(buf), "%d/%d letters   combo:%d", m, LEARN_COUNT, comboLen);
    } else {
      snprintf(buf, sizeof(buf), "ALL LETTERS   combo:%d", comboLen);
    }
  }
  lv_label_set_text(progressLbl, buf);
}

// ── Generate next challenge ──
static void nextChallenge(void);

// ── Start playback of current challenge ──
static void startPlayback(void) {
  buildRef();
  playPos = 0; playCtr = ditMs() / 10; playTone = false;

  if (diff == DIFF_LEARN) {
    // LEARN mode: assisted = letter + sound + bars, unassisted = letter only
    lv_label_set_text(challengeLbl, challenge);
    lv_label_set_text(userCharLbl, "");
    lv_label_set_text(feedbackLbl, "");
    clearBars(userBars); skClearBars();
    cbR = cbW; ebR = ebW;
    userLen = 0; userInput[0] = '\0';
    userVisLen = 0; userVisPat[0] = '\0';
    if (learnAssisted) {
      // Drill: show letter + pattern text + play sound + show bars
      lv_label_set_text(refPatLbl, fullRef);
      clearBars(refBars);
      phase = PH_LISTEN;
      lv_label_set_text(promptLbl, "COPY IT...");
      lv_obj_set_style_text_color(promptLbl, lv_color_hex(0x42A5F5), 0);
    } else {
      // Test: just the letter, NO sound, NO bars
      lv_label_set_text(refPatLbl, "");
      clearBars(refBars);
      phase = PH_WAITING;
      phaseStart = millis();
      lv_label_set_text(promptLbl, "FROM MEMORY!");
      lv_obj_set_style_text_color(promptLbl, lv_color_hex(0x00E676), 0);
    }
    return;  // don't fall through to generic setup
  }

  // Other modes: BEGINNER, INTERMEDIATE, EXPERT
  if (diff == DIFF_EXPERT) {
    lv_label_set_text(challengeLbl, "?");
  } else {
    lv_label_set_text(challengeLbl, challenge);
  }

  if (showRefBars()) {
    lv_label_set_text(refPatLbl, fullRef);
  } else {
    lv_label_set_text(refPatLbl, "");
  }

  clearBars(refBars);
  phase = PH_LISTEN;
  lv_label_set_text(promptLbl, "LISTEN...");
  lv_obj_set_style_text_color(promptLbl, lv_color_hex(0xFFB300), 0);

  lv_label_set_text(userCharLbl, "");
  lv_label_set_text(feedbackLbl, "");
  clearBars(userBars); skClearBars();
  cbR = cbW; ebR = ebW;
  userLen = 0; userInput[0] = '\0';
  userVisLen = 0; userVisPat[0] = '\0';
}

// ── Introduction: show letter, play it, user just watches ──
static void introduceChar(uint8_t idx) {
  challenge[0] = LEARN[idx];
  challenge[1] = '\0';
  progress[idx].introduced = true;

  char buf[32];
  const char* code = Morse_Encode(LEARN[idx]);
  snprintf(buf, sizeof(buf), "NEW: %c = %s", LEARN[idx], code ? code : "?");
  lv_label_set_text(titleLbl, buf);
  lv_obj_set_style_text_color(titleLbl, lv_color_hex(0x42A5F5), 0);

  // Always show bars and sound for introduction regardless of difficulty
  buildRef();
  lv_label_set_text(challengeLbl, challenge);
  lv_label_set_text(refPatLbl, fullRef);
  clearBars(refBars);
  drawBars(refBars, fullRef, 68);

  playPos = 0; playCtr = ditMs() / 10; playTone = false;
  phase = PH_INTRO;
  phaseStart = millis();

  lv_label_set_text(promptLbl, "WATCH & LISTEN");
  lv_obj_set_style_text_color(promptLbl, lv_color_hex(0x42A5F5), 0);
  lv_label_set_text(userCharLbl, "");
  lv_label_set_text(feedbackLbl, "");
  clearBars(userBars); skClearBars();
}

// ── Generate next challenge ──
static void nextChallenge(void) {
  roundCount++;

  // ── LEARN mode: completely separate logic ──
  if (diff == DIFF_LEARN) {
    static uint8_t consecutiveDrills = 0;
    static uint8_t lastPickedChar = 255;  // avoid repeats

    // 1. If no letters introduced yet, start with the first one
    if (learnNextIdx == 0 && learnDrill[0] == 0) {
      learnDrill[0] = DRILL_COUNT;
      learnNextIdx = 1;
    }

    // 2. Find if any letter needs drilling
    int drillingIdx = -1;
    for (int i = 0; i < LEARN_COUNT; i++) {
      if (learnDrill[i] > 0) { drillingIdx = i; break; }
    }

    // 3. Count learned letters available for recall
    uint8_t pool[LEARN_COUNT];
    uint8_t poolCount = 0;
    for (int i = 0; i < LEARN_COUNT; i++) {
      if (i < (int)learnNextIdx && learnDrill[i] == 0 && i != lastPickedChar) {
        pool[poolCount++] = i;
      }
    }

    // 4. Decide: drill or recall?
    // - Need at least 3 learned letters before recall is meaningful
    // - After that, max 2 consecutive drills then force recall
    // - If no drilling needed, always recall
    bool doDrill = false;
    if (drillingIdx >= 0) {
      if (poolCount < 3 || consecutiveDrills < 2) {
        doDrill = true;  // not enough pool yet, or still within drill burst
      } else {
        doDrill = false;  // force recall after 2 drills
      }
    }

    if (doDrill && drillingIdx >= 0) {
      // Assisted drill round
      learnAssisted = true;
      learnCurrentChar = drillingIdx;
      challenge[0] = LEARN[drillingIdx];
      challenge[1] = '\0';
      consecutiveDrills++;

      char buf[32];
      snprintf(buf, sizeof(buf), "DRILL: %c  (%d left)", LEARN[drillingIdx], learnDrill[drillingIdx]);
      lv_label_set_text(titleLbl, buf);
      lv_obj_set_style_text_color(titleLbl, lv_color_hex(0x42A5F5), 0);
    } else {
      // Recall round — pick random from learned pool
      learnAssisted = false;
      learnTriesLeft = LEARN_MAX_TRIES;
      consecutiveDrills = 0;

      // Include lastPickedChar back if pool too small
      if (poolCount == 0) {
        for (int i = 0; i < LEARN_COUNT; i++) {
          if (i < (int)learnNextIdx && learnDrill[i] == 0) {
            pool[poolCount++] = i;
          }
        }
      }

      if (poolCount == 0) {
        // Edge case: nothing learned yet
        learnDrill[0] = DRILL_COUNT;
        nextChallenge();
        return;
      }

      uint8_t pick = pool[random(0, poolCount)];
      lastPickedChar = pick;
      learnCurrentChar = pick;
      challenge[0] = LEARN[pick];
      challenge[1] = '\0';

      lv_label_set_text(titleLbl, "RECALL");
      lv_obj_set_style_text_color(titleLbl, lv_color_hex(0x00E676), 0);
    }

    startPlayback();
    updateProgressBar();
    return;
  }

  // ── Other modes (BEGINNER/INTERMEDIATE/EXPERT) ──

  // Find current learning target
  curCharIdx = findNextUnmastered();
  bool allMastered = (countMastered() >= LEARN_COUNT);

  // If all single chars mastered, bump combo length
  if (allMastered && comboLen < MAX_COMBO) {
    comboLen++;
    // Reset streaks for combo practice
    for (int i = 0; i < LEARN_COUNT; i++) {
      progress[i].streak = 0;
      progress[i].mastered = false;
    }
    curCharIdx = 0;
    saveProgress();
  }

  if (comboLen == 1) {
    // ── Single letter learning ──
    if (!progress[curCharIdx].introduced) {
      // First time seeing this letter — introduce it
      introduceChar(curCharIdx);
      return;
    }

    // Every REVIEW_EVERY rounds, review a previously mastered char
    if (roundCount % REVIEW_EVERY == 0 && countMastered() > 0) {
      challenge[0] = pickReviewChar();
      challenge[1] = '\0';
      lv_label_set_text(titleLbl, "REVIEW");
      lv_obj_set_style_text_color(titleLbl, lv_color_hex(0x666666), 0);
    } else {
      // Practice current char
      challenge[0] = LEARN[curCharIdx];
      challenge[1] = '\0';
      char buf[24];
      snprintf(buf, sizeof(buf), "PRACTICE: %c", LEARN[curCharIdx]);
      lv_label_set_text(titleLbl, buf);
      lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xFFB300), 0);
    }
  } else {
    // ── Multi-letter combos ──
    // Mix current learning target with mastered chars
    for (int i = 0; i < (int)comboLen; i++) {
      if (i == 0) {
        challenge[i] = LEARN[curCharIdx];
      } else {
        challenge[i] = pickReviewChar();
      }
    }
    challenge[comboLen] = '\0';
    char buf[24];
    snprintf(buf, sizeof(buf), "COMBO x%d", comboLen);
    lv_label_set_text(titleLbl, buf);
    lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xFFB300), 0);
  }

  startPlayback();
  updateProgressBar();
}

// ── Handle answer ──
static void checkAnswer(void) {
  bool correct = (strcmp(userInput, challenge) == 0);

  // ── LEARN mode handling ──
  if (diff == DIFF_LEARN) {
    if (learnAssisted) {
      // Assisted round — user is copying
      if (correct) {
        learnDrill[learnCurrentChar]--;
        if (learnDrill[learnCurrentChar] == 0) {
          // Count learned pool
          uint8_t learnedNow = 0;
          for (int i = 0; i < LEARN_COUNT; i++)
            if (i < (int)learnNextIdx && learnDrill[i] == 0) learnedNow++;
          // Auto-introduce next if pool < 3
          if (learnedNow < 3 && learnNextIdx < LEARN_COUNT) {
            learnDrill[learnNextIdx] = DRILL_COUNT;
            learnNextIdx++;
          }
          char buf[32];
          snprintf(buf, sizeof(buf), "LEARNED: %c", LEARN[learnCurrentChar]);
          lv_label_set_text(feedbackLbl, buf);
          lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0x42A5F5), 0);
          NeoPixel_LevelUp();
          saveProgress();
        } else {
          char buf[32];
          snprintf(buf, sizeof(buf), "GOOD  (%d more)", learnDrill[learnCurrentChar]);
          lv_label_set_text(feedbackLbl, buf);
          lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0x00E676), 0);
          NeoPixel_Correct();
        }
      } else {
        // Wrong during assisted — try again (don't decrement)
        lv_label_set_text(feedbackLbl, "TRY AGAIN");
        lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0xFF3D00), 0);
        NeoPixel_Wrong();
      }
    } else {
      // Unassisted round — recall from memory, 3 tries per appearance
      if (correct) {
        learnStreak++;
        score += 10;
        lv_label_set_text(feedbackLbl, "CORRECT!");
        lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0x00E676), 0);
        NeoPixel_Correct();
        learnTriesLeft = LEARN_MAX_TRIES;  // reset for next appearance
        // Introduce next letter after streak
        if (learnStreak >= LEARN_STREAK_NEED && learnNextIdx < LEARN_COUNT) {
          learnDrill[learnNextIdx] = DRILL_COUNT;
          learnNextIdx++;
          learnStreak = 0;
          saveProgress();
        }
      } else {
        learnTriesLeft--;
        learnStreak = 0;
        if (learnTriesLeft == 0) {
          // Out of tries — re-drill this letter
          learnDrill[learnCurrentChar] = DRILL_COUNT;
          learnTriesLeft = LEARN_MAX_TRIES;
          char buf[32];
          snprintf(buf, sizeof(buf), "RE-DRILL %c", LEARN[learnCurrentChar]);
          lv_label_set_text(feedbackLbl, buf);
          lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0xFF3D00), 0);
          NeoPixel_Wrong();
          saveProgress();
        } else {
          // Wrong but tries remain — will retry SAME letter
          char buf[32];
          snprintf(buf, sizeof(buf), "WRONG  (%d tries left)", learnTriesLeft);
          lv_label_set_text(feedbackLbl, buf);
          lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0xFFB300), 0);
          NeoPixel_Wrong();
          // Don't advance — replay same challenge after feedback
          phase = PH_FEEDBACK;
          phaseStart = millis();
          return;  // skip the normal nextChallenge path
        }
      }
    }
    phase = PH_FEEDBACK;
    phaseStart = millis();
    return;
  }

  // ── Other modes ──
  if (comboLen == 1) {
    // Find which char we were testing
    int idx = -1;
    for (int i = 0; i < LEARN_COUNT; i++) {
      if (LEARN[i] == challenge[0]) { idx = i; break; }
    }
    if (idx >= 0) {
      progress[idx].attempts++;
      if (correct) {
        progress[idx].streak++;
        if (progress[idx].streak >= MASTER_NEED && !progress[idx].mastered) {
          progress[idx].mastered = true;
          char buf[32];
          snprintf(buf, sizeof(buf), "MASTERED: %c !", LEARN[idx]);
          lv_label_set_text(feedbackLbl, buf);
          lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0x42A5F5), 0);
          NeoPixel_LevelUp();
          saveProgress();
          phase = PH_FEEDBACK;
          phaseStart = millis();
          score += 20;
          return;
        }
      } else {
        progress[idx].streak = 0; // reset streak on wrong
      }
    }
  }

  if (correct) {
    score += 10;
    lv_label_set_text(feedbackLbl, "CORRECT!");
    lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0x00E676), 0);
    NeoPixel_Correct();
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "WRONG - was %s", challenge);
    lv_label_set_text(feedbackLbl, buf);
    lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0xFF3D00), 0);
    NeoPixel_Wrong();
  }

  phase = PH_FEEDBACK;
  phaseStart = millis();
}

// ── Tick (10ms) ──
static void tick_cb(lv_timer_t* t) {
  if (!active) return;

  switch (phase) {

    // ── INTRO: play reference, user just watches, then auto-advance to practice ──
    case PH_INTRO: {
      if (playCtr > 0) { playCtr--; break; }
      int refLen = strlen(fullRef);
      if (playPos >= refLen) {
        Sidetone_Off();
        // Wait 1s then start practice of this char
        if (millis() - phaseStart > 2000) {
          lv_label_set_text(titleLbl, "NOW YOU TRY");
          lv_obj_set_style_text_color(titleLbl, lv_color_hex(0x00E676), 0);
          startPlayback(); // plays it again, then user tries
        }
        break;
      }
      // Play the reference
      char elem = fullRef[playPos];
      if (!playTone) {
        // Inter-character gap: standard Morse is 3 dits total. The preceding
        // tone-off already emitted 1 dit of silence, so add 2 more dits here.
        // (Was `* 4` which produced 5-dit gaps — 2 too long.)
        if (elem == ' ') { playPos++; playCtr = (ditMs() * 2) / 10; }
        else { Sidetone_On(); playCtr = (elem == '.') ? (ditMs() / 10) : ((ditMs() * 3) / 10); playTone = true;
          if (showRefBars()) {
            char partial[MAX_BARS * 2 + 1]; int j = 0;
            for (int i = 0; i <= playPos && i < refLen && j < (int)sizeof(partial)-1; i++) partial[j++] = fullRef[i];
            partial[j] = '\0'; drawBars(refBars, partial, 68);
          }
        }
      } else { Sidetone_Off(); playTone = false; playPos++; playCtr = ditMs() / 10; }
      break;
    }

    // ── LISTEN: play reference sound ──
    case PH_LISTEN: {
      if (playCtr > 0) { playCtr--; break; }
      int refLen = strlen(fullRef);
      if (playPos >= refLen) {
        Sidetone_Off();
        if (showRefBars()) drawBars(refBars, fullRef, 68);
        // Expert: reveal the letter now that audio is done
        if (diff == DIFF_EXPERT) lv_label_set_text(challengeLbl, challenge);
        phase = PH_WAITING;
        phaseStart = millis();
        lv_label_set_text(promptLbl, "YOUR TURN!");
        lv_obj_set_style_text_color(promptLbl, lv_color_hex(0x00E676), 0);
        cbR = cbW; ebR = ebW;
        userLen = 0; userInput[0] = '\0';
        userVisLen = 0; userVisPat[0] = '\0';
        clearBars(userBars); skClearBars();
        break;
      }
      char elem = fullRef[playPos];
      if (!playTone) {
        // Inter-character gap: 3-dit standard = 1 (from tone-off) + 2 here.
        if (elem == ' ') { playPos++; playCtr = (ditMs() * 2) / 10; }
        else {
          Sidetone_On();
          playCtr = (elem == '.') ? (ditMs() / 10) : ((ditMs() * 3) / 10);
          playTone = true;
          if (showRefBars()) {
            char partial[MAX_BARS * 2 + 1]; int j = 0;
            for (int i = 0; i <= playPos && i < refLen && j < (int)sizeof(partial)-1; i++) partial[j++] = fullRef[i];
            partial[j] = '\0'; drawBars(refBars, partial, 68);
          }
        }
      } else { Sidetone_Off(); playTone = false; playPos++; playCtr = ditMs() / 10; }
      break;
    }

    // ── WAITING: user keys the answer ──
    case PH_WAITING: {
      bool isStraight = (Keyer_GetMode() == KEYER_STRAIGHT);
      bool sending = Keyer_IsSending();

      if (isStraight) {
        // ── Straight key: read ISR event log, draw bars ──
        skReadElements();
        skDrawAll(148);
      } else {
        // ── Iambic: use element buffer ──
        while (ebR != ebW) {
          uint8_t isDah = ebuf[ebR % EBUF]; ebR++;
          if (userVisLen < (int)sizeof(userVisPat) - 1) {
            userVisPat[userVisLen++] = isDah ? '-' : '.';
            userVisPat[userVisLen] = '\0';
            drawBars(userBars, userVisPat, 148);
          }
        }
      }

      // Decoded chars (both modes)
      char c = popChar();
      if (c && c != ' ') {
        uint8_t cLen = strlen(challenge);
        if (userLen < cLen) {
          userInput[userLen++] = toupper(c);
          userInput[userLen] = '\0';
          lv_label_set_text(userCharLbl, userInput);
          if (userLen < cLen && userVisLen < (int)sizeof(userVisPat) - 2) {
            userVisPat[userVisLen++] = ' ';
            userVisPat[userVisLen] = '\0';
            skNextX += LETTER_GAP;  // add visual gap for straight key bars too
          }
          if (userLen >= cLen) checkAnswer();
        }
      }
      // Prompt
      if (sending) {
        lv_label_set_text(promptLbl, "KEYING...");
        lv_obj_set_style_text_color(promptLbl, lv_color_hex(0xFF3D00), 0);
      } else if (userLen == 0 && userVisLen == 0) {
        lv_label_set_text(promptLbl, "YOUR TURN!");
        lv_obj_set_style_text_color(promptLbl, lv_color_hex(0x00E676), 0);
      }
      // Timeout
      if (millis() - phaseStart > 12000) {
        lv_label_set_text(feedbackLbl, "TIMEOUT");
        lv_obj_set_style_text_color(feedbackLbl, lv_color_hex(0xFF3D00), 0);
        if (comboLen == 1) {
          for (int i = 0; i < LEARN_COUNT; i++) {
            if (LEARN[i] == challenge[0]) { progress[i].streak = 0; break; }
          }
        }
        NeoPixel_Wrong();
        phase = PH_FEEDBACK; phaseStart = millis();
      }
      break;
    }

    // ── FEEDBACK ──
    case PH_FEEDBACK: {
      if (millis() - phaseStart > 1500) {
        // In LEARN mode with tries remaining — replay same letter
        if (diff == DIFF_LEARN && !learnAssisted && learnTriesLeft > 0 && learnTriesLeft < LEARN_MAX_TRIES) {
          startPlayback();  // same challenge, same letter
        } else {
          nextChallenge();
        }
      }
      break;
    }
    default: break;
  }
}

// ── UI setup ──
static void exit_cb(lv_event_t* e) { Game_Trainer_Stop(); }
static void skip_cb(lv_event_t* e) { nextChallenge(); }

// ── Menu ──
static void startGame(Difficulty d);
static void beginner_cb(lv_event_t* e)     { startGame(DIFF_BEGINNER); }
static void intermediate_cb(lv_event_t* e) { startGame(DIFF_INTERMEDIATE); }
static void expert_cb(lv_event_t* e)       { startGame(DIFF_EXPERT); }
static void learn_cb(lv_event_t* e)        { startGame(DIFF_LEARN); }
static void reset_cb(lv_event_t* e) {
  memset(progress, 0, sizeof(progress));
  memset(learnDrill, 0, sizeof(learnDrill));
  curCharIdx = 0; comboLen = 1; bestScore = 0; score = 0;
  learnNextIdx = 0; learnStreak = 0; learnTriesLeft = LEARN_MAX_TRIES;
  saveProgress();
  // Refresh menu
  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }
  Game_Trainer_Start();
}

static lv_obj_t* mkMenuBtn(lv_obj_t* p, const char* t, const char* d, lv_color_t c, int16_t y) {
  lv_obj_t* b = lv_button_create(p);
  lv_obj_set_size(b, 280, 28);
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

static void showMenu(void) {
  loadProgress();
  menuScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(menuScr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(menuScr, LV_OPA_COVER, 0);

  lv_obj_t* t = lv_label_create(menuScr);
  lv_label_set_text(t, "MORSE TRACE");
  lv_obj_set_style_text_color(t, lv_color_hex(0xFFB300), 0);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
#endif
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 2);

  // Progress info — combined from all modes
  uint8_t learnedCount = 0;
  for (int i = 0; i < LEARN_COUNT; i++)
    if (i < (int)learnNextIdx && learnDrill[i] == 0) learnedCount++;
  uint8_t m = countMastered();
  char pbuf[48];
  if (learnedCount > 0 || m > 0) {
    snprintf(pbuf, sizeof(pbuf), "Learn:%d  Practice:%d  best:%lu", learnedCount, m, bestScore);
  } else {
    snprintf(pbuf, sizeof(pbuf), "best:%lu", bestScore);
  }
  lv_obj_t* pi = lv_label_create(menuScr);
  lv_label_set_text(pi, pbuf);
  lv_obj_set_style_text_color(pi, lv_color_hex(0x00E676), 0);
  lv_obj_align(pi, LV_ALIGN_TOP_MID, 0, 28);

  // Difficulty buttons
  lv_obj_t* b;
  b = mkMenuBtn(menuScr, "LEARN", "guided step by step", lv_color_hex(0xFFFFFF), 58);
  lv_obj_add_event_cb(b, learn_cb, LV_EVENT_CLICKED, NULL);
  b = mkMenuBtn(menuScr, "BEGINNER", "letter+bars+sound", lv_color_hex(0x00E676), 90);
  lv_obj_add_event_cb(b, beginner_cb, LV_EVENT_CLICKED, NULL);
  b = mkMenuBtn(menuScr, "INTERMEDIATE", "letter+sound", lv_color_hex(0xFFB300), 122);
  lv_obj_add_event_cb(b, intermediate_cb, LV_EVENT_CLICKED, NULL);
  b = mkMenuBtn(menuScr, "EXPERT", "sound only", lv_color_hex(0xFF3D00), 154);
  lv_obj_add_event_cb(b, expert_cb, LV_EVENT_CLICKED, NULL);

  // Bottom: BACK button centered
  lv_obj_t* bb = lv_button_create(menuScr);
  lv_obj_set_size(bb, 80, 24);
  lv_obj_set_pos(bb, 120, 188);
  lv_obj_set_style_bg_color(bb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(bb, 0, 0);
  lv_obj_set_style_radius(bb, 4, 0);
  lv_obj_set_ext_click_area(bb, 10);
  lv_obj_t* bl = lv_label_create(bb);
  lv_label_set_text(bl, "BACK"); lv_obj_set_style_text_color(bl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(bb, exit_cb, LV_EVENT_CLICKED, NULL);

  lv_screen_load(menuScr);
}

// ── Start game ──
static void startGame(Difficulty d) {
  diff = d;
  score = 0; roundCount = 0;
  active = true;
  cbR = cbW = 0; ebR = ebW = 0;

  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }

  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // Header
  lv_obj_t* hdr = lv_obj_create(scr);
  lv_obj_remove_style_all(hdr);
  lv_obj_set_size(hdr, 320, 20);
  lv_obj_set_pos(hdr, 0, 0);
  lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);

  progressLbl = lv_label_create(hdr);
  lv_obj_set_style_text_color(progressLbl, lv_color_hex(0x666666), 0);
  lv_obj_align(progressLbl, LV_ALIGN_LEFT_MID, 4, 0);

  lv_obj_t* eb = lv_button_create(hdr);
  lv_obj_set_size(eb, 42, 16);
  lv_obj_set_style_bg_color(eb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(eb, 0, 0);
  lv_obj_set_style_radius(eb, 3, 0);
  lv_obj_align(eb, LV_ALIGN_RIGHT_MID, -4, 0);
  // Smaller expansion (6px) because SKIP is directly below — larger would overlap
  lv_obj_set_ext_click_area(eb, 6);
  lv_obj_t* bl = lv_label_create(eb);
  lv_label_set_text(bl, "EXIT"); lv_obj_set_style_text_color(bl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(eb, exit_cb, LV_EVENT_CLICKED, NULL);

  // Title (changes: "NEW: E", "PRACTICE: E", "REVIEW", "COMBO x2")
  titleLbl = lv_label_create(scr);
  lv_label_set_text(titleLbl, "");
  lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xFFB300), 0);
  lv_obj_set_pos(titleLbl, 8, 22);

  // Challenge letter + reference pattern
  challengeLbl = lv_label_create(scr);
  lv_label_set_text(challengeLbl, "");
  lv_obj_set_style_text_color(challengeLbl, lv_color_hex(0x42A5F5), 0);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(challengeLbl, &lv_font_montserrat_24, 0);
#endif
  lv_obj_set_pos(challengeLbl, 8, 36);

  refPatLbl = lv_label_create(scr);
  lv_label_set_text(refPatLbl, "");
  lv_obj_set_style_text_color(refPatLbl, lv_color_hex(0x555555), 0);
  lv_obj_set_pos(refPatLbl, 100, 42);

  makeBars(scr, refBars);  // y=68

  // Prompt
  promptLbl = lv_label_create(scr);
  lv_label_set_text(promptLbl, "");
  lv_obj_set_style_text_color(promptLbl, lv_color_hex(0xFFB300), 0);
  lv_obj_set_pos(promptLbl, 8, 84);

  // Divider
  lv_obj_t* div = lv_obj_create(scr);
  lv_obj_remove_style_all(div);
  lv_obj_set_size(div, 304, 1);
  lv_obj_set_pos(div, 8, 100);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

  // YOUR INPUT
  lv_obj_t* ul = lv_label_create(scr);
  lv_label_set_text(ul, "YOUR INPUT");
  lv_obj_set_style_text_color(ul, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(ul, 8, 104);

  // User decoded chars
  userCharLbl = lv_label_create(scr);
  lv_label_set_text(userCharLbl, "");
  lv_obj_set_style_text_color(userCharLbl, lv_color_hex(0x00E676), 0);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(userCharLbl, &lv_font_montserrat_24, 0);
#endif
  lv_obj_set_pos(userCharLbl, 8, 118);

  makeBars(scr, userBars);  // y=148

  // Feedback
  feedbackLbl = lv_label_create(scr);
  lv_label_set_text(feedbackLbl, "");
#if LV_FONT_MONTSERRAT_16
  lv_obj_set_style_text_font(feedbackLbl, &lv_font_montserrat_16, 0);
#endif
  lv_obj_set_pos(feedbackLbl, 8, 166);

  // SKIP button
  lv_obj_t* sb = lv_button_create(scr);
  lv_obj_set_size(sb, 42, 16);
  lv_obj_set_style_bg_color(sb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(sb, 0, 0);
  lv_obj_set_style_radius(sb, 3, 0);
  lv_obj_set_pos(sb, 270, 22);
  lv_obj_set_ext_click_area(sb, 6);
  bl = lv_label_create(sb);
  lv_label_set_text(bl, "SKIP"); lv_obj_set_style_text_color(bl, lv_color_hex(0x666666), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(sb, skip_cb, LV_EVENT_CLICKED, NULL);

  Keyer_OnChar(game_char);
  Keyer_OnElement(game_element);
  tickTmr = lv_timer_create(tick_cb, 10, NULL);
  lv_screen_load(scr);

  updateProgressBar();
  nextChallenge();
}

// ── Public ──
void Game_Trainer_Start(void) { active = false; showMenu(); }

void Game_Trainer_Stop(void) {
  if (active) {
    saveProgress();
    if (score > 0) Score_Submit("trace", score, comboLen);
  }
  active = false;
  Sidetone_Off();
  if (tickTmr) { lv_timer_del(tickTmr); tickTmr = NULL; }
  Keyer_OnChar([](char c) { UI_PushDecodedChar(c); });
  Keyer_OnElement([](bool s, bool d) { if (s) NeoPixel_KeyFlash(d); });
  if (scr) { lv_obj_delete(scr); scr = NULL; }
  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }
  UI_ShowMain();
}
