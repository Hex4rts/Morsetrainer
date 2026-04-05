/*****************************************************************************
  LVGL_Driver.cpp — LVGL 9.x for Waveshare ESP32-S3-Touch-LCD-2.8
  Display: ST7789 240×320, Touch: CST328
******************************************************************************/
#include "LVGL_Driver.h"

// RGB565 draw buffers — 40 lines × 240 px × 2 bytes = 19200 bytes each
#define DRAW_BUF_LINES  40
#define DRAW_BUF_SIZE   (LVGL_WIDTH * DRAW_BUF_LINES * 2)
static uint8_t buf1[DRAW_BUF_SIZE];
static uint8_t buf2[DRAW_BUF_SIZE];

/* Display flush callback */
static void Lvgl_Display_LCD(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
  LCD_addWindow(area->x1, area->y1, area->x2, area->y2, (uint16_t *)px_map);
  lv_display_flush_ready(disp);
}

/* Touchpad read callback — remap portrait touch to landscape display */
static void Lvgl_Touchpad_Read(lv_indev_t *indev, lv_indev_data_t *data)
{
  uint16_t touchpad_x[5] = {0};
  uint16_t touchpad_y[5] = {0};
  uint16_t strength[5]   = {0};
  uint8_t  touchpad_cnt  = 0;
  Touch_Read_Data();
  uint8_t touchpad_pressed = Touch_Get_XY(touchpad_x, touchpad_y, strength, &touchpad_cnt, CST328_LCD_TOUCH_MAX_POINTS);
  if (touchpad_pressed && touchpad_cnt > 0) {
    // Touch reports portrait (240×320), remap to landscape (320×240)
    if (display_flipped) {
      // MADCTL=0xA0: flipped 180°
      data->point.x = (LCD_HEIGHT - 1) - touchpad_y[0];
      data->point.y = touchpad_x[0];
    } else {
      // MADCTL=0x60: normal landscape
      data->point.x = touchpad_y[0];
      data->point.y = (LCD_WIDTH - 1) - touchpad_x[0];
    }
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static uint32_t lvgl_tick_cb(void) { return millis(); }

void Lvgl_Init(void)
{
  lv_init();
  lv_tick_set_cb(lvgl_tick_cb);

  /* Create display 240×320 portrait */
  lv_display_t *disp = lv_display_create(LVGL_WIDTH, LVGL_HEIGHT);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, Lvgl_Display_LCD);
  lv_display_set_buffers(disp, buf1, buf2, DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  /* Create touch input device */
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, Lvgl_Touchpad_Read);
}

void Lvgl_Loop(void)
{
  lv_timer_handler();
}
