#include "MT_Game_FallingLetters.h"
#include "MT_UI.h"
#include "MT_Keyer.h"
#include "MT_Morse.h"
#include "MT_NeoPixel.h"
#include "MT_Score.h"
#include <lvgl.h>

#define FL_MAX       8
#define FL_TICK_MS   33
#define FL_LIVES     3
#define FL_PER_LVL   10
#define FL_W         320
#define FL_H         240
#define FL_HUD_H     22
#define FL_DANGER_Y  (FL_H - 20)

enum FLDiff { FL_BGN, FL_EXP };
// Beginner: letter + morse hint under each
// Expert:   letter only, no hint

typedef struct {
  lv_obj_t* label;
  lv_obj_t* hintLbl;  // morse hint (beginner) or morse-only (expert)
  char ch;
  int16_t x, y;
  bool active;
} fl_letter_t;

static lv_obj_t*   scr       = NULL;
static lv_obj_t*   menuScr   = NULL;
static lv_obj_t*   scoreLbl  = NULL;
static lv_obj_t*   livesLbl  = NULL;
static lv_obj_t*   levelLbl  = NULL;
static lv_obj_t*   overPanel = NULL;
static lv_timer_t* tickTmr   = NULL;
static lv_timer_t* spawnTmr  = NULL;
static fl_letter_t letters[FL_MAX];
static uint32_t    score     = 0;
static uint8_t     lives     = FL_LIVES;
static uint8_t     level     = 1;
static uint8_t     kills     = 0;
static bool        active    = false;
static FLDiff      fldiff    = FL_BGN;
static volatile char lastChar = 0;

static void game_char(char c) { lastChar = c; UI_PushDecodedChar(c); }
static void exit_cb(lv_event_t* e) { Game_FallingLetters_Stop(); }

