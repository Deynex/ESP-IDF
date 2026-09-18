#include "switch.h"
#include "driver/gpio.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/adc_channel.h"
#include <stdio.h>
#include <math.h>

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Funciones
// ---------------------------------------------------------------------------------------------------------------------------------------------------------------

static bool current_state = false; // Estado del switch, inicialmente apagado
static adc_continuous_handle_t adc_handle = NULL;
static bool adc_running = false;

// Callback para procesar datos del ADC
static bool IRAM_ATTR adc_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    // Procesar datos si es necesario
    return (mustYield == pdTRUE);
}

void switch_init(void)
{
    // Inicializar GPIO para el switch
    esp_err_t err = gpio_reset_pin(SWITCH_GPIO);
    if (err != ESP_OK)
    {
        ESP_LOGE("SWITCH", "Error al reiniciar el pin GPIO %d. Error %s", SWITCH_GPIO, esp_err_to_name(err));
        return;
    }

    err = gpio_set_direction(SWITCH_GPIO, GPIO_MODE_OUTPUT);
    if (err != ESP_OK)
    {
        ESP_LOGE("SWITCH", "Error al configurar el pin GPIO %d. Error %s", SWITCH_GPIO, esp_err_to_name(err));
        return;
    }

    // Configurar ADC para lectura de corriente
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = SOC_ADC_DIGI_DATA_BYTES_PER_CONV * 16, // Múltiplo de SOC_ADC_DIGI_DATA_BYTES_PER_CONV
        .flags.flush_pool = true,
    };

    err = adc_continuous_new_handle(&adc_config, &adc_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("SWITCH", "Error al crear handle ADC. Error %s", esp_err_to_name(err));
        return;
    }

    // Configurar patrón ADC para GPIO32 (ADC1_CHANNEL_4)
    adc_digi_pattern_config_t adc_pattern = {
        .atten = ADC_ATTEN_DB_12,
        .channel = ADC_CHANNEL_4,
        .unit = ADC_UNIT_1,
        .bit_width = SOC_ADC_DIGI_MAX_BITWIDTH,
    };

    adc_continuous_config_t dig_cfg = {
        .pattern_num = 1,
        .adc_pattern = &adc_pattern,
        .sample_freq_hz = 20000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
    };

    err = adc_continuous_config(adc_handle, &dig_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE("SWITCH", "Error al configurar ADC. Error %s", esp_err_to_name(err));
        return;
    }

    // Registrar callbacks
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = adc_conv_done_cb,
    };
    err = adc_continuous_register_event_callbacks(adc_handle, &cbs, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE("SWITCH", "Error al registrar callbacks ADC. Error %s", esp_err_to_name(err));
    }
}

void switch_set_state(bool state)
{
    if (current_state != state)
    {
        esp_err_t err = gpio_set_level(SWITCH_GPIO, state ? 1 : 0);
        if (err != ESP_OK)
        {
            ESP_LOGE("SWITCH", "Error al establecer el estado del pin GPIO %d. Error %s", SWITCH_GPIO, esp_err_to_name(err));
        }
        else
        {
            current_state = state;
            ESP_LOGI("SWITCH", "Estado del switch cambiado a: %s", state ? "ON" : "OFF");
        }
    }
    else
    {
        ESP_LOGW("SWITCH", "El estado del interruptor ya está en %s", current_state ? "ON" : "OFF");
    }
}

bool switch_get_state(void)
{
    return current_state;
}

void switch_start_adc(void)
{
    if (adc_handle == NULL)
    {
        ESP_LOGE("SWITCH", "ADC handle no inicializado");
        return;
    }

    if (!adc_running)
    {
        esp_err_t err = adc_continuous_start(adc_handle);
        if (err == ESP_OK)
        {
            adc_running = true;
        }
        else
        {
            ESP_LOGE("SWITCH", "Error al iniciar ADC. Error %s", esp_err_to_name(err));
        }
    }
    else
    {
        ESP_LOGW("SWITCH", "ADC ya está corriendo");
    }
}

