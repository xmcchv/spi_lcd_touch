#ifndef LVGL_MAIN_UI_H
#define LVGL_MAIN_UI_H

#include "lvgl.h"
#include "esp_log.h"
#include "wifi.h"

//==========================================================
// 屏幕分辨率宏定义（从spi_lcd_touch_example_main.c中复制）
#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
#define LCD_H_RES              240
#define LCD_V_RES              320
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
#define LCD_H_RES              240
#define LCD_V_RES              240
#endif

// 按钮尺寸计算宏（基于屏幕分辨率的百分比） 
#define BTN_WIDTH_PERCENT 45    // 按钮宽度占屏幕宽度的50%
#define BTN_HEIGHT_PERCENT 15   // 按钮高度占屏幕高度的15%
#define BTN_SPACING_PERCENT 5  // 按钮间距占屏幕高度的10%

// 根据旋转状态计算实际像素值
// 当屏幕旋转90°或270°时，水平和垂直分辨率交换
#define CALC_BTN_WIDTH(disp) (lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_0 || lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_180 ? \
                             (LCD_H_RES * BTN_WIDTH_PERCENT / 100) : (LCD_V_RES * BTN_WIDTH_PERCENT / 100))
#define CALC_BTN_HEIGHT(disp) (lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_0 || lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_180 ? \
                              (LCD_V_RES * BTN_HEIGHT_PERCENT / 100) : (LCD_H_RES * BTN_HEIGHT_PERCENT / 100))
#define CALC_BTN_SPACING(disp) (lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_0 || lv_display_get_rotation(disp) == LV_DISPLAY_ROTATION_180 ? \
                               (LCD_V_RES * BTN_SPACING_PERCENT / 100) : (LCD_H_RES * BTN_SPACING_PERCENT / 100))

// 函数声明
void example_lvgl_demo_ui(lv_display_t *disp);
void rotate_ui(lv_display_t *disp);
void wifi_ui(lv_display_t *disp);
void joke_ui(lv_display_t *disp);
void get_button_info(lv_area_t *btn_area, int *width, int *height);

// 事件回调包装器声明
void wifi_ui_event_cb(lv_event_t *e);
void rotate_ui_event_cb(lv_event_t *e);
void joke_ui_event_cb(lv_event_t *e);
void microphone_ui_event_cb(lv_event_t *e);

// 返回主界面按钮回调
void back_mainui_cb(lv_event_t * e);

static void set_angle(void * obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

#endif // LVGL_MAIN_UI_H