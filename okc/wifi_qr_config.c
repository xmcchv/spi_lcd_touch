/*
 * WiFi QR Code Configuration Server
 * 处理手机扫描二维码后的WiFi配置请求
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"


// 添加MIN宏定义
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

static const char *TAG = "wifi_qr_config";

// 全局变量
static esp_netif_t *ap_netif = NULL;

// HTTP服务器句柄
static httpd_handle_t server = NULL;

// URL解码函数声明（放在使用之前）
static void url_decode(const char *src, char *dst, size_t dst_size);

// 配置页面HTML
static const char* CONFIG_HTML = 
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
static const char* SUCCESS_HTML = 
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

// 配置页面处理函数
static esp_err_t config_page_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, CONFIG_HTML, strlen(CONFIG_HTML));
}

// 保存配置处理函数
static esp_err_t save_config_handler(httpd_req_t *req) {
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
            url_decode(token + 5, ssid, sizeof(ssid));
        } else if (strncmp(token, "password=", 9) == 0) {
            url_decode(token + 9, password, sizeof(password));
        } else if (strncmp(token, "api_url=", 8) == 0) {
            url_decode(token + 8, api_url, sizeof(api_url));
        }
        token = strtok(NULL, "&");
    }
    
    // 保存WiFi配置
    if (strlen(ssid) > 0) {
        wifi_save_config(ssid, password);
        wifi_connect(ssid, password);
        
        // 保存自定义API URL到NVS
        if (strlen(api_url) > 0) {
            nvs_handle_t nvs_handle;
            if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) {
                nvs_set_str(nvs_handle, "api_url", api_url);
                nvs_commit(nvs_handle);
                nvs_close(nvs_handle);
            }
        }
        
        ESP_LOGI(TAG, "WiFi configuration saved: SSID=%s, API=%s", ssid, api_url);
    }
    
    // 返回成功页面
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, SUCCESS_HTML, strlen(SUCCESS_HTML));
}

// URL解码函数
static void url_decode(const char *src, char *dst, size_t dst_size) {
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

// 启动WiFi配置服务器
void wifi_qr_config_start(void) {
    // 检查是否已经创建过AP接口，如果是则不需要重复创建
    // static bool ap_netif_created = false;
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();  // 创建默认AP接口
        // ap_netif_created = true;
    }
    if (ap_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create AP network interface");
        return;
    }
    
    // 配置AP的IP地址和DHCP服务器
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    
    // 停止DHCP服务器（如果正在运行）
    esp_netif_dhcps_stop(ap_netif);
    
    // 设置IP地址
    esp_netif_set_ip_info(ap_netif, &ip_info);
    
    // 启动DHCP服务器
    esp_netif_dhcps_start(ap_netif);
    
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
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // 注册处理函数
        httpd_uri_t config_uri = {
            .uri = "/configure",
            .method = HTTP_GET,
            .handler = config_page_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &config_uri);
        
        httpd_uri_t save_uri = {
            .uri = "/save",
            .method = HTTP_POST,
            .handler = save_config_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &save_uri);
        
        ESP_LOGI(TAG, "WiFi QR configuration server started");
        ESP_LOGI(TAG, "AP SSID: %s", wifi_config.ap.ssid);
        ESP_LOGI(TAG, "AP IP: 192.168.4.1");
        ESP_LOGI(TAG, "Configuration URL: http://192.168.4.1/configure");
    }
}

// 停止WiFi配置服务器
void wifi_qr_config_stop(void) {
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    
    // 停止DHCP服务器
    if (ap_netif) {
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_destroy_default_wifi(ap_netif);  // 销毁AP接口
        ap_netif = NULL;
    }
    // esp_wifi_stop();
    // esp_wifi_set_mode(WIFI_MODE_NULL);
    
    ESP_LOGI(TAG, "WiFi QR configuration server stopped");
}