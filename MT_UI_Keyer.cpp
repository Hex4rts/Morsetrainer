#include "MT_UI_Keyer.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Settings.h"

extern const lv_font_t* ui_font_large;
extern const lv_font_t* ui_font_normal;

static lv_obj_t* wpmBigLbl  = NULL;
static lv_obj_t* dotHintLbl = NULL;
static lv_obj_t* modeBtns[3]= {};
static lv_obj_t* swapLbl    = NULL;

static void updateModeHighlight(void) {
  keyer_mode_t cur = Keyer_GetMode();
  for (int i = 0; i < 3; i++) {
    bool on = ((int)cur == i);
    lv_obj_set_style_bg_color(modeBtns[i], on ? lv_color_hex(0x00E676) : lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(modeBtns[i], on ? lv_color_hex(0x00E676) : lv_color_hex(0x333333), 0);
    lv_obj_t* lb = lv_obj_get_child(modeBtns[i], 0);
    if (lb) lv_obj_set_style_text_color(lb, on ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF), 0);
  }
}

static void refreshWpm(void) {
  char buf[8]; snprintf(buf, sizeof(buf), "%d", Keyer_GetWPM());
  lv_label_set_text(wpmBigLbl, buf);
  char h[24]; snprintf(h, sizeof(h), "dot = %d ms", 1200 / Keyer_GetWPM());
  lv_label_set_text(dotHintLbl, h);
}

static void wpmUp(lv_event_t* e)   { Settings_SetWPM(Keyer_GetWPM() + 1); refreshWpm(); }
static void wpmDown(lv_event_t* e) { Settings_SetWPM(Keyer_GetWPM() - 1); refreshWpm(); }

static void mode_cb(lv_event_t* e) {
  int i = (int)(intptr_t)lv_event_get_user_data(e);
  Settings_SetKeyerMode((keyer_mode_t)i);
  updateModeHighlight();
}

static void swap_cb(lv_event_t* e) {
  bool on = !Keyer_GetSwap();
  Settings_SetPaddleSwap(on);
  lv_label_set_text(swapLbl, on ? "DIT=RIGHT  DAH=LEFT" : "DIT=LEFT   DAH=RIGHT");
}

void UI_Keyer_Create(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 4, 0);
  lv_obj_set_style_pad_all(parent, 4, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  // ── Row 1: SPEED label ──
  lv_obj_t* lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "SPEED (WPM)");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);

  // ── Row 2: Big WPM + hint + buttons ──
  lv_obj_t* wpmRow = lv_obj_create(parent);
  lv_obj_remove_style_all(wpmRow);
  lv_obj_set_size(wpmRow, lv_pct(100), 36);
  lv_obj_set_flex_flow(wpmRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(wpmRow, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  wpmBigLbl = lv_label_create(wpmRow);
  char buf[8]; snprintf(buf, sizeof(buf), "%d", Keyer_GetWPM());
  lv_label_set_text(wpmBigLbl, buf);
  lv_obj_set_style_text_color(wpmBigLbl, lv_color_hex(0x00E676), 0);
#if LV_FONT_MONTSERRAT_28
  lv_obj_set_style_text_font(wpmBigLbl, &lv_font_montserrat_28, 0);
#endif

  dotHintLbl = lv_label_create(wpmRow);
  char h[24]; snprintf(h, sizeof(h), "dot = %d ms", 1200 / Keyer_GetWPM());
  lv_label_set_text(dotHintLbl, h);
  lv_obj_set_style_text_color(dotHintLbl, lv_color_hex(0x666666), 0);
  lv_obj_set_flex_grow(dotHintLbl, 1);

  // + button
  lv_obj_t* bp = lv_button_create(wpmRow);
  lv_obj_set_size(bp, 36, 32);
  lv_obj_set_style_bg_color(bp, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(bp, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_border_width(bp, 1, 0);
  lv_obj_set_style_shadow_width(bp, 0, 0);
  lv_obj_t* bpl = lv_label_create(bp);
  lv_label_set_text(bpl, "+");
  lv_obj_set_style_text_color(bpl, lv_color_hex(0x00E676), 0);
  lv_obj_center(bpl);
  lv_obj_add_event_cb(bp, wpmUp, LV_EVENT_CLICKED, NULL);

  // - button
  lv_obj_t* bm = lv_button_create(wpmRow);
  lv_obj_set_size(bm, 36, 32);
  lv_obj_set_style_bg_color(bm, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(bm, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_border_width(bm, 1, 0);
  lv_obj_set_style_shadow_width(bm, 0, 0);
  lv_obj_t* bml = lv_label_create(bm);
  lv_label_set_text(bml, "-");
  lv_obj_set_style_text_color(bml, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(bml);
  lv_obj_add_event_cb(bm, wpmDown, LV_EVENT_CLICKED, NULL);

  // ── Row 3: KEYER MODE label ──
  lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "KEYER MODE");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);

  // ── Row 4: Mode buttons ──
  lv_obj_t* modeRow = lv_obj_create(parent);
  lv_obj_remove_style_all(modeRow);
  lv_obj_set_size(modeRow, lv_pct(100), 30);
  lv_obj_set_flex_flow(modeRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(modeRow, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  const char* names[] = {"IAMBIC A", "IAMBIC B", "STRAIGHT"};
  for (int i = 0; i < 3; i++) {
    modeBtns[i] = lv_button_create(modeRow);
    lv_obj_set_size(modeBtns[i], 96, 26);
    lv_obj_set_style_bg_color(modeBtns[i], lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(modeBtns[i], lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(modeBtns[i], 1, 0);
    lv_obj_set_style_radius(modeBtns[i], 6, 0);
    lv_obj_set_style_shadow_width(modeBtns[i], 0, 0);
    lv_obj_t* ml = lv_label_create(modeBtns[i]);
    lv_label_set_text(ml, names[i]);
    lv_obj_center(ml);
    lv_obj_add_event_cb(modeBtns[i], mode_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  updateModeHighlight();

  // ── Row 5: PADDLE label ──
  lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "PADDLE ASSIGNMENT");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);

  // ── Row 6: Swap button ──
  lv_obj_t* swapBtn = lv_button_create(parent);
  lv_obj_set_size(swapBtn, lv_pct(100), 28);
  lv_obj_set_style_bg_color(swapBtn, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(swapBtn, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_border_width(swapBtn, 1, 0);
  lv_obj_set_style_radius(swapBtn, 6, 0);
  lv_obj_set_style_shadow_width(swapBtn, 0, 0);
  swapLbl = lv_label_create(swapBtn);
  lv_label_set_text(swapLbl, Settings_Get()->paddleSwap ? "DIT=RIGHT  DAH=LEFT" : "DIT=LEFT   DAH=RIGHT");
  lv_obj_set_style_text_color(swapLbl, lv_color_hex(0xFFB300), 0);
  lv_obj_center(swapLbl);
  lv_obj_add_event_cb(swapBtn, swap_cb, LV_EVENT_CLICKED, NULL);
}

void UI_Keyer_Refresh(void) {
  if (wpmBigLbl) refreshWpm();
}
