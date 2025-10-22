#include "JokeService.hpp"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "WiFiManager.hpp"

static const char *TAG = "JokeService";

// 静态成员初始化
const char* JokeService::joke_url_ = "https://v2.jokeapi.dev/joke/Programming?type=twopart";
JokeService::RequestContext* JokeService::current_context_ = nullptr;
SemaphoreHandle_t JokeService::task_mutex_ = nullptr;

JokeService& JokeService::getInstance() {
    static JokeService instance;
    return instance;
}

JokeService::JokeService() {
    // 构造函数
    task_mutex_ = xSemaphoreCreateMutex();
}

JokeService::~JokeService() {
    // 析构函数
    cancelCurrentRequest();
    if (task_mutex_) {
        vSemaphoreDelete(task_mutex_);
        task_mutex_ = nullptr;
    }
}

// 取消当前请求
void JokeService::cancelCurrentRequest() {
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(1000))) {
        if (current_context_ && current_context_->task_handle) {
            vTaskDelete(current_context_->task_handle);
            current_context_->task_handle = nullptr;
            
            // 清理上下文
            if (current_context_->mutex) {
                vSemaphoreDelete(current_context_->mutex);
            }
            delete current_context_;
            current_context_ = nullptr;
        }
        xSemaphoreGive(task_mutex_);
    }
}

// 检查是否有请求正在进行
bool JokeService::isRequestInProgress() {
    bool in_progress = false;
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(100))) {
        in_progress = (current_context_ != nullptr && current_context_->task_handle != nullptr);
        xSemaphoreGive(task_mutex_);
    }
    return in_progress;
}

bool JokeService::initialize() {
    ESP_LOGI(TAG, "JokeService initialized");
    return true;
}

// 同步获取笑话的方法
std::string JokeService::getJoke() {
    // 如果已经有任务在运行，先取消它
    if (isRequestInProgress()) {
        cancelCurrentRequest();
    }

    // 检查WiFi连接状态
    WiFiManager& wifi_manager = WiFiManager::getInstance();
    if (!wifi_manager.isConnected()) {
        ESP_LOGE(TAG, "WiFi is not connected, cannot make HTTP request");
        return "WiFi not connected";
    }

    // 创建请求上下文
    RequestContext context;
    context.response_len = 0;
    context.request_complete = false;
    context.success = false;
    context.mutex = xSemaphoreCreateMutex();
    
    if (context.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return "Internal error";
    }

    // 设置回调函数
    context.callback = [&](const std::string& joke, bool success) {
        xSemaphoreTake(context.mutex, portMAX_DELAY);
        context.request_complete = true;
        context.success = success;
        if (success) {
            context.response_data = joke;
        }
        xSemaphoreGive(context.mutex);
    };

    // 在单独的线程中执行HTTP请求
    current_context_ = &context;
    xTaskCreate(httpRequestTask, "Joke HTTP Request", 8192, &context, 5, &context.task_handle);

    // 等待请求完成（最多30秒）
    TickType_t timeout = pdMS_TO_TICKS(30000);
    while (!context.request_complete) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (timeout > 0) {
            timeout -= pdMS_TO_TICKS(100);
            if (timeout <= 0) {
                ESP_LOGE(TAG, "HTTP request timeout");
                break;
            }
        }
    }

    // 清理
    vSemaphoreDelete(context.mutex);
    current_context_ = nullptr;

    if (context.success) {
        return context.response_data;
    } else {
        return "Failed to get joke";
    }
}

void JokeService::getProgrammingJoke(JokeCallback callback) {
    // 如果已经有任务在运行，先取消它
    if (isRequestInProgress()) {
        cancelCurrentRequest();
    }

    // 检查WiFi连接状态
    WiFiManager& wifi_manager = WiFiManager::getInstance();
    if (!wifi_manager.isConnected()) {
        ESP_LOGE(TAG, "WiFi is not connected, cannot make HTTP request");
        callback("WiFi not connected", false);
        return;
    }

    // 创建请求上下文
    RequestContext* context = new RequestContext();
    context->callback = callback;
    context->response_len = 0;
    context->request_complete = false;
    context->success = false;
    context->mutex = xSemaphoreCreateMutex();

    if (context->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        callback("Internal error", false);
        delete context;
        return;
    }

    // 在单独的线程中执行HTTP请求
    current_context_ = context;
    xTaskCreate(httpRequestTask, "Joke HTTP Request", 8192, context, 5, &context->task_handle);
}

