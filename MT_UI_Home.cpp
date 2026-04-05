#include "MT_UI_Home.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Settings.h"
#include "MT_NeoPixel.h"
#include "MT_Sidetone.h"
#include "Gyro_QMI8658.h"
#include "Display_ST7789.h"

extern const lv_font_t* ui_font_large;
extern const lv_font_t* ui_font_normal;

// ── Easter egg ──
static uint8_t  eggTaps = 0;
static uint32_t eggLastTap = 0;
static lv_obj_t* eggOverlay = NULL;
static lv_timer_t* eggTimer = NULL;

static void eggDismiss(lv_event_t* e) {
  (void)e;
  Sidetone_Off();
  NeoPixel_RainbowStop();
  if (eggTimer) { lv_timer_del(eggTimer); eggTimer = NULL; }
  if (eggOverlay) { lv_obj_delete(eggOverlay); eggOverlay = NULL; }
}

// ── IMU gravity balls ──
#define EGG_BALLS 8
#define BALL_SZ   12
#define BALL_LIFE 3000  // ms before fade+respawn
static struct { float x, y, vx, vy; uint32_t col; uint32_t born; } ball[EGG_BALLS];
static lv_obj_t* ballObj[EGG_BALLS] = {};
static const uint32_t bcols[] = {0xFF3D00, 0x00E676, 0x42A5F5, 0xFFB300, 0xFF00FF, 0xFFFF00, 0x00FFFF, 0xFF6600};

static void ballSpawn(int i) {
  ball[i].x = 40 + random(0, 240);
  ball[i].y = 30 + random(0, 140);
  ball[i].vx = random(-20, 21) / 10.0f;
  ball[i].vy = random(-20, 21) / 10.0f;
  ball[i].col = bcols[random(0, 8)];
  ball[i].born = millis();
  if (ballObj[i]) {
    lv_obj_set_style_bg_color(ballObj[i], lv_color_hex(ball[i].col), 0);
    lv_obj_set_style_bg_opa(ballObj[i], LV_OPA_COVER, 0);
  }
}

// ── Morse 73 playback: "--..." + "...--" ──
static const char* morse73 = "--...x...--";  // x = char gap
static uint8_t  morsePos  = 0;
static int16_t  morseCtr  = 0;
static bool     morseTone = false;
static bool     morseDone = false;
#define MORSE_DIT  6   // ticks per dit (~240ms at 40ms/tick ≈ 12 WPM)
#define MORSE_DAH  18

// Morse visual bars
#define MORSE_MAX_BARS 12
static lv_obj_t* morseBars[MORSE_MAX_BARS] = {};
static uint8_t morseBarIdx = 0;
static int16_t morseBarX = 60;

// Text label
static lv_obj_t* eggMsgLbl = NULL;

