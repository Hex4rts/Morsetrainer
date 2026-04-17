#include "MT_NeoPixel.h"
#include "MT_Pins.h"
#include "MT_Keyer.h"
#include <Adafruit_NeoPixel.h>

// ============================================================================
//  NeoPixel strip instance
// ============================================================================
static Adafruit_NeoPixel strip(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

static neo_mode_t  currentMode   = NEO_KEY_FLASH;
static uint8_t     brightness    = 40;   // key flash
static uint8_t     bgBrightness  = 30;   // background modes

// One-shot effect state
typedef enum { FX_NONE, FX_KEY, FX_CORRECT, FX_WRONG, FX_LEVELUP } fx_t;
static fx_t        activeFx      = FX_NONE;
static uint32_t    fxStart       = 0;
static bool        fxIsDah       = false;

// Ambient animation state
static uint16_t    ambientHue    = 0;
static uint32_t    ambientColor  = 0xFF6600;  // default warm amber

// Continuous rainbow (Easter egg)
static bool        rainbowActive = false;

// ============================================================================
//  Init
// ============================================================================
void NeoPixel_Init(void) {
  strip.begin();
  strip.setBrightness(brightness);
  strip.clear();
  strip.show();
  printf("NeoPixel: init OK  %d LEDs on GPIO %d\n", NEOPIXEL_COUNT, NEOPIXEL_PIN);
}

// ============================================================================
//  Mode & brightness
// ============================================================================
void NeoPixel_SetMode(neo_mode_t m) {
  if (m < NEO_MODE_COUNT) {
    currentMode = m;
    // Confirmation flash — brief white blink so user sees the change
    for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, 30, 30, 30);
    strip.show();
    delay(100);
    strip.clear();
    strip.show();
  }
  if (m == NEO_OFF) { strip.clear(); strip.show(); }
}
neo_mode_t NeoPixel_GetMode(void) { return currentMode; }

const char* NeoPixel_ModeName(neo_mode_t m) {
  static const char* names[] = {"Off", "Key Flash", "WPM Meter", "Steady", "Breathe", "Starfield", "Chase", "Rainbow"};
  return (m < NEO_MODE_COUNT) ? names[m] : "?";
}

void NeoPixel_SetBrightness(uint8_t b) {
  brightness = b;
  strip.setBrightness(b);
}
uint8_t NeoPixel_GetBrightness(void) { return brightness; }

void NeoPixel_SetBgBrightness(uint8_t b) { bgBrightness = b; }
uint8_t NeoPixel_GetBgBrightness(void) { return bgBrightness; }

void NeoPixel_SetAmbientColor(uint32_t rgb) { ambientColor = rgb; }
uint32_t NeoPixel_GetAmbientColor(void) { return ambientColor; }

// ============================================================================
//  One-shot triggers
// ============================================================================
void NeoPixel_KeyFlash(bool isDah) {
  if (currentMode == NEO_OFF) return;  // OFF = truly off
  activeFx = FX_KEY;
  fxStart = millis();
  fxIsDah = isDah;
}

void NeoPixel_Correct(void) {
  if (currentMode == NEO_OFF) return;
  activeFx = FX_CORRECT;
  fxStart = millis();
}

void NeoPixel_Wrong(void) {
  if (currentMode == NEO_OFF) return;
  activeFx = FX_WRONG;
  fxStart = millis();
}

void NeoPixel_LevelUp(void) {
  if (currentMode == NEO_OFF) return;
  activeFx = FX_LEVELUP;
  fxStart = millis();
}

// ============================================================================
//  Direct pixel control
// ============================================================================
void NeoPixel_SetPixel(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {
  if (idx < NEOPIXEL_COUNT) strip.setPixelColor(idx, r, g, b);
}

void NeoPixel_SetStrip(uint8_t s, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t base = s * NEO_STRIP_LEN;
  for (uint8_t i = 0; i < NEO_STRIP_LEN && (base + i) < NEOPIXEL_COUNT; i++) {
    strip.setPixelColor(base + i, r, g, b);
  }
}

void NeoPixel_Clear(void) { strip.clear(); }
void NeoPixel_Show(void)  { strip.show(); }

// ============================================================================
//  Internal animation helpers
// ============================================================================
static void renderKeyFlash(void) {
  uint32_t elapsed = millis() - fxStart;
  uint16_t dur = fxIsDah ? 180 : 80;
  if (elapsed > dur) { activeFx = FX_NONE; return; }
  uint8_t fade = 255 - (elapsed * 255 / dur);

  uint8_t r, g, b;
  if (Keyer_GetMode() == KEYER_STRAIGHT) {
    // Straight key: always green
    r = 0; g = fade; b = 0;
  } else {
    // Iambic: dit = blue, dah = amber
    r = fxIsDah ? fade : 0;
    g = fxIsDah ? fade / 3 : 0;
    b = fxIsDah ? 0 : fade;
  }
  for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, r, g, b);
}

static void renderCorrect(void) {
  uint32_t elapsed = millis() - fxStart;
  if (elapsed > 300) { activeFx = FX_NONE; return; }
  uint8_t fade = 255 - (elapsed * 255 / 300);
  for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, 0, fade, 0);
}

static void renderWrong(void) {
  uint32_t elapsed = millis() - fxStart;
  if (elapsed > 400) { activeFx = FX_NONE; return; }
  uint8_t fade = (elapsed < 200) ? 255 : (255 - ((elapsed - 200) * 255 / 200));
  for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, fade, 0, 0);
}

static void renderLevelUp(void) {
  uint32_t elapsed = millis() - fxStart;
  if (elapsed > 800) { activeFx = FX_NONE; return; }
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    uint16_t hue = (elapsed * 65536 / 800 + i * 65536 / NEOPIXEL_COUNT) & 0xFFFF;
    strip.setPixelColor(i, strip.ColorHSV(hue, 255, 200));
  }
}

