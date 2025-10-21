idf_component_register(SRCS "joke.c" "lvgl_main_ui.c" "wifi_ui.c" "wifi_qr_config.c" 
                            "spi_lcd_touch_example_main.c"
                            "wifi.c"
                            "microphone.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp-tls esp_wifi nvs_flash esp_http_client espressif__cjson)

                       # 添加中文字体支持
set(COMPONENT_REQUIRES lvgl lv_font)