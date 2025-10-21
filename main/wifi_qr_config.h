#ifndef WIFI_QR_CONFIG_H
#define WIFI_QR_CONFIG_H

#include "esp_http_server.h"

// 启动WiFi二维码配置服务器
void wifi_qr_config_start(void);

// 停止WiFi二维码配置服务器
void wifi_qr_config_stop(void);

#endif // WIFI_QR_CONFIG_H