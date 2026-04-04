#include "MT_UI_Home.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Settings.h"

extern const lv_font_t* ui_font_large;
extern const lv_font_t* ui_font_normal;

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
