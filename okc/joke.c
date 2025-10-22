#include "wifi_ui.h"
#include "joke.h"
#include "lvgl_main_ui.h"
#include "spi_lcd_touch_example_main.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "JOKE";
static lv_obj_t *joke_label = NULL;
static lv_obj_t *joke_loading = NULL;
lv_obj_t *scr = NULL;
// 修改为HTTP URL
static const char *joke_url = "https://v2.jokeapi.dev/joke/Programming?type=twopart";

// 全局变量存储响应数据
static char *response_data = NULL;
static size_t response_len = 0;
static SemaphoreHandle_t joke_mutex = NULL;
static TaskHandle_t joke_task_handle = NULL;

// HTTP 事件处理函数
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
            
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
            
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // 分配内存存储响应数据
            char *new_data = realloc(response_data, response_len + evt->data_len + 1);
            if (new_data == NULL) {
                ESP_LOGE(TAG, "Failed to allocate memory for response data");
                return ESP_FAIL;
            }
            response_data = new_data;
            memcpy(response_data + response_len, evt->data, evt->data_len);
            response_len += evt->data_len;
            response_data[response_len] = '\0';
            break;
            
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
            
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;
            
        default:
            break;
    }
    return ESP_OK;
}

// 解析笑话JSON响应
static bool parse_joke_response(const char *json_str, char *joke_text, size_t joke_text_len)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", json_str);
        return false;
    }
    
    bool success = false;
    
    // 检查错误字段
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error != NULL && cJSON_IsTrue(error)) {
        snprintf(joke_text, joke_text_len, "API Error: Failed to get joke");
        goto cleanup;
    }
    
    // 获取笑话类型
    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (type == NULL || !cJSON_IsString(type)) {
        snprintf(joke_text, joke_text_len, "Invalid response format");
        goto cleanup;
    }
    
    // 根据类型解析笑话
    if (strcmp(type->valuestring, "single") == 0) {
        cJSON *joke = cJSON_GetObjectItem(root, "joke");
        if (joke != NULL && cJSON_IsString(joke)) {
            snprintf(joke_text, joke_text_len, "%s", joke->valuestring);
            success = true;
        }
    } else if (strcmp(type->valuestring, "twopart") == 0) {
        cJSON *setup = cJSON_GetObjectItem(root, "setup");
        cJSON *delivery = cJSON_GetObjectItem(root, "delivery");
        if (setup != NULL && cJSON_IsString(setup) && 
            delivery != NULL && cJSON_IsString(delivery)) {
            snprintf(joke_text, joke_text_len, "%s\n\n%s", 
                    setup->valuestring, delivery->valuestring);
            success = true;
        }
    }
    
    if (!success) {
        snprintf(joke_text, joke_text_len, "Could not parse joke data");
    }

cleanup:
    cJSON_Delete(root);
    return success;
}

// HTTP请求任务（在单独的线程中执行）
static void http_request_task(void *pvParameter)
{
    // 清理之前的响应数据
    if (response_data != NULL) {
        free(response_data);
        response_data = NULL;
    }
    response_len = 0;

    ESP_LOGI(TAG, "Starting HTTP request to: %s", joke_url);

    esp_http_client_config_t config = {
        .url = joke_url,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_GET,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        goto cleanup;
    }
    
    // 设置 HTTP 头部
    esp_http_client_set_header(client, "User-Agent", "ESP32-JokeApp/1.0");
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Connection", "close");
    
    // 执行 HTTP 请求
    esp_err_t err = esp_http_client_perform(client);
    
    delete_loading_arc(&joke_loading);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTP Status Code: %d", status_code);
        
        if (status_code == 200 && response_data != NULL && response_len > 0) {
            ESP_LOGI(TAG, "Response received, length: %d", response_len);
            
            // 解析笑话
            char joke_text[512];
            if (parse_joke_response(response_data, joke_text, sizeof(joke_text))) {
                if (joke_label) {
                    lv_label_set_text(joke_label, joke_text);
                }
                ESP_LOGI(TAG, "Joke displayed successfully");
            } else {
                ESP_LOGE(TAG, "Failed to parse joke from response");
                if (joke_label) {
                    lv_label_set_text(joke_label, "Failed to parse joke");
                }
            }
        } else if (status_code == 301) {
            // 服务器要求使用HTTPS
            ESP_LOGI(TAG, "Server redirects to HTTPS (not supported)");
            if (joke_label) {
                lv_label_set_text(joke_label, "Server requires HTTPS\n(Feature not supported)");
            } 
        } else {
            ESP_LOGE(TAG, "HTTP request failed with status: %d, response_len: %d", status_code, response_len);
            if (joke_label) {
                char error_msg[64];
                snprintf(error_msg, sizeof(error_msg), "HTTP error: %d", status_code);
                lv_label_set_text(joke_label, error_msg);
            }
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        if (joke_label) {
            lv_label_set_text(joke_label, "Network error");
        }
    }
    
cleanup:
    // 清理
    if (client) {
        esp_http_client_cleanup(client);
    }
    
    
    // 删除任务
    joke_task_handle = NULL;
    vTaskDelete(NULL);
}

