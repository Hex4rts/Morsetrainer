#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_NeoPixel.h"
#include "MT_Settings.h"

// ── Theme colors (matching reference ui.h) ──
#define C_BG       lv_color_hex(0x000000)
#define C_SURFACE  lv_color_hex(0x1A1A1A)
#define C_BORDER   lv_color_hex(0x333333)
#define C_GREEN    lv_color_hex(0x00E676)
#define C_AMBER    lv_color_hex(0xFFB300)
#define C_RED      lv_color_hex(0xFF3D00)
#define C_BLUE     lv_color_hex(0x42A5F5)
#define C_WHITE    lv_color_hex(0xFFFFFF)
#define C_MUTED    lv_color_hex(0x666666)

#define CHAR_QUEUE_LEN 32
static QueueHandle_t charQueue = NULL;
static lv_obj_t* mainScreen = NULL;
static lv_obj_t* tabview    = NULL;
static lv_timer_t* refreshTimer = NULL;
const lv_font_t* ui_font_large  = NULL;
const lv_font_t* ui_font_normal = NULL;

static void refresh_timer_cb(lv_timer_t* t) { UI_Refresh(); }

static void keyer_char_handler(char c) {
  if (charQueue) xQueueSend(charQueue, &c, 0);
}
static void keyer_element_handler(bool state, bool isDah) {
  if (state) NeoPixel_KeyFlash(isDah);
}

void UI_Init(void) {
  charQueue = xQueueCreate(CHAR_QUEUE_LEN, sizeof(char));
  Keyer_OnChar(keyer_char_handler);
  Keyer_OnElement(keyer_element_handler);

#if LV_FONT_MONTSERRAT_16
  ui_font_large = &lv_font_montserrat_16;
#else
  ui_font_large = LV_FONT_DEFAULT;
#endif
#if LV_FONT_MONTSERRAT_12
  ui_font_normal = &lv_font_montserrat_12;
#else
  ui_font_normal = LV_FONT_DEFAULT;
#endif

  mainScreen = lv_screen_active();

  // ── Dark background ──
  lv_obj_set_style_bg_color(mainScreen, C_BG, 0);
  lv_obj_set_style_bg_opa(mainScreen, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(mainScreen, C_WHITE, 0);
  lv_obj_set_style_text_font(mainScreen, ui_font_normal, 0);

  // ── Tabview ──
  tabview = lv_tabview_create(mainScreen);
  lv_obj_set_style_bg_color(tabview, C_BG, 0);
  lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, 0);

  // ── Style the tab bar — compact ──
  lv_obj_t* tab_bar = lv_tabview_get_tab_bar(tabview);
  lv_obj_set_style_bg_color(tab_bar, C_SURFACE, 0);
  lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_text_font(tab_bar, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(tab_bar, C_MUTED, 0);
  lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(tab_bar, C_BORDER, 0);
  lv_obj_set_style_border_width(tab_bar, 1, 0);
  lv_obj_set_style_pad_all(tab_bar, 0, 0);
  lv_obj_set_height(tab_bar, 22);

  // Add tabs
  lv_obj_t* t1 = lv_tabview_add_tab(tabview, "HOME");
  lv_obj_t* t2 = lv_tabview_add_tab(tabview, "KEY");
  lv_obj_t* t3 = lv_tabview_add_tab(tabview, "OPS");
  lv_obj_t* t4 = lv_tabview_add_tab(tabview, "CFG");
  lv_obj_t* t5 = lv_tabview_add_tab(tabview, "NET");

  // Style each tab content dark
  lv_obj_t* tabs[] = {t1, t2, t3, t4, t5};
  for (int i = 0; i < 5; i++) {
    lv_obj_set_style_bg_color(tabs[i], C_BG, 0);
    lv_obj_set_style_bg_opa(tabs[i], LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(tabs[i], C_WHITE, 0);
    lv_obj_set_style_pad_all(tabs[i], 4, 0);
  }

  // Build screens
  UI_Home_Create(t1);
  UI_Keyer_Create(t2);
  UI_Games_Create(t3);
  UI_Settings_Create(t4);
  UI_WiFi_Create(t5);

  refreshTimer = lv_timer_create(refresh_timer_cb, 100, NULL);
}

lv_obj_t* UI_GetMainScreen(void) { return mainScreen; }
void UI_ShowMain(void) { lv_screen_load(mainScreen); }

void UI_PushDecodedChar(char c) {
  if (charQueue) xQueueSend(charQueue, &c, 0);
}

void UI_Refresh(void) {
  char c;
  while (xQueueReceive(charQueue, &c, 0) == pdTRUE) {
    extern void UI_Home_AddChar(char c);
    UI_Home_AddChar(c);
  }
  UI_Home_Refresh();
  UI_Keyer_Refresh();
  UI_Games_Refresh();
  UI_Settings_Refresh();
  UI_WiFi_Refresh();

  // Debounced NVS write — saves 3 seconds after last settings change
  Settings_FlushIfDirty();
}

// Auto-rotation stubs (disabled — kept for API compat)
void UI_CheckRotation(void) {}
void UI_SetAutoRotate(bool on) {}
bool UI_GetAutoRotate(void) { return false; }
