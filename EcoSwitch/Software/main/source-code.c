#include "switch.h"
#include "wifi.h"
#include "server.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"

void app_main(void)
{
    // Inicializar NVS, WiFi y servidor HTTP
    nvs_init();
    wifi_init();
    http_server_init();

    // Inicializar el interruptor y ADC
    switch_init();
    switch_start_adc();

    while (1)
    {
        printf("Corriente RMS medida: %.5f A\n", switch_get_current_rms());
        printf("\033[A\033[K"); // Limpia la línea anterior
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}