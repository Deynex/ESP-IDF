/**
 * @file a4988.h
 * @brief API para el driver A4988 en ESP32
 * @author DEYNEX
 */

#ifndef A4988_H
#define A4988_H

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

struct a4988_dual_handle_s;
typedef struct a4988_dual_handle_s *a4988_dual_handle_t;

/**
 * @brief Estructura de configuración para el driver dual (vehículo)
 * Controla múltiples motores con STEP y ENABLE compartidos, pero DIR independientes
 */
typedef struct
{
    gpio_num_t step_pin;        // Pin STEP compartido para todos los motores
    gpio_num_t dir_left_pin;    // Pin DIR para motores del lado izquierdo
    gpio_num_t dir_right_pin;   // Pin DIR para motores del lado derecho
    gpio_num_t enable_pin;      // Pin ENABLE compartido (opcional, usar -1 si no se usa)
    uint32_t steps_per_rev;     // Pasos por revolución (ya incluye microstepping)
    ledc_timer_t timer_num;     // Timer LEDC a usar
    ledc_channel_t channel_num; // Canal LEDC a usar
} a4988_dual_config_t;

/**
 * @brief Dirección del vehículo (modo dual)
 */
typedef enum
{
    A4988_DUAL_FORWARD = 0,  // Avanzar (ambos lados hacia adelante)
    A4988_DUAL_BACKWARD = 1, // Retroceder (ambos lados hacia atrás)
    A4988_DUAL_LEFT = 2,     // Girar a la izquierda (izq atrás, der adelante)
    A4988_DUAL_RIGHT = 3,    // Girar a la derecha (izq adelante, der atrás)
    A4988_DUAL_STOP = 4      // Detener
} a4988_dual_direction_t;

/**
 * @brief Inicializa el driver dual A4988 para vehículo
 *
 * @param config Configuración del driver dual
 * @param handle Puntero donde se almacenará el handle creado
 * @return esp_err_t ESP_OK si tiene éxito
 */
esp_err_t a4988_dual_init(const a4988_dual_config_t *config, a4988_dual_handle_t *handle);

/**
 * @brief Habilita los motores del vehículo
 *
 * @param handle Handle del driver dual
 * @return esp_err_t ESP_OK si tiene éxito
 */
esp_err_t a4988_dual_enable(a4988_dual_handle_t handle);

/**
 * @brief Deshabilita los motores del vehículo
 *
 * @param handle Handle del driver dual
 * @return esp_err_t ESP_OK si tiene éxito
 */
esp_err_t a4988_dual_disable(a4988_dual_handle_t handle);

/**
 * @brief Establece la velocidad del vehículo en RPM
 *
 * @param handle Handle del driver dual
 * @param rpm Velocidad en revoluciones por minuto
 * @return esp_err_t ESP_OK si tiene éxito
 */
esp_err_t a4988_dual_set_speed(a4988_dual_handle_t handle, float rpm);

/**
 * @brief Mueve el vehículo en una dirección específica de forma continua
 *
 * @param handle Handle del driver dual
 * @param direction Dirección del movimiento (FORWARD, BACKWARD, LEFT, RIGHT)
 * @param rpm Velocidad en revoluciones por minuto
 * @return esp_err_t ESP_OK si tiene éxito
 */
esp_err_t a4988_dual_move(a4988_dual_handle_t handle, a4988_dual_direction_t direction, float rpm);

/**
 * @brief Detiene el vehículo inmediatamente
 *
 * @param handle Handle del driver dual
 * @return esp_err_t ESP_OK si tiene éxito
 */
esp_err_t a4988_dual_stop(a4988_dual_handle_t handle);

/**
 * @brief Libera los recursos del driver dual
 *
 * @param handle Handle del driver dual
 * @return esp_err_t ESP_OK si tiene éxito
 */
esp_err_t a4988_dual_free(a4988_dual_handle_t *handle_ptr);

#endif // A4988_H