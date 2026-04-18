#include "MT_UI_Settings.h"
#include "MT_UI.h"
#include "MT_Settings.h"
#include "MT_NeoPixel.h"
#include "MT_Version.h"
#include "Display_ST7789.h"

extern const lv_font_t* ui_font_large;
extern const lv_font_t* ui_font_normal;

static lv_obj_t* volSlider   = NULL;
static lv_obj_t* volVal      = NULL;
static lv_obj_t* freqSlider  = NULL;
static lv_obj_t* freqVal     = NULL;
static lv_obj_t* blSlider    = NULL;
static lv_obj_t* callTA      = NULL;
static lv_obj_t* ledDrop     = NULL;
static lv_obj_t* ledBrtSld   = NULL;
static lv_obj_t* ledBgSld    = NULL;
static lv_obj_t* ledCountLbl = NULL;
static lv_obj_t* statusLbl   = NULL;
static lv_obj_t* kb          = NULL;

// ── Callbacks ──
static void vol_cb(lv_event_t* e) {
  int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
  Settings_SetVolume(v);
  char b[8]; snprintf(b, sizeof(b), "%d%%", v); lv_label_set_text(volVal, b);
}
static void freq_cb(lv_event_t* e) {
  int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
  Settings_SetSidetoneFreq(v);
  char b[12]; snprintf(b, sizeof(b), "%dHz", v); lv_label_set_text(freqVal, b);
}
static void bl_cb(lv_event_t* e) {
  int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
  Settings_SetBacklight(v); Set_Backlight(v);
}
static void flip_cb(lv_event_t* e) {
  bool checked = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  Settings_SetScreenFlip(checked);
}
static void led_cb(lv_event_t* e) {
  uint16_t s = lv_dropdown_get_selected((lv_obj_t*)lv_event_get_target(e));
  if (s < NEO_MODE_COUNT) Settings_SetLEDMode((neo_mode_t)s);
}
static void ledbrt_cb(lv_event_t* e) {
  int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
  Settings_SetLEDBrightness(v);
}
static void ledbg_cb(lv_event_t* e) {
  int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
  Settings_SetLEDBgBrightness(v);
}
static void refreshLedCount(void) {
  if (!ledCountLbl) return;
  char buf[8]; snprintf(buf, sizeof(buf), "%d", NeoPixel_GetCount());
  lv_label_set_text(ledCountLbl, buf);
}
static void ledCountUp(lv_event_t* e) {
  Settings_SetLEDCount(NeoPixel_GetCount() + 1);
  refreshLedCount();
}
static void ledCountDown(lv_event_t* e) {
  Settings_SetLEDCount(NeoPixel_GetCount() - 1);
  refreshLedCount();
}
static void call_done(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY || c == LV_EVENT_DEFOCUSED) {
    Settings_SetCallsign(lv_textarea_get_text(callTA));
    lv_label_set_text(statusLbl, "CALLSIGN SET");
  }
}
static void kb_cb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    Settings_SetCallsign(lv_textarea_get_text(callTA));
  }
}
static void call_focus(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_FOCUSED && kb) {
    lv_keyboard_set_textarea(kb, callTA);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}
static void backup_cb(lv_event_t* e) {
  lv_label_set_text(statusLbl, Settings_BackupToSD() ? "BACKUP OK" : "BACKUP FAIL");
}
static void restore_cb(lv_event_t* e) {
  if (Settings_RestoreFromSD()) { Settings_Apply(Settings_Get()); lv_label_set_text(statusLbl, "RESTORED"); }
  else lv_label_set_text(statusLbl, "RESTORE FAIL");
}
static void reset_cb(lv_event_t* e) {
  Settings_FactoryReset(); lv_label_set_text(statusLbl, "FACTORY RESET");
}

// ── Helpers ──
static lv_obj_t* mkSlider(lv_obj_t* p, int min, int max, int val, int w) {
  lv_obj_t* s = lv_slider_create(p);
  lv_slider_set_range(s, min, max);
  lv_slider_set_value(s, val, LV_ANIM_OFF);
  lv_obj_set_width(s, w);
  lv_obj_set_style_bg_color(s, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_color(s, lv_color_hex(0x00E676), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s, lv_color_hex(0x00E676), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s, 3, LV_PART_KNOB);
  return s;
}

static lv_obj_t* mkRow(lv_obj_t* p) {
  lv_obj_t* r = lv_obj_create(p);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return r;
}

static lv_obj_t* mkLabel(lv_obj_t* p, const char* t) {
  lv_obj_t* l = lv_label_create(p);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, lv_color_hex(0x666666), 0);
  return l;
}

