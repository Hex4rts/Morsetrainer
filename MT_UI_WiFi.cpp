#include "MT_UI_WiFi.h"
#include "MT_UI.h"
#include "MT_WiFi.h"

extern const lv_font_t* ui_font_large;
extern const lv_font_t* ui_font_normal;

static lv_obj_t* wifiSw    = NULL;
static lv_obj_t* ssidTA    = NULL;
static lv_obj_t* passTA    = NULL;
static lv_obj_t* statLbl   = NULL;
static lv_obj_t* ipLbl     = NULL;
static lv_obj_t* resultLbl = NULL;
static lv_obj_t* kb        = NULL;

static void toggle_cb(lv_event_t* e) {
  bool on = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED);
  if (on) MTWiFi_SetCredentials(lv_textarea_get_text(ssidTA), lv_textarea_get_text(passTA));
  MTWiFi_Enable(on);
}
static void fetch_cb(lv_event_t* e) {
  char buf[128];
  lv_label_set_text(resultLbl, MTWiFi_FetchLeaderboard("falling", buf, sizeof(buf)) ? buf : "NOT CONNECTED");
}
static void upload_cb(lv_event_t* e) {
  lv_label_set_text(resultLbl, MTWiFi_UploadScore("falling", 0) ? "UPLOADED" : "NOT CONNECTED");
}
static void kb_cb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_READY || lv_event_get_code(e) == LV_EVENT_CANCEL)
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}
static void ta_focus(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_FOCUSED && kb) {
    lv_keyboard_set_textarea(kb, (lv_obj_t*)lv_event_get_target(e));
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
}

void UI_WiFi_Create(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(parent, 3, 0);
  lv_obj_set_style_pad_all(parent, 4, 0);

  // Header row
  lv_obj_t* r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* lbl = lv_label_create(r);
  lv_label_set_text(lbl, "WIFI");
  lv_obj_set_style_text_font(lbl, ui_font_large, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFB300), 0);
  wifiSw = lv_switch_create(r);
  lv_obj_set_style_bg_color(wifiSw, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_color(wifiSw, lv_color_hex(0x00E676), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_add_event_cb(wifiSw, toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

  // SSID
  lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "SSID");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);
  ssidTA = lv_textarea_create(parent);
  lv_textarea_set_one_line(ssidTA, true);
  lv_textarea_set_max_length(ssidTA, 32);
  lv_textarea_set_placeholder_text(ssidTA, "network name");
  lv_obj_set_width(ssidTA, lv_pct(100));
  lv_obj_set_style_bg_color(ssidTA, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_text_color(ssidTA, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(ssidTA, lv_color_hex(0x333333), 0);
  lv_obj_add_event_cb(ssidTA, ta_focus, LV_EVENT_FOCUSED, NULL);

  // Password
  lbl = lv_label_create(parent);
  lv_label_set_text(lbl, "PASSWORD");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), 0);
  passTA = lv_textarea_create(parent);
  lv_textarea_set_one_line(passTA, true);
  lv_textarea_set_max_length(passTA, 64);
  lv_textarea_set_password_mode(passTA, true);
  lv_textarea_set_placeholder_text(passTA, "password");
  lv_obj_set_width(passTA, lv_pct(100));
  lv_obj_set_style_bg_color(passTA, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_text_color(passTA, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_color(passTA, lv_color_hex(0x333333), 0);
  lv_obj_add_event_cb(passTA, ta_focus, LV_EVENT_FOCUSED, NULL);

  // Status
  statLbl = lv_label_create(parent);
  lv_label_set_text(statLbl, "STATUS: OFF");
  lv_obj_set_style_text_color(statLbl, lv_color_hex(0x666666), 0);
  ipLbl = lv_label_create(parent);
  lv_label_set_text(ipLbl, "IP: ---");
  lv_obj_set_style_text_color(ipLbl, lv_color_hex(0x666666), 0);

  // Leaderboard buttons
  r = lv_obj_create(parent);
  lv_obj_remove_style_all(r);
  lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* b1 = lv_button_create(r);
  lv_obj_set_size(b1, 120, 26);
  lv_obj_set_style_bg_color(b1, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(b1, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_border_width(b1, 1, 0);
  lbl = lv_label_create(b1);
  lv_label_set_text(lbl, "LEADERBOARD");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFB300), 0);
  lv_obj_center(lbl);
  lv_obj_add_event_cb(b1, fetch_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* b2 = lv_button_create(r);
  lv_obj_set_size(b2, 100, 26);
  lv_obj_set_style_bg_color(b2, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(b2, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_border_width(b2, 1, 0);
  lbl = lv_label_create(b2);
  lv_label_set_text(lbl, "UPLOAD");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x00E676), 0);
  lv_obj_center(lbl);
  lv_obj_add_event_cb(b2, upload_cb, LV_EVENT_CLICKED, NULL);

  resultLbl = lv_label_create(parent);
  lv_label_set_text(resultLbl, "");
  lv_obj_set_style_text_color(resultLbl, lv_color_hex(0x666666), 0);

  // Keyboard
  kb = lv_keyboard_create(lv_screen_active());
  lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_size(kb, 320, 120);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_CANCEL, NULL);
}

void UI_WiFi_Refresh(void) {
  if (!statLbl) return;
  char buf[40];
  snprintf(buf, sizeof(buf), "STATUS: %s", MTWiFi_StateName());
  lv_label_set_text(statLbl, buf);
  snprintf(buf, sizeof(buf), "IP: %s", MTWiFi_IP().c_str());
  lv_label_set_text(ipLbl, buf);
}
