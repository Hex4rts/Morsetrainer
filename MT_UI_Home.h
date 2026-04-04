#ifndef MT_UI_HOME_H
#define MT_UI_HOME_H

#include <lvgl.h>

void UI_Home_Create(lv_obj_t* parent);
void UI_Home_Refresh(void);
void UI_Home_AddChar(char c);
void UI_Home_Clear(void);

#endif
