cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# 设置使用自定义分区表
set(PARTITION_TABLE_CUSTOM ON)
set(PARTITION_TABLE_FILENAME "partitions.csv")

project(spi_lcd_touch)