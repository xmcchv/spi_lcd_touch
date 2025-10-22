#ifndef UI_COMPONENTS_HPP
#define UI_COMPONENTS_HPP

#include "lvgl.h"

class ButtonFactory {
public:
    static lv_obj_t* createButton(lv_obj_t* parent, const char* text, 
                                 int width, int height, int x, int y);
};

class LabelFactory {
public:
    static lv_obj_t* createLabel(lv_obj_t* parent, const char* text,
                                 int x, int y, lv_align_t align = LV_ALIGN_TOP_LEFT);
};

class LayoutHelper {
public:
    static void centerVertically(lv_obj_t* parent, lv_obj_t* obj, int total_height);
    static int calculateButtonWidth(lv_display_t* disp);
    static int calculateButtonHeight(lv_display_t* disp);
};

#endif // UI_COMPONENTS_HPP 
