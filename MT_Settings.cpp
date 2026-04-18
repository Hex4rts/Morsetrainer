#include "MT_Settings.h"
#include "MT_Keyer.h"
#include "MT_Sidetone.h"
#include "MT_Koch.h"
#include "MT_NeoPixel.h"
#include "Display_ST7789.h"
#include <Preferences.h>
#include <SD_MMC.h>

// ============================================================================
//  Defaults
// ============================================================================
static const mt_settings_t defaults = {
  .wpm          = 20,
  .volume       = 80,
  .sidetoneFreq = 700,
  .keyerMode    = KEYER_IAMBIC_B,
  .paddleSwap   = false,
  .backlight    = 50,
  .ledMode      = NEO_KEY_FLASH,
  .ledBrightness= 40,
  .ledBgBrightness= 30,
  .ledCount     = 20,
  .ambientColor = 0xFF6600,
  .kochLesson   = 1,
  .callsign     = "N0CALL",
  .screenFlip   = false,
  .charGapMult  = 3.0f,
  .wordGapMult  = 7.0f,
  .ditDahMult   = 2.0f,
};

static mt_settings_t cfg;
static Preferences   prefs;
#define NVS_NS "mtcfg"

// ============================================================================
//  Internal helpers
// ============================================================================
static void pushToHardware(void) {
  Keyer_SetWPM(cfg.wpm);
  Keyer_SetMode(cfg.keyerMode);
  Keyer_SetSwap(cfg.paddleSwap);
  Sidetone_SetFreq(cfg.sidetoneFreq);
  Sidetone_SetVolume(cfg.volume);
  NeoPixel_SetMode(cfg.ledMode);
  NeoPixel_SetBrightness(cfg.ledBrightness);
  NeoPixel_SetBgBrightness(cfg.ledBgBrightness);
  NeoPixel_SetCount(cfg.ledCount);
  NeoPixel_SetAmbientColor(cfg.ambientColor);
  Koch_SetLesson(cfg.kochLesson);
  LCD_SetFlip(cfg.screenFlip);
  keyer_charGapMult = cfg.charGapMult;
  keyer_wordGapMult = cfg.wordGapMult;
  keyer_ditDahMult  = cfg.ditDahMult;
}

// NVS: config tab settings only
static void saveToNVS(void) {
  prefs.begin(NVS_NS, false);
  prefs.putUChar("wpm",       cfg.wpm);
  prefs.putUChar("vol",       cfg.volume);
  prefs.putUShort("freq",     cfg.sidetoneFreq);
  prefs.putUChar("kmode",     (uint8_t)cfg.keyerMode);
  prefs.putBool("pswap",      cfg.paddleSwap);
  prefs.putUChar("bl",        cfg.backlight);
  prefs.putUChar("ledm",      (uint8_t)cfg.ledMode);
  prefs.putUChar("ledb",      cfg.ledBrightness);
  prefs.putUChar("ledbg",     cfg.ledBgBrightness);
  prefs.putUChar("ledn",      cfg.ledCount);
  prefs.putULong("ambc",      cfg.ambientColor);
  prefs.putString("call",     cfg.callsign);
  prefs.putBool("sflip",      cfg.screenFlip);
  prefs.putFloat("cgm",       cfg.charGapMult);
  prefs.putFloat("wgm",       cfg.wordGapMult);
  prefs.putFloat("ddm",       cfg.ditDahMult);
  prefs.end();
}

static bool loadFromNVS(void) {
  prefs.begin(NVS_NS, true);
  if (!prefs.isKey("wpm")) { prefs.end(); return false; }
  cfg.wpm           = prefs.getUChar("wpm",    defaults.wpm);
  cfg.volume        = prefs.getUChar("vol",    defaults.volume);
  cfg.sidetoneFreq  = prefs.getUShort("freq",  defaults.sidetoneFreq);
  cfg.keyerMode     = (keyer_mode_t)prefs.getUChar("kmode", defaults.keyerMode);
  cfg.paddleSwap    = prefs.getBool("pswap",   defaults.paddleSwap);
  cfg.backlight     = prefs.getUChar("bl",     defaults.backlight);
  cfg.ledMode       = (neo_mode_t)prefs.getUChar("ledm", defaults.ledMode);
  cfg.ledBrightness = prefs.getUChar("ledb",   defaults.ledBrightness);
  cfg.ledBgBrightness = prefs.getUChar("ledbg", defaults.ledBgBrightness);
  cfg.ledCount      = prefs.getUChar("ledn",  defaults.ledCount);
  cfg.ambientColor  = prefs.getULong("ambc",   defaults.ambientColor);
  String cs = prefs.getString("call", defaults.callsign);
  strncpy(cfg.callsign, cs.c_str(), sizeof(cfg.callsign) - 1);
  cfg.callsign[sizeof(cfg.callsign) - 1] = '\0';
  cfg.screenFlip    = prefs.getBool("sflip",   defaults.screenFlip);
  cfg.charGapMult   = prefs.getFloat("cgm",    defaults.charGapMult);
  cfg.wordGapMult   = prefs.getFloat("wgm",    defaults.wordGapMult);
  cfg.ditDahMult    = prefs.getFloat("ddm",    defaults.ditDahMult);
  prefs.end();
  return true;
}

