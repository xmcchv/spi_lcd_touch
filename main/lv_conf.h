#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* 启用LVGL配置 */
#define LV_CONF_SKIP 0

/* 颜色深度 */
#define LV_COLOR_DEPTH 16

/* 字体配置 */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

/* 启用中文字体 */
#define LV_USE_FONT_COMPRESSED 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(lv_font_simsun_16_cjk)

/* 设置默认字体 */
#define LV_FONT_DEFAULT &lv_font_simsun_16_cjk

/* 启用Unicode支持 */
#define LV_USE_ARABIC_PERSIAN_CHARS 1
#define LV_USE_UTF8 1

/* 内存配置 */
#define LV_MEM_SIZE (32U * 1024U)

/* 其他配置 */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#endif /* LV_CONF_H */