#include "QRConfigManager.hpp"
#include "WiFiManager.hpp"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "QRConfigManager";

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

// 配置页面HTML
const char* QRConfigManager::CONFIG_HTML = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<title>WiFi Configuration</title>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<style>"
"body { font-family: Arial, sans-serif; margin: 20px; }"
".container { max-width: 400px; margin: 0 auto; }"
".form-group { margin-bottom: 15px; }"
"label { display: block; margin-bottom: 5px; }"
"input { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; }"
"button { background-color: #007bff; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; }"
"button:hover { background-color: #0056b3; }"
"</style>"
"</head>"
"<body>"
"<div class=\"container\">"
"<h2>WiFi Configuration</h2>"
"<form action=\"/save\" method=\"post\">"
"<div class=\"form-group\">"
"<label for=\"ssid\">WiFi SSID:</label>"
"<input type=\"text\" id=\"ssid\" name=\"ssid\" required>"
"</div>"
"<div class=\"form-group\">"
"<label for=\"password\">WiFi Password:</label>"
"<input type=\"password\" id=\"password\" name=\"password\">"
"</div>"
"<div class=\"form-group\">"
"<label for=\"api_url\">Custom API URL (optional):</label>"
"<input type=\"text\" id=\"api_url\" name=\"api_url\" placeholder=\"https://api.example.com/endpoint\">"
"</div>"
"<button type=\"submit\">Save Configuration</button>"
"</form>"
"</div>"
"</body>"
"</html>";

// 成功页面HTML
const char* QRConfigManager::SUCCESS_HTML = 
"<!DOCTYPE html>"
"<html>"
"<head>"
"<title>Configuration Saved</title>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<style>"
"body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }"
".container { max-width: 400px; margin: 0 auto; }"
".success { color: green; font-size: 24px; margin-bottom: 20px; }"
"</style>"
"</head>"
"<body>"
"<div class=\"container\">"
"<div class=\"success\">✓ Configuration Saved</div>"
"<p>WiFi settings have been saved successfully.</p>"
"<p>The device will now attempt to connect to the configured WiFi network.</p>"
"<p>You can close this page.</p>"
"</div>"
"</body>"
"</html>";

QRConfigManager& QRConfigManager::getInstance() {
    static QRConfigManager instance;
    return instance;
}

QRConfigManager::QRConfigManager() {
    // 构造函数
}

QRConfigManager::~QRConfigManager() {
    stop();
}

bool QRConfigManager::start() {
    ESP_LOGI(TAG, "Starting QR configuration server...");
    
    // 检查是否已经运行
    if (server_ != nullptr) {
        ESP_LOGI(TAG, "QR configuration server is already running");
        return true;
    }
    
    // 初始化NVS和网络栈（如果尚未初始化）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 创建AP网络接口
    if (!ap_netif_) {
        ap_netif_ = esp_netif_create_default_wifi_ap();
        if (ap_netif_ == NULL) {
            ESP_LOGE(TAG, "Failed to create AP network interface");
            return false;
        }
    }
    
    // 配置AP的IP地址和DHCP服务器
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    
    // 停止DHCP服务器（如果正在运行）
    esp_netif_dhcps_stop(ap_netif_);
    
    // 设置IP地址
    esp_netif_set_ip_info(ap_netif_, &ip_info);
    
    // 启动DHCP服务器
    esp_netif_dhcps_start(ap_netif_);
    
    // 设置WiFi为AP+STA模式
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    
    // 设置AP配置
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = 0,
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN,
            .ssid_hidden = 0,
            .max_connection = 4,
            .beacon_interval = 100
        }
    };
    
    // 设置AP的SSID（使用设备MAC地址）
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf((char*)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), 
             "ESP32-WiFi-Config-%02X%02X", mac[4], mac[5]);
    
    // 设置AP配置
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    
    // 启动HTTP服务器
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    
    if (httpd_start(&server_, &config) == ESP_OK) {
        // 注册处理函数
        httpd_uri_t config_uri = {
            .uri = "/configure",
            .method = HTTP_GET,
            .handler = configPageHandler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &config_uri);
        
        httpd_uri_t save_uri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = saveConfigHandler,
            .user_ctx = this
        };
        httpd_register_uri_handler(server_, &save_uri);
        
        ESP_LOGI(TAG, "QR configuration server started successfully");
        ESP_LOGI(TAG, "AP SSID: %s", wifi_config.ap.ssid);
        ESP_LOGI(TAG, "AP IP: 192.168.4.1");
        ESP_LOGI(TAG, "Configuration URL: http://192.168.4.1/configure");
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return false;
    }
}

void QRConfigManager::stop() {
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
    
    // 停止DHCP服务器
    if (ap_netif_ != nullptr) {
        esp_netif_dhcps_stop(ap_netif_);
        esp_netif_destroy_default_wifi(ap_netif_);
        ap_netif_ = nullptr;
        ESP_LOGI(TAG, "AP network interface stopped");
    }
    
    ESP_LOGI(TAG, "QR configuration server stopped");
}

esp_err_t QRConfigManager::configPageHandler(httpd_req_t *req) {
    QRConfigManager* manager = static_cast<QRConfigManager*>(req->user_ctx);
    if (!manager) {
        return ESP_FAIL;
    }
    
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, CONFIG_HTML, strlen(CONFIG_HTML));
}

esp_err_t QRConfigManager::saveConfigHandler(httpd_req_t *req) {
    QRConfigManager* manager = static_cast<QRConfigManager*>(req->user_ctx);
    if (!manager) {
        return ESP_FAIL;
    }
    
    char buf[512];
    int ret, remaining = req->content_len;
    
    // 读取POST数据
    if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)))) <= 0) {
        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            httpd_resp_send_408(req);
        }
        return ESP_FAIL;
    }
    
    buf[ret] = '\0';
    
    // 解析表单数据
    char ssid[32] = {0};
    char password[64] = {0};
    char api_url[128] = {0};
    
    // 简单的表单解析
    char *token = strtok(buf, "&");
    while (token != NULL) {
        if (strncmp(token, "ssid=", 5) == 0) {
            urlDecode(token + 5, ssid, sizeof(ssid));
        } else if (strncmp(token, "password=", 9) == 0) {
            urlDecode(token + 9, password, sizeof(password));
        } else if (strncmp(token, "api_url=", 8) == 0) {
            urlDecode(token + 8, api_url, sizeof(api_url));
        }
        token = strtok(NULL, "&");
    }
    
    // 保存WiFi配置
    if (strlen(ssid) > 0) {
        WiFiManager& wifiManager = WiFiManager::getInstance();
        wifiManager.saveConfig(ssid, password);
        wifiManager.connect(ssid, password);
        
        // 保存自定义API URL到NVS
        if (strlen(api_url) > 0) {
            nvs_handle_t nvs_handle;
            if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                nvs_set_str(nvs_handle, "api_url", api_url);
                nvs_commit(nvs_handle);
                nvs_close(nvs_handle);
                ESP_LOGI(TAG, "Custom API URL saved: %s", api_url);
            }
        }
        
        ESP_LOGI(TAG, "WiFi configuration saved: SSID=%s", ssid);
    }
    
    // 返回成功页面
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, SUCCESS_HTML, strlen(SUCCESS_HTML));
}

void QRConfigManager::urlDecode(const char *src, char *dst, size_t dst_size) {
    size_t i = 0, j = 0;
    while (src[i] && j < dst_size - 1) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            char hex[3] = {src[i+1], src[i+2], 0};
            dst[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
}