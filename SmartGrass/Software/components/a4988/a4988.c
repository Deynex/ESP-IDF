/**
 * @file a4988.c
 * @brief Implementación del driver A4988 en ESP32
 * @author DEYNEX
 */

#include "a4988.h"
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"

static const char *TAG = "A4988";

/*
 * Configuración PWM: se usa modo low speed para evitar interferencias con otros
 * periféricos y se fija un duty reducido que genere pulsos nítidos sin saturar
 * el A4988. La frecuencia máxima protege tanto al motor como al driver.
 */
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES LEDC_TIMER_13_BIT        // Resolución de 13 bits que ofrece un duty suficientemente fino
#define LEDC_DUTY (0.1 * (1 << LEDC_DUTY_RES)) // Ciclo de trabajo aprox. 10% para pulsos nítidos sin sobrecargar el driver
#define LEDC_MAX_FREQ 4000                     // Límite superior para proteger el motor y el A4988

/**
 * @brief Dirección del motor
 */
typedef enum
{
    A4988_DIR_CW = 0, // Sentido horario
    A4988_DIR_CCW = 1 // Sentido antihorario
} a4988_direction_t;
// ---------------------------------------------

/**
 * @brief Estructura del handle
 * Esta es la definición "real" de la estructura oculta.
 */
struct a4988_dual_handle_s
{
    gpio_num_t step_pin;        // Pin STEP común que distribuye los pulsos a ambos lados
    gpio_num_t dir_left_pin;    // Pin DIR para motores del lado izquierdo
    gpio_num_t dir_right_pin;   // Pin DIR para motores del lado derecho
    gpio_num_t enable_pin;      // Pin ENABLE compartido (-1 indica que no se cablea)
    uint32_t steps_per_rev;     // Pasos eléctricos por vuelta considerando microstepping
    ledc_timer_t timer_num;     // Timer LEDC a utilizar para producir los flancos de STEP
    ledc_channel_t channel_num; // Canal LEDC asociado al pin STEP
    uint32_t current_frequency; // Frecuencia actual programada en Hz
    bool enabled;               // Estado lógico del pin ENABLE
    volatile bool running;      // Flag para indicar si el PWM está activo de forma continua
};

// --- Implementación de Funciones Públicas ---

esp_err_t a4988_dual_init(const a4988_dual_config_t *config, a4988_dual_handle_t *handle)
{
    if (config == NULL || handle == NULL)
    {
        ESP_LOGE(TAG, "Parámetros inválidos");
        return ESP_ERR_INVALID_ARG;
    }

    // Reservar memoria para el handle que encapsula la configuración persistente
    a4988_dual_handle_t new_handle = (a4988_dual_handle_t)malloc(sizeof(struct a4988_dual_handle_s));
    if (new_handle == NULL)
    {
        ESP_LOGE(TAG, "Error al reservar memoria");
        *handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Copiar configuración
    new_handle->step_pin = config->step_pin;
    new_handle->dir_left_pin = config->dir_left_pin;
    new_handle->dir_right_pin = config->dir_right_pin;
    new_handle->enable_pin = config->enable_pin;
    new_handle->steps_per_rev = config->steps_per_rev;
    new_handle->timer_num = config->timer_num;
    new_handle->channel_num = config->channel_num;
    new_handle->current_frequency = 0;
    new_handle->enabled = false;
    new_handle->running = false;

    // Configurar timer LEDC responsable de la base de tiempo de los pulsos STEP
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = new_handle->timer_num,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = 100, // Frecuencia inicial
        .clk_cfg = LEDC_USE_APB_CLK,
    };
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al configurar timer LEDC");
        free(new_handle);
        *handle = NULL;
        return ret;
    }

    // Configurar canal LEDC que efectivamente conmuta el pin STEP compartido
    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = new_handle->channel_num,
        .timer_sel = new_handle->timer_num,
        .gpio_num = new_handle->step_pin,
        .duty = 0, // Inicialmente apagado
        .hpoint = 0,
    };
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al configurar canal LEDC");
        free(new_handle);
        *handle = NULL;
        return ret;
    }

    // Configurar pines DIR (izquierdo y derecho) como GPIO para permitir sentidos independientes
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
        .pin_bit_mask = (1ULL << new_handle->dir_left_pin) | (1ULL << new_handle->dir_right_pin),
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al configurar pines DIR");
        free(new_handle);
        *handle = NULL;
        return ret;
    }
    gpio_set_level(new_handle->dir_left_pin, 0);
    gpio_set_level(new_handle->dir_right_pin, 0);

    // Configurar ENABLE pin compartido (si está definido) para controlar ambos drivers a la vez
    if (new_handle->enable_pin != -1)
    {
        io_conf.pin_bit_mask = (1ULL << new_handle->enable_pin);
        ret = gpio_config(&io_conf);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Error al configurar pin ENABLE");
            free(new_handle);
            *handle = NULL;
            return ret;
        }
        gpio_set_level(new_handle->enable_pin, 1); // HIGH = deshabilitado
    }

    ESP_LOGI(TAG, "A4988 DUAL inicializado correctamente");
    ESP_LOGI(TAG, "STEP: GPIO%d (compartido), DIR_LEFT: GPIO%d, DIR_RIGHT: GPIO%d, ENABLE: GPIO%d",
             new_handle->step_pin, new_handle->dir_left_pin, new_handle->dir_right_pin, new_handle->enable_pin);
    ESP_LOGI(TAG, "Timer: %d, Canal: %d", new_handle->timer_num, new_handle->channel_num);

    // Devolvemos el handle creado
    *handle = new_handle;
    return ESP_OK;
}

// El resto de funciones ahora usan 'handle' como el puntero opaco

