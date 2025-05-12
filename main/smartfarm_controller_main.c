#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"  // ✅ esp_rom_delay_us()
#include "esp_adc/adc_oneshot.h"

#define TAG "SMARTFARM"

// 릴레이 및 센서 핀 정의
#define RELAY_LED    GPIO_NUM_4
#define RELAY_PUMP   GPIO_NUM_5
#define RELAY_FAN    GPIO_NUM_6
#define TRIG_PIN     GPIO_NUM_7
#define ECHO_PIN     GPIO_NUM_8

// 조도 센서 ADC 채널
#define LIGHT_ADC_CHANNEL ADC_CHANNEL_6  // GPIO6

adc_oneshot_unit_handle_t adc_handle;

void init_gpio() {
    gpio_reset_pin(RELAY_LED);
    gpio_set_direction(RELAY_LED, GPIO_MODE_OUTPUT);
    gpio_reset_pin(RELAY_PUMP);
    gpio_set_direction(RELAY_PUMP, GPIO_MODE_OUTPUT);
    gpio_reset_pin(RELAY_FAN);
    gpio_set_direction(RELAY_FAN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(TRIG_PIN);
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_reset_pin(ECHO_PIN);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);
}

float measure_distance_cm() {
    gpio_set_level(TRIG_PIN, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_PIN, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_PIN, 0);

    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 0) {
        if ((esp_timer_get_time() - start_time) > 20000) return -1;
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(ECHO_PIN) == 1) {
        if ((esp_timer_get_time() - echo_start) > 20000) return -1;
    }

    int64_t duration = esp_timer_get_time() - echo_start;
    return duration * 0.034 / 2.0;
}

void init_adc() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, LIGHT_ADC_CHANNEL, &chan_cfg));
}

void app_main(void) {
    init_gpio();
    init_adc();

    while (1) {
        int light_raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, LIGHT_ADC_CHANNEL, &light_raw));

        float distance = measure_distance_cm();
        ESP_LOGI(TAG, "💡 조도(raw): %d | 📏 거리: %.2f cm", light_raw, distance);

        vTaskDelay(pdMS_TO_TICKS(3000));  // 3초마다 측정
    }
}
