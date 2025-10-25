#include "UIManager.hpp"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "WiFiManager.hpp"
#include "JokeService.hpp"
#include <string>

// 互斥锁用于保护LVGL API调用
static _lock_t lvgl_api_lock;

UIManager& UIManager::getInstance() {
    static UIManager instance;
    return instance;
}

bool UIManager::initialize() {
    ESP_LOGI("UIManager", "开始初始化UI管理器");
    // 确保之前的定时器被清理
    deleteWifiStatusTimer();
    // 关闭LCD背光
    gpio_config_t bk_gpio_config = {};
    bk_gpio_config.mode = GPIO_MODE_OUTPUT;
    bk_gpio_config.pin_bit_mask = (1ULL << PIN_NUM_BK_LIGHT);
    
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(static_cast<gpio_num_t>(PIN_NUM_BK_LIGHT), 0);

    if (!initializeDisplay()) {
        ESP_LOGE("UIManager", "显示初始化失败");
        return false;
    }
    
    if (!initializeTouch()) {
        ESP_LOGE("UIManager", "触摸初始化失败");
        return false;
    }
    
    if (!initializeLVGL()) {
        ESP_LOGE("UIManager", "LVGL初始化失败");
        return false;
    }
    
    ESP_LOGI("UIManager", "UI管理器初始化完成");
    return true;
}

bool UIManager::initializeDisplay() {
    ESP_LOGI("UIManager", "初始化LCD SPI总线");
    
    // 配置LCD SPI总线
    spi_bus_config_t lcd_buscfg = {};
    lcd_buscfg.sclk_io_num = PIN_NUM_SCLK;
    lcd_buscfg.mosi_io_num = PIN_NUM_MOSI;
    lcd_buscfg.miso_io_num = PIN_NUM_MISO;
    lcd_buscfg.quadwp_io_num = -1;
    lcd_buscfg.quadhd_io_num = -1;
    lcd_buscfg.max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(static_cast<spi_host_device_t>(LCD_HOST), &lcd_buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI("UIManager", "安装面板IO");
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = PIN_NUM_LCD_DC;
    io_config.cs_gpio_num = PIN_NUM_LCD_CS;
    io_config.pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ;
    io_config.lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS;
    io_config.lcd_param_bits = EXAMPLE_LCD_PARAM_BITS;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    
    // 附加LCD到SPI总线
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle_));

    // 配置面板设备
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = PIN_NUM_LCD_RST;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;

// 安装面板驱动
#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
    ESP_LOGI("UIManager", "安装ILI9341面板驱动");
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle_, &panel_config, &panel_handle_));
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    ESP_LOGI("UIManager", "安装GC9A01面板驱动");
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle_, &panel_config, &panel_handle_));
#endif

    // 重置并初始化面板
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle_));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle_));
    
#if CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle_, true));
#endif
    
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle_, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle_, true));

    // 打开LCD背光
    gpio_set_level(static_cast<gpio_num_t>(PIN_NUM_BK_LIGHT), 1);
    ESP_LOGI("UIManager", "显示初始化完成");

    return true;
}

bool UIManager::initializeTouch() {
#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    ESP_LOGI("UIManager", "初始化触摸SPI总线");
    
    // 配置触摸SPI总线
    spi_bus_config_t touch_buscfg = {};
    touch_buscfg.sclk_io_num = PIN_NUM_TOUCH_SCLK;
    touch_buscfg.mosi_io_num = PIN_NUM_TOUCH_MOSI;
    touch_buscfg.miso_io_num = PIN_NUM_TOUCH_MISO;
    touch_buscfg.quadwp_io_num = -1;
    touch_buscfg.quadhd_io_num = -1;
touch_buscfg.max_transfer_sz = 1024;
    ESP_ERROR_CHECK(spi_bus_initialize(static_cast<spi_host_device_t>(TOUCH_HOST), &touch_buscfg, SPI_DMA_DISABLED));

    // 配置触摸IO
    esp_lcd_panel_io_spi_config_t tp_io_config;
#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
    tp_io_config = ESP_LCD_TOUCH_IO_SPI_STMPE610_CONFIG(PIN_NUM_TOUCH_CS);
#elif CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
    tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
#endif

    // 附加触摸到独立的SPI总线
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_HOST, &tp_io_config, &touch_io_handle_));

    // 配置触摸控制器
    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = EXAMPLE_LCD_V_RES;
    tp_cfg.y_max = EXAMPLE_LCD_H_RES;
    tp_cfg.rst_gpio_num = static_cast<gpio_num_t>(PIN_NUM_TOUCH_RST);
    tp_cfg.int_gpio_num = static_cast<gpio_num_t>(PIN_NUM_TOUCH_IRQ);
    tp_cfg.flags = {};
    tp_cfg.flags.swap_xy = 1;
    tp_cfg.flags.mirror_x = 0;
    tp_cfg.flags.mirror_y = 0;
    
    // 安装触摸驱动
