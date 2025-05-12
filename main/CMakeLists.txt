# /home/kcw2935/esp/projects/smartfarm_controller/main/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES
    driver
    esp_wifi
    nvs_flash
    mqtt
    esp_timer
    esp_adc
    bt
)

