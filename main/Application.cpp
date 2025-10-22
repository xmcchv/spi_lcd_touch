#include "Application.hpp"
#include "esp_log.h"

Application& Application::getInstance() {
    static Application instance;
    return instance;
}

bool Application::initialize() {
    ESP_LOGI("Application", "Initializing application...");
    
    if (!ui_manager_.initialize()) {
        ESP_LOGE("Application", "Failed to initialize UI manager");
        return false;
    }
    
    if (!wifi_manager_.initialize()) {
        ESP_LOGE("Application", "Failed to initialize WiFi manager");
        return false;
    }
    
    if (!joke_service_.initialize()) {
        ESP_LOGE("Application", "Failed to initialize joke service");
        return false;
    }
    
    if (!setting_.initialize()) {
        ESP_LOGE("Application", "Failed to initialize setting manager");
        return false;
    }
    
    // if (!microphone_service_.initialize()) {
    //     ESP_LOGE("Application", "Failed to initialize microphone service");
    //     return false;
    // }
    
    setupEventHandlers();
    
    // 切换到主界面
    ui_manager_.switchToMainUI();
    
    ESP_LOGI("Application", "Application initialized successfully");
    return true;
}

void Application::run() {
    ESP_LOGI("Application", "Application running...");
    // 主循环逻辑 - 保持运行状态
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void Application::setupEventHandlers() {
    // 设置事件处理器 - 这里可以添加全局事件处理逻辑
    ESP_LOGI("Application", "Setting up event handlers");
}