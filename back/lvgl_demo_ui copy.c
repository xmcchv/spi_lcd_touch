/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

// This demo UI is adapted from LVGL official example: https://docs.lvgl.io/master/examples.html#loader-with-arc

#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "UI_DEMO";

static lv_obj_t * btn;
static lv_display_rotation_t rotation = LV_DISP_ROTATION_0;

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
    // current_rotation++;
    // if (current_rotation > LV_DISPLAY_ROTATION_270) {
    //     current_rotation = LV_DISPLAY_ROTATION_0;
    // }
    
    // ESP_LOGI(TAG, "Setting new rotation: %d", current_rotation);
    // lv_disp_set_rotation(disp, current_rotation);
    
    // // 验证旋转是否已应用
    // lv_display_rotation_t new_rotation = lv_display_get_rotation(disp);
    // ESP_LOGI(TAG, "Rotation after setting: %d", new_rotation);
}

static void set_angle(void * obj, int32_t v)
{
    lv_arc_set_value(obj, v);
}

void example_lvgl_demo_ui(lv_display_t *disp)
{
    lv_obj_t *scr = lv_display_get_screen_active(disp);

    btn = lv_button_create(scr);
    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text_static(lbl, LV_SYMBOL_REFRESH" ROTATE");
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 30, -30);
    /*Button event*/
    lv_obj_add_event_cb(btn, btn_cb, LV_EVENT_CLICKED, disp);

    /*Create an Arc*/
    lv_obj_t * arc = lv_arc_create(scr);
    lv_arc_set_rotation(arc, 0);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);   /*Be sure the knob is not displayed*/
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);  /*To not allow adjusting by click*/
    lv_obj_center(arc);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);    /*Just for the demo*/
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_start(&a);

    // 立即输出按钮的完整区域坐标
    lv_area_t btn_area;
    lv_obj_get_coords(btn, &btn_area);
    ESP_LOGI("UI", "ROTATE按钮完整区域:");
    ESP_LOGI("UI", "  左上角坐标: (%d, %d)", btn_area.x1, btn_area.y1);
    ESP_LOGI("UI", "  右下角坐标: (%d, %d)", btn_area.x2, btn_area.y2);
    ESP_LOGI("UI", "  按钮尺寸: %d x %d", lv_obj_get_width(btn), lv_obj_get_height(btn));
    ESP_LOGI("UI", "  按钮中心点: (%d, %d)", 
             (btn_area.x1 + btn_area.x2) / 2, 
             (btn_area.y1 + btn_area.y2) / 2);
}