#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
    ESP_LOGI("UIManager", "初始化触摸控制器STMPE610");
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_stmpe610(touch_io_handle_, &tp_cfg, &touch_handle_));
#elif CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
    ESP_LOGI("UIManager", "初始化触摸控制器XPT2046");
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(touch_io_handle_, &tp_cfg, &touch_handle_));
#endif

    ESP_LOGI("UIManager", "触摸初始化完成");
#endif
    return true;
}

bool UIManager::initializeLVGL() {
    ESP_LOGI("UIManager", "初始化LVGL库");
    
    // 先检查LVGL是否已经初始化
    if (lv_is_initialized()) {
        ESP_LOGW("UIManager", "LVGL已经初始化，先进行清理");
        lv_deinit();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    lv_init();

    // 检查内存状态
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI("UIManager", "系统可用内存: %d字节", free_heap);
    
    if (free_heap < (100 * 1024)) {
        ESP_LOGW("UIManager", "系统内存不足，可能影响LVGL性能");
    }

    // 创建LVGL显示
    display_ = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
    if (!display_) {
        ESP_LOGE("UIManager", "无法创建LVGL显示");
        return false;
    }

    // 优化绘制缓冲区大小 - 减少缓冲区行数以节省内存
    const int OPTIMAL_DRAW_BUF_LINES = 10;  // 减少到10行以节省内存
    size_t draw_buffer_sz = EXAMPLE_LCD_H_RES * OPTIMAL_DRAW_BUF_LINES * sizeof(lv_color16_t);
    
    ESP_LOGI("UIManager", "分配LVGL绘制缓冲区: %d字节", draw_buffer_sz);
    
    // 分配LVGL使用的绘制缓冲区
    void *buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_DMA);
    
    if (!buf1 || !buf2) {
        ESP_LOGE("UIManager", "无法分配LVGL绘制缓冲区");
        if (buf1) free(buf1);
        if (buf2) free(buf2);
        return false;
    }

    // 初始化LVGL绘制缓冲区
    lv_display_set_buffers(display_, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_user_data(display_, panel_handle_);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display_, lvglFlushCallback);

    // 设置LVGL定时器
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &increaseLvglTick,
        .name = "lvgl_tick"
    };
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer_, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    // 注册IO面板事件回调
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notifyLvglFlushReady,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle_, &cbs, display_));

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    // 创建触摸输入设备
    touch_indev_ = lv_indev_create();
    if (!touch_indev_) {
        ESP_LOGE("UIManager", "无法创建触摸输入设备");
        return false;
    }
    lv_indev_set_type(touch_indev_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(touch_indev_, display_);
    lv_indev_set_user_data(touch_indev_, touch_handle_);
    lv_indev_set_read_cb(touch_indev_, touchCallback);
#endif

    // 创建LVGL任务（增加堆栈大小）
    BaseType_t task_ret = xTaskCreate(
        lvglTask, 
        "lvgl_task", 
        EXAMPLE_LVGL_TASK_STACK_SIZE,  // 使用配置的堆栈大小
        this, 
        EXAMPLE_LVGL_TASK_PRIORITY,    // 使用配置的优先级
        &lvgl_task_handle_              // 使用新添加的任务句柄变量
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE("UIManager", "无法创建LVGL任务");
        return false;
    }
    
    ESP_LOGI("UIManager", "LVGL初始化完成");
    return true;
}

