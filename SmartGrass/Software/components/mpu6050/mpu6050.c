/**
 * @file mpu6050.c
 * @brief Implementación de la API de alto nivel para el sensor MPU6050 en ESP32
 * @author DEYNEX
 */

#include "mpu6050.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c.h"
#include <stdio.h>

static const char *TAG = "MPU6050";

static void full_error_check(esp_err_t err)
{
    if (err != ESP_OK)
    {
        char error_msg[64];
        esp_err_to_name_r(err, error_msg, sizeof(error_msg));
        ESP_LOGE(TAG, "Error: %s (%d)", error_msg, err);
    }
}

// ==============================================================================================================================================================
// MARK: Configuración
// ==============================================================================================================================================================
esp_err_t mpu6050_init(i2c_master_dev_handle_t mpu6050_handle)
{
    // Reset del dispositivo y configuración del reloj
    esp_err_t err = i2c_register_write_byte(mpu6050_handle, MPU6050_PWR_MGMT_1_REG_ADDR, 0x01); // 0x01 = CLKSEL_PLL_XGYRO
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo al despertar el MPU6050");
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // Esperar a que el sensor se estabilice

    // Verificación de la identificación del dispositivo
    uint8_t data;
    err = i2c_register_read(mpu6050_handle, MPU6050_WHO_AM_I_REG_ADDR, &data, 1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Fallo al leer WHO_AM_I");
        return err;
    }

    // Comprobar si es un MPU-6500 (o 9250)
    if (data != 0x70) 
    {
        ESP_LOGE(TAG, "WHO_AM_I incorrecto. Se esperaba 0x70 (MPU-6500) pero se recibió 0x%02X", data);
        return ESP_FAIL; 
    }

    ESP_LOGI(TAG, "MPU-6500 encontrado (WHO_AM_I = 0x%02X)", data);
    return ESP_OK;
}

// ==============================================================================================================================================================
// MARK: Acelerómetro
// ==============================================================================================================================================================
esp_err_t mpu6050_set_accel_range(i2c_master_dev_handle_t mpu6050_handle, mpu6050_accel_range_t range)
{
    esp_err_t err = i2c_register_write_byte(mpu6050_handle, MPU6050_ACCEL_CONFIG_REG_ADDR, range);
    full_error_check(err); // full_error_check es solo para loguear
    return err;            // Devuelve el estado
}

esp_err_t mpu6050_read_accelerometer_raw(i2c_master_dev_handle_t dev_handle, int16_t *accel_x_raw, int16_t *accel_y_raw, int16_t *accel_z_raw)
{
    uint8_t accel_data[6];
    esp_err_t err = i2c_register_read(dev_handle, MPU6050_ACCEL_XOUT_H, accel_data, 6);
    if (err != ESP_OK)
        return err;

    *accel_x_raw = (int16_t)((accel_data[0] << 8) | accel_data[1]);
    *accel_y_raw = (int16_t)((accel_data[2] << 8) | accel_data[3]);
    *accel_z_raw = (int16_t)((accel_data[4] << 8) | accel_data[5]);
    return ESP_OK;
}

esp_err_t mpu6050_read_accelerometer(i2c_master_dev_handle_t dev_handle, float *a_x, float *a_y, float *a_z, mpu6050_accel_range_t range)
{
    int16_t accel_x_raw, accel_y_raw, accel_z_raw;
    esp_err_t err = mpu6050_read_accelerometer_raw(dev_handle, &accel_x_raw, &accel_y_raw, &accel_z_raw);
    if (err != ESP_OK)
        return err;

    float sensitivity;
    switch (range)
    {
    case MPU6050_ACCEL_RANGE_2G:
        sensitivity = 16384.0f;
        break;
    case MPU6050_ACCEL_RANGE_4G:
        sensitivity = 8192.0f;
        break;
    case MPU6050_ACCEL_RANGE_8G:
        sensitivity = 4096.0f;
        break;
    case MPU6050_ACCEL_RANGE_16G:
        sensitivity = 2048.0f;
        break;
    default:
        sensitivity = 16384.0f;
        break;
    }

    *a_x = (float)accel_x_raw / sensitivity;
    *a_y = (float)accel_y_raw / sensitivity;
    *a_z = (float)accel_z_raw / sensitivity;
    return ESP_OK;
}

