#ifndef __MICROPHONE_H__
#define __MICROPHONE_H__

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"

// 麦克风功能头文件
#include "lvgl.h"
#include "lvgl_main_ui.h"
#include "esp_log.h"

void microphone_init(void);
void microphone_deinit(void);
void microphone_ui(lv_display_t *disp);

void microphone_back_mainui_cb(lv_event_t * e);

#endif // __MICROPHONE_H__
