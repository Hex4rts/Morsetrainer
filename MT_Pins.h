#ifndef MT_PINS_H
#define MT_PINS_H

// ============================================================================
//  MorseTrainer v2 — Pin Definitions
//  Waveshare ESP32-S3-Touch-LCD-2.8 (REAL pins from Waveshare headers)
// ============================================================================

// --- LCD (ST7789 via SPI / FSPI) ---
//     Defined in Display_ST7789.h: MOSI=45, SCLK=40, CS=42, DC=41, RST=39, BL=5

// --- Touch (CST328 via Wire1) ---
//     Defined in Touch_CST328.h: SDA=1, SCL=3, INT=4, RST=2

// --- I2C bus (Wire — RTC, IMU) ---
//     Defined in I2C_Driver.h: SDA=11, SCL=10

// --- I2S audio (PCM5101 DAC) ---
#define I2S_BCLK_PIN        48
#define I2S_LRC_PIN         38
#define I2S_DOUT_PIN        47

// --- SD card (SD_MMC 1-bit) ---
//     Defined in SD_Card.h: CLK=14, CMD=17, D0=16, D3=21

// --- Battery ADC ---
//     Defined in BAT_Driver.h: ADC=8

// --- Power key ---
//     Defined in PWR_Key.h: KEY=6, CTRL=7

// ============================================================================
//  MorseTrainer-specific peripherals
// ============================================================================

// --- Paddle inputs (active LOW with internal pull-up) ---
#define DIT_PADDLE_PIN      15
#define DAH_PADDLE_PIN      18

// --- NeoPixels (4 strips × 5 = 20 LEDs, single data line) ---
#define NEOPIXEL_PIN        43
#define NEOPIXEL_COUNT      60    // max supported — runtime count set in settings

#define NEO_STRIP_LEN       5

#endif // MT_PINS_H
