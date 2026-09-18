/**
 * @file vehicle.c
 * @brief Implementación de la API de abstracción del vehículo
 * @author DEYNEX
 */

#include "vehicle.h"
#include "a4988.h"
#include "mks_servo42c.h"
#include "brushless.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <stdlib.h>

static const char *TAG = "VEHICLE";

#define MKS_DIR_FORWARD 1
#define MKS_DIR_BACKWARD 0

struct vehicle_handle_s
{
    a4988_dual_handle_t stepper_handle;
    brushless_config_t brushless_cfg;

    float current_rpm;
    a4988_dual_direction_t current_direction;

    bool enabled;
    bool blade_active;
};

// --- Implementación ---

esp_err_t vehicle_init(const vehicle_config_t *config, vehicle_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL)
        return ESP_ERR_INVALID_ARG;

    vehicle_handle_t handle = (vehicle_handle_t)calloc(1, sizeof(struct vehicle_handle_s));
    if (handle == NULL)
        return ESP_ERR_NO_MEM;

    // 1. Inicializar Steppers
    esp_err_t ret = a4988_dual_init(&config->stepper_config, &handle->stepper_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo al inicializar Steppers");
        free(handle);
        return ret;
    }

    // 2. Inicializar Brushless (Hardware PWM)
    // Guardamos la config en el handle para usarla en control_blade
    handle->brushless_cfg = config->brushless_config;
    ret = brushless_init(&handle->brushless_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo al inicializar Brushless");
        a4988_dual_free(&handle->stepper_handle);
        free(handle);
        return ret;
    }

    // 3. Inicializar MKS (State Reset)
    mks_stop();
    mks_set_state(MKS_DISABLE);

    handle->enabled = false;
    handle->blade_active = false;
    handle->current_rpm = 0;
    handle->current_direction = A4988_DUAL_STOP;

    *out_handle = handle;
    ESP_LOGI(TAG, "Vehículo (Podadora) inicializado correctamente");
    return ESP_OK;
}

esp_err_t vehicle_enable(vehicle_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    // Habilitar steppers
    a4988_dual_enable(handle->stepper_handle);

    // Habilitar MKS
    mks_set_state(MKS_ENABLE);

    // Armar ESC (Secuencia crítica de seguridad)
    ESP_LOGI(TAG, "Armando Cuchilla...");
    brushless_arm(&handle->brushless_cfg);

    handle->enabled = true;
    ESP_LOGI(TAG, "Vehículo Habilitado y ESC Armado");
    return ESP_OK;
}

esp_err_t vehicle_disable(vehicle_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    // 1. Parada de emergencia de todo movimiento
    vehicle_stop(handle);

    // 2. APAGAR CUCHILLA INMEDIATAMENTE
    vehicle_control_blade(handle, 0.0f);

    // 3. Deshabilitar hardware
    a4988_dual_disable(handle->stepper_handle);
    mks_set_state(MKS_DISABLE);

    handle->enabled = false;
    ESP_LOGI(TAG, "Vehículo Deshabilitado (Cuchilla Apagada)");
    return ESP_OK;
}

esp_err_t vehicle_stop(vehicle_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    // Detener Ruedas
    a4988_dual_stop(handle->stepper_handle);
    // Detener MKS
    mks_stop();

    // NOTA: NO detenemos la cuchilla aquí.
    // Queremos poder detener el avance pero seguir cortando (ej. césped muy alto).
    // Para apagar todo, usar vehicle_disable() o vehicle_control_blade(0).

    handle->current_rpm = 0;
    handle->current_direction = A4988_DUAL_STOP;
    return ESP_OK;
}

esp_err_t vehicle_move(vehicle_handle_t handle, a4988_dual_direction_t direction, float rpm)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;
    if (direction == A4988_DUAL_STOP || rpm <= 0)
        return vehicle_stop(handle);

    uint8_t mks_direction;
    switch (direction)
    {
    case A4988_DUAL_FORWARD:
    case A4988_DUAL_LEFT:
        mks_direction = MKS_DIR_FORWARD;
        break;
    case A4988_DUAL_BACKWARD:
    case A4988_DUAL_RIGHT:
        mks_direction = MKS_DIR_BACKWARD;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    // Mover Steppers
    a4988_dual_move(handle->stepper_handle, direction, rpm);
    // Mover Servo
    mks_run(mks_direction, (uint16_t)rpm);

    handle->current_rpm = rpm;
    handle->current_direction = direction;
    return ESP_OK;
}

esp_err_t vehicle_control_blade(vehicle_handle_t handle, float throttle_percent)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;

    if (!handle->enabled && throttle_percent > 0)
    {
        ESP_LOGW(TAG, "Intento de encender cuchilla con vehículo deshabilitado");
        return ESP_FAIL;
    }

    esp_err_t ret = brushless_set_throttle(&handle->brushless_cfg, throttle_percent);

    if (ret == ESP_OK)
    {
        handle->blade_active = (throttle_percent > 0);
        ESP_LOGI(TAG, "Cuchilla al %.1f%%", throttle_percent);
    }
    return ret;
}

esp_err_t vehicle_correct_drift(vehicle_handle_t handle)
{
    if (handle == NULL)
        return ESP_ERR_INVALID_ARG;
    vehicle_stop(handle);
    vTaskDelay(pdMS_TO_TICKS(100));

    int32_t pulse_drift = 0;
    if (mks_read_pulses_received(&pulse_drift))
    {
        if (pulse_drift != 0)
        {
            uint8_t dir = (pulse_drift > 0) ? 0 : 1;
            uint32_t pulses_to_fix = abs(pulse_drift);
            mks_move_relative_pulses(dir, 10, pulses_to_fix);
        }
    }
    return ESP_OK;
}

esp_err_t vehicle_free(vehicle_handle_t *handle_ptr)
{
    if (handle_ptr == NULL || *handle_ptr == NULL)
        return ESP_ERR_INVALID_ARG;
    vehicle_handle_t handle = *handle_ptr;

    vehicle_disable(handle); // Apaga todo
    a4988_dual_free(&handle->stepper_handle);
    free(handle);
    *handle_ptr = NULL;
    return ESP_OK;
}