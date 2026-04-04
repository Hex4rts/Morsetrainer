#include "MT_Game_QSO.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Morse.h"
#include "MT_Sidetone.h"
#include "MT_Settings.h"
#include "MT_NeoPixel.h"
#include "MT_Score.h"
#include <SD_MMC.h>
#include <lvgl.h>

// Playback speed matches keyer WPM
static inline uint16_t ditMs(void) { return 1200 / Keyer_GetWPM(); }

#define MAX_MSG    48
#define MAX_STEPS  4

// ── Difficulty ──
// Beginner:     see incoming text + expected response + hear sound
// Intermediate: see incoming text + hear sound (must know proper response)
// Expert:       hear sound only (must decode incoming AND know response)
enum QDiff { QD_BEGINNER, QD_INTERMEDIATE, QD_EXPERT };

static const char* qsoNames[] = {"BOB","TOM","JIM","SAM","DAN","PAT","RON","KEN","HAL","VIC"};
#define NUM_NAMES (sizeof(qsoNames)/sizeof(qsoNames[0]))
static const char* qsoPfx[] = {"W","K","N","WA","KB","KD","WB","VE","G","DL","JA","VK"};
#define NUM_PFX (sizeof(qsoPfx)/sizeof(qsoPfx[0]))

typedef struct { char incoming[MAX_MSG+1]; char expected[MAX_MSG+1]; } qso_step_t;

enum QPhase { QP_MENU, QP_PLAYING, QP_WAITING, QP_CHECK, QP_NEXT, QP_DONE };

// ── UI ──
static lv_obj_t*   scr       = NULL;
static lv_obj_t*   menuScr   = NULL;
static lv_obj_t*   incomLbl  = NULL;
static lv_obj_t*   expectLbl = NULL;
static lv_obj_t*   userLbl   = NULL;
static lv_obj_t*   statusLbl = NULL;
static lv_obj_t*   stepLbl   = NULL;
static lv_obj_t*   scoreLbl  = NULL;
static lv_timer_t* tickTmr   = NULL;

// ── State ──
static QPhase   qphase    = QP_MENU;
static QDiff    qdiff     = QD_BEGINNER;
static uint8_t  qsoLevel  = 1;  // 1=simple, 2=RST, 3=full
static uint32_t qsoScore  = 0;
static uint32_t qsoRound  = 0;
static bool     active    = false;

static qso_step_t steps[MAX_STEPS];
static uint8_t  numSteps = 0, curStep = 0;
static char     otherCall[10], otherName[8];

// Playback
static char     playBuf[MAX_MSG * 8];
static int      playPos = 0;
static bool     playTone = false;
static uint16_t playCtr = 0;

// User input
static char     userBuf[MAX_MSG + 1];
static uint8_t  userPos = 0;
static uint32_t phaseStart = 0;

#define CBUF 16
static volatile char cbuf[CBUF];
static volatile uint8_t cbW = 0, cbR = 0;

static void qso_char(char c) { cbuf[cbW % CBUF] = c; cbW++; UI_PushDecodedChar(c); }
static char popC(void) { if (cbR == cbW) return 0; return cbuf[cbR++ % CBUF]; }

static void genCall(char* buf) {
  const char* p = qsoPfx[random(0, NUM_PFX)];
  int d = random(0, 10);
  char sfx[4]; int sl = random(1, 4);
  for (int i = 0; i < sl; i++) sfx[i] = 'A' + random(0, 26);
  sfx[sl] = '\0';
  snprintf(buf, 10, "%s%d%s", p, d, sfx);
}

static void buildPlayStr(const char* text) {
  playBuf[0] = '\0';
  for (int i = 0; text[i]; i++) {
    char c = toupper(text[i]);
    if (c == ' ') { strcat(playBuf, "  "); }
    else {
      if (i > 0 && text[i-1] != ' ') strcat(playBuf, " ");
      const char* code = Morse_Encode(c);
      if (code) strcat(playBuf, code);
    }
  }
}