// 添加LVGL任务函数
// 在lvglTask函数中增加堆栈大小
void UIManager::lvglTask(void *arg) {
    UIManager* ui_manager = static_cast<UIManager*>(arg);
    if (ui_manager == nullptr) {
        ESP_LOGE("UIManager", "LVGL任务参数无效");
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI("UIManager", "LVGL任务启动");
    
    // 初始堆栈检查
    uint32_t initial_stack = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI("UIManager", "LVGL任务初始堆栈剩余: %d字节", initial_stack);
    
    if (initial_stack < 2048) {
        ESP_LOGW("UIManager", "LVGL任务堆栈可能不足，建议增加堆栈大小");
    }
    
    uint32_t time_till_next_ms = 0;
    uint32_t error_count = 0;
    const uint32_t MAX_ERROR_COUNT = 10;

    while (1) {
        // 检查错误计数，防止无限循环
        if (error_count > MAX_ERROR_COUNT) {
            ESP_LOGE("UIManager", "LVGL任务错误过多，重启任务");
            esp_restart();
        }
        
        _lock_acquire(&lvgl_api_lock);
        
        // 添加LVGL状态检查
        if (lv_display_get_default() == nullptr) {
            ESP_LOGE("UIManager", "LVGL显示未初始化");
            _lock_release(&lvgl_api_lock);
            error_count++;
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // 安全执行LVGL定时器处理（移除try-catch，改用返回值检查）
        time_till_next_ms = lv_timer_handler();
        
        // 检查LVGL处理结果是否有效
        if (time_till_next_ms > 1000) {
            ESP_LOGW("UIManager", "LVGL定时器处理返回异常延迟: %dms", time_till_next_ms);
            time_till_next_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        
        _lock_release(&lvgl_api_lock);
        error_count = 0; // 重置错误计数
        
        // 防止触发任务看门狗超时
        time_till_next_ms = std::max(time_till_next_ms, static_cast<uint32_t>(EXAMPLE_LVGL_TASK_MIN_DELAY_MS));
        time_till_next_ms = std::min(time_till_next_ms, static_cast<uint32_t>(EXAMPLE_LVGL_TASK_MAX_DELAY_MS));
        
        vTaskDelay(pdMS_TO_TICKS(time_till_next_ms));
        
        // 定期检查堆栈使用情况
        static uint32_t stack_check_counter = 0;
        if (++stack_check_counter % 200 == 0) {
            uint32_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI("UIManager", "LVGL任务堆栈剩余: %d字节", stack_remaining);
            
            if (stack_remaining < 1024) {
                ESP_LOGW("UIManager", "LVGL任务堆栈不足，剩余: %d字节", stack_remaining);
            }
        }
    }
}

// 静态回调函数实现
bool UIManager::notifyLvglFlushReady(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

void UIManager::lvglFlushCallback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    lvglPortUpdateCallback(disp);
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
// 因为SPI LCD是大端序，需要交换RGB字节顺序
    lv_draw_sw_rgb565_swap(px_map, (area->x2 + 1 - area->x1) * (area->y2 + 1 - area->y1));
    
    // 将缓冲区内容复制到显示的特定区域
    esp_lcd_panel_draw_bitmap(panel_handle, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
}

void UIManager::touchCallback(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;
    
    esp_lcd_touch_handle_t touch_pad = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(touch_pad);
    
    // 获取坐标
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(touch_pad, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void UIManager::increaseLvglTick(void *arg) {
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

void UIManager::lvglPortUpdateCallback(lv_display_t *disp) {
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISPLAY_ROTATION_90:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISPLAY_ROTATION_180:
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISPLAY_ROTATION_270:
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

                               
// 计算按钮宽度（考虑屏幕旋转状态）
int UIManager::calcBtnWidth() const {
    if (!display_) {
        ESP_LOGI("UIManager", "未初始化显示");
        return EXAMPLE_LCD_H_RES * BTN_WIDTH_PERCENT / 100;
    }
    
    lv_display_rotation_t rotation = lv_display_get_rotation(display_);
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        return EXAMPLE_LCD_H_RES * BTN_WIDTH_PERCENT / 100;
    } else {
        return EXAMPLE_LCD_V_RES * BTN_WIDTH_PERCENT / 100;
    }
}

// 计算按钮高度（考虑屏幕旋转状态）
int UIManager::calcBtnHeight() const {
    if (!display_) {
        ESP_LOGI("UIManager", "未初始化显示");
        return EXAMPLE_LCD_V_RES * BTN_HEIGHT_PERCENT / 100;
    }
    
    lv_display_rotation_t rotation = lv_display_get_rotation(display_);
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        return EXAMPLE_LCD_V_RES * BTN_HEIGHT_PERCENT / 100;
    } else {
        return EXAMPLE_LCD_H_RES * BTN_HEIGHT_PERCENT / 100;
    }
}

// 计算按钮间距（考虑屏幕旋转状态）
int UIManager::calcBtnSpacing() const {
    if (!display_) {
        ESP_LOGI("UIManager", "未初始化显示");
        return EXAMPLE_LCD_V_RES * BTN_SPACING_PERCENT / 100;
    }
    
    lv_display_rotation_t rotation = lv_display_get_rotation(display_);
    if (rotation == LV_DISPLAY_ROTATION_0 || rotation == LV_DISPLAY_ROTATION_180) {
        return EXAMPLE_LCD_V_RES * BTN_SPACING_PERCENT / 100;
    } else {
        return EXAMPLE_LCD_H_RES * BTN_SPACING_PERCENT / 100;
    }
}

// 切换到主界面
// 在界面切换函数中添加对象安全检查
void UIManager::switchToMainUI() {
    ESP_LOGI("UIManager", "切换到主界面");
    
    // 取消正在进行的笑话请求
    JokeService& joke_service = JokeService::getInstance();
    joke_service.cancelCurrentRequest();
    
    // 安全删除加载动画
    safeDeleteLvglObject(&joke_loading_arc_);
    
    // 删除WIFI状态定时器
    deleteWifiStatusTimer();
    
    // 安全删除当前屏幕
    // safeDeleteLvglObject(&current_screen_);

    // 给LVGL一些时间处理删除操作
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 创建主界面
    createMainUI();
}

// 切换到WiFi配置界面
void UIManager::switchToWiFiConfigUI() {
    ESP_LOGI("UIManager", "切换到WiFi配置界面");
    createWiFiConfigUI();
}

// 添加WiFi二维码配置界面切换函数
void UIManager::switchToWiFiQRUI() {
    ESP_LOGI("UIManager", "切换到WiFi二维码配置界面");
    createWiFiQRUI();
}

// 切换到笑话界面
void UIManager::switchToJokeUI() {
    ESP_LOGI("UIManager", "切换到笑话界面");
    createJokeUI();
}

// 添加Setting界面切换函数
void UIManager::switchToSettingUI() {
    ESP_LOGI("UIManager", "切换到设置界面");
    createSettingUI();
}

// 添加Microphone界面切换函数
void UIManager::switchToMicrophoneUI() {
    ESP_LOGI("UIManager", "切换到麦克风界面");
    createMicrophoneUI();
}

// 在createMainUI函数中添加Microphone按钮
void UIManager::createMainUI() {
    if (current_screen_) {
        lv_obj_del(current_screen_);
    }
    
    current_screen_ = lv_obj_create(NULL);
    lv_screen_load(current_screen_);
    
    // 计算自适应尺寸
    int btn_width = calcBtnWidth();
    int btn_height = calcBtnHeight();
    int btn_spacing = calcBtnSpacing();
    int margin = btn_spacing / 2;
    
    ESP_LOGI("UIManager", "创建主界面 - 屏幕分辨率: %dx%d, 按钮尺寸: %dx%d, 间距: %d", 
             EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, btn_width, btn_height, btn_spacing);
    
    // 创建标题
    lv_obj_t *title_label = lv_label_create(current_screen_);
    lv_label_set_text(title_label, "MENU");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, margin);
    
    // 计算按钮起始Y位置（居中显示）
    int total_buttons_height = (btn_height + margin); // 两个按钮加间距

    // 创建WiFi功能按钮
    lv_obj_t *wifi_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(wifi_btn, btn_width, btn_height);
    lv_obj_align(wifi_btn, LV_ALIGN_TOP_MID, 0, total_buttons_height + margin);
    lv_obj_t *wifi_label = lv_label_create(wifi_btn);
    lv_label_set_text(wifi_label, LV_SYMBOL_WIFI" WiFi");
    lv_obj_center(wifi_label);
    lv_obj_add_event_cb(wifi_btn, wifiUIEventCallback, LV_EVENT_CLICKED, this);
    
    // 创建笑话功能按钮
    lv_obj_t *joke_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(joke_btn, btn_width, btn_height);
    lv_obj_align(joke_btn, LV_ALIGN_TOP_MID, 0, total_buttons_height * 2 + margin);
    lv_obj_t *joke_label = lv_label_create(joke_btn);
    lv_label_set_text(joke_label, LV_SYMBOL_EYE_OPEN" Jokes");
    lv_obj_center(joke_label);
    lv_obj_add_event_cb(joke_btn, jokeUIEventCallback, LV_EVENT_CLICKED, this);

    // 创建设置功能按钮
    lv_obj_t *setting_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(setting_btn, btn_width, btn_height);
    lv_obj_align(setting_btn, LV_ALIGN_TOP_MID, 0, total_buttons_height * 3 + margin);
    lv_obj_t *setting_label = lv_label_create(setting_btn);
    lv_label_set_text(setting_label, LV_SYMBOL_SETTINGS" Settings");
    lv_obj_center(setting_label);
    lv_obj_add_event_cb(setting_btn, settingUIEventCallback, LV_EVENT_CLICKED, this);

    // 创建麦克风功能按钮
    lv_obj_t *microphone_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(microphone_btn, btn_width, btn_height);
    lv_obj_align(microphone_btn, LV_ALIGN_TOP_MID, 0, total_buttons_height * 4 + margin);
    lv_obj_t *microphone_label = lv_label_create(microphone_btn);
    lv_label_set_text(microphone_label, LV_SYMBOL_AUDIO" Voice");
    lv_obj_center(microphone_label);
    lv_obj_add_event_cb(microphone_btn, microphoneUIEventCallback, LV_EVENT_CLICKED, this);
}

// 创建WiFi配置界面
void UIManager::createWiFiConfigUI() {
    if (current_screen_) {
        lv_obj_del(current_screen_);
    }
    
    current_screen_ = lv_obj_create(NULL);
    lv_screen_load(current_screen_);
    
    // 计算自适应尺寸
    int btn_width = calcBtnWidth();
    int btn_height = calcBtnHeight();
    int btn_spacing = calcBtnSpacing();
    int margin = btn_spacing / 2;
    
    ESP_LOGI("UIManager", "创建WiFi配置界面 - 屏幕分辨率: %dx%d", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

    // 创建WiFi状态标签
    wifi_status_label_ = lv_label_create(current_screen_);
    lv_label_set_text(wifi_status_label_, "status: initializing...");
    lv_obj_align(wifi_status_label_, LV_ALIGN_TOP_LEFT, margin, margin);
    
    // 创建IP地址标签
    wifi_ip_label_ = lv_label_create(current_screen_);
    lv_label_set_text(wifi_ip_label_, "IP: unknown");
    lv_obj_align(wifi_ip_label_, LV_ALIGN_TOP_LEFT, margin, margin + btn_height);
    
    // 创建SSID标签
    wifi_ssid_label_ = lv_label_create(current_screen_);
    WiFiManager& wifi_manager = WiFiManager::getInstance();
    std::string ssid = wifi_manager.getSSID();
    if (!ssid.empty()) {
        lv_label_set_text_fmt(wifi_ssid_label_, "SSID: %s", ssid.c_str());
    } else {
        lv_label_set_text(wifi_ssid_label_, "SSID: unknown");
    }
    lv_obj_align(wifi_ssid_label_, LV_ALIGN_TOP_LEFT, margin, margin + btn_height * 2);
    
    // 创建连接按钮
    lv_obj_t *wifi_connect_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(wifi_connect_btn, btn_width, btn_height);
    lv_obj_align(wifi_connect_btn, LV_ALIGN_TOP_RIGHT, -margin, margin*2);
    lv_obj_t *connect_label = lv_label_create(wifi_connect_btn);
    lv_label_set_text(connect_label, "connect WiFi");
    lv_obj_center(connect_label);
    lv_obj_add_event_cb(wifi_connect_btn, wifiConnectBtnCallback, LV_EVENT_CLICKED, this);
    
    // 创建配置按钮
    lv_obj_t *wifi_config_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(wifi_config_btn, btn_width, btn_height);
    lv_obj_align(wifi_config_btn, LV_ALIGN_TOP_RIGHT, -margin, margin*3 + btn_height);
    lv_obj_t *config_label = lv_label_create(wifi_config_btn);
    lv_label_set_text(config_label, "config WiFi");
    lv_obj_center(config_label);
    lv_obj_add_event_cb(wifi_config_btn, wifiQRConfigBtnCallback, LV_EVENT_CLICKED, this);

    // 创建返回按钮
    lv_obj_t *back_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -margin, -margin);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT"back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, backToMainUICallback, LV_EVENT_CLICKED, this);

    // 创建WiFi状态更新定时器（每秒更新一次）
    deleteWifiStatusTimer();
    wifi_status_timer_ = lv_timer_create(wifiStatusTimerCallback, 1000, this);
}

// 创建WiFi配置界面
void UIManager::createWiFiQRUI() {
    if (current_screen_) {
        lv_obj_del(current_screen_);
    }
    
    current_screen_ = lv_obj_create(NULL);
    lv_screen_load(current_screen_);
    
    // 计算自适应尺寸
    int btn_width = calcBtnWidth();
    int btn_height = calcBtnHeight();
    int btn_spacing = calcBtnSpacing();
    int margin = btn_spacing / 2;
    
    ESP_LOGI("UIManager", "创建WiFi二维码配置界面 - 屏幕分辨率: %dx%d", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

    // 创建标题
    lv_obj_t *title_label = lv_label_create(current_screen_);
    lv_label_set_text(title_label, "WiFi QR Code Configuration");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, margin);

    // 创建说明文字
    lv_obj_t *instruction_label = lv_label_create(current_screen_);
    lv_label_set_text(instruction_label, "Scan QR code with your phone\nto configure WiFi settings");
    lv_obj_set_style_text_align(instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(instruction_label, LV_ALIGN_TOP_MID, 0, margin + btn_height);

    // 创建二维码
#if LV_USE_QRCODE
    lv_obj_t *qrcode_obj = lv_qrcode_create(current_screen_);
    lv_qrcode_set_size(qrcode_obj, 150);
    lv_qrcode_set_dark_color(qrcode_obj, lv_color_black());
    lv_qrcode_set_light_color(qrcode_obj, lv_color_white());
    
    // 生成二维码数据 - 包含设备信息和配置URL
    char qr_data[256];
    // 获取设备MAC地址作为唯一标识
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(qr_data, sizeof(qr_data),
             "http://192.168.4.1/configure?device=%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    lv_qrcode_update(qrcode_obj, qr_data, strlen(qr_data));
    lv_obj_align(qrcode_obj, LV_ALIGN_CENTER, 0, 0);
#else
    // 如果不支持二维码，显示错误信息
    lv_obj_t *error_label = lv_label_create(current_screen_);
    lv_label_set_text(error_label, "QR Code feature not available");
    lv_obj_align(error_label, LV_ALIGN_CENTER, 0, 0);
#endif
    
    // 返回按钮
    lv_obj_t *back_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -margin);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, backToWiFiConfigUICallback, LV_EVENT_CLICKED, this);
}


// 添加Setting界面创建函数
void UIManager::createSettingUI() {
    if (current_screen_) {
        lv_obj_del(current_screen_);
    }
    
    current_screen_ = lv_obj_create(NULL);
    lv_screen_load(current_screen_);
    
    // 计算自适应尺寸
    int btn_width = calcBtnWidth();
    int btn_height = calcBtnHeight();
    int btn_spacing = calcBtnSpacing();
    int margin = btn_spacing / 2;
    
    ESP_LOGI("UIManager", "创建设置界面 - 屏幕分辨率: %dx%d", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

    // 创建设置标题
    setting_label_ = lv_label_create(current_screen_);
    lv_label_set_text(setting_label_, "Settings");
    lv_obj_align(setting_label_, LV_ALIGN_TOP_MID, 0, margin);
    
    // 创建旋转按钮
    lv_obj_t *rotate_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(rotate_btn, btn_width+btn_spacing, btn_height);
    lv_obj_align(rotate_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *rotate_label = lv_label_create(rotate_btn);
    lv_label_set_text(rotate_label, LV_SYMBOL_REFRESH" Rotate");
    lv_obj_center(rotate_label);
    lv_obj_add_event_cb(rotate_btn, rotateBtnCallback, LV_EVENT_CLICKED, this);

    // 创建返回按钮
    lv_obj_t *back_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -margin, -margin);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT" Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, backToMainUICallback, LV_EVENT_CLICKED, this);
}

// 创建笑话界面
void UIManager::createJokeUI() {
    if (current_screen_) {
        lv_obj_del(current_screen_);
    }
    
    current_screen_ = lv_obj_create(NULL);
    lv_screen_load(current_screen_);
    // 初始化加载动画指针
    joke_loading_arc_ = nullptr;

    // 计算自适应尺寸
    int btn_width = calcBtnWidth();
    int btn_height = calcBtnHeight();
    int btn_spacing = calcBtnSpacing();
    int margin = btn_spacing / 2;
    
    ESP_LOGI("UIManager", "创建笑话界面 - 屏幕分辨率: %dx%d", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

    lv_obj_t *title_label = lv_label_create(current_screen_);
    lv_label_set_text(title_label, "PROGRAMMING JOKES");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, margin);

    // 创建笑话显示标签
    joke_label_ = lv_label_create(current_screen_);
    lv_label_set_text(joke_label_, "Press button to get a joke!");
    lv_obj_set_width(joke_label_, EXAMPLE_LCD_H_RES - 2 * margin);
    lv_obj_align(joke_label_, LV_ALIGN_TOP_MID, 0, margin + btn_height * 2);
    lv_obj_set_style_text_align(joke_label_, LV_TEXT_ALIGN_CENTER, 0);
    
    // 创建获取笑话按钮
    lv_obj_t *get_joke_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(get_joke_btn, btn_width, btn_height);
    lv_obj_align(get_joke_btn, LV_ALIGN_BOTTOM_LEFT, margin, -margin);
    lv_obj_t *joke_btn_label = lv_label_create(get_joke_btn);
    lv_label_set_text(joke_btn_label, LV_SYMBOL_REFRESH" Refresh");
    lv_obj_center(joke_btn_label);
    lv_obj_add_event_cb(get_joke_btn, getJokeBtnCallback, LV_EVENT_CLICKED, this);

    // 创建返回按钮
    lv_obj_t *back_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -margin, -margin);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT"back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, backToMainUICallback, LV_EVENT_CLICKED, this);
}

// 添加Microphone界面创建函数
void UIManager::createMicrophoneUI() {
    if (current_screen_) {
        lv_obj_del(current_screen_);
    }
    
    current_screen_ = lv_obj_create(NULL);
    lv_screen_load(current_screen_);
    
    // 计算自适应尺寸
    int btn_width = calcBtnWidth();
    int btn_height = calcBtnHeight();
    int btn_spacing = calcBtnSpacing();
    int margin = btn_spacing / 2;
    
    ESP_LOGI("UIManager", "创建麦克风界面 - 屏幕分辨率: %dx%d", EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

    // 创建麦克风状态标签
    microphone_status_label_ = lv_label_create(current_screen_);
    lv_label_set_text(microphone_status_label_, "Microphone Ready - Click Start");
    lv_obj_align(microphone_status_label_, LV_ALIGN_TOP_MID, 0, margin);
    
    // 创建开始/停止按钮
    lv_obj_t *start_stop_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(start_stop_btn, btn_width, btn_height);
    lv_obj_align(start_stop_btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *start_stop_label = lv_label_create(start_stop_btn);
    lv_label_set_text(start_stop_label, "Start Recording");
    lv_obj_center(start_stop_label);
    lv_obj_add_event_cb(start_stop_btn, microphoneStartStopCallback, LV_EVENT_CLICKED, this);

    // 实时录音转喇叭播放按钮
    // lv_obj_t *play_btn = lv_btn_create(current_screen_);
    // lv_obj_set_size(play_btn, btn_width, btn_height);
    // lv_obj_align(play_btn, LV_ALIGN_CENTER, 0, btn_spacing);
    // lv_obj_t *play_label = lv_label_create(play_btn);
    // lv_label_set_text(play_label, "Play Audio");
    // lv_obj_center(play_label);
    // lv_obj_add_event_cb(play_btn, playAudioCallback, LV_EVENT_CLICKED, this);

    // 创建返回按钮
    lv_obj_t *back_btn = lv_btn_create(current_screen_);
    lv_obj_set_size(back_btn, btn_width, btn_height);
    lv_obj_align(back_btn, LV_ALIGN_BOTTOM_RIGHT, -margin, -margin);
    lv_obj_t *back_label = lv_label_create(back_btn);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT" Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(back_btn, backToMainUICallback, LV_EVENT_CLICKED, this);
}

// 添加Microphone按钮事件回调函数
void UIManager::microphoneUIEventCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        ui_manager->switchToMicrophoneUI();
    }
}

// 添加麦克风开始/停止按钮事件回调函数
void UIManager::microphoneStartStopCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        MicrophoneService& microphone_service = MicrophoneService::getInstance();
        lv_obj_t* button = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_obj_t* label = lv_obj_get_child(button, 0);
        
        if (!microphone_service.isRecording()) {
            // 开始录音前先初始化I2S通道
            if (!microphone_service.initialize()) {
                ESP_LOGE("UIManager", "Failed to initialize microphone service");
                if (ui_manager->microphone_status_label_) {
                    lv_label_set_text(ui_manager->microphone_status_label_, "Initialization Failed");
                }
                return;
            }
            // 开始录音
            microphone_service.startRecording();
            lv_label_set_text(label, "Stop Recording");
            if (ui_manager->microphone_status_label_) {
                lv_label_set_text(ui_manager->microphone_status_label_, "Recording Active - Speak now");
            }
            ESP_LOGI("UIManager", "Microphone recording started");
        } else {
            // 停止录音
            microphone_service.stopRecording();
            lv_label_set_text(label, "Start Recording");
            if (ui_manager->microphone_status_label_) {
                lv_label_set_text(ui_manager->microphone_status_label_, "Recording Stopped");
            }
            ESP_LOGI("UIManager", "Microphone recording stopped");
        }
    }
}

void UIManager::playAudioCallback(lv_event_t *e){
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        MicrophoneService& microphone_service = MicrophoneService::getInstance();
        // 播放录音
        // microphone_service.playAudio();
    }
}


// 事件回调函数实现
void UIManager::wifiUIEventCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        ui_manager->switchToWiFiConfigUI();
    }
}

