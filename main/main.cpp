#include "Application.hpp"

extern "C" void app_main(void) {
    ESP_LOGI("main", "Starting ESP32 LCD Touch Application");
    
    Application& app = Application::getInstance();
    
    if (app.initialize()) {
        app.run();
    } else {
        ESP_LOGE("main", "Failed to initialize application");
    }
}