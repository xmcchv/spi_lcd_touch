#ifndef SPI_LCD_TOUCH_EXAMPLE_MAIN_H
#define SPI_LCD_TOUCH_EXAMPLE_MAIN_H

#include <stdio.h>
#include <unistd.h>
#include <sys/lock.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "wifi.h"
#include "wifi_qr_config.h"
#include "esp_mac.h"
#include <stdlib.h>
#include "esp_lcd_touch.h"

//==========================================================
// WiFi相关变量声明（使用extern关键字）
extern lv_obj_t *wifi_status_label;
extern lv_obj_t *wifi_ssid_label;
extern lv_obj_t *wifi_ip_label;
extern lv_obj_t *wifi_connect_btn;
extern lv_obj_t *wifi_config_btn;
extern lv_timer_t *wifi_status_timer;

// WiFi配置界面变量声明
extern lv_obj_t *wifi_config_scr    ;
extern lv_obj_t *wifi_qrcode_obj;
extern lv_obj_t *wifi_instruction_label;
extern lv_obj_t *wifi_back_btn;
extern lv_obj_t *wifi_ssid_textarea;
extern lv_obj_t *wifi_password_textarea;
extern lv_obj_t *wifi_save_btn;

// 函数声明
void create_wifi_config_ui(lv_display_t *disp);
void wifi_connect_btn_cb(lv_event_t *e);
void wifi_config_btn_cb(lv_event_t *e);
void wifi_config_back_btn_cb(lv_event_t *e);
void wifi_save_btn_cb(lv_event_t *e);
void wifi_status_timer_cb(lv_timer_t *timer);

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
static void example_lvgl_port_update_callback(lv_display_t *disp);
static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void example_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data);
static void example_increase_lvgl_tick(void *arg);
static void example_lvgl_port_task(void *arg);
void app_main(void);
void delete_wifi_status_timer(void);

// 声明转圈动画创建和删除函数
lv_obj_t* create_loading_arc(lv_obj_t* parent, lv_obj_t* ref_obj, uint16_t size, int32_t y_offset);
void delete_loading_arc(lv_obj_t** arc);

#endif // SPI_LCD_TOUCH_EXAMPLE_MAIN_H