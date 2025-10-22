idf_component_register(SRCS "main.cpp" 
"Application.cpp" "JokeService.cpp" 
"WiFiManager.cpp" "UIManager.cpp" 
"Setting.cpp" "MicrophoneService.cpp"
INCLUDE_DIRS "."
REQUIRES esp-tls esp_wifi nvs_flash esp_http_client espressif__cjson)

# 添加中文字体支持
set(COMPONENT_REQUIRES lvgl lv_font)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)