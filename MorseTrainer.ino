// ============================================================================
//  MorseTrainer — Standalone Morse Code Trainer
//  Waveshare ESP32-S3-Touch-LCD-2.8
// ============================================================================
#include "MT_Version.h"

#include <driver/gpio.h>
#include <Preferences.h>
#include "Display_ST7789.h"
#include "I2C_Driver.h"
#include "LVGL_Driver.h"
#include "SD_Card.h"
#include "BAT_Driver.h"
#include "PWR_Key.h"
#include "RTC_PCF85063.h"
#include "Gyro_QMI8658.h"

#include "MT_Pins.h"
#include "MT_Morse.h"
#include "MT_Sidetone.h"
#include "MT_Keyer.h"
#include "MT_NeoPixel.h"
#include "MT_Koch.h"
#include "MT_Settings.h"
#include "MT_Score.h"
#include "MT_WiFi.h"
#include "MT_UI.h"
#include "MT_Game_FallingLetters.h"
#include "MT_Game_CallsignRush.h"
#include "MT_Game_Trainer.h"
#include "MT_Game_QSO.h"
#include "MT_OTA.h"

// Background task — core 0
void DriverTask(void* parameter) {
  while (1) {
    PWR_Loop();
    BAT_Get_Volts();
    PCF85063_Loop();
    QMI8658_Loop();
    NeoPixel_Update();
    vTaskDelay(pdMS_TO_TICKS(33));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial0.end();
  gpio_reset_pin((gpio_num_t)43);
  pinMode(43, OUTPUT);
  digitalWrite(43, LOW);

  // Phase 1: Board hardware
  Flash_test();
  PWR_Init();
  BAT_Init();
  I2C_Init();
  PCF85063_Init();
  QMI8658_Init();
  Backlight_Init();
  SD_Init();

  // Check for OTA update on SD card (/firmware.bin)
  OTA_CheckAndUpdate();  // if found, flashes and reboots — never returns

  // Phase 2: Display
  LCD_Init();
  Lvgl_Init();

  // Apply screen flip early (before splash) from NVS
  // Safety: both paddles held at boot = force flip OFF
  {
    pinMode(15, INPUT_PULLUP);
    pinMode(18, INPUT_PULLUP);
    delay(10);  // let pullups settle
    Preferences p;
    bool bothPaddles = (digitalRead(15) == LOW && digitalRead(18) == LOW);
    if (bothPaddles) {
      p.begin("mtcfg", false);
      p.putBool("sflip", false);
      p.end();
      LCD_SetFlip(false);
    } else {
      p.begin("mtcfg", true);
      if (p.getBool("sflip", false)) LCD_SetFlip(true);
      p.end();
    }
  }

  // Splash screen
  {
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t* l1 = lv_label_create(scr);
    lv_label_set_text(l1, "MORSE");
    lv_obj_set_style_text_color(l1, lv_color_hex(0x00E676), 0);
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(l1, &lv_font_montserrat_28, 0);
#endif
    lv_obj_align(l1, LV_ALIGN_CENTER, 0, -22);

    lv_obj_t* l2 = lv_label_create(scr);
    lv_label_set_text(l2, "TRAINER");
    lv_obj_set_style_text_color(l2, lv_color_hex(0x00E676), 0);
#if LV_FONT_MONTSERRAT_28
    lv_obj_set_style_text_font(l2, &lv_font_montserrat_28, 0);
#endif
    lv_obj_align(l2, LV_ALIGN_CENTER, 0, 14);

    lv_obj_t* l3 = lv_label_create(scr);
    lv_label_set_text(l3, "v" MT_VERSION);
    lv_obj_set_style_text_color(l3, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(l3, &lv_font_montserrat_12, 0);
    lv_obj_align(l3, LV_ALIGN_CENTER, 0, 46);

    lv_timer_handler();
    delay(1800);
    lv_obj_clean(scr);
  }

  // Phase 3: MorseTrainer peripherals
  Sidetone_Init();
  NeoPixel_Init();
  Keyer_Init();
  Koch_Init();
  MTWiFi_Init();

  // Phase 4: Settings
  Settings_Init();
  Set_Backlight(Settings_Get()->backlight);

  // Phase 5: UI
  UI_Init();

  // Phase 6: Background task
  xTaskCreatePinnedToCore(DriverTask, "DriverTask", 4096, NULL, 3, NULL, 0);

  Serial.printf("MorseTrainer v" MT_VERSION " ready  WPM:%d  Mode:%s  Koch:%d\n",
    Keyer_GetWPM(), Keyer_ModeName(Keyer_GetMode()), Koch_GetLesson());
}

void loop() {
  lv_timer_handler();
  vTaskDelay(pdMS_TO_TICKS(5));
}
