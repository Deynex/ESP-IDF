/**
 * @file vehicle.h
 * @brief API de abstracción para controlar el vehículo (Ruedas + Cuchilla)
 * @author DEYNEX
 */

#ifndef VEHICLE_H
#define VEHICLE_H

#include "esp_err.h"
#include "a4988.h"        // Ruedas
#include "mks_servo42c.h" // Servo dirección
#include "brushless.h"    // Cuchilla

/**
 * @brief Handle opaco para el componente del vehículo
 */
struct vehicle_handle_s;
typedef struct vehicle_handle_s *vehicle_handle_t;

/**
 * @brief Configuración completa del vehículo
 */
typedef struct
{
    /** @brief Configuración para los 3 steppers NEMA (Ruedas) */
    a4988_dual_config_t stepper_config;

    /** @brief Configuración para el motor brushless (Cuchilla) */
    brushless_config_t brushless_config;

} vehicle_config_t;

/**
 * @brief Inicializa el componente completo del vehículo
 */
esp_err_t vehicle_init(const vehicle_config_t *config, vehicle_handle_t *out_handle);

/**
 * @brief Habilita todos los motores (Incluyendo armado del ESC)
 * NOTA: Esto hará sonar el ESC indicando armado.
 */
esp_err_t vehicle_enable(vehicle_handle_t handle);

/**
 * @brief Deshabilita todos los motores y APAGA LA CUCHILLA
 */
esp_err_t vehicle_disable(vehicle_handle_t handle);

/**
 * @brief Mueve el vehículo (Ruedas)
 */
esp_err_t vehicle_move(vehicle_handle_t handle, a4988_dual_direction_t direction, float rpm);

/**
 * @brief Detiene el movimiento de las ruedas
 * NOTA: NO detiene la cuchilla (para permitir cortar sin moverse),
 * a menos que se llame a vehicle_disable().
 */
esp_err_t vehicle_stop(vehicle_handle_t handle);

/**
 * @brief Controla la potencia de la cuchilla
 * @param throttle_percent Porcentaje de potencia (0.0 a 100.0)
 */
esp_err_t vehicle_control_blade(vehicle_handle_t handle, float throttle_percent);

/**
 * @brief Libera los recursos
 */
esp_err_t vehicle_free(vehicle_handle_t *handle_ptr);

/**
 * @brief Corrige desfase MKS
 */
esp_err_t vehicle_correct_drift(vehicle_handle_t handle);

#endif // VEHICLE_H