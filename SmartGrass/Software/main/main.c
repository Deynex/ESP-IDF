#include <stdio.h>
#include <inttypes.h>
#include "board_config.h"
#include "i2c.h"
#include "mpu6050.h"
#include "a4988.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "uart.h"
#include "mks_servo42c.h"
#include "soft_ap.h"
#include "vehicle.h"
#include "server.h"
#include "ws.h"
#include "brushless.h"

// ===============================================================================================================================================================
// MARK: Variables
// ===============================================================================================================================================================
static const char *TAG = "MAIN";
i2c_master_bus_handle_t i2c_bus_handle;
i2c_master_dev_handle_t mpu6050_handle;
vehicle_handle_t vehicle_handle = NULL;

// ===============================================================================================================================================================
// MARK: Core 1: Telemetría
// ===============================================================================================================================================================
void telemetry_task(void *pvParameters)
{
    // Variables locales para sensores
    int16_t ax, ay, az;
    int16_t temp_raw;
    int16_t gx, gy, gz;
    
    // Variables para encoder y motor
    mks_encoder_data_t enc;
    mks_status_t st;
    float shaft_error;
    
    char json_buffer[512];

    while (true)
    {
        // 1. Leer sensores
        mpu6050_read_all_raw(mpu6050_handle, &ax, &ay, &az, &temp_raw, &gx, &gy, &gz);
        mks_read_encoder(&enc);
        mks_read_shaft_error_angle(&shaft_error);
        mks_get_status(&st);

        // Convertir temperatura a grados Celsius para enviarla lista para mostrar
        float temp_c = (temp_raw / 340.0f) + 36.53f;

        // 2. Construir JSON completo con TODOS los datos
        snprintf(json_buffer, sizeof(json_buffer),
                 "{"
                 "\"accel_x\": %d, \"accel_y\": %d, \"accel_z\": %d, "
                 "\"gyro_x\": %d, \"gyro_y\": %d, \"gyro_z\": %d, "
                 "\"temp\": %.1f, "
                 "\"enc_angle\": %.2f, \"enc_total\": %.2f, \"enc_carry\": %" PRId32 ", "
                 "\"shaft_error\": %.2f, "
                 "\"motor_speed\": %d, \"motor_current\": %d"
                 "}",
                 ax, ay, az,
                 gx, gy, gz,
                 temp_c,
                 enc.angle_deg, enc.total_angle, enc.carry,
                 shaft_error,
                 st.speed, st.current);

        // 3. Enviar por WebSocket
        ws_server_send_text_all(json_buffer);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ===============================================================================================================================================================
// MARK: Main
// ===============================================================================================================================================================
void app_main(void)
{
    // Inicialización de I2C y MPU6050
    esp_err_t ret;
    ret = i2c_master_bus_init(&i2c_bus_handle, I2C_MASTER_SDA, I2C_MASTER_SCL, I2C_MASTER_NUM);

    if (ret == ESP_OK)
    {
        ret = i2c_add_device(i2c_bus_handle, &mpu6050_handle, MPU6050_ADDR, MPU6050_I2C_FREQ_HZ);
    }

    if (ret == ESP_OK)
    {
        ret = mpu6050_init(mpu6050_handle);
    }

    if (ret == ESP_OK)
    {
        mpu6050_set_accel_range(mpu6050_handle, MPU6050_ACCEL_RANGE_4G);
        mpu6050_set_gyro_range(mpu6050_handle, MPU6050_GYRO_RANGE_250DPS);
    }

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "¡Falló la inicialización de I2C/MPU6050!");
        return;
    }

    // UART MKS
    uart_init();
    vTaskDelay(pdMS_TO_TICKS(100));
    mks_init();

    // WiFi
    nvs_init();
    wifi_init();
    
    // Config Ruedas
    a4988_dual_config_t stepper_cfg = {
        .step_pin = VEHICLE_STEP_PIN,
        .dir_left_pin = VEHICLE_DIR_LEFT,
        .dir_right_pin = VEHICLE_DIR_RIGHT,
        .enable_pin = VEHICLE_ENABLE_PIN,
        .steps_per_rev = VEHICLE_TOTAL_STEPS,
        .timer_num = VEHICLE_LEDC_TIMER,
        .channel_num = VEHICLE_LEDC_CHANNEL,
    };

    // Config Cuchilla
    brushless_config_t blade_cfg = {
        .gpio_num = BLADE_GPIO_PIN,
        .timer_num = BLADE_LEDC_TIMER,
        .channel_num = BLADE_LEDC_CHANNEL,
        .speed_mode = LEDC_LOW_SPEED_MODE
    };

    vehicle_config_t vehicle_cfg = {
        .stepper_config = stepper_cfg,
        .brushless_config = blade_cfg
    };

    // Inicializar y Habilitar
    ret = vehicle_init(&vehicle_cfg, &vehicle_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Fallo init vehículo");
        return;
    }
    
    // Esto armará el ESC (esperar pitidos)
    vehicle_enable(vehicle_handle);

    // Servidor Web
    http_server_init(vehicle_handle);

    // Tareas
    xTaskCreatePinnedToCore(telemetry_task, "telemetry_task", 4096, NULL, 5, NULL, APP_CPU_NUM);
}