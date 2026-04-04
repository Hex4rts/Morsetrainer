#ifndef MT_UI_H
#define MT_UI_H

#include <Arduino.h>
#include <lvgl.h>

// Initialise the full UI (call after Lvgl_Init)
void UI_Init(void);

// Return to the main tabview screen (e.g. after exiting a game)
void UI_ShowMain(void);

// Get the main screen object (for games to switch away and back)
lv_obj_t* UI_GetMainScreen(void);

// Periodic refresh (called from LVGL timer, ~1 Hz)
void UI_Refresh(void);

// Auto-rotation check (call from DriverTask ~30 Hz)
void UI_CheckRotation(void);
void UI_SetAutoRotate(bool on);
bool UI_GetAutoRotate(void);

// Called by keyer when a character is decoded — thread safe via queue
void UI_PushDecodedChar(char c);

// Forward declarations for tab creators
void UI_Home_Create(lv_obj_t* parent);
void UI_Keyer_Create(lv_obj_t* parent);
void UI_Games_Create(lv_obj_t* parent);
void UI_Settings_Create(lv_obj_t* parent);
void UI_WiFi_Create(lv_obj_t* parent);

// Tab refresh callbacks (called at ~1 Hz)
void UI_Home_Refresh(void);
void UI_Keyer_Refresh(void);
void UI_Games_Refresh(void);
void UI_Settings_Refresh(void);
void UI_WiFi_Refresh(void);

#endif // MT_UI_H
