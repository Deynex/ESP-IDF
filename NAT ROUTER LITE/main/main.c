#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "router_nvs.h"
#include "server.h"
#include "status_led.h"
#include "wifi.h"

void app_main(void)
{
    status_led_init();
    ESP_ERROR_CHECK(router_nvs_init());
    router_wifi_start();
    router_server_start();

    while (true)
    {
        status_led_update(router_wifi_is_connected());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
