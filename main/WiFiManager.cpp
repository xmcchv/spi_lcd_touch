#include "WiFiManager.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"  // 添加这个头文件
#include <string>
#include <cstring>

static const char *TAG = "WiFiManager";

WiFiManager& WiFiManager::getInstance() {
    static WiFiManager instance;
    return instance;
}

WiFiManager::WiFiManager() {
    ip_address_ = "0.0.0.0";
    connected_ssid_ = "";
    config_ = {0};
}

WiFiManager::~WiFiManager() {
    disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
}

bool WiFiManager::initialize() {
    ESP_LOGI(TAG, "Initializing WiFi manager...");
    
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
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifiEventHandler, this, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifiEventHandler, this, NULL));

    // 设置WiFi模式为AP+STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 从NVS加载保存的配置
    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        size_t required_size = 0;
        if (nvs_get_blob(nvs_handle, "config", NULL, &required_size) == ESP_OK && required_size == sizeof(config_)) {
            nvs_get_blob(nvs_handle, "config", &config_, &required_size);
            ESP_LOGI(TAG, "Loaded WiFi config: SSID=%s, configured=%d", config_.ssid, config_.is_configured);
            
            // 如果已配置，自动连接
            if (config_.is_configured) {
                connect(config_.ssid, config_.password);
            }
        }
        nvs_close(nvs_handle);
    }

    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return true;
}

bool WiFiManager::isConfigured() const {
    return config_.is_configured;
}

bool WiFiManager::connect() {
    if (!isConfigured()) {
        ESP_LOGI(TAG, "WiFi is not configured");
        return false;
    }
    return connect(config_.ssid, config_.password);
}

bool WiFiManager::connect(const std::string& ssid, const std::string& password) {
    if (status_ == Status::CONNECTING || status_ == Status::CONNECTED) {
        ESP_LOGI(TAG, "WiFi is already connecting/connected");
        return false;
    }

    wifi_config_t wifi_config_sta = {};
    wifi_config_sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    // 设置SSID和密码
    strncpy((char*)wifi_config_sta.sta.ssid, ssid.c_str(), sizeof(wifi_config_sta.sta.ssid) - 1);
    strncpy((char*)wifi_config_sta.sta.password, password.c_str(), sizeof(wifi_config_sta.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config_sta));
    ESP_ERROR_CHECK(esp_wifi_connect());

    status_ = Status::CONNECTING;
    connected_ssid_ = ssid;
    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid.c_str());
    return true;
}

void WiFiManager::disconnect() {
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    status_ = Status::DISCONNECTED;
    ip_address_ = "0.0.0.0";
    ESP_LOGI(TAG, "WiFi disconnected");
}

void WiFiManager::saveConfig(const std::string& ssid, const std::string& password) {
    strncpy(config_.ssid, ssid.c_str(), sizeof(config_.ssid) - 1);
    strncpy(config_.password, password.c_str(), sizeof(config_.password) - 1);
    config_.is_configured = true;

    nvs_handle_t nvs_handle;
    if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_blob(nvs_handle, "config", &config_, sizeof(config_));
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(TAG, "WiFi config saved: SSID=%s", ssid.c_str());
    }
}

void WiFiManager::getConfig(Config& config) const {
    memcpy(&config, &config_, sizeof(Config));
}

bool WiFiManager::isConnected() const {
    return status_ == Status::CONNECTED;
}

std::string WiFiManager::getIP() const {
    return ip_address_;
}

std::string WiFiManager::getSSID() const {
    return connected_ssid_;
}

void WiFiManager::wifiEventHandler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data) {
    WiFiManager* manager = static_cast<WiFiManager*>(arg);
    if (!manager) return;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi STA started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "WiFi connected to AP");
                manager->status_ = Status::CONNECTED;
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "WiFi disconnected from AP");
                manager->status_ = Status::DISCONNECTED;
                manager->ip_address_ = "0.0.0.0";
                
                // 自动重连
                if (manager->config_.is_configured) {
                    esp_wifi_connect();
                    manager->status_ = Status::CONNECTING;
                }
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            manager->ip_address_ = ip_str;
            ESP_LOGI(TAG, "Got IP: %s", ip_str);
            manager->status_ = Status::CONNECTED;
        }
    }
}

// QR配置相关方法实现

// 启动QR配置
bool WiFiManager::startQRConfig() {
    if (qr_config_running_) {
        ESP_LOGI(TAG, "QR配置服务器已经在运行");
        return true;
    }
    
    ESP_LOGI(TAG, "启动QR配置服务器");
    
    // 设置AP模式
    if (!setupAPMode()) {
        ESP_LOGE(TAG, "设置AP模式失败");
        return false;
    }
    
    // 启动HTTP服务器
    if (!startHTTPServer()) {
        ESP_LOGE(TAG, "启动HTTP服务器失败");
        return false;
    }
    
    qr_config_running_ = true;
    ESP_LOGI(TAG, "QR配置服务器启动成功");
    return true;
}

// 停止QR配置
void WiFiManager::stopQRConfig() {
    if (!qr_config_running_) {
        return;
    }
    
    ESP_LOGI(TAG, "停止QR配置服务器");
    
    // 停止HTTP服务器
    stopHTTPServer();
    
    // 重置AP模式
    if (ap_netif_) {
        esp_netif_destroy(ap_netif_);
        ap_netif_ = nullptr;
    }
    
    qr_config_running_ = false;
    ESP_LOGI(TAG, "QR配置服务器已停止");
}

