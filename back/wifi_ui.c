/***
 * @brief 创建WiFi状态UI
 * @author xmcchv
 * @date 2025-10-19
 * 
 */

#include "lvgl.h"
#include "esp_log.h"
#include "wifi.h"

#include "wifi_ui.h"
#include "spi_lcd_touch_example_main.h"
#include "lvgl_main_ui.h"

static const char *TAG = "UI_WIFI";

void wifi_ui(lv_display_t *disp) {
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clean(scr);

    // 计算自适应尺寸
    int btn_width = CALC_BTN_WIDTH(disp);
    int btn_height = CALC_BTN_HEIGHT(disp);
    int btn_spacing = CALC_BTN_SPACING(disp);
    int margin = btn_spacing / 2; // 边距
    
    ESP_LOGI(TAG, "WiFi界面 - 屏幕分辨率: %dx%d", LCD_H_RES, LCD_V_RES);

    // 创建WiFi状态标签 - 自适应位置
    wifi_status_label = lv_label_create(scr);
    lv_label_set_text(wifi_status_label, "status: initializing...");
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_LEFT, margin, margin);
    
    // 创建IP地址标签 - 自适应位置
    wifi_ip_label = lv_label_create(scr);
    lv_label_set_text(wifi_ip_label, "IP: unknown");
    lv_obj_align(wifi_ip_label, LV_ALIGN_TOP_LEFT, margin, margin + btn_height);
    
    // 创建SSID标签 - 自适应位置
    wifi_ssid_label = lv_label_create(scr);
    app_wifi_config_t config;
    wifi_get_config(&config);
    if (config.is_configured) {
        lv_label_set_text_fmt(wifi_ssid_label, "SSID: %s", config.ssid);
    } else {
        lv_label_set_text(wifi_ssid_label, "SSID: unknown");
    }
    lv_obj_align(wifi_ssid_label, LV_ALIGN_TOP_LEFT, margin, margin + btn_height * 2);
    
    // 创建连接按钮 - 自适应尺寸和位置
    wifi_connect_btn = lv_btn_create(scr);
    lv_obj_set_size(wifi_connect_btn, btn_width, btn_height);
    lv_obj_align(wifi_connect_btn, LV_ALIGN_TOP_RIGHT, -margin, margin);
    lv_obj_t *connect_label = lv_label_create(wifi_connect_btn);
    lv_label_set_text(connect_label, "connect WiFi");
    lv_obj_center(connect_label);
    lv_obj_add_event_cb(wifi_connect_btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, disp);
    
    // 创建配置按钮 - 自适应尺寸和位置
    wifi_config_btn = lv_btn_create(scr);
    lv_obj_set_size(wifi_config_btn, btn_width, btn_height);
    lv_obj_align(wifi_config_btn, LV_ALIGN_TOP_RIGHT, -margin, margin + btn_height + margin);
    lv_obj_t *config_label = lv_label_create(wifi_config_btn);
    lv_label_set_text(config_label, "config WiFi");
    lv_obj_center(config_label);
    lv_obj_add_event_cb(wifi_config_btn, wifi_config_btn_cb, LV_EVENT_CLICKED, disp);

    // 创建返回按钮 - 自适应尺寸和位置
    lv_obj_t *back_btn = lv_btn_create(scr);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -margin, -margin);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT"back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, back_mainui_cb, LV_EVENT_CLICKED, disp);

    // 创建WiFi状态更新定时器
    delete_wifi_status_timer();
    wifi_status_timer = lv_timer_create(wifi_status_timer_cb, 1000, disp);
}

