#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

// WiFi配置结构体
typedef struct {
    char ssid[32];
    char password[64];
    bool is_configured;
} app_wifi_config_t;

// WiFi状态枚举
typedef enum {
    WIFI_DISCONNECTED,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED
} wifi_status_t;

// WiFi管理函数声明
void wifi_init(void);
void wifi_connect(const char *ssid, const char *password);
void wifi_disconnect(void);
wifi_status_t wifi_get_status(void);
void wifi_get_config(app_wifi_config_t *config);
void wifi_save_config(const char *ssid, const char *password);
bool wifi_is_connected(void);
char* wifi_get_ip(void);

// WiFi事件回调
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

#endif