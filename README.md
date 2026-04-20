# MorseTrainer

A standalone Morse code practice device built on the **Waveshare ESP32-S3-Touch-LCD-2.8**.

case : https://www.thingiverse.com/thing:7329182
amazon: https://www.amazon.ca/dp/B0D8KKRV5V/ref=cm_sw_r_as_gl_api_gl_i_VZ4R3VETSRMXKA6MG6WE

## Features

- **Iambic A/B keyer** with WPM-driven timing
- **Straight key** with fully adaptive timing (learns your speed)
- **I2S sidetone** via PCM5101 DAC (adjustable tone & volume)
- **20 NeoPixels** — green flash (straight key), blue/amber flash (iambic), plus ambient modes
- **4 practice games** with difficulty levels and persistent high scores
- **LEARN mode** — guided letter-by-letter Morse trainer with drill/recall cycle
- **OTA updates** from SD card — drop `firmware.bin` on SD and reboot
- **Dark military theme** UI — black background, green/amber/red accents

## Hardware

| Component         | Detail                                |
|-------------------|---------------------------------------|
| Board             | Waveshare ESP32-S3-Touch-LCD-2.8      |
| Flash             | 16MB (custom partition: 2x3MB OTA)    |
| Dit paddle        | GPIO 15 (active LOW, internal pullup) |
| Dah paddle        | GPIO 18 (active LOW, internal pullup) |
| NeoPixels         | 20x WS2812 on GPIO 43                |
| Audio             | PCM5101 DAC via I2S                   |
| Display           | 320x240 ST7789 + CST328 capacitive touch |
| SD card           | 4-bit SDMMC                           |

## Arduino IDE Setup

### Board Settings (Tools menu)
- **Board:** ESP32S3 Dev Module
- **Flash Size:** 16 MB (128Mb)
- **Flash Mode:** QIO 80MHz
- **PSRAM:** OPI PSRAM
- **Partition Scheme:** Custom (uses partitions.csv from sketch folder)
- **USB CDC On Boot:** Enabled
- **CPU Frequency:** 240MHz

### Required Libraries
Install via Library Manager:
1. **LVGL** 9.x
2. **Adafruit NeoPixel**

### lv_conf.h
Copy the included `lv_conf.h` to your Arduino libraries folder (next to the `lvgl/` folder, NOT inside it):
```
Arduino/libraries/lvgl/        <- installed by Library Manager
Arduino/libraries/lv_conf.h    <- copy here
```

### Build
1. Place all files in `Documents/Arduino/MorseTrainer/`
2. `partitions.csv` MUST be in the sketch folder
3. Open `MorseTrainer.ino`, compile, upload via USB (first time)

## UI Tabs

| Tab  | Function |
|------|----------|
| HOME | Decoded text, visual dit/dah bars, WPM/MODE/CALL cards |
| KEY  | Keyer mode selector (Iambic A/B/Straight), WPM slider |
| OPS  | 4 game cards with best scores - tap to play |
| CFG  | Callsign, volume, tone, backlight, LED mode, backup/restore, version |
| NET  | WiFi settings (stub) |

## Keyer Modes

- **Iambic A:** Alternating dit/dah while both paddles held; stops on release
- **Iambic B:** Like A, but latches the opposite element during sounding
- **Straight key:** Either paddle acts as straight key. Fully adaptive timing.

### Straight Key Adaptive Engine
- `sk_dit_avg` - exponential moving average of actual dit durations
- Dit/dah boundary: `dit_avg x 2`
- Character gap: `dit_avg x 3`, word gap: `dit_avg x 7`
- Dah duration refines estimate: `implied_dit = dah / 3`
- ISR event log at 1ms - no missed events regardless of UI refresh rate
- WPM setting only affects game playback speed

### Visual Feedback
- **Home screen:** Iambic = blue (dit) / amber (dah) centered bars. Straight key = green proportional bars from left edge (fixed ms/pixel scale)
- **NeoPixel:** Iambic = blue/amber flash. Straight key = solid green while held.

## Games (OPS tab)

All 4 games save best scores to SD and display them on the OPS tab cards.

### Falling Letters (2 levels)
- **Beginner:** Letter + morse hint underneath
- **Expert:** Letter only
- Full A-Z alphabet. Speed increases per level. Always kills the lowest matching letter on duplicates.

### Callsign Rush (3 levels)
- **Beginner:** Text + visual bars + audio, NO timer - practice at your own pace
- **Intermediate:** Text + audio, 12s timer
- **Expert:** Audio only ("???"), 12s timer
- All playback at keyer WPM.

### Morse Trace (4 levels)
- **LEARN:** Guided step-by-step letter trainer (see below)
- **Beginner:** Letter + visual bars + audio
- **Intermediate:** Letter + audio (no bars)
- **Expert:** Audio only, letter revealed after playback

### QSO Simulator (3x3 grid)
- 3 difficulties (Beginner/Intermediate/Expert) x 3 complexity levels (SIMPLE/RST/FULL)
- Random callsigns from realistic prefix pool

## LEARN Mode (Morse Trace)

Progressive letter-by-letter trainer with spaced drilling and recall.

