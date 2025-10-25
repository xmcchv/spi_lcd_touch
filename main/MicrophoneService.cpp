#include "MicrophoneService.hpp"
#include <cstring>

static const char *TAG = "MicrophoneService";

MicrophoneService& MicrophoneService::getInstance() {
    static MicrophoneService instance;
    return instance;
}

MicrophoneService::MicrophoneService() {
    // 构造函数
}

MicrophoneService::~MicrophoneService() {
    deinitialize();
}

bool MicrophoneService::initialize() {
    ESP_LOGI(TAG, "=== Starting Microphone Service Initialization ===");
    
    // 先清理现有资源
    if (rx_handle_ != nullptr || tx_handle_ != nullptr) {
        ESP_LOGI(TAG, "Cleaning up existing I2S resources before reinitialization");
        cleanup();
    }
    
    // 先初始化TX，再初始化RX
    if (!initializeI2STX()) {
        ESP_LOGE(TAG, "Failed to initialize I2S TX");
        return false;
    }
    
    if (!initializeI2SRX()) {
        ESP_LOGE(TAG, "Failed to initialize I2S RX");
        cleanup();
        return false;
    }
    
    ESP_LOGI(TAG, "=== Microphone Service Initialization Complete ===");
    return true;
}

bool MicrophoneService::initializeI2SRX() {
    ESP_LOGI(TAG, "Initializing I2S RX...");
    
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_frame_num = 1023;  // 增加DMA帧数减少数据丢失
    chan_cfg.auto_clear = true;    // 启用自动清除
    
    // 修复：RX通道应该是第二个参数（rx_handle）
    i2s_chan_handle_t rx_handle = nullptr;
    esp_err_t ret = i2s_new_channel(&chan_cfg, nullptr, &rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RX channel: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "RX channel created successfully");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = INMP_SCK,
            .ws = INMP_WS,
            .dout = I2S_GPIO_UNUSED,
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
        i2s_del_channel(rx_handle);
        return false;
    }

    ret = i2s_channel_enable(rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RX channel: %s", esp_err_to_name(ret));
        i2s_del_channel(rx_handle);
        return false;
    }
    
    // 确保通道句柄正确赋值
    rx_handle_ = rx_handle;
    ESP_LOGI(TAG, "I2S RX initialized successfully");
    return true;
}

bool MicrophoneService::initializeI2STX() {
    ESP_LOGI(TAG, "Initializing I2S TX...");
    
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.dma_frame_num = 1023;  // 增加DMA帧数减少数据丢失
    chan_cfg.auto_clear = true;    // 启用自动清除
    
    // 修复：TX通道应该是第一个参数（tx_handle）
    i2s_chan_handle_t tx_handle = nullptr;
    esp_err_t ret = i2s_new_channel(&chan_cfg, &tx_handle, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create TX channel: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "TX channel created successfully");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MAX_BCLK,
            .ws = MAX_LRC,
            .dout = MAX_DIN,
            .din = I2S_GPIO_UNUSED,
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
        i2s_del_channel(tx_handle);
        return false;
    }

    ret = i2s_channel_enable(tx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable TX channel: %s", esp_err_to_name(ret));
        i2s_del_channel(tx_handle);
        return false;
    }
    
    // 确保通道句柄正确赋值
    tx_handle_ = tx_handle;
    ESP_LOGI(TAG, "I2S TX initialized successfully");
    return true;
}

void MicrophoneService::startRecording() {
    if (is_recording_) {
        ESP_LOGW(TAG, "Recording is already in progress");
        return;
    }
    
    // 检查通道是否有效，如果无效则重新初始化
    if (rx_handle_ == nullptr || tx_handle_ == nullptr) {
        ESP_LOGW(TAG, "I2S channels not initialized, attempting to reinitialize...");
        if (!reinitializeI2SChannels()) {
            ESP_LOGE(TAG, "Failed to reinitialize I2S channels");
            return;
        }
    }
    
    // 创建录音任务
    BaseType_t task_ret = xTaskCreate(
        recordingTask, 
        "microphone_recording", 
        4096 * 2, 
        this, 
        tskIDLE_PRIORITY + 2, 
        &recording_task_
    );
    
    if (task_ret == pdPASS) {
        is_recording_ = true;
        ESP_LOGI(TAG, "Recording task created successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create recording task");
    }
}