static void eggAnimTick(lv_timer_t* t) {
  if (!eggOverlay) return;

  // ── IMU gravity balls ──
  float ax = -Accel.x * 0.8f;  // negate X for correct left/right
  float ay = Accel.y * 0.8f;
  // Negate axes when screen is flipped 180°
  if (display_flipped) { ax = -ax; ay = -ay; }
  uint32_t now = millis();
  for (int i = 0; i < EGG_BALLS; i++) {
    uint32_t age = now - ball[i].born;
    // Fade out in last 500ms of life, then respawn
    if (age > BALL_LIFE) {
      ballSpawn(i);
      age = 0;
    } else if (age > BALL_LIFE - 500) {
      uint8_t fade = 255 - ((age - (BALL_LIFE - 500)) * 255 / 500);
      if (ballObj[i]) lv_obj_set_style_bg_opa(ballObj[i], fade, 0);
    } else {
      if (ballObj[i]) lv_obj_set_style_bg_opa(ballObj[i], LV_OPA_COVER, 0);
    }
    ball[i].vx += ax;
    ball[i].vy -= ay;
    ball[i].vx *= 0.98f;
    ball[i].vy *= 0.98f;
    ball[i].x += ball[i].vx;
    ball[i].y += ball[i].vy;
    if (ball[i].x < 0)   { ball[i].x = 0;   ball[i].vx = -ball[i].vx * 0.7f; }
    if (ball[i].x > 308)  { ball[i].x = 308; ball[i].vx = -ball[i].vx * 0.7f; }
    if (ball[i].y < 0)    { ball[i].y = 0;   ball[i].vy = -ball[i].vy * 0.7f; }
    if (ball[i].y > 228)  { ball[i].y = 228; ball[i].vy = -ball[i].vy * 0.7f; }
    if (ballObj[i]) lv_obj_set_pos(ballObj[i], (int16_t)ball[i].x, (int16_t)ball[i].y);
  }

  // ── Morse 73 playback ──
  if (!morseDone) {
    if (morseCtr > 0) { morseCtr--; return; }
    char elem = morse73[morsePos];
    if (elem == '\0') {
      Sidetone_Off(); morseDone = true; return;
    }
    if (elem == 'x') {
      // Character gap — audio pause + visual space
      Sidetone_Off(); morseTone = false;
      morseCtr = MORSE_DIT * 3;
      morseBarX += 16;  // visible gap between 7 and 3
      morsePos++;
    } else if (!morseTone) {
      // Start element
      Sidetone_On();
      morseTone = true;
      bool dah = (elem == '-');
      morseCtr = dah ? MORSE_DAH : MORSE_DIT;
      // Draw visual bar
      if (morseBarIdx < MORSE_MAX_BARS && morseBars[morseBarIdx]) {
        int16_t w = dah ? 24 : 10;
        lv_obj_set_size(morseBars[morseBarIdx], w, 8);
        lv_obj_set_pos(morseBars[morseBarIdx], morseBarX, 195);
        lv_obj_set_style_bg_color(morseBars[morseBarIdx], dah ? lv_color_hex(0xFFB300) : lv_color_hex(0x00E676), 0);
        lv_obj_set_style_bg_opa(morseBars[morseBarIdx], LV_OPA_COVER, 0);
        lv_obj_clear_flag(morseBars[morseBarIdx], LV_OBJ_FLAG_HIDDEN);
        morseBarX += w + 4;
        morseBarIdx++;
      }
    } else {
      // End element — inter-element gap
      Sidetone_Off(); morseTone = false;
      morseCtr = MORSE_DIT;
      morsePos++;
    }
  }
}

