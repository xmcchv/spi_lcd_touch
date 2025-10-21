/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "spi_lcd_touch_example_main.h"
#include "lvgl_main_ui.h"
#include "wifi_ui.h"

#if LV_USE_QRCODE
#include "libs/qrcode/lv_qrcode.h"
#endif

#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
#include "esp_lcd_ili9341.h"
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
#include "esp_lcd_gc9a01.h"
#endif

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
#include "esp_lcd_touch_stmpe610.h"
#elif CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
#include "esp_lcd_touch_xpt2046.h"
#endif

static const char *TAG = "example";

// Using SPI2 for LCD, SPI3 for touch
#define LCD_HOST  SPI2_HOST
#define TOUCH_HOST SPI3_HOST

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     (20 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL

// LCD SPI pins (屏幕SPI引脚)
#define EXAMPLE_PIN_NUM_SCLK           18
#define EXAMPLE_PIN_NUM_MOSI           19
#define EXAMPLE_PIN_NUM_MISO           21

// LCD control pins (屏幕控制引脚)
#define EXAMPLE_PIN_NUM_LCD_DC         5
#define EXAMPLE_PIN_NUM_LCD_RST        3
#define EXAMPLE_PIN_NUM_LCD_CS         4
#define EXAMPLE_PIN_NUM_BK_LIGHT       2

// Touch SPI pins (触摸SPI引脚 - 使用独立的SPI总线)
#define EXAMPLE_PIN_NUM_TOUCH_SCLK     7   // T_CLK
#define EXAMPLE_PIN_NUM_TOUCH_MOSI     6   // T_DIN  
#define EXAMPLE_PIN_NUM_TOUCH_MISO     8   // T_DO
#define EXAMPLE_PIN_NUM_TOUCH_CS       15  // T_CS
#define EXAMPLE_PIN_NUM_TOUCH_IRQ      16  // T_IRQ (中断引脚)

// The pixel number in horizontal and vertical
#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
#define EXAMPLE_LCD_H_RES              240
#define EXAMPLE_LCD_V_RES              320
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
#define EXAMPLE_LCD_H_RES              240
#define EXAMPLE_LCD_V_RES              240
#endif
// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS           8
#define EXAMPLE_LCD_PARAM_BITS         8

#define EXAMPLE_LVGL_DRAW_BUF_LINES    20 // number of display lines in each draw buffer
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1000 / CONFIG_FREERTOS_HZ
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (16 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     2

// LVGL library is not thread-safe, this example will call LVGL APIs from different tasks, so use a mutex to protect it
static _lock_t lvgl_api_lock;

//==========================================================
// WiFi相关变量定义（在源文件中定义）
lv_obj_t *wifi_status_label = NULL;
lv_obj_t *wifi_ssid_label = NULL;
lv_obj_t *wifi_ip_label = NULL;
lv_obj_t *wifi_connect_btn = NULL;
lv_obj_t *wifi_config_btn = NULL;
lv_timer_t *wifi_status_timer = NULL;

// WiFi配置界面变量定义
lv_obj_t *wifi_config_scr = NULL;
lv_obj_t *wifi_qrcode_obj = NULL;
lv_obj_t *wifi_instruction_label = NULL;
lv_obj_t *wifi_back_btn = NULL;
lv_obj_t *wifi_ssid_textarea = NULL;
lv_obj_t *wifi_password_textarea = NULL;
lv_obj_t *wifi_save_btn = NULL;

void delete_wifi_status_timer(void) {
    if (wifi_status_timer) {
        lv_timer_delete(wifi_status_timer);
        wifi_status_timer = NULL;
        ESP_LOGI("UI_MAIN", "WiFi状态定时器已删除");
    }
}

// WiFi状态更新定时器回调
void wifi_status_timer_cb(lv_timer_t *timer) {
    lv_display_t *display = (lv_display_t *)lv_timer_get_user_data(timer);
    if (!display) return;
    if (!wifi_status_label) {
        ESP_LOGI(TAG, "WiFi status label not created");
        return;
    }
    // 更新WiFi状态显示
    wifi_status_t status = wifi_get_status();
    char status_text[32];
    char ip_text[32];
    
    switch (status) {
        case WIFI_DISCONNECTED:
            strcpy(status_text, "WiFi: disconnected");
            break;
        case WIFI_CONNECTING:
            strcpy(status_text, "WiFi: connecting...");
            break;
        case WIFI_CONNECTED:
            strcpy(status_text, "WiFi: connected");
            break;
        case WIFI_FAILED:
            strcpy(status_text, "WiFi: connection failed");
            break;
    }
    
    char* ip_addr = wifi_get_ip();
    if (strcmp(ip_addr, "0.0.0.0") != 0) {
        snprintf(ip_text, sizeof(ip_text), "IP: %s", ip_addr);
    } else {
        strcpy(ip_text, "IP: not acquired");
    }
    
    if (wifi_status_label && lv_obj_is_valid(wifi_status_label)) {
        lv_label_set_text(wifi_status_label, status_text);  
    }
    if (wifi_ip_label) {
        lv_label_set_text(wifi_ip_label, ip_text);
    }
}

// WiFi连接按钮回调（修改为在未配置时显示二维码）
void wifi_connect_btn_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    (void)obj;
    
    app_wifi_config_t config;
    wifi_get_config(&config);
    
    if (config.is_configured) {
        wifi_connect(config.ssid, config.password);
    } else {
        lv_display_t *disp = lv_event_get_user_data(e);
        
        // 启动WiFi配置服务器
        wifi_qr_config_start();
        
        // 显示二维码配置界面
        create_wifi_config_ui(disp);
    }
}
// WiFi配置按钮回调（修改为启动二维码配置）
void wifi_config_btn_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    (void)obj;
    
    lv_display_t *disp = lv_event_get_user_data(e);
    
    // 启动WiFi配置服务器
    wifi_qr_config_start();
    
    // 显示二维码配置界面
    create_wifi_config_ui(disp);
}

