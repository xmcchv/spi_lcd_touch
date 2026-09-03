#ifndef MICROPHONE_SERVICE_HPP
#define MICROPHONE_SERVICE_HPP

#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class MicrophoneService {
public:
    static MicrophoneService& getInstance();
    
    // GPIO引脚定义
    static constexpr gpio_num_t INMP_SD = GPIO_NUM_7;
    static constexpr gpio_num_t INMP_SCK = GPIO_NUM_38;
    static constexpr gpio_num_t INMP_WS = GPIO_NUM_4;
    static constexpr gpio_num_t MAX_DIN = GPIO_NUM_6;
    static constexpr gpio_num_t MAX_BCLK = GPIO_NUM_15;
    static constexpr gpio_num_t MAX_LRC = GPIO_NUM_16;
    
    // 音频配置
    static constexpr int AUDIO_INPUT_SAMPLE_RATE  = 16000;
    static constexpr int AUDIO_OUTPUT_SAMPLE_RATE = 24000;

    static constexpr int BUF_SIZE = (1023 * 1 * 32 / 8); // DMA帧数 * 声道数 * 数据位宽 / 8
    
    bool initialize();
    void startRecording();
    void stopRecording();
    bool isRecording() const;
    void deinitialize();
    
private:
    MicrophoneService();
    ~MicrophoneService();
    
    
    // I2S通道句柄
    i2s_chan_handle_t rx_handle_ = nullptr;
    i2s_chan_handle_t tx_handle_ = nullptr;
    
    // 任务句柄
    TaskHandle_t recording_task_ = nullptr;
    bool is_recording_ = false;
    
    // 私有方法
    bool initializeI2SRX();
    bool initializeI2STX();
    bool reinitializeI2SChannels();  // 添加重新初始化方法
    static void recordingTask(void* parameter);
    void cleanup();
    
    // 添加音频处理函数
    static void applyLowPassFilter(uint8_t* input, uint8_t* output, size_t size);
};

#endif // MICROPHONE_SERVICE_HPP