static void spawn(void) {
  for (int i = 0; i < FL_MAX; i++) {
    if (letters[i].active) continue;
    letters[i].ch = 'A' + random(0, 26);
    letters[i].x  = random(10, FL_W - 40);
    letters[i].y  = -20;
    letters[i].active = true;

    const char* code = Morse_Encode(letters[i].ch);
    char buf[2] = {letters[i].ch, '\0'};

    if (!letters[i].label) {
      letters[i].label = lv_label_create(scr);
#if LV_FONT_MONTSERRAT_24
      lv_obj_set_style_text_font(letters[i].label, &lv_font_montserrat_24, 0);
#endif
    }
    if (!letters[i].hintLbl) {
      letters[i].hintLbl = lv_label_create(scr);
    }

    // Difficulty determines what's shown
    // Both show the letter, beginner adds morse hint
    lv_label_set_text(letters[i].label, buf);
    lv_obj_set_style_text_color(letters[i].label, lv_color_hex(0xFFFFFF), 0);
#if LV_FONT_MONTSERRAT_24
    lv_obj_set_style_text_font(letters[i].label, &lv_font_montserrat_24, 0);
#endif
    if (fldiff == FL_BGN && code) {
      // Beginner: show morse hint under letter
      lv_label_set_text(letters[i].hintLbl, code);
      lv_obj_set_style_text_color(letters[i].hintLbl, lv_color_hex(0x42A5F5), 0);
      lv_obj_clear_flag(letters[i].hintLbl, LV_OBJ_FLAG_HIDDEN);
    } else {
      // Expert: no hint
      lv_label_set_text(letters[i].hintLbl, "");
      lv_obj_add_flag(letters[i].hintLbl, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(letters[i].label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(letters[i].label, letters[i].x, letters[i].y);
    return;
  }
}

static void showGameOver(void) {
  active = false;
  if (tickTmr)  lv_timer_pause(tickTmr);
  if (spawnTmr) lv_timer_pause(spawnTmr);

  overPanel = lv_obj_create(scr);
  lv_obj_set_size(overPanel, 220, 130);
  lv_obj_center(overPanel);
  lv_obj_set_style_bg_color(overPanel, lv_color_hex(0x0D0D0D), 0);
  lv_obj_set_style_bg_opa(overPanel, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(overPanel, 8, 0);
  lv_obj_set_style_border_color(overPanel, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_border_width(overPanel, 2, 0);

  char buf[40];
  snprintf(buf, sizeof(buf), "MISSION FAILED\nSCORE: %lu\nLEVEL: %d", score, level);
  lv_obj_t* lbl = lv_label_create(overPanel);
  lv_label_set_text(lbl, buf);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t* btn = lv_button_create(overPanel);
  lv_obj_set_size(btn, 120, 32);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFB300), 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_t* bl = lv_label_create(btn);
  lv_label_set_text(bl, "RTB");
  lv_obj_set_style_text_color(bl, lv_color_hex(0x000000), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(btn, exit_cb, LV_EVENT_CLICKED, NULL);

  Score_Submit("falling", score, level);
}

static void tick_cb(lv_timer_t* t) {
  if (!active) return;
  char input = lastChar; lastChar = 0;
  float speed = 1.0f + (level - 1) * 0.4f;

  // Phase 1: move all letters, check for landing
  for (int i = 0; i < FL_MAX; i++) {
    if (!letters[i].active) continue;
    letters[i].y += (int16_t)speed;
    lv_obj_set_pos(letters[i].label, letters[i].x, letters[i].y);
    if (letters[i].hintLbl && !lv_obj_has_flag(letters[i].hintLbl, LV_OBJ_FLAG_HIDDEN))
      lv_obj_set_pos(letters[i].hintLbl, letters[i].x, letters[i].y + 26);

    if (letters[i].y >= FL_DANGER_Y) {
      letters[i].active = false;
      lv_obj_add_flag(letters[i].label, LV_OBJ_FLAG_HIDDEN);
      if (letters[i].hintLbl) lv_obj_add_flag(letters[i].hintLbl, LV_OBJ_FLAG_HIDDEN);
      lives--;
      NeoPixel_Wrong();
    }
  }

  // Phase 2: match input to LOWEST matching letter (highest Y)
  if (input) {
    int bestIdx = -1;
    int16_t bestY = -999;
    char uc = toupper(input);
    for (int i = 0; i < FL_MAX; i++) {
      if (letters[i].active && letters[i].ch == uc && letters[i].y > bestY) {
        bestIdx = i;
        bestY = letters[i].y;
      }
    }
    if (bestIdx >= 0) {
      letters[bestIdx].active = false;
      lv_obj_add_flag(letters[bestIdx].label, LV_OBJ_FLAG_HIDDEN);
      if (letters[bestIdx].hintLbl) lv_obj_add_flag(letters[bestIdx].hintLbl, LV_OBJ_FLAG_HIDDEN);
      score += level * 10;
      kills++;
      NeoPixel_Correct();
      if (kills >= FL_PER_LVL) {
        level++; kills = 0;
        NeoPixel_LevelUp();
        char b[12]; snprintf(b, sizeof(b), "LVL %d", level);
        lv_label_set_text(levelLbl, b);
      }
    }
  }

  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", score); lv_label_set_text(scoreLbl, buf);
  snprintf(buf, sizeof(buf), "%d", lives); lv_label_set_text(livesLbl, buf);
  if (lives <= 1) lv_obj_set_style_text_color(livesLbl, lv_color_hex(0xFF3D00), 0);
  if (lives == 0) showGameOver();
}

static void spawn_cb(lv_timer_t* t) {
  if (!active) return;
  spawn();
  uint32_t interval = 2000 - (level - 1) * 150;
  if (interval < 600) interval = 600;
  lv_timer_set_period(spawnTmr, interval);
}

// ── Menu ──
static void startFL(FLDiff d);
static void menu_exit(lv_event_t* e) { if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; } UI_ShowMain(); }
static void bgn_cb(lv_event_t* e) { startFL(FL_BGN); }
static void exp_cb(lv_event_t* e) { startFL(FL_EXP); }

static lv_obj_t* mkBtn(lv_obj_t* p, const char* t, const char* d, lv_color_t c, int16_t y) {
  lv_obj_t* b = lv_button_create(p);
  lv_obj_set_size(b, 280, 32);
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

void Game_FallingLetters_Start(void) {
  menuScr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(menuScr, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(menuScr, LV_OPA_COVER, 0);

  lv_obj_t* t = lv_label_create(menuScr);
  lv_label_set_text(t, "FALLING LETTERS");
  lv_obj_set_style_text_color(t, lv_color_hex(0x00E676), 0);
#if LV_FONT_MONTSERRAT_24
  lv_obj_set_style_text_font(t, &lv_font_montserrat_24, 0);
#endif
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t* sub = lv_label_create(menuScr);
  lv_label_set_text(sub, "Key the letter before it lands");
  lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 42);

  lv_obj_t* b;
  b = mkBtn(menuScr, "BEGINNER", "letter + morse hint", lv_color_hex(0x00E676), 80);
  lv_obj_add_event_cb(b, bgn_cb, LV_EVENT_CLICKED, NULL);
  b = mkBtn(menuScr, "EXPERT", "letter only", lv_color_hex(0xFF3D00), 120);
  lv_obj_add_event_cb(b, exp_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* bb = lv_button_create(menuScr);
  lv_obj_set_size(bb, 80, 26);
  lv_obj_align(bb, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(bb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(bb, 0, 0);
  lv_obj_set_style_radius(bb, 4, 0);
  lv_obj_t* bl = lv_label_create(bb);
  lv_label_set_text(bl, "BACK"); lv_obj_set_style_text_color(bl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(bl);
  lv_obj_add_event_cb(bb, menu_exit, LV_EVENT_CLICKED, NULL);

  lv_screen_load(menuScr);
}

static void startFL(FLDiff d) {
  fldiff = d;
  score = 0; lives = FL_LIVES; level = 1; kills = 0; lastChar = 0;
  active = true; overPanel = NULL;

  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }

  scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x050510), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  // HUD
  lv_obj_t* hud = lv_obj_create(scr);
  lv_obj_remove_style_all(hud);
  lv_obj_set_size(hud, FL_W, FL_HUD_H);
  lv_obj_set_pos(hud, 0, 0);
  lv_obj_set_style_bg_color(hud, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(hud, LV_OPA_COVER, 0);

  scoreLbl = lv_label_create(hud);
  lv_label_set_text(scoreLbl, "0");
  lv_obj_set_style_text_color(scoreLbl, lv_color_hex(0x00E676), 0);
  lv_obj_align(scoreLbl, LV_ALIGN_LEFT_MID, 8, 0);

  levelLbl = lv_label_create(hud);
  lv_label_set_text(levelLbl, "LVL 1");
  lv_obj_set_style_text_color(levelLbl, lv_color_hex(0x42A5F5), 0);
  lv_obj_align(levelLbl, LV_ALIGN_CENTER, 0, 0);

  livesLbl = lv_label_create(hud);
  char lb[8]; snprintf(lb, sizeof(lb), "%d", lives);
  lv_label_set_text(livesLbl, lb);
  lv_obj_set_style_text_color(livesLbl, lv_color_hex(0x00E676), 0);
  lv_obj_align(livesLbl, LV_ALIGN_RIGHT_MID, -50, 0);

  // Exit button in HUD
  lv_obj_t* eb = lv_button_create(hud);
  lv_obj_set_size(eb, 36, 18);
  lv_obj_align(eb, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_bg_color(eb, lv_color_hex(0x333333), 0);
  lv_obj_set_style_shadow_width(eb, 0, 0);
  lv_obj_set_style_radius(eb, 4, 0);
  lv_obj_t* ebl = lv_label_create(eb);
  lv_label_set_text(ebl, "EXIT");
  lv_obj_set_style_text_color(ebl, lv_color_hex(0xFF3D00), 0);
  lv_obj_center(ebl);
  lv_obj_add_event_cb(eb, exit_cb, LV_EVENT_CLICKED, NULL);

  // Danger zone
  lv_obj_t* dz = lv_obj_create(scr);
  lv_obj_remove_style_all(dz);
  lv_obj_set_size(dz, FL_W, 2);
  lv_obj_set_pos(dz, 0, FL_DANGER_Y);
  lv_obj_set_style_bg_color(dz, lv_color_hex(0xFF3D00), 0);
  lv_obj_set_style_bg_opa(dz, LV_OPA_60, 0);

  for (int i = 0; i < FL_MAX; i++) { letters[i].active = false; letters[i].label = NULL; letters[i].hintLbl = NULL; }

  Keyer_OnChar(game_char);
  tickTmr  = lv_timer_create(tick_cb,  FL_TICK_MS, NULL);
  spawnTmr = lv_timer_create(spawn_cb, 2000, NULL);
  lv_screen_load(scr);
}

void Game_FallingLetters_Stop(void) {
  active = false;
  if (tickTmr)  { lv_timer_del(tickTmr);  tickTmr = NULL; }
  if (spawnTmr) { lv_timer_del(spawnTmr); spawnTmr = NULL; }
  Keyer_OnChar([](char c) { UI_PushDecodedChar(c); });
  if (scr) { UI_ShowMain(); lv_obj_delete(scr); scr = NULL; }
  if (menuScr) { lv_obj_delete(menuScr); menuScr = NULL; }
}