static void genQSO(void) {
  genCall(otherCall);
  strncpy(otherName, qsoNames[random(0, NUM_NAMES)], sizeof(otherName)-1);
  const char* myCall = Settings_Get()->callsign;
  numSteps = 0;
  if (qsoLevel == 1) {
    snprintf(steps[0].incoming, MAX_MSG, "CQ DE %s K", otherCall);
    snprintf(steps[0].expected, MAX_MSG, "%s DE %s K", otherCall, myCall);
    snprintf(steps[1].incoming, MAX_MSG, "73 SK");
    snprintf(steps[1].expected, MAX_MSG, "73 SK");
    numSteps = 2;
  } else if (qsoLevel == 2) {
    snprintf(steps[0].incoming, MAX_MSG, "CQ DE %s K", otherCall);
    snprintf(steps[0].expected, MAX_MSG, "%s DE %s K", otherCall, myCall);
    snprintf(steps[1].incoming, MAX_MSG, "UR 599 K");
    snprintf(steps[1].expected, MAX_MSG, "R 599 K");
    snprintf(steps[2].incoming, MAX_MSG, "73 SK");
    snprintf(steps[2].expected, MAX_MSG, "73 SK");
    numSteps = 3;
  } else {
    snprintf(steps[0].incoming, MAX_MSG, "CQ DE %s K", otherCall);
    snprintf(steps[0].expected, MAX_MSG, "%s DE %s K", otherCall, myCall);
    snprintf(steps[1].incoming, MAX_MSG, "UR 599 NAME %s K", otherName);
    snprintf(steps[1].expected, MAX_MSG, "R 599 73 SK");
    numSteps = 2;
  }
}

static void startPlayStep(void) {
  buildPlayStr(steps[curStep].incoming);
  playPos = 0; playCtr = ditMs() / 10; playTone = false;
  qphase = QP_PLAYING;

  // Show based on difficulty
  if (qdiff == QD_EXPERT) {
    lv_label_set_text(incomLbl, "???");  // must decode by ear
  } else {
    lv_label_set_text(incomLbl, steps[curStep].incoming);
  }

  if (qdiff == QD_BEGINNER) {
    lv_label_set_text(expectLbl, steps[curStep].expected);
  } else {
    lv_label_set_text(expectLbl, "");  // must know what to say
  }

  lv_label_set_text(userLbl, "");
  lv_label_set_text(statusLbl, "LISTEN...");
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFFB300), 0);
  char sb[16]; snprintf(sb, sizeof(sb), "STEP %d/%d", curStep + 1, numSteps);
  lv_label_set_text(stepLbl, sb);
  cbR = cbW; userPos = 0; userBuf[0] = '\0';
}

