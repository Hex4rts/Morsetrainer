#ifndef MT_KEYER_H
#define MT_KEYER_H

#include <Arduino.h>

// Keyer operating modes
typedef enum {
  KEYER_IAMBIC_A = 0,
  KEYER_IAMBIC_B,
  KEYER_STRAIGHT,
  KEYER_MODE_COUNT
} keyer_mode_t;

// Callback fired when a full character has been decoded
// (called from timer context — keep it short, use a queue for UI work)
typedef void (*keyer_char_cb_t)(char decoded);

// Callback fired when an element (dit or dah) starts/stops
// state: true = key down, false = key up
typedef void (*keyer_element_cb_t)(bool state, bool isDah);

// Initialise the keyer (call once in setup, after Sidetone_Init)
void Keyer_Init(void);

// Mode
void           Keyer_SetMode(keyer_mode_t mode);
keyer_mode_t   Keyer_GetMode(void);
const char*    Keyer_ModeName(keyer_mode_t mode);

// WPM (5–40)
void     Keyer_SetWPM(uint8_t wpm);
uint8_t  Keyer_GetWPM(void);

// Paddle swap
void Keyer_SetSwap(bool swap);
bool Keyer_GetSwap(void);

// Straight-key timing mode
//   true  = adaptive (learns operator dit length, the original behavior)
//   false = WPM-based (threshold derived from the Keyer tab WPM setting)
void Keyer_SetSKAutoTiming(bool on);
bool Keyer_GetSKAutoTiming(void);

// Input gate — when true the keyer ignores paddles and the straight key.
// No sidetone, no LED flash, no decoded characters. Used by games to suppress
// user input while the device is playing back morse for the user to copy.
void Keyer_SetInputBlocked(bool blocked);
bool Keyer_GetInputBlocked(void);

// Drop any in-progress pattern / straight-key element without entering the
// blocked state. Use after judging a wrong answer so the next character the
// keyer emits is unambiguously the start of the retry, not the tail of the
// previous attempt.
void Keyer_FlushInput(void);

// Register callbacks
void Keyer_OnChar(keyer_char_cb_t cb);
void Keyer_OnElement(keyer_element_cb_t cb);

// Query live state
bool Keyer_IsSending(void);

// Get the in-progress morse pattern (e.g. ".-" while building a character)
const char* Keyer_GetPattern(void);

// Straight key timing (for visual bar drawing)
// Straight key timing — ISR-driven, never misses events
uint32_t Keyer_GetDownTime(void);
uint32_t Keyer_GetDitMs(void);      // WPM-based dit (for playback only)
uint32_t Keyer_GetSKDitAvg(void);   // adaptive dit average

// Straight key event log — call from UI to read completed elements
bool     Keyer_SKIsDown(void);                          // true while key physically held
uint16_t Keyer_SKHeldMs(void);                          // current hold duration (updates every 1ms)
bool     Keyer_SKPopElement(uint16_t* duration_ms, bool forGame);  // read next completed element
void     Keyer_SKFlush(bool forGame);                   // discard pending elements

// Timing multipliers (set by Settings, used by ISR)
extern float keyer_charGapMult;
extern float keyer_wordGapMult;
extern float keyer_ditDahMult;

// Call periodically if needed (straight key debounce helper)
// Usually not needed — the keyer runs on a 1 ms esp_timer internally.
void Keyer_Tick(void);

#endif // MT_KEYER_H