**Learning order:** E T I A N M S U R W D K G O H V F L P J B X C Z Y Q 0-9

### Phase 1 - Build Initial Pool
Letters drilled back-to-back until 3 are learned. No recall rounds yet since the pool is too small to be meaningful.

### Per-Letter Drill (4 assisted rounds)
1. Device shows letter + plays sound + shows visual bars
2. User copies (keys the pattern back)
3. Repeat 4 times total
4. Letter enters learned pool. Next letter auto-introduced if pool < 3.

### Phase 2 - Interleaved Drill + Recall
Once 3+ letters learned, the system alternates:
- Max 2 consecutive drill rounds for new letters
- Then forces a recall round from the learned pool

### Recall Rounds
- Just the letter appears - no sound, no bars, no hints
- User keys from memory
- **3 tries per appearance** - same letter stays on wrong answer
- 3rd fail: letter goes back to drilling (4 assisted rounds)
- Correct: streak increments. After 3 correct recalls, next new letter introduced.

### Anti-Repeat
Random pool excludes the last picked letter to avoid repetition.

### Progress Persistence
All drill counters, learned state, and next letter index saved to SD. Progress survives reboot.

## OTA Update (SD Card)

Requires the custom partition table (`partitions.csv`) with 2x3MB app slots. "Huge APP (3MB No OTA)" will NOT work.

1. Arduino IDE: Sketch -> Export Compiled Binary
2. Copy `MorseTrainer_ino.bin` to SD card root
3. Rename to `firmware.bin`
4. Insert SD, power on
5. Device detects file on boot, flashes in 4KB chunks with serial progress
6. Renames to `firmware.done`, reboots into new firmware

## File Map

```
MorseTrainer/
  MorseTrainer.ino            Main sketch
  MT_Version.h                Version number (single source of truth)
  MT_Pins.h                   GPIO definitions
  partitions.csv              Custom partition table (2x3MB OTA)
  lv_conf.h                   LVGL configuration

  MT_Morse.h/.cpp             Morse encode/decode table
  MT_Sidetone.h/.cpp          I2S sine-wave tone generator
  MT_Keyer.h/.cpp             Iambic A/B + adaptive straight key
  MT_NeoPixel.h/.cpp          20-LED effects
  MT_Koch.h/.cpp              Koch method lesson tracking
  MT_Settings.h/.cpp          NVS + SD settings persistence
  MT_Score.h/.cpp             Per-game high scores (SD)
  MT_WiFi.h/.cpp              WiFi stubs
  MT_OTA.h/.cpp               SD card OTA update

  MT_UI.h/.cpp                5-tab LVGL shell
  MT_UI_Home.h/.cpp           Home tab
  MT_UI_Keyer.h/.cpp          Keyer settings tab
  MT_UI_Games.h/.cpp          OPS tab (game cards + scores)
  MT_UI_Settings.h/.cpp       CFG tab
  MT_UI_WiFi.h/.cpp           NET tab

  MT_Game_FallingLetters.h/.cpp
  MT_Game_CallsignRush.h/.cpp
  MT_Game_Trainer.h/.cpp      Morse Trace + LEARN mode
  MT_Game_QSO.h/.cpp          QSO Simulator

  (Waveshare driver files)    Display, Touch, I2C, SD, BAT, PWR, RTC, IMU
```

## Architecture

```
Core 1 (main loop)              Core 0 (DriverTask @ 33ms)
  lv_timer_handler() (5ms)        PWR_Loop()
    UI refresh                    BAT_Get_Volts()
    Game tick                     NeoPixel_Update()
                                  IMU / RTC

  esp_timer (1ms):               Sidetone task (core 0):
    Keyer ISR state machine        I2S DMA feed
```

## SD Card Files

```
/firmware.bin           OTA update (renamed to .done after flash)
/mt_settings.json       Settings backup
/koch.txt               Koch lesson number
/trainer2.txt           Morse Trace + LEARN mode progress
/scores/falling.txt     Falling Letters high scores
/scores/callrush.txt    Callsign Rush high scores
/scores/trace.txt       Morse Trace high scores
/scores/qso.txt         QSO Simulator high scores
```

## Settings Persistence

| Data | Storage | When |
|------|---------|------|
| WPM, volume, tone, mode, callsign | NVS (3s debounce) + SD backup | On change |
| Koch lesson | SD /koch.txt | On advance/reset |
| Game scores | SD /scores/*.txt | After each game |
| LEARN progress | SD /trainer2.txt | On level-up / exit |

## Version History

- **v0.9** - Progress bar shows LEARN mode stats, combined menu display
- **v0.8** - Pool < 3 auto-introduction, no recall until 3 letters learned
- **v0.7** - Drill threshold tuning
- **v0.6** - Score_Submit for Morse Trace + QSO, all 4 scores on OPS tab
- **v0.5** - Fixed score display, showRefBars() for LEARN + Beginner
- **v0.4** - Per-appearance 3-try recall, LEARN mode startPlayback fix
- **v0.3** - LEARN mode, MT_Version.h, version in CFG tab
- **v0.2** - Falling Letters full alphabet, kill lowest duplicate, no timer beginner