esp_err_t a4988_dual_enable(a4988_dual_handle_t handle)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->enable_pin != -1)
    {
        gpio_set_level(handle->enable_pin, 0); // LOW = habilitado (el A4988 activa con nivel bajo)
        handle->enabled = true;
        ESP_LOGI(TAG, "Motores del vehículo habilitados");
    }

    return ESP_OK;
}

esp_err_t a4988_dual_disable(a4988_dual_handle_t handle)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Detener PWM si está corriendo para evitar pasos residuales antes de cortar energía
    if (handle->running)
    {
        a4988_dual_stop(handle);
    }

    if (handle->enable_pin != -1)
    {
        gpio_set_level(handle->enable_pin, 1); // HIGH = deshabilitado (deja el motor libre y reduce consumo)
        handle->enabled = false;
        ESP_LOGI(TAG, "Motores del vehículo deshabilitados");
    }

    return ESP_OK;
}

esp_err_t a4988_dual_set_speed(a4988_dual_handle_t handle, float rpm)
{
    if (handle == NULL || rpm < 0) // Permitir rpm = 0 para detener
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (rpm == 0)
    {
        // Tratar rpm 0 como un stop
        return a4988_dual_stop(handle);
    }

    // Calcular frecuencia en Hz desde RPM para traducir la consigna mecánica a pulsos STEP
    float frequency_hz = (rpm / 60.0f) * handle->steps_per_rev;

    if (frequency_hz > LEDC_MAX_FREQ)
    {
        ESP_LOGW(TAG, "Frecuencia limitada a %d Hz", LEDC_MAX_FREQ);
        frequency_hz = LEDC_MAX_FREQ;
    }

    // Actualizar frecuencia del timer para que el canal STEP emita al ritmo deseado
    esp_err_t ret = ledc_set_freq(LEDC_MODE, handle->timer_num, (uint32_t)frequency_hz);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al establecer frecuencia %.2f Hz", frequency_hz);
        return ret;
    }

    handle->current_frequency = (uint32_t)frequency_hz;
    return ESP_OK;
}

esp_err_t a4988_dual_move(a4988_dual_handle_t handle, a4988_dual_direction_t direction, float rpm)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Si la dirección es STOP, reutilizar la lógica de frenado inmediato
    if (direction == A4988_DUAL_STOP || rpm <= 0)
    {
        return a4988_dual_stop(handle);
    }

    // Detener primero si está en movimiento para evitar invertir pasos a alta velocidad
    if (handle->running)
    {
        a4988_dual_stop(handle);
    }

    // Establecer velocidad antes de modificar las señales DIR
    esp_err_t ret = a4988_dual_set_speed(handle, rpm);
    if (ret != ESP_OK)
    {
        return ret;
    }

    // Configurar dirección según el movimiento deseado ajustando ambos pines DIR
    switch (direction)
    {
    case A4988_DUAL_FORWARD:
        gpio_set_level(handle->dir_left_pin, A4988_DIR_CW);
        gpio_set_level(handle->dir_right_pin, A4988_DIR_CW);
        break;

    case A4988_DUAL_BACKWARD:
        gpio_set_level(handle->dir_left_pin, A4988_DIR_CCW);
        gpio_set_level(handle->dir_right_pin, A4988_DIR_CCW);
        break;

    case A4988_DUAL_LEFT:
        gpio_set_level(handle->dir_left_pin, A4988_DIR_CCW);
        gpio_set_level(handle->dir_right_pin, A4988_DIR_CW);
        break;

    case A4988_DUAL_RIGHT:
        gpio_set_level(handle->dir_left_pin, A4988_DIR_CW);
        gpio_set_level(handle->dir_right_pin, A4988_DIR_CCW);
        break;

    default:
        ESP_LOGW(TAG, "Dirección inválida");
        return ESP_ERR_INVALID_ARG;
    }

    // Pequeño delay para estabilizar las señales DIR antes de inyectar nuevos pasos
    vTaskDelay(pdMS_TO_TICKS(2));

    // Iniciar PWM continuo en el pin STEP compartido para mantener el movimiento
    ledc_set_duty(LEDC_MODE, handle->channel_num, LEDC_DUTY);
    ledc_update_duty(LEDC_MODE, handle->channel_num);

    handle->running = true;

    return ESP_OK;
}

esp_err_t a4988_dual_stop(a4988_dual_handle_t handle)
{
    if (handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!handle->running)
    {
        return ESP_OK; // Ya está detenido
    }

    // Detener PWM inmediatamente para cortar la secuencia actual de pasos
    ledc_set_duty(LEDC_MODE, handle->channel_num, 0);
    ledc_update_duty(LEDC_MODE, handle->channel_num);

    handle->running = false;
    handle->current_frequency = 0;

    return ESP_OK;
}

esp_err_t a4988_dual_free(a4988_dual_handle_t *handle_ptr)
{
    // Recibimos un puntero al handle (a4988_dual_handle_t*)
    if (handle_ptr == NULL || *handle_ptr == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    a4988_dual_handle_t handle = *handle_ptr; // Obtenemos el puntero real

    // Detener si está en movimiento para garantizar que no queden pulsos pendientes
    a4988_dual_stop(handle);

    // Deshabilitar motores antes de liberar para dejar el hardware en estado seguro
    a4988_dual_disable(handle);

    // Detener el canal LEDC para liberar recursos del temporizador
    ledc_stop(LEDC_MODE, handle->channel_num, 0);

    // Liberar memoria
    free(handle);

    // Establecer el puntero original a NULL para evitar "dangling pointers"
    *handle_ptr = NULL;

    ESP_LOGI(TAG, "Driver A4988 DUAL liberado");

    return ESP_OK;
}