void UIManager::jokeUIEventCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        ui_manager->switchToJokeUI();
    }
}
void UIManager::backToMainUICallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        ui_manager->deleteWifiStatusTimer();
        ui_manager->switchToMainUI();
    }
}

void UIManager::backToWiFiConfigUICallback(lv_event_t *e){
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        WiFiManager& wifi_manager = WiFiManager::getInstance();
        wifi_manager.stopQRConfig();

        ui_manager->switchToWiFiConfigUI();
    }
}

void UIManager::wifiConnectBtnCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        WiFiManager& wifi_manager = WiFiManager::getInstance();
        wifi_manager.connect();
        
        // 更新UI状态
        if (ui_manager->wifi_status_label_) {
            lv_label_set_text(ui_manager->wifi_status_label_, "status: connecting...");
        }
    }
}

void UIManager::wifiQRConfigBtnCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        WiFiManager& wifi_manager = WiFiManager::getInstance();
        wifi_manager.startQRConfig();
        
        ui_manager->switchToWiFiQRUI();
    }
}

// 创建加载动画圆弧
lv_obj_t* UIManager::createLoadingArc(lv_obj_t* parent, lv_obj_t* reference, uint16_t size, int32_t x_offset, int32_t y_offset) {
    if (!parent || !reference) {
        ESP_LOGE("LOADING", "无效的父对象或参考对象");
        return NULL;
    }
    // 创建圆弧对象
    lv_obj_t* arc = lv_arc_create(parent);
    if (!arc) {
        ESP_LOGE("LOADING", "创建圆弧对象失败");
        return NULL;
    }
    lv_obj_set_size(arc, size, size);       // 设置圆弧大小
    lv_arc_set_bg_angles(arc, 0, 360);      // 设置背景角度为360度
    lv_arc_set_angles(arc, 0, 270);        // 设置初始角度为270度
    lv_arc_set_rotation(arc, 270);      // 设置旋转角度为270度
    lv_obj_set_style_arc_width(arc, size/10, 0);      // 设置圆弧宽度
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x0080FF), 0);     // 设置圆弧颜色为蓝色
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, 0);     // 设置圆弧透明度为完全不透明
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB); // 移除旋钮样式
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE); // 禁止点击交互
    
    // 添加旋转动画
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);    /*Just for the demo*/
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_time(&a, 1000);
    lv_anim_start(&a);
    
    // 定位到参考对象附近
    if (reference) {
        lv_obj_align_to(arc, reference, LV_ALIGN_OUT_BOTTOM_MID, x_offset, y_offset);
    }
    
    return arc;
}