// ── Tick ──
static void tick_cb(lv_timer_t* t) {
  if (!active) return;

  switch (qphase) {
    case QP_PLAYING: {
      if (playCtr > 0) { playCtr--; break; }
      int plen = strlen(playBuf);
      if (playPos >= plen) {
        Sidetone_Off();
        qphase = QP_WAITING; phaseStart = millis();
        lv_label_set_text(statusLbl, "YOUR TURN!");
        lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);

        // Expert: now reveal what was sent so user can respond
        if (qdiff == QD_EXPERT) {
          lv_label_set_text(incomLbl, steps[curStep].incoming);
        }
        cbR = cbW; break;
      }
      char e = playBuf[playPos];
      if (!playTone) {
        if (e == ' ') { playPos++; playCtr = (ditMs() * 3) / 10; }
        else { Sidetone_On(); playCtr = (e == '.') ? (ditMs()/10) : ((ditMs()*3)/10); playTone = true; }
      } else { Sidetone_Off(); playTone = false; playPos++; playCtr = ditMs() / 10; }
      break;
    }

    case QP_WAITING: {
      char c = popC();
      if (c) {
        if (c == ' ') {
          if (userPos > 0 && userBuf[userPos-1] != ' ') { userBuf[userPos++] = ' '; userBuf[userPos] = '\0'; }
        } else {
          userBuf[userPos++] = toupper(c); userBuf[userPos] = '\0';
        }
        lv_label_set_text(userLbl, userBuf);

        // Trim and compare
        char expTrim[MAX_MSG+1]; strncpy(expTrim, steps[curStep].expected, MAX_MSG);
        int el = strlen(expTrim); while (el > 0 && expTrim[el-1] == ' ') expTrim[--el] = '\0';
        int ul = userPos; while (ul > 0 && userBuf[ul-1] == ' ') ul--;

        if (ul >= el) {
          char userTrim[MAX_MSG+1]; strncpy(userTrim, userBuf, ul); userTrim[ul] = '\0';
          bool ok = (strcmp(userTrim, expTrim) == 0);
          if (ok) {
            qsoScore += 50;
            lv_label_set_text(statusLbl, "GOOD COPY!");
            lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);
            NeoPixel_Correct();
          } else {
            char fb[MAX_MSG+16];
            snprintf(fb, sizeof(fb), "EXPECTED: %s", expTrim);
            lv_label_set_text(statusLbl, fb);
            lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFF3D00), 0);
            NeoPixel_Wrong();
          }
          char sc[12]; snprintf(sc, sizeof(sc), "%lu", qsoScore); lv_label_set_text(scoreLbl, sc);
          qphase = QP_CHECK; phaseStart = millis();
        }
      }
      if (Keyer_IsSending()) {
        lv_label_set_text(statusLbl, "KEYING...");
        lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFF3D00), 0);
      }
      if (millis() - phaseStart > 30000) {
        lv_label_set_text(statusLbl, "TIMEOUT");
        lv_obj_set_style_text_color(statusLbl, lv_color_hex(0xFF3D00), 0);
        qphase = QP_CHECK; phaseStart = millis();
      }
      break;
    }

    case QP_CHECK: {
      if (millis() - phaseStart > 2000) {
        curStep++;
        if (curStep >= numSteps) {
          qsoRound++;
          char buf[40]; snprintf(buf, sizeof(buf), "QSO #%lu DONE! Score: %lu", qsoRound, qsoScore);
          lv_label_set_text(statusLbl, buf);
          lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);
          lv_label_set_text(incomLbl, ""); lv_label_set_text(expectLbl, "");
          lv_label_set_text(stepLbl, "COMPLETE");
          NeoPixel_LevelUp();
          qphase = QP_DONE; phaseStart = millis();
        } else {
          startPlayStep();
        }
      }
      break;
    }

    case QP_DONE: {
      if (millis() - phaseStart > 2500) { genQSO(); curStep = 0; startPlayStep(); }
      break;
    }
    default: break;
  }
}

// ── Menu ──
static void exit_cb(lv_event_t* e) { Game_QSO_Stop(); }
static void startQSO(QDiff d, uint8_t lvl);

static void menu_exit(lv_event_t* e) { if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; } UI_ShowMain(); }

// Beginner callbacks
static void b_simple(lv_event_t* e) { startQSO(QD_BEGINNER, 1); }
static void b_rst(lv_event_t* e)    { startQSO(QD_BEGINNER, 2); }
static void b_full(lv_event_t* e)   { startQSO(QD_BEGINNER, 3); }
// Intermediate
static void i_simple(lv_event_t* e) { startQSO(QD_INTERMEDIATE, 1); }
static void i_rst(lv_event_t* e)    { startQSO(QD_INTERMEDIATE, 2); }
static void i_full(lv_event_t* e)   { startQSO(QD_INTERMEDIATE, 3); }
// Expert
static void e_simple(lv_event_t* e) { startQSO(QD_EXPERT, 1); }
static void e_rst(lv_event_t* e)    { startQSO(QD_EXPERT, 2); }
static void e_full(lv_event_t* e)   { startQSO(QD_EXPERT, 3); }