static void get_joke(void)
{
    // 检查WiFi连接状态
    if (!wifi_is_connected()) {
        ESP_LOGE(TAG, "WiFi is not connected, cannot make HTTP request");
        if (joke_label) {
            lv_label_set_text(joke_label, "WiFi not connected");
        }
        return;
    }

    // 如果已经有任务在运行，先删除它
    if (joke_task_handle != NULL) {
        vTaskDelete(joke_task_handle);
        joke_task_handle = NULL;
    }

    if (joke_label) {
        lv_label_set_text(joke_label, "Loading joke...");
        // 为joke_loading 绘制转圈动画
        joke_loading = create_loading_arc(scr, joke_label, 30, 10);
    }

    // 创建互斥锁（如果还没有创建）
    if (joke_mutex == NULL) {
        joke_mutex = xSemaphoreCreateMutex();
    }

    // 在单独的线程中执行HTTP请求
    xTaskCreate(http_request_task, "HTTP Request", 8192, NULL, 5, &joke_task_handle);
}

// 刷新按钮回调
static void refresh_btn_cb(lv_event_t *e)
{
    get_joke();
}

// 创建笑话界面
void joke_ui(lv_display_t *disp)
{
    scr = lv_display_get_screen_active(disp);
    lv_obj_clean(scr);

    // 根据旋转状态计算自适应尺寸
    int btn_width = CALC_BTN_WIDTH(disp);
    int btn_height = CALC_BTN_HEIGHT(disp);
    int btn_spacing = CALC_BTN_SPACING(disp);
    int margin = btn_spacing / 2;

    ESP_LOGI(TAG, "笑话界面 - 当前旋转: %d, 按钮尺寸: %dx%d", 
             lv_display_get_rotation(disp), btn_width, btn_height);

    // 创建标题
    lv_obj_t *title_label = lv_label_create(scr);
    lv_label_set_text(title_label, "PROGRAMMING JOKES");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, margin);

    // 创建笑话显示区域
    joke_label = lv_label_create(scr);
    lv_label_set_text(joke_label, "Press refresh to get a joke!");
    lv_obj_set_width(joke_label, lv_display_get_horizontal_resolution(disp) - margin * 2);
    lv_obj_align(joke_label, LV_ALIGN_TOP_MID, 0, margin + btn_height * 2);
    lv_label_set_long_mode(joke_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(joke_label, LV_TEXT_ALIGN_CENTER, 0);

    // 创建刷新按钮
    lv_obj_t *refresh_btn = lv_btn_create(scr);
    lv_obj_set_size(refresh_btn, btn_width, btn_height);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_LEFT, margin, -margin);
    lv_obj_t *refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH" Refresh");
    lv_obj_center(refresh_label);
    lv_obj_add_event_cb(refresh_btn, refresh_btn_cb, LV_EVENT_CLICKED, NULL);

    // 创建返回按钮
    lv_obj_t *back_btn = lv_btn_create(scr);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -margin, -margin);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT" Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, back_mainui_cb, LV_EVENT_CLICKED, disp);

    get_joke();
}