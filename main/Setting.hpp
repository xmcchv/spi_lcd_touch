#ifndef SETTING_HPP
#define SETTING_HPP

#include "esp_log.h"
#include "lvgl.h"

class Setting {
public:
    enum Rotation {
        ROTATION_0 = LV_DISPLAY_ROTATION_0,
        ROTATION_90 = LV_DISPLAY_ROTATION_90,
        ROTATION_180 = LV_DISPLAY_ROTATION_180,
        ROTATION_270 = LV_DISPLAY_ROTATION_270
    };
    
    static Setting& getInstance();
    
    bool initialize();
    void setRotation(Rotation rotation);
    Rotation getCurrentRotation() const;
    void rotateNext();
    void saveRotationToNVS();
    void loadRotationFromNVS();
    
private:
    Setting() = default;
    ~Setting() = default;
    
    lv_display_t* display_ = nullptr;
    Rotation current_rotation_ = ROTATION_0;
    static const char* NVS_KEY_ROTATION;
};

#endif // SETTING_HPP