/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl_main_ui.h"
#include "wifi_ui.h"
#include "spi_lcd_touch_example_main.h"
#include "microphone.h"

static const char *TAG = "UI_MAIN";

static lv_obj_t * btn;
static lv_display_rotation_t rotation = LV_DISP_ROTATION_0;

// WiFi UI事件回调包装器
void wifi_ui_event_cb(lv_event_t *e) {
    lv_display_t *disp = lv_event_get_user_data(e);
    wifi_ui(disp);
}

// 旋转UI事件回调包装器  
void rotate_ui_event_cb(lv_event_t *e) {
    lv_display_t *disp = lv_event_get_user_data(e);
    rotate_ui(disp);
}

// 笑话UI事件回调包装器
void joke_ui_event_cb(lv_event_t *e) {
    lv_display_t *disp = lv_event_get_user_data(e);
    joke_ui(disp);
}

// 麦克风UI事件回调包装器
void microphone_ui_event_cb(lv_event_t *e) {
    lv_display_t *disp = lv_event_get_user_data(e);
    microphone_ui(disp);
}

// 添加获取按钮信息的函数
void get_button_info(lv_area_t *btn_area, int *width, int *height) {
    if (btn != NULL) {
        lv_obj_get_coords(btn, btn_area);
        *width = lv_obj_get_width(btn);
        *height = lv_obj_get_height(btn);
    }
}

static void btn_cb(lv_event_t * e)
{
    lv_display_t *disp = lv_event_get_user_data(e);
    
    // 获取当前旋转状态
    lv_display_rotation_t current_rotation = lv_display_get_rotation(disp);
    ESP_LOGI(TAG, "Current rotation before click: %d", current_rotation);
    
    // 从事件中获取触摸坐标
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t point;
    
    if (indev) {
        lv_indev_get_point(indev, &point);
        
        // 打印触摸坐标
        ESP_LOGI(TAG, "按钮触发坐标: X=%d, Y=%d", point.x, point.y);
        
        // 打印按钮位置信息
        lv_area_t btn_area;
        lv_obj_get_coords(btn, &btn_area);
        ESP_LOGI(TAG, "按钮位置: 左上角(%d,%d) - 右下角(%d,%d)", 
                 btn_area.x1, btn_area.y1, btn_area.x2, btn_area.y2);
        
        // 检查触摸点是否在按钮区域内
        if (point.x >= btn_area.x1 && point.x <= btn_area.x2 && 
            point.y >= btn_area.y1 && point.y <= btn_area.y2) {
            ESP_LOGI(TAG, "触摸点在按钮区域内");
        } else {
            ESP_LOGI(TAG, "触摸点在按钮区域外");
        }
    }

    // 更新旋转状态
    current_rotation++;
    if (current_rotation > LV_DISPLAY_ROTATION_270) {
        current_rotation = LV_DISPLAY_ROTATION_0;
    }
    
    ESP_LOGI(TAG, "Setting new rotation: %d", current_rotation);
    lv_disp_set_rotation(disp, current_rotation);
    
    // 验证旋转是否已应用
    lv_display_rotation_t new_rotation = lv_display_get_rotation(disp);
    ESP_LOGI(TAG, "Rotation after setting: %d", new_rotation);
}

void back_mainui_cb(lv_event_t * e)
{
    delete_wifi_status_timer();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    lv_display_t *disp = lv_event_get_user_data(e);
    example_lvgl_demo_ui(disp);
}

void rotate_ui(lv_display_t *disp) {
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clean(scr);

    // 计算自适应尺寸
    int btn_width = CALC_BTN_WIDTH(disp);
    int btn_height = CALC_BTN_HEIGHT(disp);
    int btn_spacing = CALC_BTN_SPACING(disp);
    int margin = btn_spacing / 2; // 边距
    
    ESP_LOGI(TAG, "旋转界面 - 屏幕分辨率: %dx%d, 按钮尺寸: %dx%d, 间距: %d", 
             LCD_H_RES, LCD_V_RES, btn_width, btn_height, btn_spacing);
    
    // 创建旋转按钮（原有功能）
    btn = lv_btn_create(scr);
    lv_obj_set_size(btn, btn_width, btn_height);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, margin, -margin - btn_height);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, LV_SYMBOL_REFRESH" ROTATE");
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, disp);

    // 创建返回按钮
    lv_obj_t *back_btn = lv_btn_create(scr);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -margin, -margin - btn_height);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT" BACK");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, back_mainui_cb, LV_EVENT_CLICKED, disp);
}

