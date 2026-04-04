/**
 * lv_conf.h — Configuration for LVGL 9.x
 * Waveshare ESP32-S3-Touch-LCD-2.8 + MorseTrainer v2
 *
 * PLACE THIS FILE at:  Arduino/libraries/lv_conf.h
 *   (NEXT TO the lvgl folder, NOT inside it)
 */

#if 1 /* Set to "1" to enable content */

#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH          16

/*====================
   MEMORY
 *====================*/
#define LV_MEM_SIZE             (96U * 1024U)

/*====================
   TICK
 *====================*/
/* We use lv_tick_set_cb(millis) in code, so no custom tick needed here */

/*====================
   DISPLAY / RENDERING
 *====================*/
#define LV_DPI_DEF              130

/*====================
   OS / THREADING
 *====================*/
#define LV_USE_OS               LV_OS_NONE

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG              0

/*====================
   ASSERTS
 *====================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*====================
   FONT USAGE
 *====================*/
#define LV_FONT_MONTSERRAT_8     0
#define LV_FONT_MONTSERRAT_10    0
#define LV_FONT_MONTSERRAT_12    1   /* UI normal font */
#define LV_FONT_MONTSERRAT_14    1   /* default font */
#define LV_FONT_MONTSERRAT_16    1   /* UI large font */
#define LV_FONT_MONTSERRAT_18    1   /* game fallback */
#define LV_FONT_MONTSERRAT_20    0
#define LV_FONT_MONTSERRAT_22    0
#define LV_FONT_MONTSERRAT_24    1   /* Falling Letters game */
#define LV_FONT_MONTSERRAT_26    0
#define LV_FONT_MONTSERRAT_28    1   /* Callsign Rush game */
#define LV_FONT_MONTSERRAT_30    0
#define LV_FONT_MONTSERRAT_32    0
#define LV_FONT_MONTSERRAT_34    0
#define LV_FONT_MONTSERRAT_36    0
#define LV_FONT_MONTSERRAT_38    0
#define LV_FONT_MONTSERRAT_40    0
#define LV_FONT_MONTSERRAT_42    0
#define LV_FONT_MONTSERRAT_44    0
#define LV_FONT_MONTSERRAT_46    0
#define LV_FONT_MONTSERRAT_48    0

#define LV_FONT_DEFAULT          &lv_font_montserrat_14
#define LV_USE_FONT_PLACEHOLDER  1

/*====================
   WIDGETS
 *====================*/
#define LV_USE_ARC            1
#define LV_USE_BAR            1
#define LV_USE_BUTTON         1
#define LV_USE_BUTTONMATRIX   1
#define LV_USE_CANVAS         0
#define LV_USE_CHECKBOX       1
#define LV_USE_DROPDOWN       1
#define LV_USE_IMAGE          1
#define LV_USE_LABEL          1
#define LV_USE_LINE           1
#define LV_USE_ROLLER         1
#define LV_USE_SCALE          0
#define LV_USE_SLIDER         1
#define LV_USE_SWITCH         1
#define LV_USE_TABLE          0
#define LV_USE_TEXTAREA       1

/*====================
   EXTRA WIDGETS
 *====================*/
#define LV_USE_ANIMIMAGE      0
#define LV_USE_CALENDAR       0
#define LV_USE_CHART          0
#define LV_USE_COLORWHEEL     0
#define LV_USE_IMAGEBUTTON    0
#define LV_USE_KEYBOARD       1
#define LV_USE_LED            0
#define LV_USE_LIST           0
#define LV_USE_MENU           0
#define LV_USE_METER          0
#define LV_USE_MSGBOX         0
#define LV_USE_SPAN           0
#define LV_USE_SPINBOX        0
#define LV_USE_SPINNER        0
#define LV_USE_TABVIEW        1
#define LV_USE_TILEVIEW       0
#define LV_USE_WIN            0

/*====================
   THEMES
 *====================*/
#define LV_USE_THEME_DEFAULT  1
#define LV_USE_THEME_SIMPLE   1

/*====================
   LAYOUTS
 *====================*/
#define LV_USE_FLEX           1
#define LV_USE_GRID           1

/*====================
   DEBUG / MONITORS
 *====================*/
#define LV_USE_PERF_MONITOR   0
#define LV_USE_MEM_MONITOR    0

/*====================
   DEMOS (disabled)
 *====================*/
#define LV_USE_DEMO_WIDGETS        0
#define LV_USE_DEMO_BENCHMARK      0
#define LV_USE_DEMO_STRESS         0
#define LV_USE_DEMO_MUSIC          0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER 0

/*====================
   BUILT-IN DRIVERS (disabled — we use our own SPI driver)
 *====================*/
#define LV_USE_TFT_ESPI       0
#define LV_USE_ST7735         0
#define LV_USE_ST7789         0
#define LV_USE_ST7796         0
#define LV_USE_ILI9341        0
#define LV_USE_EVDEV          0
#define LV_USE_LIBINPUT       0

#endif /* LV_CONF_H */

#endif /* End of "Content enable" */
