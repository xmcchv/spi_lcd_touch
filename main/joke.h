#ifndef JOKE_H
#define JOKE_H

#include "lvgl.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "esp_tls.h"

// 笑话界面函数声明
void joke_ui(lv_display_t *disp);

#endif // JOKE_H