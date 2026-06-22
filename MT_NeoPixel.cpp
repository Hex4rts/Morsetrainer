#include "MT_NeoPixel.h"
#include "MT_Pins.h"
#include "MT_Keyer.h"
#include <Adafruit_NeoPixel.h>
#include <math.h>

// ============================================================================
//  NeoPixel strip instance
// ============================================================================
static Adafruit_NeoPixel strip(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

// Runtime LED count (user-configurable, default 20)
static uint8_t neo_count = 20;

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
  printf("NeoPixel: init OK  %d LEDs on GPIO %d\n", neo_count, NEOPIXEL_PIN);
}

// ============================================================================
//  Count, Mode & brightness
// ============================================================================
void NeoPixel_SetMode(neo_mode_t m) {
  if (m < NEO_MODE_COUNT) {
    currentMode = m;
    // Confirmation flash — brief white blink so user sees the change
    for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, 30, 30, 30);
    strip.show();
    delay(100);
    strip.clear();
    strip.show();
  }
  if (m == NEO_OFF) { strip.clear(); strip.show(); }
}
neo_mode_t NeoPixel_GetMode(void) { return currentMode; }

const char* NeoPixel_ModeName(neo_mode_t m) {
  static const char* names[] = {"Off", "Key Flash", "WPM Meter", "Steady", "Breathe", "Starfield", "Chase", "Rainbow",
                                "Comet", "Scanner", "Fire", "Twinkle", "Aurora",
                                "Theater", "Wipe", "Pulse", "Wave", "Plasma", "Police",
                                "Lightning", "Matrix", "Mood", "Lava", "Firefly", "Meteor"};
  return (m < NEO_MODE_COUNT) ? names[m] : "?";
}

void NeoPixel_SetCount(uint8_t n) {
  if (n < 1) n = 1;
  if (n > NEOPIXEL_COUNT) n = NEOPIXEL_COUNT;
  neo_count = n;
  strip.updateLength(n);
  strip.clear();
  strip.show();
}
uint8_t NeoPixel_GetCount(void) { return neo_count; }

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
  if (idx < neo_count) strip.setPixelColor(idx, r, g, b);
}

void NeoPixel_SetStrip(uint8_t s, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t base = s * NEO_STRIP_LEN;
  for (uint8_t i = 0; i < NEO_STRIP_LEN && (base + i) < neo_count; i++) {
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
  for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, r, g, b);
}

static void renderCorrect(void) {
  uint32_t elapsed = millis() - fxStart;
  if (elapsed > 300) { activeFx = FX_NONE; return; }
  uint8_t fade = 255 - (elapsed * 255 / 300);
  for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, 0, fade, 0);
}

static void renderWrong(void) {
  uint32_t elapsed = millis() - fxStart;
  if (elapsed > 400) { activeFx = FX_NONE; return; }
  uint8_t fade = (elapsed < 200) ? 255 : (255 - ((elapsed - 200) * 255 / 200));
  for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, fade, 0, 0);
}

static void renderLevelUp(void) {
  uint32_t elapsed = millis() - fxStart;
  if (elapsed > 800) { activeFx = FX_NONE; return; }
  for (int i = 0; i < neo_count; i++) {
    uint16_t hue = (elapsed * 65536 / 800 + i * 65536 / neo_count) & 0xFFFF;
    strip.setPixelColor(i, strip.ColorHSV(hue, 255, 200));
  }
}

// ── Steady: solid ambient color ──
static void renderSteady(void) {
  uint8_t r = (ambientColor >> 16) & 0xFF;
  uint8_t g = (ambientColor >> 8)  & 0xFF;
  uint8_t b = (ambientColor)       & 0xFF;
  for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, r, g, b);
}

// ── Breathe: ambient color with slow pulse ──
static void renderBreathe(void) {
  uint8_t breath = (millis() / 30) % 255;
  breath = (breath < 128) ? breath : (255 - breath);
  float scale = breath / 128.0f;  // 0.0 to 1.0
  uint8_t r = ((ambientColor >> 16) & 0xFF) * scale;
  uint8_t g = ((ambientColor >> 8)  & 0xFF) * scale;
  uint8_t b = ((ambientColor)       & 0xFF) * scale;
  for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, r, g, b);
}