// Debounced NVS write
static bool      nvsDirty    = false;
static uint32_t  nvsLastMark = 0;
#define NVS_DEBOUNCE_MS  3000

static void markDirty(void) {
  nvsDirty = true;
  nvsLastMark = millis();
}

void Settings_FlushIfDirty(void) {
  if (nvsDirty && (millis() - nvsLastMark >= NVS_DEBOUNCE_MS)) {
    saveToNVS();
    nvsDirty = false;
  }
}

// ============================================================================
//  Public API
// ============================================================================
void Settings_Init(void) {
  cfg = defaults;
  if (loadFromNVS()) {
    printf("Settings: loaded from NVS\n");
  } else {
    printf("Settings: using defaults\n");
    saveToNVS();
  }
  pushToHardware();
}

const mt_settings_t* Settings_Get(void) { return &cfg; }

void Settings_Apply(const mt_settings_t* s) {
  cfg = *s;
  saveToNVS();
  pushToHardware();
}

// --- Individual setters ---
void Settings_SetWPM(uint8_t v) {
  cfg.wpm = constrain(v, 5, 60);
  Keyer_SetWPM(cfg.wpm);
  markDirty();
}

void Settings_SetVolume(uint8_t v) {
  cfg.volume = constrain(v, 0, 100);
  Sidetone_SetVolume(cfg.volume);
  markDirty();
}

void Settings_SetSidetoneFreq(uint16_t hz) {
  cfg.sidetoneFreq = constrain(hz, 200, 1200);
  Sidetone_SetFreq(cfg.sidetoneFreq);
  markDirty();
}

void Settings_SetKeyerMode(keyer_mode_t m) {
  cfg.keyerMode = m;
  Keyer_SetMode(m);
  markDirty();
}

void Settings_SetPaddleSwap(bool s) {
  cfg.paddleSwap = s;
  Keyer_SetSwap(s);
  markDirty();
}

void Settings_SetBacklight(uint8_t bl) {
  cfg.backlight = constrain(bl, 0, 100);
  markDirty();
}

void Settings_SetLEDMode(neo_mode_t m) {
  cfg.ledMode = m;
  NeoPixel_SetMode(m);
  markDirty();
}

void Settings_SetLEDBrightness(uint8_t b) {
  cfg.ledBrightness = b;
  NeoPixel_SetBrightness(b);
  markDirty();
}

void Settings_SetLEDBgBrightness(uint8_t b) {
  cfg.ledBgBrightness = b;
  NeoPixel_SetBgBrightness(b);
  markDirty();
}

void Settings_SetLEDCount(uint8_t n) {
  if (n < 1) n = 1;
  if (n > 60) n = 60;
  cfg.ledCount = n;
  NeoPixel_SetCount(n);
  markDirty();
}

void Settings_SetAmbientColor(uint32_t rgb) {
  cfg.ambientColor = rgb;
  NeoPixel_SetAmbientColor(rgb);
  markDirty();
}

void Settings_SetKochLesson(uint8_t l) {
  cfg.kochLesson = l;
  Koch_SetLesson(l);
  Koch_Save();
}

void Settings_SetCallsign(const char* call) {
  strncpy(cfg.callsign, call, sizeof(cfg.callsign) - 1);
  cfg.callsign[sizeof(cfg.callsign) - 1] = '\0';
  for (char* p = cfg.callsign; *p; p++) *p = toupper(*p);
  markDirty();
}

void Settings_SetScreenFlip(bool flip) {
  cfg.screenFlip = flip;
  LCD_SetFlip(flip);
  markDirty();
}