void example_lvgl_demo_ui(lv_display_t *disp) {
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clean(scr);
    
    // 计算自适应尺寸
    int btn_width = CALC_BTN_WIDTH(disp);
    int btn_height = CALC_BTN_HEIGHT(disp);
    int btn_spacing = CALC_BTN_SPACING(disp);
    
    ESP_LOGI(TAG, "屏幕分辨率: %dx%d, 按钮尺寸: %dx%d, 间距: %d", 
             LCD_H_RES, LCD_V_RES, btn_width, btn_height, btn_spacing);
    
    // 创建标题 - 自适应位置
    lv_obj_t *title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "MENU");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, btn_spacing / 2);
    
    
    // 计算按钮起始Y位置（居中显示）
    int total_buttons_height = (btn_height + btn_spacing) * 2; // 两个按钮加间距
    int start_y = (LCD_V_RES - total_buttons_height) / 2;

    // 创建WiFi功能按钮
    lv_obj_t *wifi_btn = lv_btn_create(scr);
    lv_obj_set_size(wifi_btn, btn_width, btn_height);
    lv_obj_align(wifi_btn, LV_ALIGN_CENTER, 0, start_y - LCD_V_RES / 2);
    lv_obj_t *wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI" WiFi");
    lv_obj_center(wifi_label);
    lv_obj_add_event_cb(wifi_btn, wifi_ui_event_cb, LV_EVENT_CLICKED, disp);
    
    // 创建旋转功能按钮
    lv_obj_t *rotate_btn = lv_btn_create(scr);
    lv_obj_set_size(rotate_btn, btn_width, btn_height);
    lv_obj_align(rotate_btn, LV_ALIGN_CENTER, 0, start_y + btn_height + btn_spacing - LCD_V_RES / 2);
    lv_obj_t *rotate_label = lv_label_create(rotate_btn);
    lv_label_set_text(rotate_label, LV_SYMBOL_REFRESH" Rotation");
    lv_obj_center(rotate_label);
    lv_obj_add_event_cb(rotate_btn, rotate_ui_event_cb, LV_EVENT_CLICKED, disp);

    // 创建笑话功能按钮
    lv_obj_t *joke_btn = lv_btn_create(scr);
    lv_obj_set_size(joke_btn, btn_width, btn_height);
    lv_obj_align(joke_btn, LV_ALIGN_CENTER, 0, start_y + (btn_height + btn_spacing) * 2 - lv_display_get_vertical_resolution(disp) / 2);
    lv_obj_t *joke_label = lv_label_create(joke_btn);
    lv_label_set_text(joke_label, LV_SYMBOL_EYE_OPEN" Jokes");
    lv_obj_center(joke_label);
    lv_obj_add_event_cb(joke_btn, joke_ui_event_cb, LV_EVENT_CLICKED, disp);

    // 创建麦克风功能按钮
    lv_obj_t *microphone_btn = lv_btn_create(scr);
    lv_obj_set_size(microphone_btn, btn_width, btn_height);
    lv_obj_align(microphone_btn, LV_ALIGN_CENTER, 0, start_y + (btn_height + btn_spacing) * 3 - lv_display_get_vertical_resolution(disp) / 2);
    lv_obj_t *microphone_label = lv_label_create(microphone_btn);
    lv_label_set_text(microphone_label, "Microphone");
    lv_obj_center(microphone_label);
    lv_obj_add_event_cb(microphone_btn, microphone_ui_event_cb, LV_EVENT_CLICKED, disp);
}