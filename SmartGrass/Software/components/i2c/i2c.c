/**
 * @file i2c.c
 * @brief Implementación de funciones de utilidad para comunicación I2C
 *
 * Este archivo contiene las funciones auxiliares para simplificar el uso
 * del bus I2C en modo maestro del ESP32, incluyendo inicialización del bus,
 * gestión de dispositivos, y operaciones de lectura/escritura de registros.
 * @author DEYNEX
 */

#include "i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

/** Tag para identificar los mensajes de log de este módulo */
static const char *TAG = "I2C";

/** Tiempo máximo de espera para operaciones I2C en milisegundos */
#define I2C_MASTER_TIMEOUT_MS 500

/**
 * @brief Función auxiliar para verificación completa de errores I2C
 *
 * Revisa el código de error devuelto por las operaciones I2C y registra
 * un mensaje detallado en el log si ocurre algún error. No aborta la
 * ejecución del programa, permitiendo que el flujo continúe.
 *
 * @param err Código de error devuelto por una función ESP-IDF
 */
static void full_error_check(esp_err_t err)
{
    if (err != ESP_OK)
    {
        char error_msg[64];
        // Convierte el código de error numérico a su nombre descriptivo
        esp_err_to_name_r(err, error_msg, sizeof(error_msg));
        ESP_LOGE(TAG, "Error I2C: %s (%d)", error_msg, err);
        // Registra el error sin detener la ejecución del programa
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
    }
}

esp_err_t i2c_master_bus_init(i2c_master_bus_handle_t *bus_handle, int sda_pin, int scl_pin, i2c_port_t port_num)
{
    // Configuración del bus I2C maestro
    i2c_master_bus_config_t bus_config = {
        .i2c_port = port_num,                 // Puerto I2C a utilizar (I2C_NUM_0 o I2C_NUM_1)
        .sda_io_num = sda_pin,                // Pin GPIO para la línea de datos (SDA)
        .scl_io_num = scl_pin,                // Pin GPIO para la línea de reloj (SCL)
        .clk_source = I2C_CLK_SRC_DEFAULT,    // Usa la fuente de reloj por defecto del sistema
        .glitch_ignore_cnt = 7,               // Ignora glitches menores a 7 pulsos de reloj
        .flags.enable_internal_pullup = true, // Activa resistencias pull-up internas (SDA y SCL)
    };

    // Crea e inicializa el bus I2C maestro con la configuración especificada
    esp_err_t err = i2c_new_master_bus(&bus_config, bus_handle);
    full_error_check(err);
    return err;
}

esp_err_t i2c_add_device(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle, uint8_t dev_addr, uint32_t freq_hz)
{
    // Configuración del dispositivo I2C esclavo
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, // Dirección de 7 bits (estándar más común)
        .device_address = dev_addr,            // Dirección I2C del dispositivo esclavo
        .scl_speed_hz = freq_hz,               // Frecuencia del reloj SCL (típicamente 100kHz o 400kHz)
    };

    // Registra el dispositivo en el bus I2C maestro
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_config, dev_handle);
    full_error_check(err);
    return err;
}

esp_err_t i2c_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len)
{
    // Realiza una operación de escritura (dirección del registro) seguida de lectura (datos del registro)
    // Esto es común en dispositivos I2C donde primero se indica qué registro leer
    esp_err_t err = i2c_master_transmit_receive(dev_handle, &reg_addr, 1, data, len, I2C_MASTER_TIMEOUT_MS);
    full_error_check(err);
    return err;
}

esp_err_t i2c_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data)
{
    // Prepara el buffer con la dirección del registro seguida del dato a escribir
    // Formato: [dirección_registro, valor_a_escribir]
    uint8_t write_buf[2] = {reg_addr, data};

    // Transmite ambos bytes en una sola transacción I2C
    esp_err_t err = i2c_master_transmit(dev_handle, write_buf, sizeof(write_buf), I2C_MASTER_TIMEOUT_MS);
    full_error_check(err);
    return err;
}