static lv_obj_t* mkBtn(lv_obj_t* p, const char* t, lv_color_t c, int16_t x, int16_t y, int16_t w) {
  lv_obj_t* b = lv_button_create(p);
  lv_obj_set_size(b, w, 26);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(b, c, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_radius(b, 4, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, t); lv_obj_set_style_text_color(l, c, 0);
  lv_obj_center(l);
  return b;
}

static void showMenu(void) {
  menuScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(menuScr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(menuScr, LV_OPA_COVER, 0);

  lv_obj_t* t = lv_label_create(menuScr);
  lv_label_set_text(t, "QSO SIMULATOR");
  lv_obj_set_style_text_color(t, lv_color_hex(0x42A5F5), 0);
#if LV_FONT_MONTSERRAT_24
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
#endif
#endif
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 4);

  // Column headers
  lv_obj_t* lbl;
  lbl = lv_label_create(menuScr); lv_label_set_text(lbl, ""); lv_obj_set_pos(lbl, 8, 34);
  lbl = lv_label_create(menuScr); lv_label_set_text(lbl, "SIMPLE"); lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0); lv_obj_set_pos(lbl, 20, 34);
  lbl = lv_label_create(menuScr); lv_label_set_text(lbl, "RST"); lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0); lv_obj_set_pos(lbl, 120, 34);
  lbl = lv_label_create(menuScr); lv_label_set_text(lbl, "FULL"); lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0); lv_obj_set_pos(lbl, 220, 34);

  // Row labels + buttons — 3×3 grid
  lv_color_t cg = lv_color_hex(0x00E676);
  lv_color_t ca = lv_color_hex(0xFFB300);
  lv_color_t cr = lv_color_hex(0xFF3D00);

  // Beginner row
  lbl = lv_label_create(menuScr); lv_label_set_text(lbl, "BGN"); lv_obj_set_style_text_color(lbl, cg, 0); lv_obj_set_pos(lbl, 8, 56);
  lv_obj_t* b;
  b = mkBtn(menuScr, "CQ+73", cg, 44, 50, 80); lv_obj_add_event_cb(b, b_simple, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "CQ+RST", cg, 130, 50, 80); lv_obj_add_event_cb(b, b_rst, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "FULL", cg, 216, 50, 80); lv_obj_add_event_cb(b, b_full, LV_EVENT_CLICKED, NULL);

  // Intermediate row
  lbl = lv_label_create(menuScr); lv_label_set_text(lbl, "INT"); lv_obj_set_style_text_color(lbl, ca, 0); lv_obj_set_pos(lbl, 8, 88);
  b = mkBtn(menuScr, "CQ+73", ca, 44, 82, 80); lv_obj_add_event_cb(b, i_simple, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "CQ+RST", ca, 130, 82, 80); lv_obj_add_event_cb(b, i_rst, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "FULL", ca, 216, 82, 80); lv_obj_add_event_cb(b, i_full, LV_EVENT_CLICKED, NULL);

  // Expert row
  lbl = lv_label_create(menuScr); lv_label_set_text(lbl, "EXP"); lv_obj_set_style_text_color(lbl, cr, 0); lv_obj_set_pos(lbl, 8, 120);
  b = mkBtn(menuScr, "CQ+73", cr, 44, 114, 80); lv_obj_add_event_cb(b, e_simple, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "CQ+RST", cr, 130, 114, 80); lv_obj_add_event_cb(b, e_rst, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "FULL", cr, 216, 114, 80); lv_obj_add_event_cb(b, e_full, LV_EVENT_CLICKED, NULL);

  // Description
  lbl = lv_label_create(menuScr); lv_obj_set_pos(lbl, 8, 148);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x555555), 0);
  lv_label_set_text(lbl, "BGN: see text+response  INT: see text\nEXP: decode by ear only");
  lv_obj_set_width(lbl, 304);

  // Back
  lv_obj_t* bb = lv_button_create(menuScr);
  lv_obj_set_size(bb, 80, 26);
  lv_obj_align(bb, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_obj_set_style_bg_color(bb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(bb, 0, 0);
  lv_obj_set_style_radius(bb, 4, 0);
  lv_obj_t* bl = lv_label_create(bb);
  lv_label_set_text(bl, "BACK"); lv_obj_set_style_text_color(bl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(bb, menu_exit, LV_EVENT_CLICKED, NULL);

  lv_screen_load(menuScr);
}

static void startQSO(QDiff d, uint8_t lvl) {
  qdiff = d; qsoLevel = lvl;
  qsoScore = 0; qsoRound = 0;
  active = true; cbR = cbW = 0;

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

  const char* dn[] = {"BGN","INT","EXP"};
  const char* ln[] = {"SIMPLE","RST","FULL"};
  char hb[24]; snprintf(hb, sizeof(hb), "QSO [%s %s]", dn[(int)d], ln[lvl-1]);
  lv_obj_t* tn = lv_label_create(hdr);
  lv_label_set_text(tn, hb);
  lv_obj_set_style_text_color(tn, lv_color_hex(0x42A5F5), 0);
  lv_obj_align(tn, LV_ALIGN_LEFT_MID, 4, 0);

  stepLbl = lv_label_create(hdr);
  lv_label_set_text(stepLbl, "");
  lv_obj_set_style_text_color(stepLbl, lv_color_hex(0x666666), 0);
  lv_obj_align(stepLbl, LV_ALIGN_CENTER, 20, 0);

  scoreLbl = lv_label_create(hdr);
  lv_label_set_text(scoreLbl, "0");
  lv_obj_set_style_text_color(scoreLbl, lv_color_hex(0x00E676), 0);
  lv_obj_align(scoreLbl, LV_ALIGN_RIGHT_MID, -50, 0);

  lv_obj_t* eb = mkBtn(hdr, "EXIT", lv_color_hex(0xFF3D00), 270, 1, 42);
  lv_obj_add_event_cb(eb, exit_cb, LV_EVENT_CLICKED, NULL);

  // INCOMING
  lv_obj_t* il = lv_label_create(scr);
  lv_label_set_text(il, "INCOMING");
  lv_obj_set_style_text_color(il, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(il, 8, 24);

  incomLbl = lv_label_create(scr);
  lv_label_set_text(incomLbl, "");
  lv_obj_set_style_text_color(incomLbl, lv_color_hex(0x42A5F5), 0);
  lv_obj_set_style_text_font(incomLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(incomLbl, 8, 38);
  lv_obj_set_width(incomLbl, 304); lv_label_set_long_mode(incomLbl, LV_LABEL_LONG_WRAP);

  // RESPOND (only shown in beginner)
  lv_obj_t* rl = lv_label_create(scr);
  lv_label_set_text(rl, "RESPOND");
  lv_obj_set_style_text_color(rl, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(rl, 8, 66);

  expectLbl = lv_label_create(scr);
  lv_label_set_text(expectLbl, "");
  lv_obj_set_style_text_color(expectLbl, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_text_font(expectLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(expectLbl, 8, 80);
  lv_obj_set_width(expectLbl, 304); lv_label_set_long_mode(expectLbl, LV_LABEL_LONG_WRAP);

  // Divider
  lv_obj_t* div = lv_obj_create(scr);
  lv_obj_remove_style_all(div);
  lv_obj_set_size(div, 304, 1);
  lv_obj_set_pos(div, 8, 108);
  lv_obj_set_style_bg_color(div, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);

  // YOUR INPUT
  lv_obj_t* ul = lv_label_create(scr);
  lv_label_set_text(ul, "YOUR INPUT");
  lv_obj_set_style_text_color(ul, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(ul, 8, 112);

  userLbl = lv_label_create(scr);
  lv_label_set_text(userLbl, "");
  lv_obj_set_style_text_color(userLbl, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_text_font(userLbl, &lv_font_montserrat_16, 0);
  lv_obj_set_pos(userLbl, 8, 128);
  lv_obj_set_width(userLbl, 304); lv_label_set_long_mode(userLbl, LV_LABEL_LONG_WRAP);

  // Status
  statusLbl = lv_label_create(scr);
  lv_label_set_text(statusLbl, "");
  lv_obj_set_style_text_font(statusLbl, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(statusLbl, 8, 168);
  lv_obj_set_width(statusLbl, 304); lv_label_set_long_mode(statusLbl, LV_LABEL_LONG_WRAP);

  Keyer_OnChar(qso_char);
  tickTmr = lv_timer_create(tick_cb, 10, NULL);
  lv_screen_load(scr);

  genQSO(); curStep = 0; startPlayStep();
}

void Game_QSO_Start(void) { active = false; showMenu(); }

void Game_QSO_Stop(void) {
  if (active && qsoScore > 0) Score_Submit("qso", qsoScore, qsoLevel);
  active = false; Sidetone_Off();
  if (tickTmr) { lv_timer_del(tickTmr); tickTmr = NULL; }
  Keyer_OnChar([](char c) { UI_PushDecodedChar(c); });
  if (scr) { lv_obj_delete(scr); scr = NULL; }
  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }
  UI_ShowMain();
}
