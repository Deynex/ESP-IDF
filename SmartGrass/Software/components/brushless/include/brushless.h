/*
 * brushless.h
 * Controlador para Motor A2208 2600KV + ESC Genérico (XXD/Yellow)
 * Basado en ESP-IDF LEDC Driver
 */

#ifndef BRUSHLESS_H
#define BRUSHLESS_H

#include <stdint.h>
#include "driver/ledc.h"
#include "esp_err.h"

// Parámetros definidos en el Informe Técnico
#define ESC_PWM_FREQ_HZ 50    // Frecuencia estándar para ESCs (50Hz / 20ms)
#define ESC_MIN_PULSE_US 1000 // 1ms: Motor Apagado / Armado
#define ESC_MAX_PULSE_US 2000 // 2ms: 100% Potencia
#define ESC_ARM_DELAY_MS 2000 // Tiempo de espera para secuencia de armado

// Estructura de configuración para inicializar el motor
typedef struct
{
    int gpio_num;               // Pin GPIO donde está conectado el cable de señal (Blanco/Naranja)
    ledc_timer_t timer_num;     // Timer del LEDC a utilizar (0-3)
    ledc_channel_t channel_num; // Canal del LEDC a utilizar (0-7)
    ledc_mode_t speed_mode;     // Modo de velocidad (LEDC_LOW_SPEED_MODE o LEDC_HIGH_SPEED_MODE)
} brushless_config_t;

/**
 * @brief Inicializa el hardware PWM para el ESC
 * @param config Puntero a la estructura de configuración
 * @return ESP_OK si fue exitoso
 */
esp_err_t brushless_init(const brushless_config_t *config);

/**
 * @brief Ejecuta la secuencia de seguridad de armado
 * Envía pulso mínimo (1000us) y espera a que el ESC reconozca la señal.
 * CRÍTICO: El motor no girará si no se llama a esto primero.
 * @param config Puntero a la estructura de configuración
 */
void brushless_arm(const brushless_config_t *config);

/**
 * @brief Establece la potencia del motor en porcentaje (0.0 a 100.0)
 * @param config Puntero a la estructura de configuración
 * @param percentage Valor flotante de 0.0 a 100.0
 * @note El motor empieza a girar aproximadamente desde el 11%
 * @return ESP_OK si el valor es válido
 */
esp_err_t brushless_set_throttle(const brushless_config_t *config, float percentage);

/**
 * @brief Función de bajo nivel para enviar microsegundos crudos
 * Útil para calibración (1000-2000us)
 * @param config Puntero a la estructura de configuración
 * @param width_us Ancho de pulso en microsegundos
 */
esp_err_t brushless_set_pulse_width(const brushless_config_t *config, uint32_t width_us);

#endif // BRUSHLESS_H