// ==============================================================================================================================================================
// MARK: Termometro
// ==============================================================================================================================================================
esp_err_t mpu6050_read_temperature_raw(i2c_master_dev_handle_t dev_handle, int16_t *temp_raw)
{
    uint8_t temp_data[2];
    esp_err_t err = i2c_register_read(dev_handle, MPU6050_TEMP_OUT_H, temp_data, 2);
    if (err != ESP_OK)
        return err;

    *temp_raw = (int16_t)((temp_data[0] << 8) | temp_data[1]);
    return ESP_OK;
}

esp_err_t mpu6050_read_temperature(i2c_master_dev_handle_t dev_handle, float *temperature)
{
    int16_t temp_raw;
    esp_err_t err = mpu6050_read_temperature_raw(dev_handle, &temp_raw);
    if (err != ESP_OK)
        return err;

    // Fórmula de la hoja de datos
    *temperature = (temp_raw / 340.0) + 36.53;
    return ESP_OK;
}

// ==============================================================================================================================================================
// MARK: Giroscopio
// ==============================================================================================================================================================
esp_err_t mpu6050_set_gyro_range(i2c_master_dev_handle_t mpu6050_handle, mpu6050_gyro_range_t range)
{
    esp_err_t err = i2c_register_write_byte(mpu6050_handle, MPU6050_GYRO_CONFIG_REG_ADDR, range);
    full_error_check(err);
    return err;
}

esp_err_t mpu6050_read_gyroscope_raw(i2c_master_dev_handle_t dev_handle, int16_t *gyro_x_raw, int16_t *gyro_y_raw, int16_t *gyro_z_raw)
{
    uint8_t gyro_data[6];
    esp_err_t err = i2c_register_read(dev_handle, MPU6050_GYRO_XOUT_H, gyro_data, 6);
    if (err != ESP_OK)
        return err;

    *gyro_x_raw = (int16_t)((gyro_data[0] << 8) | gyro_data[1]);
    *gyro_y_raw = (int16_t)((gyro_data[2] << 8) | gyro_data[3]);
    *gyro_z_raw = (int16_t)((gyro_data[4] << 8) | gyro_data[5]);
    return ESP_OK;
}

esp_err_t mpu6050_read_gyroscope(i2c_master_dev_handle_t dev_handle, float *g_x, float *g_y, float *g_z, mpu6050_gyro_range_t range)
{
    int16_t gyro_x_raw, gyro_y_raw, gyro_z_raw;
    esp_err_t err = mpu6050_read_gyroscope_raw(dev_handle, &gyro_x_raw, &gyro_y_raw, &gyro_z_raw);
    if (err != ESP_OK)
        return err;

    float sensitivity;
    switch (range)
    {
    case MPU6050_GYRO_RANGE_250DPS:
        sensitivity = 131.0f;
        break;
    case MPU6050_GYRO_RANGE_500DPS:
        sensitivity = 65.5f;
        break;
    case MPU6050_GYRO_RANGE_1000DPS:
        sensitivity = 32.8f;
        break;
    case MPU6050_GYRO_RANGE_2000DPS:
        sensitivity = 16.4f;
        break;
    default:
        sensitivity = 131.0f;
        break;
    }

    *g_x = (float)gyro_x_raw / sensitivity;
    *g_y = (float)gyro_y_raw / sensitivity;
    *g_z = (float)gyro_z_raw / sensitivity;
    return ESP_OK;
}

// ==============================================================================================================================================================
// MARK: Lectura Rápida
// ==============================================================================================================================================================
esp_err_t mpu6050_read_all_raw(i2c_master_dev_handle_t dev_handle, int16_t *a_x, int16_t *a_y, int16_t *a_z, int16_t *temp, int16_t *g_x, int16_t *g_y, int16_t *g_z)
{
    uint8_t data[14];
    // Lee 14 bytes de una vez, desde ACCEL_XOUT_H (0x3B) hasta GYRO_ZOUT_L (0x48)
    esp_err_t err = i2c_register_read(dev_handle, MPU6050_ACCEL_XOUT_H, data, 14);
    if (err != ESP_OK)
        return err;

    *a_x = (int16_t)((data[0] << 8) | data[1]);
    *a_y = (int16_t)((data[2] << 8) | data[3]);
    *a_z = (int16_t)((data[4] << 8) | data[5]);
    *temp = (int16_t)((data[6] << 8) | data[7]);
    *g_x = (int16_t)((data[8] << 8) | data[9]);
    *g_y = (int16_t)((data[10] << 8) | data[11]);
    *g_z = (int16_t)((data[12] << 8) | data[13]);
    return ESP_OK;
}