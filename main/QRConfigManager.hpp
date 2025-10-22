#ifndef QR_CONFIG_MANAGER_HPP
#define QR_CONFIG_MANAGER_HPP

#include <string>
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_netif.h"

class QRConfigManager {
public:
    static QRConfigManager& getInstance();
    
    bool start();
    void stop();
    bool isRunning() const { return server_ != nullptr; }
    
private:
    QRConfigManager();
    ~QRConfigManager();
    
    // 禁止拷贝和赋值
    QRConfigManager(const QRConfigManager&) = delete;
    QRConfigManager& operator=(const QRConfigManager&) = delete;
    
    // HTTP服务器处理函数
    static esp_err_t configPageHandler(httpd_req_t *req);
    static esp_err_t saveConfigHandler(httpd_req_t *req);
    static void urlDecode(const char *src, char *dst, size_t dst_size);
    
    // HTML页面内容
    static const char* CONFIG_HTML;
    static const char* SUCCESS_HTML;
    
    httpd_handle_t server_ = nullptr;
    esp_netif_t* ap_netif_ = nullptr;
};

#endif // QR_CONFIG_MANAGER_HPP