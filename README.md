# MorseTrainer v2

A standalone Morse code practice device built on the **Waveshare ESP32-S3-Touch-LCD-2.8**.

## Hardware

| Component         | Detail                              |
|-------------------|-------------------------------------|
| Board             | Waveshare ESP32-S3-Touch-LCD-2.8    |
| Dit paddle        | GPIO 15 (active LOW, internal pullup)|
| Dah paddle        | GPIO 18 (active LOW, internal pullup)|
| NeoPixels         | 20× WS2812 on GPIO 43              |
| Audio             | PCM5101 DAC (I2S sidetone)          |
| Display           | 320×240 ST7789 + CST328 touch       |
| SD card           | Scores, Koch progress, settings      |

## Arduino IDE Setup

### Board Settings
- **Board:** ESP32S3 Dev Module
- **Flash Size:** 16 MB
- **PSRAM:** OPI PSRAM
- **Partition Scheme:** custom
- **USB CDC On Boot:** Enabled

### Required Libraries
Install via Library Manager:
1. **LVGL** 8.x — the GUI framework
2. **Adafruit NeoPixel** — LED strip driver

### lv_conf.h Changes
Copy `lv_conf.h` from LVGL `examples/` to your Arduino libraries folder and enable:

```c
#define LV_COLOR_DEPTH          16
#define LV_HOR_RES_MAX          320
#define LV_VER_RES_MAX          240

// Fonts required by MorseTrainer
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_24   1   /* Falling Letters game */
#define LV_FONT_MONTSERRAT_28   1   /* Callsign Rush game  */

// Widgets used
#define LV_USE_TABVIEW          1
#define LV_USE_DROPDOWN         1
#define LV_USE_SLIDER           1
#define LV_USE_SWITCH           1
#define LV_USE_BAR              1
#define LV_USE_TEXTAREA         1
#define LV_USE_KEYBOARD         1
#define LV_USE_CALENDAR         1
#define LV_USE_CHART            1
```

### Project Folder
Place **all** files (MorseTrainer.ino + all MT_*.h/.cpp + Waveshare driver .h/.cpp files) in a single folder named `MorseTrainer/`.

The Waveshare driver files you already have:
- `Display_ST7789.h/.cpp`
- `Touch_CST328.h/.cpp`
- `I2C_Driver.h/.cpp`
- `LVGL_Driver.h/.cpp`
- `SD_Card.h/.cpp`
- `BAT_Driver.h/.cpp`
- `PWR_Key.h/.cpp`
- `RTC_PCF85063.h/.cpp`
- `Gyro_QMI8658.h/.cpp`

**Not used** (replaced by MorseTrainer modules):
- `Audio_PCM5101.h/.cpp` → replaced by `MT_Sidetone`
- `Wireless.h/.cpp` → replaced by `MT_WiFi`
- `LVGL_Example.h/.cpp` → replaced by `MT_UI`
- `LVGL_Music.h/.cpp` → not needed

## File Map

```
MorseTrainer/
├── MorseTrainer.ino          Main sketch (setup + loop)
│
├── MT_Pins.h                 All GPIO definitions
├── MT_Morse.h/.cpp           Morse table (encode/decode)
├── MT_Sidetone.h/.cpp        I2S sine-wave tone generator
├── MT_Keyer.h/.cpp           Iambic A/B + straight key engine
├── MT_NeoPixel.h/.cpp        20-LED NeoPixel effects
├── MT_Koch.h/.cpp             Koch method lesson tracking
├── MT_Settings.h/.cpp        NVS + SD settings persistence
├── MT_Score.h/.cpp            Per-game high scores (SD)
├── MT_WiFi.h/.cpp             WiFi + leaderboard stubs
│
├── MT_UI.h/.cpp               5-tab LVGL shell
├── MT_UI_Home.h/.cpp          Home: decoded text, WPM, TX
├── MT_UI_Keyer.h/.cpp         Keyer: mode, WPM slider
├── MT_UI_Games.h/.cpp         Games: launcher + high scores
├── MT_UI_Settings.h/.cpp      Settings: all controls
├── MT_UI_WiFi.h/.cpp          WiFi: connect + leaderboard
│
├── MT_Game_FallingLetters.h/.cpp   Falling Letters game
├── MT_Game_CallsignRush.h/.cpp     Callsign Rush game
│
└── (Waveshare driver files)   Your existing board drivers
```

## Architecture

```
Core 1 (main loop)          Core 0 (DriverTask)
  │                            │
  ├─ lv_timer_handler()        ├─ PWR_Loop()
  │   ├─ UI refresh (1 Hz)    ├─ BAT_Get_Volts()
  │   ├─ Game tick (30 Hz)    ├─ PCF85063_Loop()
  │   └─ Spectrum anim        ├─ QMI8658_Loop()
  │                            └─ NeoPixel_Update() (30 Hz)
  │
  esp_timer (1 ms):            Sidetone task (core 0):
  └─ Keyer state machine       └─ I2S DMA feed (continuous)
```

## Keyer Modes
- **Iambic A:** Alternating dit/dah while both paddles held; stops on release
- **Iambic B:** Like A, but completes the pending alternate element after release
- **Straight:** Either paddle acts as a straight key; element classified by duration

## Games
- **Falling Letters:** Koch-ordered letters fall; send correct Morse to destroy. Levels increase speed.
- **Callsign Rush:** Random callsigns appear; copy them before the timer runs out.

## Settings Persistence
- **NVS (primary):** Instant access, survives reboot
- **SD backup:** Manual backup/restore, survives firmware reflash
- **Factory reset:** Clears NVS, reverts to defaults

## SD Card Files
```
/koch.txt               Koch lesson number
/mt_settings.json       Settings backup
/scores/falling.txt     Falling Letters high scores
/scores/callrush.txt    Callsign Rush high scores
```

## Future Plans
- Speed Climb game mode
- Decode Challenge (receive practice)
- QSO Simulator
- WiFi leaderboard backend
- Radio integration (TX/RX keying)