// 添加重新初始化I2S通道的方法
bool MicrophoneService::reinitializeI2SChannels() {
    ESP_LOGI(TAG, "Reinitializing I2S channels...");
    
    // 先停止录音任务
    stopRecording();
    
    // 完全清理现有通道
    cleanup();
    
    // 给系统一些时间释放资源
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 重新初始化TX和RX通道（注意顺序）
    if (!initializeI2STX()) {
        ESP_LOGE(TAG, "Failed to reinitialize I2S TX");
        cleanup();
        return false;
    }
    
    if (!initializeI2SRX()) {
        ESP_LOGE(TAG, "Failed to reinitialize I2S RX");
        cleanup(); // 清理部分初始化的通道
        return false;
    }
    
    ESP_LOGI(TAG, "I2S channels reinitialized successfully");
    return true;
}


void MicrophoneService::stopRecording() {
    if (!is_recording_) {
        ESP_LOGW(TAG, "Recording is not in progress");
        return;
    }
    
    is_recording_ = false;
    
    // 等待任务自行结束
    if (recording_task_ != nullptr) {
        // 给任务一点时间自行结束
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // 重置任务句柄，任务会自行删除
        recording_task_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Recording stopped");
}

bool MicrophoneService::isRecording() const {
    return is_recording_;
}

void MicrophoneService::deinitialize() {
    ESP_LOGI(TAG, "Deinitializing microphone service...");
    
    stopRecording();
    cleanup();
    
    ESP_LOGI(TAG, "Microphone service deinitialized");
}

void MicrophoneService::recordingTask(void* parameter) {
    MicrophoneService* service = static_cast<MicrophoneService*>(parameter);
    if (service == nullptr) {
        vTaskDelete(nullptr);
        return;
    }
    
    ESP_LOGI(TAG, "Recording task started");
    
    uint8_t* buf = new uint8_t[BUF_SIZE];
    if (buf == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        vTaskDelete(nullptr);
        return;
    }
    
    size_t bytes_read = 0;
    size_t bytes_written = 0;
    uint32_t loop_count = 0;
    
    // 给硬件一点时间稳定
    vTaskDelay(pdMS_TO_TICKS(100));
    
    while (service->is_recording_) {
        loop_count++;
        
        // 从RX读取数据
        esp_err_t read_ret = i2s_channel_read(service->rx_handle_, buf, BUF_SIZE, &bytes_read, 1000);
        if (read_ret == ESP_OK) {
            if (bytes_read > 0) {
                // 检查缓冲区中是否有非零数据（简单的声音检测）
                bool has_data = false;
                for (int i = 0; i < bytes_read; i += 4) {
                    if (*(uint32_t*)(buf + i) != 0) {
                        has_data = true;
                        break;
                    }
                }
                
                if (has_data && loop_count % 50 == 0) { // 每50次循环输出一次检测结果
                    ESP_LOGI(TAG, "Audio data detected");
                }
                
                // 写入TX播放（实时监听）
                esp_err_t write_ret = i2s_channel_write(service->tx_handle_, buf, bytes_read, &bytes_written, 1000);
                if (write_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to write to TX: %s", esp_err_to_name(write_ret));
                }
            }
        } else {
            ESP_LOGE(TAG, "Failed to read from RX: %s", esp_err_to_name(read_ret));
        }
        
        // 每100次循环输出一次状态信息
        if (loop_count % 100 == 0) {
            ESP_LOGI(TAG, "Recording task running, total loops: %lu", loop_count);
        }
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // 清理资源
    delete[] buf;
    ESP_LOGI(TAG, "Recording task ended");
    
    // 正确退出FreeRTOS任务
    vTaskDelete(nullptr);
}

void MicrophoneService::cleanup() {
    // 先停止录音任务
    stopRecording();
    
    // 确保任务已经完全停止
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 禁用并删除I2S通道
    if (rx_handle_ != nullptr) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
        ESP_LOGI(TAG, "RX channel deleted");
    }
    
    if (tx_handle_ != nullptr) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
        tx_handle_ = nullptr;
        ESP_LOGI(TAG, "TX channel deleted");
    }
    
    // 添加延迟确保资源完全释放
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "I2S channels cleaned up");
}