#include "microphone.h"
#include "lvgl_main_ui.h"
#include "spi_lcd_touch_example_main.h"
#include "esp_log.h"

// INMP引脚，根据自己连线修改
#define INMP_SD     GPIO_NUM_37
#define INMP_SCK    GPIO_NUM_38
#define INMP_WS     GPIO_NUM_39

// MAX98357A引脚，根据自己连线修改
#define MAX_DIN     GPIO_NUM_42
#define MAX_BCLK    GPIO_NUM_40
#define MAX_LRC     GPIO_NUM_41

// 配置rx对INMP441的采样率为44.1kHz，这是常用的人声采样率
#define SAMPLE_RATE 44100

// buf size计算方法：根据esp32官方文档，buf size = dma frame num * 声道数 * 数据位宽 / 8
#define BUF_SIZE    (1023 * 1 * 32 / 8)

// 音频buffer
uint8_t buf[BUF_SIZE];

i2s_chan_handle_t rx_handle;
i2s_chan_handle_t tx_handle;

static const char *TAG = "MICROPHONE";

// 初始化i2s rx，用于从INMP441接收数据
static void i2s_rx_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S RX...");
    
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    
    // dma frame num使用最大值，增大dma一次搬运的数据量，能够提高效率，减小杂音
    chan_cfg.dma_frame_num = 1023;
    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RX channel: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "RX channel created successfully");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        // 虽然inmp441采集数据为24bit，但是仍可使用32bit来接收
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .dout = I2S_GPIO_UNUSED,
            .bclk = INMP_SCK,
            .ws = INMP_WS,
            .din = INMP_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init RX std mode: %s", esp_err_to_name(ret));
        return;
    }

    ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RX channel: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "I2S RX initialized successfully");
    ESP_LOGI(TAG, "RX GPIO - SD: %d, SCK: %d, WS: %d", INMP_SD, INMP_SCK, INMP_WS);
}

// 初始化tx，用于向MAX98357A写数据
static void i2s_tx_init(void)
{
    ESP_LOGI(TAG, "Initializing I2S TX...");
    
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_frame_num = 1023;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create TX channel: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "TX channel created successfully");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .din = I2S_GPIO_UNUSED,
            .bclk = MAX_BCLK,
            .ws = MAX_LRC,
            .dout = MAX_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ret = i2s_channel_init_std_mode(tx_handle, &std_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init TX std mode: %s", esp_err_to_name(ret));
        return;
    }

    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable TX channel: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "I2S TX initialized successfully");
    ESP_LOGI(TAG, "TX GPIO - DIN: %d, BCLK: %d, LRC: %d", MAX_DIN, MAX_BCLK, MAX_LRC);
}

static void i2s_read_task(void *args)
{
    size_t bytes_read = 0;
    size_t bytes_written = 0;
    uint32_t loop_count = 0;
    
    ESP_LOGI(TAG, "I2S read task started");
    
    // 给硬件一点时间稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        loop_count++;
        
        // 从RX读取数据
        esp_err_t read_ret = i2s_channel_read(rx_handle, buf, BUF_SIZE, &bytes_read, 1000);
        if (read_ret == ESP_OK) {
            if (bytes_read > 0) {
                ESP_LOGD(TAG, "Loop %lu: Read %d bytes from RX", loop_count, bytes_read);
                
                // 检查缓冲区中是否有非零数据（简单的声音检测）
                bool has_data = false;
                for (int i = 0; i < bytes_read; i += 4) { // 每4字节检查一次（32位采样）
                    if (*(uint32_t*)(buf + i) != 0) {
                        has_data = true;
                        break;
                    }
                }
                
                if (has_data) {
                    ESP_LOGI(TAG, "Audio data detected in buffer");
                }
                
                // 写入TX播放
                esp_err_t write_ret = i2s_channel_write(tx_handle, buf, bytes_read, &bytes_written, 1000);
                if (write_ret == ESP_OK) {
                    if (bytes_written > 0) {
                        ESP_LOGD(TAG, "Loop %lu: Written %d bytes to TX", loop_count, bytes_written);
                    } else {
                        ESP_LOGW(TAG, "Loop %lu: No bytes written to TX", loop_count);
                    }
                } else {
                    ESP_LOGE(TAG, "Loop %lu: Failed to write to TX: %s", loop_count, esp_err_to_name(write_ret));
                }
            } else {
                if (loop_count % 100 == 0) { // 每100次循环输出一次警告，避免日志过多
                    ESP_LOGW(TAG, "Loop %lu: No data read from RX", loop_count);
                }
            }
        } else {
            ESP_LOGE(TAG, "Loop %lu: Failed to read from RX: %s", loop_count, esp_err_to_name(read_ret));
        }
        
        // 每100次循环输出一次状态信息
        if (loop_count % 100 == 0) {
            ESP_LOGI(TAG, "I2S task running, total loops: %lu", loop_count);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1)); // 减少延迟，提高响应性
    }
    
    vTaskDelete(NULL);
}

// 初始化麦克风功能
void microphone_init(void)
{
    ESP_LOGI(TAG, "=== Starting Microphone Initialization ===");
    
    // 先初始化TX，再初始化RX
    i2s_tx_init();
    i2s_rx_init();
    
    // 创建任务
    BaseType_t task_ret = xTaskCreate(i2s_read_task, "i2s_read_task", 4096 * 2, NULL, tskIDLE_PRIORITY + 2, NULL);
    if (task_ret == pdPASS) {
        ESP_LOGI(TAG, "I2S read task created successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create I2S read task");
    }
    
    ESP_LOGI(TAG, "=== Microphone Initialization Complete ===");
}

// 停止麦克风功能（如果需要）
void microphone_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing microphone...");
    
    if (rx_handle) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
    }
    
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
    }
    
    ESP_LOGI(TAG, "Microphone deinitialized");
}

// microphone_ui
void microphone_ui(lv_display_t *disp)
{
    ESP_LOGI(TAG, "Loading microphone UI");
    
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_clean(scr);

    // 初始化麦克风功能
    microphone_init();

    int btn_width = CALC_BTN_WIDTH(disp);
    int btn_height = CALC_BTN_HEIGHT(disp);
    int btn_spacing = CALC_BTN_SPACING(disp);
    int margin = btn_spacing / 2;

    // 创建返回按钮
    lv_obj_t *back_btn = lv_btn_create(scr);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_CENTER, 0, -margin);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, microphone_back_mainui_cb, LV_EVENT_CLICKED, NULL);
    
    // 添加状态标签显示当前状态
    lv_obj_t *status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Microphone Active - Speak now");
    lv_obj_align(status_label, LV_ALIGN_CENTER, 0, margin + btn_height);
    
    ESP_LOGI(TAG, "Microphone UI loaded");
}

void microphone_back_mainui_cb(lv_event_t * e)
{
    microphone_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    lv_display_t *disp = lv_event_get_user_data(e);
    example_lvgl_demo_ui(disp);
}