// ── Starfield: random pixels flash white briefly ──
static void renderStarfield(void) {
  static uint8_t stars[NEOPIXEL_COUNT] = {};
  for (int i = 0; i < neo_count; i++) {
    if (stars[i] > 8) stars[i] -= 8;
    else stars[i] = 0;
  }
  if (random(0, 3) == 0) {
    int idx = random(0, neo_count);
    stars[idx] = 200 + random(0, 56);
  }
  for (int i = 0; i < neo_count; i++) {
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
  pos = (millis() / 60) % neo_count;
  for (int i = 0; i < neo_count; i++) {
    int dist = (i - (int)pos + neo_count) % neo_count;
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
  for (int i = 0; i < neo_count; i++) {
    uint16_t h = hue + (i * 65536 / neo_count);
    uint32_t c = strip.ColorHSV(h, 255, 255);
    strip.setPixelColor(i, strip.gamma32(c));
  }
}

// ── Comet: bright head with a long fading tail circling the strip ──
static void renderComet(void) {
  const int TAIL = 8;
  uint8_t ar = (ambientColor >> 16) & 0xFF;
  uint8_t ag = (ambientColor >> 8)  & 0xFF;
  uint8_t ab = (ambientColor)       & 0xFF;
  int head = (millis() / 45) % neo_count;
  for (int i = 0; i < neo_count; i++) {
    int dist = (head - i + neo_count) % neo_count;  // pixels behind the head
    if (dist < TAIL) {
      float f = 1.0f - (float)dist / TAIL;
      f *= f;  // quadratic falloff for a sharper head
      strip.setPixelColor(i, (uint8_t)(ar * f), (uint8_t)(ag * f), (uint8_t)(ab * f));
    }
  }
}

// ── Scanner: a Larson/Cylon dot sweeping back and forth with a short tail ──
static void renderScanner(void) {
  const int TAIL = 3;
  uint8_t ar = (ambientColor >> 16) & 0xFF;
  uint8_t ag = (ambientColor >> 8)  & 0xFF;
  uint8_t ab = (ambientColor)       & 0xFF;
  uint32_t span = (neo_count > 1) ? (uint32_t)(neo_count - 1) * 2 : 1;
  uint32_t t = (millis() / 55) % span;
  int head = (t < (uint32_t)neo_count) ? (int)t : (int)(span - t);  // bounce 0..n-1..0
  for (int i = 0; i < neo_count; i++) {
    int dist = abs(i - head);
    if (dist <= TAIL) {
      float f = 1.0f - (float)dist / (TAIL + 1);
      strip.setPixelColor(i, (uint8_t)(ar * f), (uint8_t)(ag * f), (uint8_t)(ab * f));
    }
  }
}

// ── Fire: per-pixel heat that cools and randomly sparks (warm palette) ──
static void renderFire(void) {
  static uint8_t heat[NEOPIXEL_COUNT] = {};
  for (int i = 0; i < neo_count; i++) {
    int cool = random(0, 26);
    heat[i] = (heat[i] > cool) ? heat[i] - cool : 0;
    if (random(0, 4) == 0) {
      int h = heat[i] + random(60, 160);
      heat[i] = (h > 255) ? 255 : h;
    }
    uint8_t h = heat[i];
    // Embers stay deep red; hottest spots warm to orange — green is kept to a
    // small fraction of red so it never drifts into yellow.
    uint8_t r = h;
    uint8_t g = (uint8_t)(((uint16_t)h * 65) / 255);  // peaks ~65 = orange
    strip.setPixelColor(i, r, g, 0);
  }
}

// ── Twinkle: random pixels spark in random hues, then fade out ──
static void renderTwinkle(void) {
  static uint8_t  bri[NEOPIXEL_COUNT] = {};
  static uint16_t hue[NEOPIXEL_COUNT] = {};
  for (int i = 0; i < neo_count; i++)
    bri[i] = (bri[i] > 6) ? bri[i] - 6 : 0;
  if (random(0, 2) == 0) {
    int idx = random(0, neo_count);
    bri[idx] = 200 + random(0, 56);
    hue[idx] = random(0, 65536);
  }
  for (int i = 0; i < neo_count; i++) {
    if (bri[i] > 0)
      strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue[i], 200, bri[i])));
  }
}

// ── Aurora: slow, low-motion green/blue/purple gradient that drifts ──
static void renderAurora(void) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < neo_count; i++) {
    // Hue oscillates within the green→cyan→blue band (~26k–46k of 65536).
    float wave = sinf(i * 0.55f + t * 0.6f);
    uint16_t h = (uint16_t)(36000 + wave * 9000);
    uint8_t  v = (uint8_t)(120 + sinf(i * 0.35f - t * 0.4f) * 60);
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(h, 255, v)));
  }
}

// ── Theater: classic marquee — every 3rd pixel lit, pattern marches along ──
static void renderTheater(void) {
  uint8_t ar = (ambientColor >> 16) & 0xFF;
  uint8_t ag = (ambientColor >> 8)  & 0xFF;
  uint8_t ab = (ambientColor)       & 0xFF;
  int step = (millis() / 120) % 3;
  for (int i = 0; i < neo_count; i++)
    if (i % 3 == step) strip.setPixelColor(i, ar, ag, ab);
}