// 创建WiFi配置界面（修改为二维码方式）
void create_wifi_config_ui(lv_display_t *disp) {
    // 创建配置界面屏幕
    wifi_config_scr = lv_obj_create(NULL);
    lv_screen_load(wifi_config_scr);
    
    // 创建标题
    lv_obj_t *title_label = lv_label_create(wifi_config_scr);
    lv_label_set_text(title_label, "WiFi QR Code Configuration");
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 20);
    
    // 创建说明文字
    wifi_instruction_label = lv_label_create(wifi_config_scr);
    lv_label_set_text(wifi_instruction_label, "Scan QR code with your phone\nto configure WiFi settings");
    lv_obj_set_style_text_align(wifi_instruction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(wifi_instruction_label, LV_ALIGN_TOP_MID, 0, 50);
    
    // 创建二维码
#if LV_USE_QRCODE
    wifi_qrcode_obj = lv_qrcode_create(wifi_config_scr);
    lv_qrcode_set_size(wifi_qrcode_obj, 150);
    lv_qrcode_set_dark_color(wifi_qrcode_obj, lv_color_black());
    lv_qrcode_set_light_color(wifi_qrcode_obj, lv_color_white());
    
    // 生成二维码数据 - 包含设备信息和配置URL
    char qr_data[256];
    // 获取设备MAC地址作为唯一标识
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(qr_data, sizeof(qr_data), 
             "http://192.168.4.1/configure?device=%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    lv_qrcode_update(wifi_qrcode_obj, qr_data, strlen(qr_data));
    lv_obj_align(wifi_qrcode_obj, LV_ALIGN_CENTER, 0, 0);
#else
    // 如果不支持二维码，显示错误信息
    lv_obj_t *error_label = lv_label_create(wifi_config_scr);
    lv_label_set_text(error_label, "QR Code feature not available");
    lv_obj_align(error_label, LV_ALIGN_CENTER, 0, 0);
#endif
    
    // 返回按钮
    wifi_back_btn = lv_btn_create(wifi_config_scr);
    lv_obj_set_size(wifi_back_btn, 100, 40);
    lv_obj_align(wifi_back_btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t *back_label = lv_label_create(wifi_back_btn);
    lv_label_set_text(back_label, "Back");
    lv_obj_center(back_label);
    lv_obj_add_event_cb(wifi_back_btn, wifi_config_back_btn_cb, LV_EVENT_CLICKED, disp);
    
}

// WiFi保存按钮回调（不再需要，因为通过二维码配置）
void wifi_save_btn_cb(lv_event_t *e) {
    lv_display_t *disp = (lv_display_t *)lv_event_get_user_data(e);
    example_lvgl_demo_ui(disp);
}

// WiFi返回按钮回调（修改为停止配置服务器）
void wifi_config_back_btn_cb(lv_event_t *e) {
    lv_display_t *disp = (lv_display_t *)lv_event_get_user_data(e);
    
    // 停止WiFi配置服务器
    wifi_qr_config_stop();
    
    wifi_ui(disp);
    // example_lvgl_demo_ui(disp);
}


static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver parameters are updated. */
static void example_lvgl_port_update_callback(lv_display_t *disp)
{
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    lv_display_rotation_t rotation = lv_display_get_rotation(disp);

    switch (rotation) {
    case LV_DISPLAY_ROTATION_0:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, true, false);
        break;
    case LV_DISPLAY_ROTATION_90:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, true, true);
        break;
    case LV_DISPLAY_ROTATION_180:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, false);
        esp_lcd_panel_mirror(panel_handle, false, true);
        break;
    case LV_DISPLAY_ROTATION_270:
        // Rotate LCD display
        esp_lcd_panel_swap_xy(panel_handle, true);
        esp_lcd_panel_mirror(panel_handle, false, false);
        break;
    }
}

