#include "MT_UI_Games.h"
#include "MT_UI.h"
#include "MT_Score.h"
#include "MT_Koch.h"
#include "MT_Game_FallingLetters.h"
#include "MT_Game_CallsignRush.h"
#include "MT_Game_Trainer.h"
#include "MT_Game_QSO.h"

extern const lv_font_t* ui_font_large;
extern const lv_font_t* ui_font_normal;

static lv_obj_t* hsLbl1 = NULL;
static lv_obj_t* hsLbl2 = NULL;
static lv_obj_t* hsLbl3 = NULL;
static lv_obj_t* hsLbl4 = NULL;

static void falling_cb(lv_event_t* e) { Game_FallingLetters_Start(); }
static void callrush_cb(lv_event_t* e) { Game_CallsignRush_Start(); }
static void trainer_cb(lv_event_t* e) { Game_Trainer_Start(); }
static void qso_cb(lv_event_t* e) { Game_QSO_Start(); }

// Helper: game card
static lv_obj_t* makeGameCard(lv_obj_t* parent, const char* icon, const char* name,
                               const char* desc, int16_t x, int16_t y,
                               lv_obj_t** hsLabel) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 150, 78);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 6, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

  // Icon (large green text)
  lv_obj_t* ic = lv_label_create(card);
  lv_label_set_text(ic, icon);
  lv_obj_set_style_text_color(ic, lv_color_hex(0x00E676), 0);
  lv_obj_set_style_text_font(ic, ui_font_large, 0);
  lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 0, 0);

  // Name
  lv_obj_t* nm = lv_label_create(card);
  lv_label_set_text(nm, name);
  lv_obj_set_style_text_color(nm, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 0, 22);

  // Description
  lv_obj_t* dc = lv_label_create(card);
  lv_label_set_text(dc, desc);
  lv_obj_set_style_text_color(dc, lv_color_hex(0x666666), 0);
  lv_obj_align(dc, LV_ALIGN_TOP_LEFT, 0, 36);

  // High score
  lv_obj_t* hs = lv_label_create(card);
  lv_label_set_text(hs, "Best: ---");
  lv_obj_set_style_text_color(hs, lv_color_hex(0x00E676), 0);
  lv_obj_align(hs, LV_ALIGN_TOP_LEFT, 0, 52);
  if (hsLabel) *hsLabel = hs;

  return card;
}

void UI_Games_Create(lv_obj_t* parent) {
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  // Load scores once at creation
  score_board_t fl = Score_Load("falling");
  score_board_t cr = Score_Load("callrush");

  // Game cards — 2×2 grid
  lv_obj_t* c1 = makeGameCard(parent, LV_SYMBOL_DOWN, "Falling Letters",
                               "Destroy before landing", 4, 4, &hsLbl1);
  lv_obj_add_event_cb(c1, falling_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* c2 = makeGameCard(parent, LV_SYMBOL_CALL, "Callsign Rush",
                               "Send callsigns fast", 160, 4, &hsLbl2);
  lv_obj_add_event_cb(c2, callrush_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* c3 = makeGameCard(parent, LV_SYMBOL_AUDIO, "Morse Trace",
                               "Learn & practice", 4, 88, &hsLbl3);
  lv_obj_add_event_cb(c3, trainer_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* c4 = makeGameCard(parent, "Q", "QSO Simulator",
                               "Practice CW contacts", 160, 88, &hsLbl4);
  lv_obj_add_event_cb(c4, qso_cb, LV_EVENT_CLICKED, NULL);

  // Set initial scores
  score_board_t tr = Score_Load("trace");
  score_board_t qo = Score_Load("qso");
  char buf[20];
  if (hsLbl1) {
    if (fl.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", fl.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl1, buf);
  }
  if (hsLbl2) {
    if (cr.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", cr.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl2, buf);
  }
  if (hsLbl3) {
    if (tr.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", tr.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl3, buf);
  }
  if (hsLbl4) {
    if (qo.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", qo.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl4, buf);
  }
}

void UI_Games_Refresh(void) {
  static uint32_t lastRefresh = 0;
  if (millis() - lastRefresh < 2000) return;
  lastRefresh = millis();

  score_board_t fl = Score_Load("falling");
  score_board_t cr = Score_Load("callrush");
  score_board_t tr = Score_Load("trace");
  score_board_t qo = Score_Load("qso");
  char buf[20];
  if (hsLbl1) {
    if (fl.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", fl.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl1, buf);
  }
  if (hsLbl2) {
    if (cr.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", cr.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl2, buf);
  }
  if (hsLbl3) {
    if (tr.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", tr.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl3, buf);
  }
  if (hsLbl4) {
    if (qo.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", qo.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl4, buf);
  }
}