static lv_obj_t* mkSmBtn(lv_obj_t* p, const char* t, lv_color_t bg, int w) {
  lv_obj_t* b = lv_button_create(p);
  lv_obj_set_size(b, w, 26);
  lv_obj_set_style_bg_color(b, bg, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, 4, 0);
  lv_obj_t* l = lv_label_create(b);
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, lv_color_hex(0x000000), 0);
  lv_obj_center(l);
  return b;
}

void UI_Settings_Create(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 2, 0);
  lv_obj_set_style_pad_all(parent, 4, 0);

  const mt_settings_t* s = Settings_Get();
  char buf[16];

  // ── Callsign (at top so keyboard doesn't cover it) ──
  lv_obj_t* r = mkRow(parent);
  mkLabel(r, "CALL");
  callTA = lv_textarea_create(r);
  lv_textarea_set_one_line(callTA, true);
  lv_textarea_set_max_length(callTA, 11);
  lv_textarea_set_text(callTA, s->callsign);
  lv_obj_set_width(callTA, 120);
  lv_obj_set_style_bg_color(callTA, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_text_color(callTA, lv_color_hex(0x42A5F5), 0);
  lv_obj_set_style_border_color(callTA, lv_color_hex(0x42A5F5), 0);
  lv_obj_add_event_cb(callTA, call_done, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(callTA, call_done, LV_EVENT_DEFOCUSED, NULL);
  lv_obj_add_event_cb(callTA, call_focus, LV_EVENT_FOCUSED, NULL);

  // ── Volume ──
  r = mkRow(parent);
  mkLabel(r, "VOLUME");
  volSlider = mkSlider(r, 0, 100, s->volume, 120);
  lv_obj_add_event_cb(volSlider, vol_cb, LV_EVENT_VALUE_CHANGED, NULL);
  volVal = lv_label_create(r);
  snprintf(buf, sizeof(buf), "%d%%", s->volume); lv_label_set_text(volVal, buf);

  // ── Sidetone ──
  r = mkRow(parent);
  mkLabel(r, "TONE");
  freqSlider = mkSlider(r, 200, 1200, s->sidetoneFreq, 120);
  lv_obj_add_event_cb(freqSlider, freq_cb, LV_EVENT_VALUE_CHANGED, NULL);
  freqVal = lv_label_create(r);
  snprintf(buf, sizeof(buf), "%dHz", s->sidetoneFreq); lv_label_set_text(freqVal, buf);

  // ── Backlight ──
  r = mkRow(parent);
  mkLabel(r, "LIGHT");
  blSlider = mkSlider(r, 5, 100, s->backlight, 140);
  lv_obj_add_event_cb(blSlider, bl_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // ── Screen Flip ──
  r = mkRow(parent);
  mkLabel(r, "FLIP");
  lv_obj_t* flipSw = lv_switch_create(r);
  lv_obj_set_style_bg_color(flipSw, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_color(flipSw, lv_color_hex(0x00E676), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (s->screenFlip) lv_obj_add_state(flipSw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(flipSw, flip_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // ── LED Mode ──
  r = mkRow(parent);
  mkLabel(r, "LED");
  ledDrop = lv_dropdown_create(r);
  lv_dropdown_set_options(ledDrop, "OFF\nKEY FLASH\nWPM METER\nSTEADY\nBREATHE\nSTARFIELD\nCHASE\nRAINBOW");
  lv_dropdown_set_selected(ledDrop, (uint16_t)s->ledMode);
  lv_obj_set_width(ledDrop, 120);
  lv_obj_set_style_bg_color(ledDrop, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_text_color(ledDrop, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_border_color(ledDrop, lv_color_hex(0x333333), 0);
  lv_obj_add_event_cb(ledDrop, led_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // ── LED Count ──
  r = mkRow(parent);
  mkLabel(r, "LEDs");
  lv_obj_t* bm = lv_button_create(r);
  lv_obj_set_size(bm, 30, 22);
  lv_obj_set_style_bg_color(bm, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(bm, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_border_width(bm, 1, 0);
  lv_obj_set_style_shadow_width(bm, 0, 0);
  lv_obj_set_style_radius(bm, 4, 0);
  lv_obj_t* bml = lv_label_create(bm);
  lv_label_set_text(bml, "-");
  lv_obj_set_style_text_color(bml, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(bml);
  lv_obj_add_event_cb(bm, ledCountDown, LV_EVENT_CLICKED, NULL);

  ledCountLbl = lv_label_create(r);
  char lcBuf[8]; snprintf(lcBuf, sizeof(lcBuf), "%d", s->ledCount);
  lv_label_set_text(ledCountLbl, lcBuf);
  lv_obj_set_style_text_color(ledCountLbl, lv_color_hex(0x00E676), 0);

  lv_obj_t* bp = lv_button_create(r);
  lv_obj_set_size(bp, 30, 22);
  lv_obj_set_style_bg_color(bp, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(bp, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_border_width(bp, 1, 0);
  lv_obj_set_style_shadow_width(bp, 0, 0);
  lv_obj_set_style_radius(bp, 4, 0);
  lv_obj_t* bpl = lv_label_create(bp);
  lv_label_set_text(bpl, "+");
  lv_obj_set_style_text_color(bpl, lv_color_hex(0x00E676), 0);
  lv_obj_center(bpl);
  lv_obj_add_event_cb(bp, ledCountUp, LV_EVENT_CLICKED, NULL);

  // ── LED Key Brightness ──
  r = mkRow(parent);
  mkLabel(r, "KEY BRT");
  ledBrtSld = mkSlider(r, 0, 255, s->ledBrightness, 140);
  lv_obj_set_style_bg_color(ledBrtSld, lv_color_hex(0xFFB300), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ledBrtSld, lv_color_hex(0xFFB300), LV_PART_KNOB);
  lv_obj_add_event_cb(ledBrtSld, ledbrt_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // ── LED Background Brightness ──
  r = mkRow(parent);
  mkLabel(r, "BG BRT");
  ledBgSld = mkSlider(r, 0, 255, s->ledBgBrightness, 140);
  lv_obj_set_style_bg_color(ledBgSld, lv_color_hex(0x42A5F5), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ledBgSld, lv_color_hex(0x42A5F5), LV_PART_KNOB);
  lv_obj_add_event_cb(ledBgSld, ledbg_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // ── Ambient Color presets ──
  r = mkRow(parent);
  mkLabel(r, "COLOR");
  {
    static const uint32_t presets[] = {0xFF6600, 0xFF0000, 0x00FF00, 0x0066FF, 0xFF00FF, 0xFFFF00, 0xFFFFFF};
    static const int NUM_PRESETS = 7;
    for (int i = 0; i < NUM_PRESETS; i++) {
      lv_obj_t* cb = lv_button_create(r);
      lv_obj_set_size(cb, 28, 20);
      lv_obj_set_style_bg_color(cb, lv_color_hex(presets[i]), 0);
      lv_obj_set_style_radius(cb, 4, 0);
      lv_obj_set_style_shadow_width(cb, 0, 0);
      lv_obj_set_style_border_color(cb, lv_color_hex(0x666666), 0);
      lv_obj_set_style_border_width(cb, 1, 0);
      lv_obj_set_style_pad_all(cb, 0, 0);
      lv_obj_add_event_cb(cb, [](lv_event_t* e) {
        uint32_t c = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
        Settings_SetAmbientColor(c);
      }, LV_EVENT_CLICKED, (void*)(uintptr_t)presets[i]);
    }
  }

  // ── Buttons ──
  r = mkRow(parent);
  lv_obj_t* b;
  b = mkSmBtn(r, "BACKUP", lv_color_hex(0x00E676), 80);
  lv_obj_add_event_cb(b, backup_cb, LV_EVENT_CLICKED, NULL);
  b = mkSmBtn(r, "RESTORE", lv_color_hex(0xFFB300), 80);
  lv_obj_add_event_cb(b, restore_cb, LV_EVENT_CLICKED, NULL);
  b = mkSmBtn(r, "RESET", lv_color_hex(0xFF3D00), 70);
  lv_obj_add_event_cb(b, reset_cb, LV_EVENT_CLICKED, NULL);

  // ── Status ──
  statusLbl = lv_label_create(parent);
  lv_label_set_text(statusLbl, "");
  lv_obj_set_style_text_color(statusLbl, lv_color_hex(0x00E676), 0);

  // ── Version ──
  lv_obj_t* verLbl = lv_label_create(parent);
  lv_label_set_text(verLbl, "MorseTrainer v" MT_VERSION);
  lv_obj_set_style_text_color(verLbl, lv_color_hex(0xFFFFFF), 0);

  // ── Keyboard (hidden) ──
  kb = lv_keyboard_create(lv_screen_active());
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(kb, 320, 120);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_CANCEL, NULL);
}

void UI_Settings_Refresh(void) {}