static void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    example_lvgl_port_update_callback(disp);
    esp_lcd_panel_handle_t panel_handle = lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // because SPI LCD is big-endian, we need to swap the RGB bytes order
    lv_draw_sw_rgb565_swap(px_map, (offsetx2 + 1 - offsetx1) * (offsety2 + 1 - offsety1));
    // copy a buffer's content to a specific area of the display
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
}

// 触摸回调函数实现
static void example_lvgl_touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;
    
    esp_lcd_touch_handle_t touch_pad = lv_indev_get_user_data(indev);
    esp_lcd_touch_read_data(touch_pad);
    /* Get coordinates */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(touch_pad, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGI(TAG, "Touchpad pressed at: x=%d, y=%d", touchpad_x[0], touchpad_y[0]);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t time_till_next_ms = 0;
    while (1) {
        _lock_acquire(&lvgl_api_lock);
        time_till_next_ms = lv_timer_handler();
        _lock_release(&lvgl_api_lock);
        // in case of triggering a task watch dog time out
        time_till_next_ms = MAX(time_till_next_ms, EXAMPLE_LVGL_TASK_MIN_DELAY_MS);
        // in case of lvgl display not ready yet
        time_till_next_ms = MIN(time_till_next_ms, EXAMPLE_LVGL_TASK_MAX_DELAY_MS);
        usleep(1000 * time_till_next_ms);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "初始化WiFi");
    wifi_init();

    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

    ESP_LOGI(TAG, "Initialize LCD SPI bus");
    spi_bus_config_t lcd_buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_SCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_MOSI,
        .miso_io_num = EXAMPLE_PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = EXAMPLE_LCD_H_RES * 80 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &lcd_buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Initialize Touch SPI bus");
    spi_bus_config_t touch_buscfg = {
        .sclk_io_num = EXAMPLE_PIN_NUM_TOUCH_SCLK,
        .mosi_io_num = EXAMPLE_PIN_NUM_TOUCH_MOSI,
        .miso_io_num = EXAMPLE_PIN_NUM_TOUCH_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1024,  // 触摸数据传输量较小
    };
    ESP_ERROR_CHECK(spi_bus_initialize(TOUCH_HOST, &touch_buscfg, SPI_DMA_DISABLED));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS,
        .lcd_param_bits = EXAMPLE_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
#if CONFIG_EXAMPLE_LCD_CONTROLLER_ILI9341
    ESP_LOGI(TAG, "Install ILI9341 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel_handle));
#elif CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    ESP_LOGI(TAG, "Install GC9A01 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
#endif

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
#if CONFIG_EXAMPLE_LCD_CONTROLLER_GC9A01
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
#endif
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));

    // user can flush pre-defined pattern to the screen before we turn on the screen or backlight
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    ESP_LOGI(TAG, "Initialize LVGL library");
    lv_init();

    // create a lvgl display
    lv_display_t *display = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);

    // alloc draw buffers used by LVGL
    // it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    size_t draw_buffer_sz = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_DRAW_BUF_LINES * sizeof(lv_color16_t);

    void *buf1 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    assert(buf1);
    void *buf2 = spi_bus_dma_memory_alloc(LCD_HOST, draw_buffer_sz, 0);
    assert(buf2);
    // initialize LVGL draw buffers
    lv_display_set_buffers(display, buf1, buf2, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);
    // associate the mipi panel handle to the display
    lv_display_set_user_data(display, panel_handle);
    // set color depth
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    // set the callback which can copy the rendered image to an area of the display
    lv_display_set_flush_cb(display, example_lvgl_flush_cb);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

    ESP_LOGI(TAG, "Register io panel event callback for LVGL flush ready notification");
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = example_notify_lvgl_flush_ready,
    };
    /* Register done callback */
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display));

