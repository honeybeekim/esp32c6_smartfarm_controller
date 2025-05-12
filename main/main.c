#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_bt.h"  // Bluetooth용
#include "nvs.h"
#include "esp_mac.h"

#define TAG "SMARTFARM"

// GPIO 핀 정의
#define TRIG_PIN  GPIO_NUM_7
#define ECHO_PIN  GPIO_NUM_6
#define LIGHT_ADC_CHANNEL ADC_CHANNEL_2  // GPIO3
#define SOIL_ADC_CHANNEL  ADC_CHANNEL_1  // GPIO2
#define RELAY_LED   GPIO_NUM_21
#define RELAY_PUMP  GPIO_NUM_5
#define RELAY_FAN   GPIO_NUM_18

// Wi-Fi & MQTT 정보
#define WIFI_SSID       "DS_305"
#define WIFI_PASS       "ds4715555*"
#define MQTT_BROKER_URI "mqtt://test.mosquitto.org"

adc_oneshot_unit_handle_t adc_handle;
esp_mqtt_client_handle_t mqtt_client;

// 릴레이 및 센서 핀 초기화
void init_gpio() {
    gpio_set_direction(RELAY_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_PUMP, GPIO_MODE_OUTPUT);
    gpio_set_direction(RELAY_FAN, GPIO_MODE_OUTPUT);
    gpio_set_direction(TRIG_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(ECHO_PIN, GPIO_MODE_INPUT);

    gpio_set_level(RELAY_LED, 0);
    gpio_set_level(RELAY_PUMP, 0);
    gpio_set_level(RELAY_FAN, 0);
}

// ADC 초기화
void init_adc() {
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, LIGHT_ADC_CHANNEL, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, SOIL_ADC_CHANNEL, &chan_cfg));
}

// 초음파 거리 측정 함수
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

// Wi-Fi 이벤트 핸들러
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGW(TAG, "🔄 Reconnecting Wi-Fi...");
    }
    else if (id == IP_EVENT_STA_GOT_IP) ESP_LOGI(TAG, "📶 Wi-Fi connected");
}

// Wi-Fi 연결 함수
void connect_wifi() {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    wifi_config_t wifi_cfg = { .sta = { .ssid = WIFI_SSID, .password = WIFI_PASS } };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// MQTT 콜백 함수 (✅ 수정된 부분)
static void mqtt_event_handler_cb(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI("MQTT", "Connected to broker");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI("MQTT", "Disconnected from broker");
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI("MQTT", "Received data on topic: %.*s", event->topic_len, event->topic);
            break;
        default:
            ESP_LOGI("MQTT", "Other event id:%d", event->event_id);
            break;
    }
}

// MQTT 연결 함수
void connect_mqtt() {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler_cb, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// 메인 앱
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    init_gpio();
    init_adc();
    connect_wifi();
    connect_mqtt();

    while (1) {
        int light = 0, soil = 0;
        adc_oneshot_read(adc_handle, LIGHT_ADC_CHANNEL, &light);
        adc_oneshot_read(adc_handle, SOIL_ADC_CHANNEL, &soil);
        float dist = measure_distance_cm();

        // 제어 로직
        gpio_set_level(RELAY_LED, light < 1000);
        gpio_set_level(RELAY_PUMP, soil < 1000);
        gpio_set_level(RELAY_FAN, light > 2500);

        // 로깅 출력
        ESP_LOGI(TAG, "☀ 조도: %d | 🌱 수분: %d | 📏 거리: %.2f cm", light, soil, dist);

        // MQTT 전송
        if (mqtt_client) {
            char msg[128];
            snprintf(msg, sizeof(msg), "{\"light\":%d,\"soil\":%d,\"dist\":%.2f}", light, soil, dist);
            esp_mqtt_client_publish(mqtt_client, "smartfarm/sensor", msg, 0, 1, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

