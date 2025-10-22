#ifndef JOKE_SERVICE_HPP
#define JOKE_SERVICE_HPP

#include <string>
#include <functional>
#include "esp_http_client.h"
#include "esp_tls.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

class JokeService {
public:
    using JokeCallback = std::function<void(const std::string& joke, bool success)>;
    
    static JokeService& getInstance();
    
    bool initialize();
    void getProgrammingJoke(JokeCallback callback);
    std::string getJoke(); // 同步获取笑话的方法
    
    // 任务管理相关方法
    void cancelCurrentRequest();
    bool isRequestInProgress();
    
private:
    JokeService();
    ~JokeService();
    
    // 请求上下文结构体
    struct RequestContext {
        JokeCallback callback;
        std::string response_data;
        size_t response_len;
        bool request_complete;
        bool success;
        SemaphoreHandle_t mutex;
        TaskHandle_t task_handle;
    };
    
    static esp_err_t httpEventHandler(esp_http_client_event_t* event);
    static void httpRequestTask(void* parameter);
    static bool parseJokeResponse(const char* json_str, std::string& joke_text);
    
    static const char* joke_url_;
    static RequestContext* current_context_;
    static SemaphoreHandle_t task_mutex_;
};

#endif // JOKE_SERVICE_HPP