static void eggShow(void) {
  NeoPixel_RainbowStart();

  eggOverlay = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(eggOverlay);
  lv_obj_set_size(eggOverlay, 320, 240);
  lv_obj_set_pos(eggOverlay, 0, 0);
  lv_obj_set_style_bg_color(eggOverlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(eggOverlay, LV_OPA_COVER, 0);
  lv_obj_clear_flag(eggOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(eggOverlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(eggOverlay, eggDismiss, LV_EVENT_CLICKED, NULL);

  // "73 de VE2HXR" 
  eggMsgLbl = lv_label_create(eggOverlay);
  lv_label_set_text(eggMsgLbl, "73 de VE2HXR");
  lv_obj_set_style_text_color(eggMsgLbl, lv_color_hex(0x00E676), 0);
#if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(eggMsgLbl, &lv_font_montserrat_28, 0);
#endif
  lv_obj_align(eggMsgLbl, LV_ALIGN_CENTER, 0, -20);

  // Init balls — stagger birth times so they don't all respawn together
  for (int i = 0; i < EGG_BALLS; i++) {
    ballObj[i] = lv_obj_create(eggOverlay);
    lv_obj_remove_style_all(ballObj[i]);
    lv_obj_set_size(ballObj[i], BALL_SZ, BALL_SZ);
    lv_obj_set_style_radius(ballObj[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(ballObj[i], LV_OBJ_FLAG_CLICKABLE);
    ballSpawn(i);
    ball[i].born = millis() - (i * BALL_LIFE / EGG_BALLS);  // stagger
    lv_obj_set_pos(ballObj[i], (int16_t)ball[i].x, (int16_t)ball[i].y);
  }

  // Morse visual bar placeholders
  morseBarIdx = 0;
  morseBarX = 60;
  for (int i = 0; i < MORSE_MAX_BARS; i++) {
    morseBars[i] = lv_obj_create(eggOverlay);
    lv_obj_remove_style_all(morseBars[i]);
    lv_obj_set_style_radius(morseBars[i], 2, 0);
    lv_obj_add_flag(morseBars[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(morseBars[i], LV_OBJ_FLAG_CLICKABLE);
  }

  // Init morse state
  morsePos = 0; morseCtr = MORSE_DIT * 4; morseTone = false; morseDone = false;

  eggTimer = lv_timer_create(eggAnimTick, 40, NULL);
}

static void callTap_cb(lv_event_t* e) {
  uint32_t now = millis();
  if (now - eggLastTap > 2000) eggTaps = 0;
  eggTaps++;
  eggLastTap = now;
  if (eggTaps >= 7 && !eggOverlay) {
    eggTaps = 0;
    eggShow();
  }
}

// Straight key raw bar tracking for home screen
#define HOME_SK_MAX  8
static struct { int16_t x; int16_t w; } homeSKBars[HOME_SK_MAX];
static uint8_t  homeSKCount  = 0;
static int16_t  homeSKNextX  = 2;

static lv_obj_t* textArea    = NULL;
static lv_obj_t* wpmCard     = NULL;
static lv_obj_t* wpmVal      = NULL;
static lv_obj_t* modeCard    = NULL;
static lv_obj_t* modeVal     = NULL;
static lv_obj_t* callCard    = NULL;
static lv_obj_t* callVal     = NULL;
static lv_obj_t* txDot       = NULL;

// Visual dit/dah bars on home screen
#define HOME_MAX_BARS 8
#define HOME_DIT_W    14
#define HOME_DAH_W    36
#define HOME_BAR_H    10
#define HOME_BAR_GAP  4
static lv_obj_t* homeBars[HOME_MAX_BARS] = {};

#define TEXT_BUF_LEN 200
static char textBuf[TEXT_BUF_LEN + 1];
static uint16_t textLen = 0;

// ── Helper: create a status card ──
static lv_obj_t* makeCard(lv_obj_t* parent, const char* title, const char* value,
                           lv_color_t accent, int16_t x, int16_t y, int16_t w, int16_t h,
                           lv_obj_t** valLabel) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, w, h);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 6, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 4, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* tl = lv_label_create(card);
  lv_label_set_text(tl, title);
  lv_obj_set_style_text_color(tl, lv_color_hex(0x666666), 0);
  lv_obj_set_style_text_font(tl, ui_font_normal, 0);
  lv_obj_align(tl, LV_ALIGN_TOP_LEFT, 2, 0);

  lv_obj_t* vl = lv_label_create(card);
  lv_label_set_text(vl, value);
  lv_obj_set_style_text_color(vl, accent, 0);
  lv_obj_set_style_text_font(vl, ui_font_large, 0);
  lv_obj_align(vl, LV_ALIGN_CENTER, 0, 6);
  *valLabel = vl;

  return card;
}

void UI_Home_Create(lv_obj_t* parent) {
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  // ── TOP ROW: Dit/dah colored bars + TX dot ──
  // Bars span the full top band
  for (int i = 0; i < HOME_MAX_BARS; i++) {
    homeBars[i] = lv_obj_create(parent);
    lv_obj_remove_style_all(homeBars[i]);
    lv_obj_set_size(homeBars[i], HOME_DIT_W, HOME_BAR_H);
    lv_obj_add_flag(homeBars[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(homeBars[i], (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  }

  // TX dot (top-right corner)
  txDot = lv_obj_create(parent);
  lv_obj_remove_style_all(txDot);
  lv_obj_set_size(txDot, 10, 10);
  lv_obj_set_style_radius(txDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(txDot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(txDot, lv_color_hex(0x333333), 0);
  lv_obj_set_pos(txDot, 300, 3);

  // ── ROW 2: DECODED label + CLR button ──
  lv_obj_t* decLbl = lv_label_create(parent);
  lv_label_set_text(decLbl, "DECODED");
  lv_obj_set_style_text_color(decLbl, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(decLbl, 4, 18);

  lv_obj_t* clrBtn = lv_button_create(parent);
  lv_obj_set_size(clrBtn, 36, 14);
  lv_obj_set_style_bg_color(clrBtn, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(clrBtn, 0, 0);
  lv_obj_set_style_radius(clrBtn, 3, 0);
  lv_obj_set_style_pad_all(clrBtn, 0, 0);
  lv_obj_set_pos(clrBtn, 276, 18);
  lv_obj_t* clrLbl = lv_label_create(clrBtn);
  lv_label_set_text(clrLbl, "CLR");
  lv_obj_set_style_text_color(clrLbl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(clrLbl);
  lv_obj_add_event_cb(clrBtn, [](lv_event_t* e) {
    extern void UI_Home_Clear(void);
    UI_Home_Clear();
  }, LV_EVENT_CLICKED, NULL);

  // ── ROW 3: Decoded text area — fills middle ──
  textArea = lv_textarea_create(parent);
  lv_obj_set_size(textArea, 308, 106);
  lv_obj_set_pos(textArea, 2, 32);
  lv_obj_set_style_bg_color(textArea, lv_color_hex(0x0A0A0A), 0);
  lv_obj_set_style_border_color(textArea, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(textArea, 1, 0);
  lv_obj_set_style_text_color(textArea, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_text_font(textArea, ui_font_large, 0);
  lv_obj_set_style_radius(textArea, 4, 0);
  lv_textarea_set_text(textArea, "");
  lv_obj_clear_flag(textArea, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  textLen = 0; textBuf[0] = '\0';

  // ── ROW 4: Status cards — fill to bottom ──
  const mt_settings_t* s = Settings_Get();
  char wpmStr[8]; snprintf(wpmStr, sizeof(wpmStr), "%d", s->wpm);
  wpmCard  = makeCard(parent, "WPM",  wpmStr, lv_color_hex(0x00E676), 2,  142, 100, 66, &wpmVal);
  modeCard = makeCard(parent, "MODE", Keyer_ModeName(s->keyerMode), lv_color_hex(0xFFB300), 106, 142, 100, 66, &modeVal);
  callCard = makeCard(parent, "CALL", s->callsign, lv_color_hex(0x42A5F5), 210, 142, 100, 66, &callVal);
  lv_obj_add_flag(callCard, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(callCard, callTap_cb, LV_EVENT_CLICKED, NULL);
}

void UI_Home_AddChar(char c) {
  if (textLen >= TEXT_BUF_LEN - 1) {
    memmove(textBuf, textBuf + 40, textLen - 40);
    textLen -= 40;
    textBuf[textLen] = '\0';
  }
  textBuf[textLen++] = c;
  textBuf[textLen] = '\0';
  if (textArea) {
    lv_textarea_set_text(textArea, textBuf);
    lv_textarea_set_cursor_pos(textArea, LV_TEXTAREA_CURSOR_LAST);
  }
}

void UI_Home_Clear(void) {
  textLen = 0;
  textBuf[0] = '\0';
  if (textArea) lv_textarea_set_text(textArea, "");
}

void UI_Home_Refresh(void) {
  if (!wpmVal) return;
  char buf[8]; snprintf(buf, sizeof(buf), "%d", Keyer_GetWPM());
  lv_label_set_text(wpmVal, buf);
  lv_label_set_text(modeVal, Keyer_ModeName(Keyer_GetMode()));
  lv_label_set_text(callVal, Settings_Get()->callsign);
  lv_obj_set_style_bg_color(txDot, Keyer_IsSending() ?
    lv_color_hex(0xFF3D00) : lv_color_hex(0x333333), 0);
  // Show current in-progress morse pattern as visual bars
  {
    bool isStraight = (Keyer_GetMode() == KEYER_STRAIGHT);
    bool sending = Keyer_IsSending();

    if (isStraight) {
      // ── Straight key: green bars from ISR event log ──
      // Read completed elements (never missed, ISR pushes them)
      uint16_t dur;
      while (Keyer_SKPopElement(&dur, false)) {
        if (homeSKCount < HOME_SK_MAX) {
          int16_t w = dur / 3;  // 3ms per pixel
          if (w < 2) w = 2;
          homeSKBars[homeSKCount].x = homeSKNextX;
          homeSKBars[homeSKCount].w = w;
          homeSKCount++;
          homeSKNextX += w + HOME_BAR_GAP;
        }
      }

      // Draw all frozen bars
      for (int i = 0; i < HOME_MAX_BARS; i++) lv_obj_add_flag(homeBars[i], LV_OBJ_FLAG_HIDDEN);
      int idx = 0;
      for (int i = 0; i < (int)homeSKCount && idx < HOME_MAX_BARS; i++) {
        lv_obj_set_size(homeBars[idx], homeSKBars[i].w, HOME_BAR_H);
        lv_obj_set_pos(homeBars[idx], homeSKBars[i].x, 3);
        lv_obj_set_style_bg_color(homeBars[idx], lv_color_hex(0x00E676), 0);
        lv_obj_set_style_bg_opa(homeBars[idx], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(homeBars[idx], 2, 0);
        lv_obj_clear_flag(homeBars[idx], LV_OBJ_FLAG_HIDDEN);
        idx++;
      }
      // Live growing bar while key held (ISR updates every 1ms)
      if (Keyer_SKIsDown() && idx < HOME_MAX_BARS) {
        int16_t w = Keyer_SKHeldMs() / 3;
        if (w < 2) w = 2;
        lv_obj_set_size(homeBars[idx], w, HOME_BAR_H);
        lv_obj_set_pos(homeBars[idx], homeSKNextX, 3);
        lv_obj_set_style_bg_color(homeBars[idx], lv_color_hex(0x00E676), 0);
        lv_obj_set_style_bg_opa(homeBars[idx], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(homeBars[idx], 2, 0);
        lv_obj_clear_flag(homeBars[idx], LV_OBJ_FLAG_HIDDEN);
      }

      // Clear bars after character decoded (pattern consumed)
      const char* pat = Keyer_GetPattern();
      if (!pat || !pat[0]) {
        if (homeSKCount > 0 && !Keyer_SKIsDown()) {
          static uint32_t clearTime = 0;
          if (clearTime == 0) clearTime = millis();
          if (millis() - clearTime > 600) {
            homeSKCount = 0; homeSKNextX = 2;
            clearTime = 0;
          }
        }
      } else {
        // Pattern building — reset clear timer
      }
    } else {
      // ── Iambic: blue/amber classified bars ──
      // Reset straight key state
      homeSKCount = 0; homeSKNextX = 2;

      const char* pat = Keyer_GetPattern();
      if (pat && pat[0]) {
        int len = strlen(pat);
        int16_t totalW = 0;
        for (int i = 0; i < len && i < HOME_MAX_BARS; i++)
          totalW += (pat[i] == '-' ? HOME_DAH_W : HOME_DIT_W) + HOME_BAR_GAP;
        int16_t x = (304 - totalW) / 2;
        if (x < 70) x = 70;
        for (int i = 0; i < HOME_MAX_BARS; i++) {
          if (i < len) {
            bool dah = (pat[i] == '-');
            int16_t w = dah ? HOME_DAH_W : HOME_DIT_W;
            lv_obj_set_size(homeBars[i], w, HOME_BAR_H);
            lv_obj_set_pos(homeBars[i], x, 3);
            lv_obj_set_style_bg_color(homeBars[i], dah ? lv_color_hex(0xFFB300) : lv_color_hex(0x42A5F5), 0);
            lv_obj_set_style_bg_opa(homeBars[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(homeBars[i], 2, 0);
            lv_obj_clear_flag(homeBars[i], LV_OBJ_FLAG_HIDDEN);
            x += w + HOME_BAR_GAP;
          } else {
            lv_obj_add_flag(homeBars[i], LV_OBJ_FLAG_HIDDEN);
          }
        }
      } else {
        for (int i = 0; i < HOME_MAX_BARS; i++)
          lv_obj_add_flag(homeBars[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
}