// ── Wipe: fill the strip one pixel at a time, then unfill ──
static void renderWipe(void) {
  uint8_t ar = (ambientColor >> 16) & 0xFF;
  uint8_t ag = (ambientColor >> 8)  & 0xFF;
  uint8_t ab = (ambientColor)       & 0xFF;
  uint32_t span = (neo_count > 0) ? (uint32_t)neo_count * 2 : 1;
  uint32_t t = (millis() / 55) % span;
  int lit = (t < (uint32_t)neo_count) ? (int)t : (int)(span - t);  // grow then shrink
  for (int i = 0; i < lit && i < neo_count; i++)
    strip.setPixelColor(i, ar, ag, ab);
}

// ── Pulse: heartbeat — two quick thumps then a rest ──
static void renderPulse(void) {
  uint32_t t = millis() % 1300;
  float b = 0.0f;
  if      (t < 120) b = t / 120.0f;
  else if (t < 240) b = 1.0f - (t - 120) / 120.0f;
  else if (t < 360) b = (t - 240) / 120.0f * 0.7f;
  else if (t < 480) b = 0.7f - (t - 360) / 120.0f * 0.7f;
  // 480–1300ms: dark rest
  uint8_t r = (uint8_t)(((ambientColor >> 16) & 0xFF) * b);
  uint8_t g = (uint8_t)(((ambientColor >> 8)  & 0xFF) * b);
  uint8_t bl = (uint8_t)(((ambientColor)      & 0xFF) * b);
  for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, r, g, bl);
}

// ── Wave: a sine brightness wave in ambient color travels along the strip ──
static void renderWave(void) {
  float t = millis() / 180.0f;
  uint8_t ar = (ambientColor >> 16) & 0xFF;
  uint8_t ag = (ambientColor >> 8)  & 0xFF;
  uint8_t ab = (ambientColor)       & 0xFF;
  for (int i = 0; i < neo_count; i++) {
    float s = (sinf(i * 0.6f - t) + 1.0f) * 0.5f;  // 0..1
    strip.setPixelColor(i, (uint8_t)(ar * s), (uint8_t)(ag * s), (uint8_t)(ab * s));
  }
}

// ── Plasma: overlapping sines drive a slowly shifting full-spectrum hue ──
static void renderPlasma(void) {
  float t = millis() / 1000.0f;
  for (int i = 0; i < neo_count; i++) {
    float v = sinf(i * 0.30f + t) + sinf(i * 0.17f - t * 0.7f);  // -2..2
    uint16_t hue = (uint16_t)((v + 2.0f) / 4.0f * 65535.0f);
    strip.setPixelColor(i, strip.gamma32(strip.ColorHSV(hue, 255, 200)));
  }
}

// ── Police: left half flashes red, right half flashes blue, alternating ──
static void renderPolice(void) {
  uint32_t ph = (millis() / 180) % 4;
  int half = neo_count / 2;
  for (int i = 0; i < neo_count; i++) {
    bool left = (i < half);
    if (ph == 0 && left)  strip.setPixelColor(i, 255, 0, 0);
    if (ph == 2 && !left) strip.setPixelColor(i, 0, 0, 255);
    // ph 1 and 3 = dark gap
  }
}

// ── Lightning: a dark storm broken by random blue-white strikes + afterglow ──
static void renderLightning(void) {
  static uint8_t  flash      = 0;
  static uint32_t nextStrike = 0;
  uint32_t now = millis();
  if (flash > 30) flash -= 30; else flash = 0;   // fast decay
  if (now >= nextStrike) {
    flash = 255;
    nextStrike = now + random(350, 2600);         // irregular storm timing
  } else if (flash > 40 && random(0, 3) == 0) {
    flash = 255;                                   // crackle / double strike
  }
  uint8_t b = flash;
  uint8_t r = (uint8_t)(((uint16_t)b * 200) / 255);  // cool blue-white
  uint8_t g = (uint8_t)(((uint16_t)b * 220) / 255);
  for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, r, g, b);
}

// ── Matrix: green "digital rain" cascades along the strip with fading trails ──
static void renderMatrix(void) {
  static uint8_t  rain[NEOPIXEL_COUNT] = {};
  static uint32_t lastShift = 0;
  uint32_t now = millis();
  if (now - lastShift > 55) {
    for (int i = neo_count - 1; i > 0; i--) rain[i] = rain[i - 1];  // fall
    rain[0] = (random(0, 4) == 0) ? 255 : 0;                       // new drop head
    lastShift = now;
  }
  for (int i = 0; i < neo_count; i++) {
    uint8_t g = rain[i];
    // White-hot head, green trail
    strip.setPixelColor(i, g > 240 ? 180 : g / 6, g, g > 240 ? 180 : g / 6);
    if (rain[i] > 12) rain[i] -= 12;   // fade the trail
  }
}