// 设置AP模式
bool WiFiManager::setupAPMode() {
    ESP_LOGI(TAG, "设置AP模式");
    
    // 创建AP网络接口
    ap_netif_ = esp_netif_create_default_wifi_ap();
    if (ap_netif_ == nullptr) {
        ESP_LOGE(TAG, "创建AP网络接口失败");
        return false;
    }
    
    // 配置AP的IP地址和DHCP服务器
    esp_netif_ip_info_t ip_info;
    ip_info.ip.addr = PP_HTONL(LWIP_MAKEU32(192, 168, 4, 1));
    ip_info.gw.addr = PP_HTONL(LWIP_MAKEU32(192, 168, 4, 1));
    ip_info.netmask.addr = PP_HTONL(LWIP_MAKEU32(255, 255, 255, 0));
    
    // 停止DHCP服务器（如果正在运行）
    esp_netif_dhcps_stop(ap_netif_);
    
    // 设置IP地址
    esp_netif_set_ip_info(ap_netif_, &ip_info);
    
    // 启动DHCP服务器
    esp_netif_dhcps_start(ap_netif_);
    
    // 设置WiFi为AP+STA模式
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    
    // 设置AP配置
    wifi_config_t wifi_config = {};
    wifi_config.ap.ssid_len = 0;
    wifi_config.ap.channel = 1;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.ssid_hidden = 0;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.beacon_interval = 100;
    
    // 设置AP的SSID（使用设备MAC地址）
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    
    snprintf((char*)wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid), 
             "ESP32_%02X%02X%02X", mac[3], mac[4], mac[5]);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
    ESP_LOGI(TAG, "AP模式设置完成，SSID: %s", wifi_config.ap.ssid);
    return true;
}

// 启动HTTP服务器
bool WiFiManager::startHTTPServer() {
    ESP_LOGI(TAG, "启动HTTP服务器");
    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 16;
    
    esp_err_t ret = httpd_start(&http_server_, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动HTTP服务器失败: %s", esp_err_to_name(ret));
        return false;
    }
    
    // 注册URI处理函数
    httpd_uri_t config_uri = {};
    config_uri.uri = "/";
    config_uri.method = HTTP_GET;
    config_uri.handler = configPageHandler;
    config_uri.user_ctx = this;
    httpd_register_uri_handler(http_server_, &config_uri);
    
    httpd_uri_t save_uri = {};
    save_uri.uri = "/save";
    save_uri.method = HTTP_POST;
    save_uri.handler = saveConfigHandler;
    save_uri.user_ctx = this;
    httpd_register_uri_handler(http_server_, &save_uri);
    
    ESP_LOGI(TAG, "HTTP服务器启动成功");
    return true;
}

// 停止HTTP服务器
void WiFiManager::stopHTTPServer() {
    if (http_server_) {
        httpd_stop(http_server_);
        http_server_ = nullptr;
        ESP_LOGI(TAG, "HTTP服务器已停止");
    }
}

// 配置页面处理函数
esp_err_t WiFiManager::configPageHandler(httpd_req_t *req) {
    WiFiManager* manager = static_cast<WiFiManager*>(req->user_ctx);
    if (!manager) {
        return ESP_FAIL;
    }
    
    const char* config_html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>WiFi Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .container { max-width: 400px; margin: 0 auto; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; }
        input { width: 100%; padding: 8px; box-sizing: border-box; }
        button { background-color: #007bff; color: white; padding: 10px 20px; border: none; cursor: pointer; }
        button:hover { background-color: #0056b3; }
    </style>
</head>
<body>
    <div class="container">
        <h2>WiFi Configuration</h2>
        <form action="/save" method="post">
            <div class="form-group">
                <label for="ssid">WiFi SSID:</label>
                <input type="text" id="ssid" name="ssid" required>
            </div>
            <div class="form-group">
                <label for="password">WiFi Password:</label>
                <input type="password" id="password" name="password">
            </div>
            <button type="submit">Save Configuration</button>
        </form>
    </div>
</body>
</html>
)";
    
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, config_html, strlen(config_html));
}

// 保存配置处理函数
esp_err_t WiFiManager::saveConfigHandler(httpd_req_t *req) {
    WiFiManager* manager = static_cast<WiFiManager*>(req->user_ctx);
    if (!manager) {
        return ESP_FAIL;
    }
    
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    content[ret] = '\0';
    
    // 解析表单数据
    char ssid[32] = {0};
    char password[64] = {0};
    
    char* token = strtok(content, "&");
    while (token != NULL) {
        if (strncmp(token, "ssid=", 5) == 0) {
            manager->url_decode(token + 5, ssid, sizeof(ssid));
        } else if (strncmp(token, "password=", 9) == 0) {
            manager->url_decode(token + 9, password, sizeof(password));
        }
        token = strtok(NULL, "&");
    }
    
    // 保存配置
    if (strlen(ssid) > 0) {
        manager->saveConfig(ssid, password);
        ESP_LOGI(TAG, "WiFi配置已保存: SSID=%s", ssid);
    }
    
    // 返回成功页面
    const char* success_html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Configuration Saved</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; text-align: center; }
        .success { color: green; font-size: 18px; margin: 20px 0; }
    </style>
</head>
<body>
    <div class="success">WiFi配置已保存！设备将自动重启并连接WiFi。</div>
</body>
</html>
)";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, success_html, strlen(success_html));
    
    // 延迟重启设备
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
    
    return ESP_OK;
}

// URL解码函数
void WiFiManager::url_decode(const char* src, char* dst, size_t dst_size) {
    size_t i = 0, j = 0;
    while (src[i] != '\0' && j < dst_size - 1) {
        if (src[i] == '%' && isxdigit(src[i+1]) && isxdigit(src[i+2])) {
            char hex[3] = {src[i+1], src[i+2], '\0'};
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