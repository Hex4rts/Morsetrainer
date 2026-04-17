#ifndef MT_NEOPIXEL_H
#define MT_NEOPIXEL_H

#include <Arduino.h>

// LED effect modes
typedef enum {
  NEO_OFF = 0,
  NEO_KEY_FLASH,       // flash on key down
  NEO_WPM_METER,       // bar graph of current WPM
  NEO_STEADY,          // solid ambient color
  NEO_BREATHE,         // ambient color breathing
  NEO_STARFIELD,       // random pixels flash
  NEO_CHASE,           // color chases around strip
  NEO_RAINBOW,         // continuous rainbow cycle
  NEO_MODE_COUNT
} neo_mode_t;

void NeoPixel_Init(void);

// Mode
void       NeoPixel_SetMode(neo_mode_t m);
neo_mode_t NeoPixel_GetMode(void);
const char* NeoPixel_ModeName(neo_mode_t m);

// Brightness (0-255)
void    NeoPixel_SetBrightness(uint8_t b);    // key flash brightness
uint8_t NeoPixel_GetBrightness(void);
void    NeoPixel_SetBgBrightness(uint8_t b);  // background mode brightness
uint8_t NeoPixel_GetBgBrightness(void);

// Ambient color (RGB packed as 0xRRGGBB)
void     NeoPixel_SetAmbientColor(uint32_t rgb);
uint32_t NeoPixel_GetAmbientColor(void);

// One-shot effects (override current mode briefly)
void NeoPixel_KeyFlash(bool isDah);        // flash on element
void NeoPixel_Correct(void);               // green flash
void NeoPixel_Wrong(void);                 // red flash
void NeoPixel_LevelUp(void);              // rainbow sweep

// Continuous rainbow (Easter egg)
void NeoPixel_RainbowStart(void);
void NeoPixel_RainbowStop(void);

// Update (call from main loop ~30 Hz)
void NeoPixel_Update(void);

// Direct control (for games etc.)
void NeoPixel_SetPixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);
void NeoPixel_SetStrip(uint8_t strip, uint8_t r, uint8_t g, uint8_t b);
void NeoPixel_Clear(void);
void NeoPixel_Show(void);

#endif // MT_NEOPIXEL_H