// ── Mood: the whole strip holds one colour that drifts slowly through the wheel ──
static void renderMood(void) {
  uint16_t hue = (uint16_t)((millis() / 15) & 0xFFFF);
  uint32_t c = strip.gamma32(strip.ColorHSV(hue, 255, 255));
  for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, c);
}

// ── Lava: overlapping slow sines make warm blobs rise and merge (lava lamp) ──
static void renderLava(void) {
  float t = millis() / 1200.0f;
  for (int i = 0; i < neo_count; i++) {
    float v = sinf(i * 0.25f + t) + sinf(i * 0.11f - t * 0.6f) + sinf(i * 0.07f + t * 0.3f);
    float n = (v + 3.0f) / 6.0f;          // 0..1
    uint8_t r = (uint8_t)(n * 255);
    uint8_t g = (uint8_t)(n * n * 70);    // warm orange peaks only — no yellow
    strip.setPixelColor(i, r, g, 0);
  }
}

// ── Firefly: sparse soft warm-green glows that fade in, hold, and fade out ──
static void renderFirefly(void) {
  static uint8_t bri[NEOPIXEL_COUNT] = {};
  static int8_t  dir[NEOPIXEL_COUNT] = {};
  for (int i = 0; i < neo_count; i++) {
    if (bri[i] == 0 && dir[i] == 0) {
      if (random(0, 50) == 0) dir[i] = 1;        // occasionally wake a firefly
    } else {
      int v = bri[i] + dir[i] * 5;
      if (v >= 200) { v = 200; dir[i] = -1; }
      if (v <= 0)   { v = 0;   dir[i] = 0; }
      bri[i] = v;
    }
    uint8_t b = bri[i];
    strip.setPixelColor(i, (uint8_t)(((uint16_t)b * 180) / 255), b, 0);  // warm yellow-green
  }
}

// ── Meteor: a bright head sweeps the strip leaving a randomly-decaying trail ──
static void renderMeteor(void) {
  static uint8_t trail[NEOPIXEL_COUNT] = {};
  uint8_t ar = (ambientColor >> 16) & 0xFF;
  uint8_t ag = (ambientColor >> 8)  & 0xFF;
  uint8_t ab = (ambientColor)       & 0xFF;
  int span = neo_count + 12;
  int head = (int)((millis() / 38) % span) - 6;   // sweeps in from off-screen
  for (int i = 0; i < neo_count; i++)
    if (random(0, 10) > 4 && trail[i] > 24) trail[i] -= 24;  // uneven sparkle decay
  for (int i = head; i < head + 2; i++)
    if (i >= 0 && i < neo_count) trail[i] = 255;
  for (int i = 0; i < neo_count; i++) {
    float f = trail[i] / 255.0f;
    strip.setPixelColor(i, (uint8_t)(ar * f), (uint8_t)(ag * f), (uint8_t)(ab * f));
  }
}

static void renderWPMMeter(void) {
  uint8_t w = Keyer_GetWPM();
  // Map 5-40 WPM to 0-20 LEDs
  uint8_t lit = map(w, 5, 60, 1, neo_count);
  for (int i = 0; i < neo_count; i++) {
    if (i < lit) {
      // Green→Yellow→Red gradient
      uint8_t r = (i > neo_count / 2) ? map(i, neo_count / 2, neo_count, 0, 255) : 0;
      uint8_t g = (i < neo_count * 3 / 4) ? 180 : 60;
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
    for (int i = 0; i < neo_count; i++) {
      uint16_t h = rHue + (i * 65536 / neo_count);
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
    for (int i = 0; i < neo_count; i++) strip.setPixelColor(i, 0, 255, 0);
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
    case NEO_COMET:      renderComet();       break;
    case NEO_SCANNER:    renderScanner();     break;
    case NEO_FIRE:       renderFire();        break;
    case NEO_TWINKLE:    renderTwinkle();     break;
    case NEO_AURORA:     renderAurora();      break;
    case NEO_THEATER:    renderTheater();     break;
    case NEO_WIPE:       renderWipe();        break;
    case NEO_PULSE:      renderPulse();       break;
    case NEO_WAVE:       renderWave();        break;
    case NEO_PLASMA:     renderPlasma();      break;
    case NEO_POLICE:     renderPolice();      break;
    case NEO_LIGHTNING:  renderLightning();   break;
    case NEO_MATRIX:     renderMatrix();      break;
    case NEO_MOOD:       renderMood();        break;
    case NEO_LAVA:       renderLava();        break;
    case NEO_FIREFLY:    renderFirefly();     break;
    case NEO_METEOR:     renderMeteor();      break;
    default: break;
  }
  strip.show();
}
