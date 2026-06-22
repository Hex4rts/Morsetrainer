#include "MT_UI_Games.h"
#include "MT_UI.h"
#include "MT_Score.h"
#include "MT_Koch.h"
#include <SD_MMC.h>
#include "MT_Game_FallingLetters.h"
#include "MT_Game_CallsignRush.h"
#include "MT_Game_Trainer.h"
#include "MT_Game_QSO.h"
#include "MT_Game_Phrases.h"
#include "MT_Game_MorseMemory.h"

extern const lv_font_t* ui_font_large;
extern const lv_font_t* ui_font_normal;

static lv_obj_t* hsLbl1 = NULL;
static lv_obj_t* hsLbl2 = NULL;
static lv_obj_t* hsLbl3 = NULL;
static lv_obj_t* hsLbl4 = NULL;
static lv_obj_t* hsLbl5 = NULL;
static lv_obj_t* hsLbl6 = NULL;

static void falling_cb(lv_event_t* e) { Game_FallingLetters_Start(); }
static void callrush_cb(lv_event_t* e) { Game_CallsignRush_Start(); }
static void trainer_cb(lv_event_t* e) { Game_Trainer_Start(); }
static void qso_cb(lv_event_t* e) { Game_QSO_Start(); }
static void phrases_cb(lv_event_t* e) { Game_Phrases_Start(); }
static void memory_cb(lv_event_t* e) { Game_MorseMemory_Start(); }

// Helper: game card (no icon — title + desc + best score)
static lv_obj_t* makeGameCard(lv_obj_t* parent, const char* name,
                               const char* desc, int16_t x, int16_t y,
                               lv_obj_t** hsLabel) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 150, 52);
  lv_obj_set_pos(card, x, y);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 8, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 6, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

  // Name (title) — top line, white
  lv_obj_t* nm = lv_label_create(card);
  lv_label_set_text(nm, name);
  lv_obj_set_style_text_color(nm, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(nm, LV_ALIGN_TOP_LEFT, 0, 0);

  // Description
  lv_obj_t* dc = lv_label_create(card);
  lv_label_set_text(dc, desc);
  lv_obj_set_style_text_color(dc, lv_color_hex(0x666666), 0);
  lv_obj_align(dc, LV_ALIGN_TOP_LEFT, 0, 16);

  // High score
  lv_obj_t* hs = lv_label_create(card);
  lv_label_set_text(hs, "Best: ---");
  lv_obj_set_style_text_color(hs, lv_color_hex(0x00E676), 0);
  lv_obj_align(hs, LV_ALIGN_TOP_LEFT, 0, 32);
  if (hsLabel) *hsLabel = hs;

  return card;
}

void UI_Games_Create(lv_obj_t* parent) {
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

  // Load scores once at creation
  score_board_t fl = Score_Load("falling");
  score_board_t cr = Score_Load("callrush");

  // Game cards - 2x2 grid + 5th centered
  lv_obj_t* c1 = makeGameCard(parent, "Falling Letters",
                               "Destroy before landing", 4, 2, &hsLbl1);
  lv_obj_add_event_cb(c1, falling_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* c2 = makeGameCard(parent, "Callsign Rush",
                               "Send callsigns fast", 160, 2, &hsLbl2);
  lv_obj_add_event_cb(c2, callrush_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* c3 = makeGameCard(parent, "Morse Trace",
                               "Learn & practice", 4, 58, &hsLbl3);
  lv_obj_add_event_cb(c3, trainer_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* c4 = makeGameCard(parent, "QSO Simulator",
                               "Practice CW contacts", 160, 58, &hsLbl4);
  lv_obj_add_event_cb(c4, qso_cb, LV_EVENT_CLICKED, NULL);

  // Row 3: two more full-size cards
  lv_obj_t* c5 = makeGameCard(parent, "CW Essentials",
                               "Prosigns & Q-codes", 4, 114, &hsLbl5);
  lv_obj_add_event_cb(c5, phrases_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* c6 = makeGameCard(parent, "Morse Memory",
                               "Recall the sequence", 160, 114, &hsLbl6);
  lv_obj_add_event_cb(c6, memory_cb, LV_EVENT_CLICKED, NULL);

  // Set initial scores
  score_board_t tr = Score_Load("trace");
  score_board_t qo = Score_Load("qso");
  score_board_t ph = Score_Load("phrases");
  score_board_t mm = Score_Load("memory");
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
  if (hsLbl5) {
    if (ph.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", ph.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl5, buf);
  }
  if (hsLbl6) {
    if (mm.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", mm.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl6, buf);
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
  score_board_t ph = Score_Load("phrases");
  score_board_t mm = Score_Load("memory");
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
  if (hsLbl5) {
    if (ph.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", ph.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl5, buf);
  }
  if (hsLbl6) {
    if (mm.count > 0) snprintf(buf, sizeof(buf), "Best: %lu", mm.entries[0].score);
    else snprintf(buf, sizeof(buf), "Best: ---");
    lv_label_set_text(hsLbl6, buf);
  }
}
