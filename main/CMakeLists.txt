# /home/kcw2935/esp/projects/smartfarm_controller/main/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "smartfarm_controller_main.c"
    PRIV_REQUIRES esp_timer esp_driver_gpio esp_adc
    INCLUDE_DIRS "."
)