void switch_stop_adc(void)
{
    if (adc_handle && adc_running)
    {
        esp_err_t err = adc_continuous_stop(adc_handle);
        if (err == ESP_OK)
        {
            adc_running = false;
            ESP_LOGW("SWITCH", "ADC detenido correctamente");
        }
        else
        {
            ESP_LOGE("SWITCH", "Error al detener ADC. Error %s", esp_err_to_name(err));
        }
    }
}

void switch_deinit(void)
{
    if (adc_running)
    {
        switch_stop_adc();
    }

    if (adc_handle)
    {
        esp_err_t err = adc_continuous_deinit(adc_handle);
        if (err == ESP_OK)
        {
            adc_handle = NULL;
            ESP_LOGW("SWITCH", "ADC deinicializado correctamente");
        }
        else
        {
            ESP_LOGE("SWITCH", "Error al deinicializar ADC. Error %s", esp_err_to_name(err));
        }
    }
}

float switch_get_current(void)
{
    if (!adc_handle || !adc_running)
    {
        ESP_LOGW("SWITCH", "ADC no está iniciado");
        return 0.0f;
    }

    float current = 0.0f; // Valor de corriente inicializado a 0.0
    uint8_t result[SOC_ADC_DIGI_DATA_BYTES_PER_CONV * 16] = {0};
    uint32_t ret_num = 0;
    esp_err_t err = adc_continuous_read(adc_handle, result, sizeof(result), &ret_num, 100);

    if (err == ESP_OK && ret_num > 0)
    {
        adc_digi_output_data_t *p = (adc_digi_output_data_t *)result;
        uint32_t sum = 0;
        uint32_t count = 0;

        for (uint32_t i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES)
        {
            if (p->type1.channel == ADC_CHANNEL_4)
            {
                sum += p->type1.data;
                count++;
            }
            p = (adc_digi_output_data_t *)((uint8_t *)p + SOC_ADC_DIGI_RESULT_BYTES);
        }

        if (count > 0)
        {
            float voltage = (float)(sum / count) * 3.3f / 4095.0f; // Conversión a voltaje
            current = (voltage - ACS712_OFFSET) / ACS712_SCALE;    // Aquí puedes aplicar la conversión específica a corriente
            return current;
        }
    }
    else if (err == ESP_ERR_TIMEOUT)
    {
        ESP_LOGD("SWITCH", "Timeout en lectura ADC");
    }
    else
    {
        ESP_LOGE("SWITCH", "Error en lectura ADC: %s", esp_err_to_name(err));
    }

    return current;
}

float switch_get_current_rms(void)
{
    if (!adc_handle || !adc_running)
    {
        ESP_LOGW("SWITCH", "ADC no está iniciado");
        return 0.0f;
    }

    uint8_t result[SOC_ADC_DIGI_DATA_BYTES_PER_CONV * 64] = {0}; // más muestras
    uint32_t ret_num = 0;
    esp_err_t err = adc_continuous_read(adc_handle, result, sizeof(result), &ret_num, 100);

    if (err != ESP_OK || ret_num == 0)
    {
        if (err == ESP_ERR_TIMEOUT)
            ESP_LOGD("SWITCH", "Timeout en lectura ADC");
        else
            ESP_LOGE("SWITCH", "Error en lectura ADC: %s", esp_err_to_name(err));
        return 0.0f;
    }

    adc_digi_output_data_t *p = (adc_digi_output_data_t *)result;
    float sum_sq = 0.0f;
    uint32_t count = 0;

    for (uint32_t i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES)
    {
        if (p->type1.channel == ADC_CHANNEL_4)
        {
            float v_adc = (float)p->type1.data * 3.3f / 4095.0f; // voltaje medido en ESP
            float v_acs = v_adc * 0.6f;                           // voltaje real de ACS712
            float delta = v_acs - ACS712_OFFSET;                  // desviación respecto al offset
            float current = delta / ACS712_SCALE;                 // conversión a amperios
            sum_sq += current * current;
            count++;
        }
        p = (adc_digi_output_data_t *)((uint8_t *)p + SOC_ADC_DIGI_RESULT_BYTES);
    }

    if (count == 0)
        return 0.0f;

    float i_rms = sqrtf(sum_sq / count); // corriente RMS
    return i_rms - 16.75f;
}