// ── Steady: solid ambient color ──
static void renderSteady(void) {
  uint8_t r = (ambientColor >> 16) & 0xFF;
  uint8_t g = (ambientColor >> 8)  & 0xFF;
  uint8_t b = (ambientColor)       & 0xFF;
  for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, r, g, b);
}

// ── Breathe: ambient color with slow pulse ──
static void renderBreathe(void) {
  uint8_t breath = (millis() / 30) % 255;
  breath = (breath < 128) ? breath : (255 - breath);
  float scale = breath / 128.0f;  // 0.0 to 1.0
  uint8_t r = ((ambientColor >> 16) & 0xFF) * scale;
  uint8_t g = ((ambientColor >> 8)  & 0xFF) * scale;
  uint8_t b = ((ambientColor)       & 0xFF) * scale;
  for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, r, g, b);
}

// ── Starfield: random pixels flash white briefly ──
static void renderStarfield(void) {
  static uint8_t stars[NEOPIXEL_COUNT] = {};
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    if (stars[i] > 8) stars[i] -= 8;
    else stars[i] = 0;
  }
  if (random(0, 3) == 0) {
    int idx = random(0, NEOPIXEL_COUNT);
    stars[idx] = 200 + random(0, 56);
  }
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    float s = stars[i] / 255.0f;
    uint8_t r = ((ambientColor >> 16) & 0xFF) * s;
    uint8_t g = ((ambientColor >> 8)  & 0xFF) * s;
    uint8_t b = ((ambientColor)       & 0xFF) * s;
    strip.setPixelColor(i, r, g, b);
  }
}

// ── Chase: color runs around the strip ──
static void renderChase(void) {
  static uint16_t pos = 0;
  pos = (millis() / 60) % NEOPIXEL_COUNT;
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    int dist = (i - (int)pos + NEOPIXEL_COUNT) % NEOPIXEL_COUNT;
    if (dist < 4) {
      float fade = 1.0f - (dist / 4.0f);
      uint8_t r = ((ambientColor >> 16) & 0xFF) * fade;
      uint8_t g = ((ambientColor >> 8)  & 0xFF) * fade;
      uint8_t b = ((ambientColor)       & 0xFF) * fade;
      strip.setPixelColor(i, r, g, b);
    }
  }
}

// ── Rainbow: continuous hue cycle across strip ──
static void renderRainbowMode(void) {
  static uint16_t hue = 0;
  hue += 256;
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    uint16_t h = hue + (i * 65536 / NEOPIXEL_COUNT);
    uint32_t c = strip.ColorHSV(h, 255, 255);
    strip.setPixelColor(i, strip.gamma32(c));
  }
}

static void renderWPMMeter(void) {
  uint8_t w = Keyer_GetWPM();
  // Map 5-40 WPM to 0-20 LEDs
  uint8_t lit = map(w, 5, 60, 1, NEOPIXEL_COUNT);
  for (int i = 0; i < NEOPIXEL_COUNT; i++) {
    if (i < lit) {
      // Green→Yellow→Red gradient
      uint8_t r = (i > NEOPIXEL_COUNT / 2) ? map(i, NEOPIXEL_COUNT / 2, NEOPIXEL_COUNT, 0, 255) : 0;
      uint8_t g = (i < NEOPIXEL_COUNT * 3 / 4) ? 180 : 60;
      strip.setPixelColor(i, r, g, 0);
    } else {
      strip.setPixelColor(i, 0, 0, 0);
    }
  }
}

// ============================================================================
//  Update (call ~30 Hz from main loop)
// ============================================================================
void NeoPixel_RainbowStart(void) { rainbowActive = true; }
void NeoPixel_RainbowStop(void)  { rainbowActive = false; strip.clear(); strip.show(); }

void NeoPixel_Update(void) {
  // Rainbow overrides everything
  if (rainbowActive) {
    static uint16_t rHue = 0;
    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
      uint16_t h = rHue + (i * 65536 / NEOPIXEL_COUNT);
      uint32_t c = strip.ColorHSV(h, 255, 200);
      strip.setPixelColor(i, strip.gamma32(c));
    }
    rHue += 512;
    strip.show();
    return;
  }

  // OFF = truly off, always
  if (currentMode == NEO_OFF) { strip.clear(); strip.show(); return; }

  strip.clear();

  // Straight key held = solid green
  if (Keyer_GetMode() == KEYER_STRAIGHT && Keyer_SKIsDown()) {
    strip.setBrightness(brightness);  // key brightness
    for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, 0, 255, 0);
    strip.show();
    return;
  }

  // One-shot effects take priority (key flash brightness)
  if (activeFx != FX_NONE) {
    strip.setBrightness(brightness);
    switch (activeFx) {
      case FX_KEY:      renderKeyFlash(); break;
      case FX_CORRECT:  renderCorrect();  break;
      case FX_WRONG:    renderWrong();    break;
      case FX_LEVELUP:  renderLevelUp();  break;
      default: break;
    }
    strip.show();
    return;
  }

  // Background mode (bg brightness)
  strip.setBrightness(bgBrightness);
  switch (currentMode) {
    case NEO_KEY_FLASH: {
      // No background — dark until key flash triggers
      break;
    }
    case NEO_WPM_METER:  renderWPMMeter();    break;
    case NEO_STEADY:     renderSteady();      break;
    case NEO_BREATHE:    renderBreathe();     break;
    case NEO_STARFIELD:  renderStarfield();   break;
    case NEO_CHASE:      renderChase();       break;
    case NEO_RAINBOW:    renderRainbowMode(); break;
    default: break;
  }
  strip.show();
}