// HTTP事件处理函数
esp_err_t JokeService::httpEventHandler(esp_http_client_event_t* event) {
    if (current_context_ == nullptr) {
        return ESP_OK;
    }

    switch (event->event_id) {
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", event->data_len);
            // 追加响应数据
            xSemaphoreTake(current_context_->mutex, portMAX_DELAY);
            current_context_->response_data.append((char*)event->data, event->data_len);
            current_context_->response_len += event->data_len;
            xSemaphoreGive(current_context_->mutex);
            break;
            
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
            
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

// HTTP请求任务
void JokeService::httpRequestTask(void* parameter) {
    RequestContext* context = static_cast<RequestContext*>(parameter);
    esp_http_client_handle_t client = NULL;
    
    if (context == nullptr) {
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting HTTP request to: %s", joke_url_);

    esp_http_client_config_t config = {};
    config.url = joke_url_;
    config.event_handler = httpEventHandler;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 30000;
    config.buffer_size = 4096;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        context->callback("Failed to initialize HTTP client", false);
    } else {
        // 设置HTTP头部
        esp_http_client_set_header(client, "User-Agent", "ESP32-JokeApp/1.0");
        esp_http_client_set_header(client, "Accept", "application/json");
        esp_http_client_set_header(client, "Connection", "close");
        
        // 执行HTTP请求
        esp_err_t err = esp_http_client_perform(client);
        
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);
            
            if (status_code == 200 && !context->response_data.empty()) {
                ESP_LOGI(TAG, "Response received, length: %d", context->response_len);
                
                // 解析笑话
                std::string joke_text;
                if (parseJokeResponse(context->response_data.c_str(), joke_text)) {
                    context->callback(joke_text, true);
                    context->success = true;
                } else {
                    ESP_LOGE(TAG, "Failed to parse joke from response");
                    context->callback("Failed to parse joke", false);
                }
            } else {
                ESP_LOGE(TAG, "HTTP request failed with status: %d", status_code);
                char error_msg[64];
                snprintf(error_msg, sizeof(error_msg), "HTTP error: %d", status_code);
                context->callback(error_msg, false);
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
            context->callback("Network error", false);
        }
    }
    
    // 清理
    if (client) {
        esp_http_client_cleanup(client);
    }
    
    context->request_complete = true;
    
    // 清理上下文
    if (context->mutex) {
        vSemaphoreDelete(context->mutex);
    }
    delete context;
    
    // 清理任务句柄
    if (xSemaphoreTake(task_mutex_, pdMS_TO_TICKS(100))) {
        if (current_context_ == context) {
            current_context_->task_handle = nullptr;
            if (context->request_complete) {
                current_context_ = nullptr;
            }
        }
        xSemaphoreGive(task_mutex_);
    }
    
    vTaskDelete(NULL);
}

// 解析笑话JSON响应
bool JokeService::parseJokeResponse(const char* json_str, std::string& joke_text) {
    cJSON* root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }
    
    bool success = false;
    
    // 检查错误字段
    cJSON* error = cJSON_GetObjectItem(root, "error");
    if (error != NULL && cJSON_IsTrue(error)) {
        joke_text = "API Error: Failed to get joke";
        cJSON_Delete(root);
        return false;
    }
    
    // 获取笑话类型
    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (type == NULL || !cJSON_IsString(type)) {
        joke_text = "Invalid response format";
        cJSON_Delete(root);
        return false;
    }
    
    // 根据类型解析笑话
    if (strcmp(type->valuestring, "single") == 0) {
        cJSON* joke = cJSON_GetObjectItem(root, "joke");
        if (joke != NULL && cJSON_IsString(joke)) {
            joke_text = joke->valuestring;
            success = true;
        }
    } else if (strcmp(type->valuestring, "twopart") == 0) {
        cJSON* setup = cJSON_GetObjectItem(root, "setup");
        cJSON* delivery = cJSON_GetObjectItem(root, "delivery");
        if (setup != NULL && cJSON_IsString(setup) && 
            delivery != NULL && cJSON_IsString(delivery)) {
            joke_text = std::string(setup->valuestring) + "\n\n" + delivery->valuestring;
            success = true;
        }
    }
    
    if (!success) {
        joke_text = "Could not parse joke data";
    }

    cJSON_Delete(root);
    return success;
}