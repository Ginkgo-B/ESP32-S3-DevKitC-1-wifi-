#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "sdkconfig.h"

static const char *TAG = "wifi_blink";

#define BLINK_GPIO CONFIG_BLINK_GPIO

static uint8_t s_led_state = 0;
static led_strip_handle_t led_strip;

// Wi-Fi 配置
#define WIFI_SSID      "k70"
#define WIFI_PASS      "99999999ik"

#define RSSI_MAX       -10   // 信号最强 dBm
#define RSSI_MIN       -90   // 信号最弱 dBm
#define PERIOD_MIN_MS  50   // LED 最快闪烁 100ms
#define PERIOD_MAX_MS  1000  // LED 最慢闪烁 1000ms

// 映射 RSSI 到周期
static uint32_t rssi_to_period(int8_t rssi)
{
    if(rssi > RSSI_MAX) rssi = RSSI_MAX;
    if(rssi < RSSI_MIN) rssi = RSSI_MIN;
    // 线性映射: 信号越强周期越短
    uint32_t period = PERIOD_MIN_MS + ((RSSI_MAX - rssi) * (PERIOD_MAX_MS - PERIOD_MIN_MS)) / (RSSI_MAX - RSSI_MIN);
    return period;
}

// 初始化 Wi-Fi STA
static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
    
    ESP_LOGI(TAG, "Wi-Fi STA initialized, connecting to %s", WIFI_SSID);
}

// LED 初始化
static void configure_led(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1,
    };
#if CONFIG_BLINK_LED_STRIP
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10*1000*1000,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
#else
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
#endif
}

// LED 点亮/熄灭
static void blink_led(void)
{
#if CONFIG_BLINK_LED_STRIP
    if(s_led_state) {
        led_strip_set_pixel(led_strip, 0, 5,5,5);
        led_strip_refresh(led_strip);
    } else {
        led_strip_clear(led_strip);
    }
#else
    gpio_set_level(BLINK_GPIO, s_led_state);
#endif
}

// 获取当前 Wi-Fi RSSI
static int8_t get_current_rssi(void)
{
    wifi_ap_record_t ap_info;
    if(esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK){
        return ap_info.rssi;
    }
    return RSSI_MIN; // 获取失败则返回最小值
}

void app_main(void)
{
    configure_led();
    wifi_init_sta();
    
    while(1){
        int8_t rssi = get_current_rssi();
        uint32_t period = rssi_to_period(rssi);
        ESP_LOGI(TAG, "RSSI: %d dBm, period: %d ms", rssi, period);
        
        s_led_state = !s_led_state;
        blink_led();
        vTaskDelay(pdMS_TO_TICKS(period));
    }
}