void Settings_SetTimingMults(float cg, float wg, float dd) {
  cfg.charGapMult = constrain(cg, 1.0f, 10.0f);
  cfg.wordGapMult = constrain(wg, 2.0f, 20.0f);
  cfg.ditDahMult  = constrain(dd, 1.0f, 5.0f);
  markDirty();
}

// ============================================================================
//  Callsign substitution
// ============================================================================
void Settings_SubstituteCallsign(char* buf, size_t bufLen, const char* tmpl) {
  const char* tag = "%CALL%";
  const char* pos = strstr(tmpl, tag);
  if (!pos) {
    strncpy(buf, tmpl, bufLen);
    return;
  }
  size_t prefixLen = pos - tmpl;
  size_t callLen   = strlen(cfg.callsign);
  size_t suffixLen = strlen(pos + strlen(tag));
  if (prefixLen + callLen + suffixLen >= bufLen) {
    strncpy(buf, tmpl, bufLen);
    return;
  }
  memcpy(buf, tmpl, prefixLen);
  memcpy(buf + prefixLen, cfg.callsign, callLen);
  strcpy(buf + prefixLen + callLen, pos + strlen(tag));
}

// ============================================================================
//  SD backup / restore (manual backup of NVS settings to SD)
// ============================================================================
#define SD_CFG "/mt_settings.txt"

bool Settings_BackupToSD(void) {
  File f = SD_MMC.open(SD_CFG, FILE_WRITE);
  if (!f) return false;
  f.printf("wpm=%d\n",       cfg.wpm);
  f.printf("vol=%d\n",       cfg.volume);
  f.printf("freq=%d\n",      cfg.sidetoneFreq);
  f.printf("kmode=%d\n",     (int)cfg.keyerMode);
  f.printf("pswap=%d\n",     cfg.paddleSwap ? 1 : 0);
  f.printf("bl=%d\n",        cfg.backlight);
  f.printf("ledm=%d\n",      (int)cfg.ledMode);
  f.printf("ledb=%d\n",      cfg.ledBrightness);
  f.printf("ledbg=%d\n",     cfg.ledBgBrightness);
  f.printf("ledn=%d\n",      cfg.ledCount);
  f.printf("ambc=%lu\n",     cfg.ambientColor);
  f.printf("call=%s\n",      cfg.callsign);
  f.printf("sflip=%d\n",     cfg.screenFlip ? 1 : 0);
  f.printf("cgm=%.2f\n",     cfg.charGapMult);
  f.printf("wgm=%.2f\n",     cfg.wordGapMult);
  f.printf("ddm=%.2f\n",     cfg.ditDahMult);
  f.close();
  return true;
}

bool Settings_RestoreFromSD(void) {
  if (!SD_MMC.exists(SD_CFG)) return false;
  File f = SD_MMC.open(SD_CFG, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    if      (key == "wpm")   cfg.wpm          = val.toInt();
    else if (key == "vol")   cfg.volume       = val.toInt();
    else if (key == "freq")  cfg.sidetoneFreq = val.toInt();
    else if (key == "kmode") cfg.keyerMode    = (keyer_mode_t)val.toInt();
    else if (key == "pswap") cfg.paddleSwap   = (val.toInt() != 0);
    else if (key == "bl")    cfg.backlight    = val.toInt();
    else if (key == "ledm")  cfg.ledMode      = (neo_mode_t)val.toInt();
    else if (key == "ledb")  cfg.ledBrightness= val.toInt();
    else if (key == "ledbg") cfg.ledBgBrightness= val.toInt();
    else if (key == "ledn")  cfg.ledCount       = val.toInt();
    else if (key == "ambc")  cfg.ambientColor = (uint32_t)val.toInt();
    else if (key == "call")  { strncpy(cfg.callsign, val.c_str(), sizeof(cfg.callsign)-1); }
    else if (key == "sflip") cfg.screenFlip   = (val.toInt() != 0);
    else if (key == "cgm")   cfg.charGapMult  = val.toFloat();
    else if (key == "wgm")   cfg.wordGapMult  = val.toFloat();
    else if (key == "ddm")   cfg.ditDahMult   = val.toFloat();
  }
  f.close();
  saveToNVS();
  pushToHardware();
  return true;
}

// ============================================================================
//  Factory reset
// ============================================================================
void Settings_FactoryReset(void) {
  prefs.begin(NVS_NS, false);
  prefs.clear();
  prefs.end();
  cfg = defaults;
  saveToNVS();
  pushToHardware();
}
