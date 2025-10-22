#ifndef UIMANAGER_HPP
#define UIMANAGER_HPP

#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"

#include "QRConfigManager.hpp"
#include "WiFiManager.hpp"
#include "JokeService.hpp"
#include "Setting.hpp"
#include "MicrophoneService.hpp"
#include <string>
#include <algorithm>

#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
#include "esp_lcd_ili9341.h"
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
#include "esp_lcd_gc9a01.h"
#endif

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
#include "esp_lcd_touch_stmpe610.h"
#elif CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_FT5406
#include "esp_lcd_touch_ft5406.h"
#elif CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
#include "esp_lcd_touch_xpt2046.h"
#endif


class UIManager {
public:
    static UIManager& getInstance();
    
    bool initialize();
    void switchToMainUI();
    void switchToWiFiConfigUI();
    void switchToJokeUI();
    void switchToSettingUI();
    void switchToMicrophoneUI();
    void switchToWiFiQRUI();
    
    lv_display_t* getDisplay() { return display_; }
    lv_indev_t* getTouchInput() { return touch_indev_; }

    static void set_angle(void * obj, int32_t v)
    {
        lv_arc_set_value(static_cast<lv_obj_t*>(obj), v);
    }
    
private:
    UIManager() = default;
    ~UIManager() = default;
    
    // 添加LVGL任务函数声明
    static void lvglTask(void *arg);
    
    // 显示相关成员
    lv_display_t* display_ = nullptr;
    esp_lcd_panel_handle_t panel_handle_ = nullptr;
    esp_lcd_panel_io_handle_t io_handle_ = nullptr;
    
    // 触摸相关成员
    lv_indev_t* touch_indev_ = nullptr;
    esp_lcd_touch_handle_t touch_handle_ = nullptr;
    esp_lcd_panel_io_handle_t touch_io_handle_ = nullptr;
    
    // 定时器相关
    esp_timer_handle_t lvgl_tick_timer_ = nullptr;
    lv_timer_t* wifi_status_timer_ = nullptr;  // 添加WiFi状态更新定时器
    
    // UI组件成员
    lv_obj_t* current_screen_ = nullptr;
    lv_obj_t* wifi_status_label_ = nullptr;
    lv_obj_t* wifi_ip_label_ = nullptr;
    lv_obj_t* wifi_ssid_label_ = nullptr;
    // 笑话界面相关成员
    lv_obj_t* joke_label_;
    lv_obj_t* joke_loading_arc_;
    lv_obj_t* setting_label_ = nullptr;
    lv_obj_t* microphone_status_label_ = nullptr;
    
    // 配置常量
    static constexpr int LCD_HOST = SPI2_HOST;
    static constexpr int TOUCH_HOST = SPI3_HOST;
    #if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
    static constexpr int EXAMPLE_LCD_H_RES = 240;
    static constexpr int EXAMPLE_LCD_V_RES = 320;
    #elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    static constexpr int EXAMPLE_LCD_H_RES = 240;
    static constexpr int EXAMPLE_LCD_V_RES = 240;
    #endif
    
    // GPIO引脚定义
    // LCD SPI pins (屏幕SPI引脚)
    static constexpr int PIN_NUM_SCLK = 18;
    static constexpr int PIN_NUM_MOSI = 19;
    static constexpr int PIN_NUM_MISO = 21;
    // LCD control pins (屏幕控制引脚)
    static constexpr int PIN_NUM_LCD_DC = 5;
    static constexpr int PIN_NUM_LCD_RST = 3;
    static constexpr int PIN_NUM_LCD_CS = 4;
    static constexpr int PIN_NUM_BK_LIGHT = 2;
    // Touch SPI pins (触摸SPI引脚 - 使用独立的SPI总线)
    static constexpr int PIN_NUM_TOUCH_SCLK = 7; // T_CLK
    static constexpr int PIN_NUM_TOUCH_MOSI = 6; // T_MOSI
    static constexpr int PIN_NUM_TOUCH_MISO = 8; // T_MISO
    static constexpr int PIN_NUM_TOUCH_CS = 15; // T_CS
    static constexpr int PIN_NUM_TOUCH_IRQ = 16; // T_IRQ
    static constexpr int PIN_NUM_TOUCH_RST = -1; // T_RST
    
    // Bit number used to represent command and parameter
    static constexpr int EXAMPLE_LCD_CMD_BITS = 8;
    static constexpr int EXAMPLE_LCD_PARAM_BITS = 8;

    // LVGL configuration (LVGL配置)
    static constexpr int EXAMPLE_LCD_PIXEL_CLOCK_HZ = 20 * 1000 * 1000;
    static constexpr int EXAMPLE_LVGL_DRAW_BUF_LINES = 20;
    static constexpr int EXAMPLE_LVGL_TICK_PERIOD_MS = 2;
    static constexpr int EXAMPLE_LVGL_TASK_MAX_DELAY_MS = 500;
    static constexpr int EXAMPLE_LVGL_TASK_MIN_DELAY_MS = 1000 / CONFIG_FREERTOS_HZ;
    static constexpr int EXAMPLE_LVGL_TASK_STACK_SIZE = (16 * 1024);
    static constexpr int EXAMPLE_LVGL_TASK_PRIORITY = 2;

    // 按钮尺寸计算百分比（基于屏幕分辨率的百分比）
    static constexpr int BTN_WIDTH_PERCENT = 45;    // 按钮宽度占屏幕宽度的45%
    static constexpr int BTN_HEIGHT_PERCENT = 15;   // 按钮高度占屏幕高度的15%
    static constexpr int BTN_SPACING_PERCENT = 5;   // 按钮间距占屏幕高度的5%

    // 辅助函数
    bool initializeDisplay();
    bool initializeTouch();
    bool initializeLVGL();
    
    // 界面创建函数
    void createMainUI();
    void createWiFiConfigUI();
    void createWiFiQRUI();
    void createJokeUI();
    void createSettingUI();
    void createMicrophoneUI();
    
    // 计算函数
    int calcBtnWidth() const;
    int calcBtnHeight() const;
    int calcBtnSpacing() const;
    
    // 静态回调函数
    static bool notifyLvglFlushReady(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
    static void lvglFlushCallback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
    static void touchCallback(lv_indev_t *indev, lv_indev_data_t *data);
    static void increaseLvglTick(void *arg);
    static void lvglPortUpdateCallback(lv_display_t *disp);
    
    // 事件回调函数
    static void wifiUIEventCallback(lv_event_t *e);
    static void jokeUIEventCallback(lv_event_t *e);
    static void settingUIEventCallback(lv_event_t *e);
    static void backToMainUICallback(lv_event_t *e);
    static void backToWiFiConfigUICallback(lv_event_t *e);
    static void microphoneUIEventCallback(lv_event_t *e);
    
    static void wifiConnectBtnCallback(lv_event_t *e);
    static void wifiQRConfigBtnCallback(lv_event_t *e);
    static void getJokeBtnCallback(lv_event_t *e);
    static void rotateBtnCallback(lv_event_t *e);
    static void microphoneStartStopCallback(lv_event_t *e);

    // WiFi状态更新定时器相关函数
    void deleteWifiStatusTimer();
    static void wifiStatusTimerCallback(lv_timer_t* timer);

    // 加载动画相关方法
    lv_obj_t* createLoadingArc(lv_obj_t* parent, lv_obj_t* reference, uint16_t size, int32_t x_offset, int32_t y_offset);
    void deleteLoadingArc(lv_obj_t** loading_arc);
};

    
    
#endif // UIMANAGER_HPP