// 删除加载动画
void UIManager::deleteLoadingArc(lv_obj_t** arc_ptr) {
    if (arc_ptr && *arc_ptr) {
        if (lv_obj_is_valid(*arc_ptr)) {
            lv_obj_del(*arc_ptr);
        }
        *arc_ptr = nullptr;
    }
}

void UIManager::getJokeBtnCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager && ui_manager->joke_label_) {
        // 如果已经有加载动画，先删除它
        if (ui_manager->joke_loading_arc_) {
            ui_manager->deleteLoadingArc(&ui_manager->joke_loading_arc_);
        }
        // 设置加载文本
        lv_label_set_text(ui_manager->joke_label_, "Loading joke...");
        
        // 创建加载动画
        ui_manager->joke_loading_arc_ = ui_manager->createLoadingArc(
            ui_manager->current_screen_, 
            ui_manager->joke_label_, 
            30, 
            0,
            10
        );
        JokeService& joke_service = JokeService::getInstance();
        // 使用异步回调方式获取笑话
        joke_service.getProgrammingJoke([ui_manager](const std::string& joke, bool success) {
            // 删除加载动画
            if (ui_manager->joke_loading_arc_) {
                ui_manager->deleteLoadingArc(&ui_manager->joke_loading_arc_);
            }
            
            // 更新笑话文本
            if (ui_manager->joke_label_) {
                if (success) {
                    lv_label_set_text(ui_manager->joke_label_, joke.c_str());
                } else {
                    lv_label_set_text(ui_manager->joke_label_, "Failed to get joke");
                }
            }
        });
    }
}




