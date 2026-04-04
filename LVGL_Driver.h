#ifndef LVGL_DRIVER_H
#define LVGL_DRIVER_H

#include <Arduino.h>
#include <lvgl.h>
#include "Display_ST7789.h"
#include "Touch_CST328.h"

#define LVGL_WIDTH              LCD_HEIGHT   // 320 in landscape
#define LVGL_HEIGHT             LCD_WIDTH    // 240 in landscape
#define EXAMPLE_LVGL_TICK_PERIOD_MS  2

void Lvgl_Init(void);
void Lvgl_Loop(void);

#endif
