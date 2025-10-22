#include "Setting.hpp"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "Setting";
const char* Setting::NVS_KEY_ROTATION = "disp_rot";  // 缩短键名以避免NVS键名过长错误

Setting& Setting::getInstance() {
    static Setting instance;
    return instance;
}

bool Setting::initialize() {
    ESP_LOGI(TAG, "Initializing Setting manager");
    
    // 获取默认显示设备
    display_ = lv_display_get_default();
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to get default display");
        return false;
    }
    
    // 从NVS加载保存的旋转设置
    loadRotationFromNVS();
    
    ESP_LOGI(TAG, "Setting manager initialized successfully");
    return true;
}

void Setting::setRotation(Rotation rotation) {
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Display not initialized");
        return;
    }
    
    current_rotation_ = rotation;
    lv_display_rotation_t lv_rotation = static_cast<lv_display_rotation_t>(rotation);
    
    ESP_LOGI(TAG, "Setting display rotation to: %d", rotation);
    lv_disp_set_rotation(display_, lv_rotation);
    
    // 保存设置到NVS
    saveRotationToNVS();
}

Setting::Rotation Setting::getCurrentRotation() const {
    return current_rotation_;
}

void Setting::rotateNext() {
    Rotation new_rotation;
    
    switch (current_rotation_) {
        case ROTATION_0:
            new_rotation = ROTATION_90;
            break;
        case ROTATION_90:
            new_rotation = ROTATION_180;
            break;
        case ROTATION_180:
            new_rotation = ROTATION_270;
            break;
        case ROTATION_270:
            new_rotation = ROTATION_0;
            break;
        default:
            new_rotation = ROTATION_0;
            break;
    }
    
    setRotation(new_rotation);
}

void Setting::saveRotationToNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return;
    }
    
    err = nvs_set_i32(nvs_handle, NVS_KEY_ROTATION, static_cast<int32_t>(current_rotation_));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving rotation to NVS: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Rotation saved to NVS: %d", current_rotation_);
    }
    
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

void Setting::loadRotationFromNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Error opening NVS: %s, using default rotation", esp_err_to_name(err));
        return;
    }
    
    int32_t saved_rotation = ROTATION_0;
    err = nvs_get_i32(nvs_handle, NVS_KEY_ROTATION, &saved_rotation);
    
    if (err == ESP_OK) {
        current_rotation_ = static_cast<Rotation>(saved_rotation);
        ESP_LOGI(TAG, "Rotation loaded from NVS: %d", current_rotation_);
        
        // 应用加载的旋转设置
        if (display_ != nullptr) {
            lv_display_rotation_t lv_rotation = static_cast<lv_display_rotation_t>(current_rotation_);
            lv_disp_set_rotation(display_, lv_rotation);
        }
    } else {
        ESP_LOGW(TAG, "No saved rotation found in NVS, using default");
    }
    
    nvs_close(nvs_handle);
}