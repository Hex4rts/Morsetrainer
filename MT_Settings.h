#ifndef MT_SETTINGS_H
#define MT_SETTINGS_H

#include <Arduino.h>
#include "MT_Keyer.h"
#include "MT_NeoPixel.h"

// Central settings struct
typedef struct {
  uint8_t       wpm;              // 5–40
  uint8_t       volume;           // 0–100
  uint16_t      sidetoneFreq;     // 200–1200 Hz
  keyer_mode_t  keyerMode;
  bool          paddleSwap;
  uint8_t       backlight;        // 0–100
  neo_mode_t    ledMode;
  uint8_t       ledBrightness;    // 0–255 (key flash brightness)
  uint8_t       ledBgBrightness;  // 0–255 (background mode brightness)
  uint8_t       ledCount;         // number of NeoPixels (1–60)
  uint32_t      ambientColor;     // RGB color for ambient LED mode
  uint8_t       kochLesson;
  char          callsign[12];     // max 11 chars + null
  bool          screenFlip;       // 180° screen rotation
  // Keyer timing multipliers (hidden debug menu)
  float         charGapMult;      // character gap = dit × this (default 3.0)
  float         wordGapMult;      // word gap = dit × this (default 7.0)
  float         ditDahMult;       // dit/dah threshold = dit × this (default 2.0)
} mt_settings_t;

// Initialise (loads from NVS, falls back to SD, then defaults)
void Settings_Init(void);

// Get a read-only reference to current settings
const mt_settings_t* Settings_Get(void);

// Apply a full settings struct (writes to NVS)
void Settings_Apply(const mt_settings_t* s);

// Individual setters (each writes to NVS immediately)
void Settings_SetWPM(uint8_t wpm);
void Settings_SetVolume(uint8_t vol);
void Settings_SetSidetoneFreq(uint16_t hz);
void Settings_SetKeyerMode(keyer_mode_t m);
void Settings_SetPaddleSwap(bool swap);
void Settings_SetBacklight(uint8_t bl);
void Settings_SetLEDMode(neo_mode_t m);
void Settings_SetLEDBrightness(uint8_t b);
void Settings_SetLEDBgBrightness(uint8_t b);
void Settings_SetLEDCount(uint8_t n);
void Settings_SetAmbientColor(uint32_t rgb);

// Call periodically (~100ms) to debounce NVS writes
void Settings_FlushIfDirty(void);
void Settings_SetKochLesson(uint8_t lesson);
void Settings_SetCallsign(const char* call);
void Settings_SetScreenFlip(bool flip);
void Settings_SetTimingMults(float charGap, float wordGap, float ditDah);

// Replace %CALL% in a string with the stored callsign
void Settings_SubstituteCallsign(char* buf, size_t bufLen, const char* tmpl);

// SD backup / restore
bool Settings_BackupToSD(void);
bool Settings_RestoreFromSD(void);

// Factory reset (clears NVS, resets to defaults)
void Settings_FactoryReset(void);

#endif // MT_SETTINGS_H
