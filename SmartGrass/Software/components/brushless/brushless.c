/*
 * brushless.c
 * Implementación de control para A2208 8T usando ESP-IDF LEDC
 */

#include "brushless.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

// Usamos 14 bits para tener buena resolución a 50Hz
// Resolución: 2^14 = 16384 pasos totales en 20ms
#define LEDC_DUTY_RES LEDC_TIMER_14_BIT

static uint32_t calculate_duty(uint32_t pulse_us)
{
    // Periodo total a 50Hz es 20000us (20ms)
    // Duty = (pulse_us / 20000) * (2^14 - 1)
    const uint32_t max_duty = (1 << LEDC_DUTY_RES) - 1;
    uint32_t duty = (uint32_t)((float)pulse_us * (float)max_duty / 20000.0f);
    return duty;
}

esp_err_t brushless_init(const brushless_config_t *config)
{
    // 1. Configuración del Timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode = config->speed_mode,
        .timer_num = config->timer_num,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = ESC_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Configuración del Canal
    ledc_channel_config_t ledc_channel = {
        .speed_mode = config->speed_mode,
        .channel = config->channel_num,
        .timer_sel = config->timer_num,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = config->gpio_num,
        .duty = 0, // Iniciar en 0
        .hpoint = 0,
        .flags.output_invert = 0,
        .deconfigure = false,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    return ESP_OK;
}

esp_err_t brushless_set_pulse_width(const brushless_config_t *config, uint32_t width_us)
{
    // Protección de seguridad según informe (límites físicos)
    if (width_us < ESC_MIN_PULSE_US)
        width_us = ESC_MIN_PULSE_US;
    if (width_us > ESC_MAX_PULSE_US)
        width_us = ESC_MAX_PULSE_US;

    uint32_t duty = calculate_duty(width_us);

    // 1. Configurar el valor en el registro
    esp_err_t err = ledc_set_duty(config->speed_mode, config->channel_num, duty);
    if (err != ESP_OK) return err;
    
    // 2. Forzar la actualización inmediata del hardware
    return ledc_update_duty(config->speed_mode, config->channel_num);
}

void brushless_arm(const brushless_config_t *config)
{
    // Paso crítico: Enviar señal de 0% (1000us)
    brushless_set_pulse_width(config, ESC_MIN_PULSE_US);

    // Esperar a que el ESC arranque y haga los pitidos de conteo de celdas
    // Según informe: 'Beep-Beep' para indicar detección de batería LiPo
    // NOTA: Dado a que esta funcion se llama por primera vez en app main, opera en el core 0
    // pero el servidor se llama despues, igual en el core 0, por lo que cuando se active el brushless
    // la telemetría dejara de funcionar temporalmente. Idealmente habría que hacer esto en otro core.
    // vTaskDelay(pdMS_TO_TICKS(ESC_ARM_DELAY_MS));
}

esp_err_t brushless_set_throttle(const brushless_config_t *config, float percentage)
{
    if (percentage < 0.0f)
        percentage = 0.0f;
    if (percentage > 100.0f)
        percentage = 100.0f;

    // Mapeo lineal: 0% -> 1000us, 100% -> 2000us
    uint32_t pulse_width = (uint32_t)(ESC_MIN_PULSE_US + (percentage / 100.0f) * (ESC_MAX_PULSE_US - ESC_MIN_PULSE_US));

    return brushless_set_pulse_width(config, pulse_width);
}