// 添加Setting按钮事件回调函数
void UIManager::settingUIEventCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        ui_manager->switchToSettingUI();
    }
}

// 添加旋转按钮事件回调函数
void UIManager::rotateBtnCallback(lv_event_t *e) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_event_get_user_data(e));
    if (ui_manager) {
        Setting& setting = Setting::getInstance();
        setting.rotateNext();
        
        // 更新UI状态
        if (ui_manager->setting_label_) {
            Setting::Rotation rotation = setting.getCurrentRotation();
            const char* rotation_text = "";
            switch (rotation) {
                case Setting::ROTATION_0:
                    rotation_text = "0°";
                    break;
                case Setting::ROTATION_90:
                    rotation_text = "90°";
                    break;
                case Setting::ROTATION_180:
                    rotation_text = "180°";
                    break;
                case Setting::ROTATION_270:
                    rotation_text = "270°";
                    break;
            }
            lv_label_set_text_fmt(ui_manager->setting_label_, "Settings - Rotation: %s", rotation_text);
        }
    }
}

// 删除WiFi状态更新定时器
void UIManager::deleteWifiStatusTimer() {
    if (wifi_status_timer_) {
        lv_timer_delete(wifi_status_timer_);
        wifi_status_timer_ = nullptr;
        ESP_LOGI("UIManager", "WiFi状态定时器已删除");
    }
}