#if CONFIG_EXAMPLE_LCD_TOUCH_ENABLED
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_spi_config_t tp_io_config =
#ifdef CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
        ESP_LCD_TOUCH_IO_SPI_STMPE610_CONFIG(EXAMPLE_PIN_NUM_TOUCH_CS);
#elif CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
        ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(EXAMPLE_PIN_NUM_TOUCH_CS);
#endif
    // Attach the TOUCH to the separate SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)TOUCH_HOST, &tp_io_config, &tp_io_handle));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_V_RES,
        .y_max = EXAMPLE_LCD_H_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_IRQ,  // 启用中断功能
        .flags = {
            .swap_xy = 1,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_lcd_touch_handle_t tp = NULL;

#if CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_STMPE610
    ESP_LOGI(TAG, "Initialize touch controller STMPE610");
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_stmpe610(tp_io_handle, &tp_cfg, &tp));
#elif CONFIG_EXAMPLE_LCD_TOUCH_CONTROLLER_XPT2046
    ESP_LOGI(TAG, "Initialize touch controller XPT2046");
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_cfg, &tp));
#endif

    static lv_indev_t *indev;
    indev = lv_indev_create(); // Input device driver (Touch)
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_user_data(indev, tp);
    lv_indev_set_read_cb(indev, example_lvgl_touch_cb);

    // 显示主界面
    _lock_acquire(&lvgl_api_lock);
    example_lvgl_demo_ui(display);
    _lock_release(&lvgl_api_lock);
    
    ESP_LOGI(TAG, "主界面已启动");
#else
    // 如果没有触摸功能，直接显示主界面
    _lock_acquire(&lvgl_api_lock);
    example_lvgl_demo_ui(display);
    _lock_release(&lvgl_api_lock);
#endif

    ESP_LOGI(TAG, "Start LVGL task");
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);
}



/**
 * 创建转圈加载动画
 * @param parent 父对象（通常是屏幕）
 * @param ref_obj 参考对象（用于对齐，如joke_label）
 * @param size 动画尺寸（宽高相同）
 * @param y_offset 与参考对象的垂直间距
 * @return 创建的动画对象指针（失败返回NULL）
 */
lv_obj_t* create_loading_arc(lv_obj_t* parent, lv_obj_t* ref_obj, uint16_t size, int32_t y_offset) {
    if (!parent || !ref_obj) {
        ESP_LOGE("LOADING", "无效的父对象或参考对象");
        return NULL;
    }

    // 创建圆弧对象
    lv_obj_t* arc = lv_arc_create(parent);
    if (!arc) {
        ESP_LOGE("LOADING", "创建圆弧对象失败");
        return NULL;
    }

    // 配置圆弧样式和属性
    lv_arc_set_rotation(arc, 0);                  // 设置旋转角度
    lv_arc_set_bg_angles(arc, 0, 360);            // 设置背景圆弧角度（完整圆形）
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB); // 移除旋钮样式
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE); // 禁止点击交互
    lv_obj_set_size(arc, size, size);             // 设置尺寸

    // 对齐到参考对象下方中间位置
    lv_obj_align_to(arc, ref_obj, LV_ALIGN_OUT_BOTTOM_MID, 0, y_offset);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, set_angle);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);    /*Just for the demo*/
    lv_anim_set_repeat_delay(&a, 500);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_start(&a);
    return arc;
}


/**
 * 停止并删除转圈加载动画
 * @param arc 动画对象指针的指针（用于置空避免悬空指针）
 */
void delete_loading_arc(lv_obj_t** arc) {
    if (arc && *arc) {
        // 检查对象是否有效
        if (lv_obj_is_valid(*arc)) {
            lv_obj_del(*arc); // 删除动画对象
        }
        *arc = NULL; // 置空指针，避免悬空引用
        ESP_LOGI("LOADING", "转圈动画已删除");
    }
}
