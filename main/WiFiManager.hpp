#ifndef WIFI_MANAGER_HPP
#define WIFI_MANAGER_HPP

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <string>
#include "esp_http_server.h"
#include "esp_mac.h"

class WiFiManager {
public:
    enum class Status {
        DISCONNECTED,
        CONNECTING,
        CONNECTED,
        FAILED
    };
    
    // WiFi配置结构体
    struct Config {
        char ssid[32];
        char password[64];
        bool is_configured;
    };
    
    static WiFiManager& getInstance();
    
    bool initialize();
    bool connect();
    bool connect(const std::string& ssid, const std::string& password);
    void disconnect();
    void saveConfig(const std::string& ssid, const std::string& password);
    void getConfig(Config& config) const;
    bool isConnected() const;
    bool isConfigured() const;


    // QR配置相关方法
    bool startQRConfig();
    void stopQRConfig();
    bool isQRConfigRunning() const { return qr_config_running_; }
    
    Status getStatus() const { return status_; }
    std::string getIP() const;
    std::string getSSID() const;
    
    void url_decode(const char* src, char* dst, size_t dst_size);
private:
    WiFiManager();
    ~WiFiManager();
    
    Status status_ = Status::DISCONNECTED;
    std::string ip_address_;
    std::string connected_ssid_;
    Config config_;
    bool qr_config_running_ = false;
    httpd_handle_t http_server_ = nullptr;
    esp_netif_t* ap_netif_ = nullptr;
    
    // QR配置相关私有方法
    bool setupAPMode();
    bool startHTTPServer();
    void stopHTTPServer();
    
    // HTTP请求处理函数
    static esp_err_t configPageHandler(httpd_req_t *req);
    static esp_err_t saveConfigHandler(httpd_req_t *req);
    
    static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
};

#endif // WIFI_MANAGER_HPP