// WiFi状态更新定时器回调函数
void UIManager::wifiStatusTimerCallback(lv_timer_t* timer) {
    UIManager* ui_manager = static_cast<UIManager*>(lv_timer_get_user_data(timer));
    if (!ui_manager) return;
    
    if (!ui_manager->wifi_status_label_) {
        ESP_LOGI("UIManager", "WiFi status label not created");
        return;
    }
    
    // 更新WiFi状态显示
    WiFiManager& wifi_manager = WiFiManager::getInstance();
    WiFiManager::Status status = wifi_manager.getStatus();
    std::string status_text;
    std::string ip_text;
    
    switch (status) {
        case WiFiManager::Status::DISCONNECTED:
            status_text = "WiFi: disconnected";
            break;
        case WiFiManager::Status::CONNECTING:
            status_text = "WiFi: connecting...";
            break;
        case WiFiManager::Status::CONNECTED:
            status_text = "WiFi: connected";
            break;
        case WiFiManager::Status::FAILED:
            status_text = "WiFi: connection failed";
            break;
    }
    
    std::string ip_addr = wifi_manager.getIP();
    if (ip_addr != "0.0.0.0" && !ip_addr.empty()) {
        ip_text = "IP: " + ip_addr;
    } else {
        ip_text = "IP: not acquired";
    }
    
    // 更新状态标签
    if (ui_manager->wifi_status_label_ && lv_obj_is_valid(ui_manager->wifi_status_label_)) {
        lv_label_set_text(ui_manager->wifi_status_label_, status_text.c_str());
    }
    
    // 更新IP标签
    if (ui_manager->wifi_ip_label_ && lv_obj_is_valid(ui_manager->wifi_ip_label_)) {
        lv_label_set_text(ui_manager->wifi_ip_label_, ip_text.c_str());
    }
    
    // 更新SSID标签
    if (ui_manager->wifi_ssid_label_ && lv_obj_is_valid(ui_manager->wifi_ssid_label_)) {
        std::string ssid = wifi_manager.getSSID();
        if (!ssid.empty()) {
            lv_label_set_text_fmt(ui_manager->wifi_ssid_label_, "SSID: %s", ssid.c_str());
        } else {
            lv_label_set_text(ui_manager->wifi_ssid_label_, "SSID: unknown");
        }
    }
}

// 实现内存保护函数
bool UIManager::isLvglObjectValid(lv_obj_t* obj) {
    if (obj == nullptr) return false;
    
    // 简单的有效性检查
    // 注意：LVGL没有提供标准的对象有效性检查函数
    // 这里使用一些启发式方法
    return (lv_obj_get_parent(obj) != nullptr || lv_obj_get_screen(obj));
}

void UIManager::safeDeleteLvglObject(lv_obj_t** obj_ptr) {
    if (obj_ptr == nullptr || *obj_ptr == nullptr) return;
    
    _lock_acquire(&lvgl_api_lock);
    if (isLvglObjectValid(*obj_ptr)) {
        lv_obj_del(*obj_ptr);
    }
    *obj_ptr = nullptr;
    _lock_release(&lvgl_api_lock);
}