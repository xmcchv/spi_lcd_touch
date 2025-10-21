#include "wifi.h"

static const char *TAG = "wifi";

// WiFi状态变量
static wifi_status_t wifi_status = WIFI_DISCONNECTED;
static app_wifi_config_t wifi_config = {0};
static char wifi_ip[16] = "0.0.0.0";

// WiFi初始化
void wifi_init(void) {
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化TCP/IP栈
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 创建默认WiFi STA
    esp_netif_create_default_wifi_sta();

    // WiFi配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    // 设置WiFi模式为AP+STA，而不是单纯的STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 从NVS加载保存的配置
    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        size_t required_size = 0;
        if (nvs_get_blob(nvs_handle, "config", NULL, &required_size) == ESP_OK && required_size == sizeof(wifi_config)) {
            nvs_get_blob(nvs_handle, "config", &wifi_config, &required_size);
            ESP_LOGI(TAG, "Loaded WiFi config: SSID=%s, configured=%d", wifi_config.ssid, wifi_config.is_configured);
            
            // 如果已配置，自动连接
            if (wifi_config.is_configured) {
                wifi_connect(wifi_config.ssid, wifi_config.password);
            }
        }
        nvs_close(nvs_handle);
    }

    ESP_LOGI(TAG, "WiFi initialized");
}

// WiFi连接
void wifi_connect(const char *ssid, const char *password) {
    if (wifi_status == WIFI_CONNECTING || wifi_status == WIFI_CONNECTED) {
        ESP_LOGI(TAG, "WiFi is already connecting/connected");
        return;
    }

    wifi_config_t wifi_config_sta = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    // 设置SSID和密码
    strncpy((char*)wifi_config_sta.sta.ssid, ssid, sizeof(wifi_config_sta.sta.ssid) - 1);
    strncpy((char*)wifi_config_sta.sta.password, password, sizeof(wifi_config_sta.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta));
    ESP_ERROR_CHECK(esp_wifi_connect());

    wifi_status = WIFI_CONNECTING;
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
}

// WiFi断开连接
void wifi_disconnect(void) {
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    wifi_status = WIFI_DISCONNECTED;
    strcpy(wifi_ip, "0.0.0.0");
    ESP_LOGI(TAG, "WiFi disconnected");
}

// 获取WiFi状态
wifi_status_t wifi_get_status(void) {
    return wifi_status;
}

// 获取WiFi配置
void wifi_get_config(app_wifi_config_t *config) {
    if (config) {
        memcpy(config, &wifi_config, sizeof(app_wifi_config_t));
    }
}

// 保存WiFi配置到NVS
void wifi_save_config(const char *ssid, const char *password) {
    strncpy(wifi_config.ssid, ssid, sizeof(wifi_config.ssid) - 1);
    strncpy(wifi_config.password, password, sizeof(wifi_config.password) - 1);
    wifi_config.is_configured = true;

    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_blob(nvs_handle, "config", &wifi_config, sizeof(wifi_config));
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi config saved: SSID=%s", ssid);
    }
}

// 检查是否已连接
bool wifi_is_connected(void) {
    return wifi_status == WIFI_CONNECTED;
}

// 获取IP地址
char* wifi_get_ip(void) {
    return wifi_ip;
}

// WiFi事件处理
void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected to AP");
                wifi_status = WIFI_CONNECTED;
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected from AP");
                wifi_status = WIFI_DISCONNECTED;
                strcpy(wifi_ip, "0.0.0.0");
                
                // 自动重连
                if (wifi_config.is_configured) {
                    esp_wifi_connect();
                    wifi_status = WIFI_CONNECTING;
                }
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "Got IP: %s", wifi_ip);
            wifi_status = WIFI_CONNECTED;
        }
    }
}