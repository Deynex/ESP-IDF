/**
 * @file mpu6050.h
 * @brief API de alto nivel para el sensor MPU6050 en ESP32
 * @author DEYNEX
 */

#include "driver/i2c_master.h"
#include "esp_err.h"

// El MPU6050 es un sensor que combina un acelerómetro, un giroscopio y un magnetómetro en un solo dispositivo.
// La dirección I2C del sensor depende del estado del pin AD0:
//  - Si AD0 está conectado a GND, la dirección será 0x68 (por defecto).
//  - Si AD0 está conectado a VCC, la dirección será 0x69.
#define MPU6050_ADDR 0x68 // Dirección del sensor MPU9250

// La frecuencia de operación del bus I2C del MPU6050.
#define MPU6050_I2C_FREQ_HZ 400000

#define MPU6050_FIFO_COUNTH_REG_ADDR 0x72 // Dirección del registro de conteo de FIFO
#define MPU6050_FIFO_COUNTL_REG_ADDR 0x73 // Dirección del registro de conteo de FIFO
#define MPU6050_FIFO_R_W_REG_ADDR 0x74    // Dirección del registro de lectura/escritura de FIFO

// El registro WHO_AM_I (0x75) contiene un valor fijo que permite verificar la comunicación con el sensor.
// Al leer este registro, el valor esperado es 0x70.
#define MPU6050_WHO_AM_I_REG_ADDR 0x75

// El registro PWR_MGMT_1 (0x6B) controla la gestión de energía del sensor.
// Este registro incluye configuraciones para:
//  - Salir del estado de suspensión.
//  - Configurar la fuente de reloj.
//  - Reiniciar el sensor (bit 7).
// Es crucial configurar este registro adecuadamente al iniciar el sensor.
#define MPU6050_PWR_MGMT_1_REG_ADDR 0x6B // Dirección del registro de gestión de energía

#define MPU6050_INT_PIN_CFG_REG_ADDR 0x37 // Dirección del registro de configuración del acelerómetro
#define MPU6050_SMPLRT_DIV_REG_ADDR 0x19  // Dirección del registro de división de tasa de muestreo
#define MPU6050_CONFIG_REG_ADDR 0x1A      // Registro de configuración de filtro
#define MPU6050_FIFO_EN_REG_ADDR 0x23     // Registro de habilitación de FIFO
#define MPU6050_USER_CTRL_REG_ADDR 0x6A   // Registro de control de usuario
#define MPU6050_INT_ENABLE_REG_ADDR 0x38  // Registro de habilitación de interrupciones

// El registro ACCEL_CONFIG (0x1C) permite configurar la escala del acelerómetro.
// Este registro incluye los bits para seleccionar el rango de medición:
// +----------------------+----------------------+
// | Bits [4:3]           | Rango                |
// +----------------------+----------------------+
// | 00                   | ±2 g                 |
// | 01                   | ±4 g                 |
// | 10                   | ±8 g                 |
// | 11                   | ±16 g                |
// +----------------------+----------------------+
// Cambiar el rango afecta la sensibilidad del acelerómetro:
// Sensibilidad (LSB/g): 16384 (±2 g), 8192 (±4 g), 4096 (±8 g), 2048 (±16 g).
#define MPU6050_ACCEL_CONFIG_REG_ADDR 0x1C

// El registro MPU6050_ACCEL_XOUT_H (0x3B) define la dirección del byte alto del eje X del acelerómetro.
// Este registro sirve como punto inicial para leer los datos crudos del acelerómetro.
// El acelerómetro del MPU6050 organiza sus datos en 6 bytes consecutivos, distribuidos de la siguiente manera:
// +---------------------+----------------------+---------------------+
// | Registro            | Dirección (Hex)      | Descripción         |
// +---------------------+----------------------+---------------------+
// | ACCEL_XOUT_H        | 0x3B                 | Byte alto del eje X |
// | ACCEL_XOUT_L        | 0x3C                 | Byte bajo del eje X |
// | ACCEL_YOUT_H        | 0x3D                 | Byte alto del eje Y |
// | ACCEL_YOUT_L        | 0x3E                 | Byte bajo del eje Y |
// | ACCEL_ZOUT_H        | 0x3F                 | Byte alto del eje Z |
// | ACCEL_ZOUT_L        | 0x40                 | Byte bajo del eje Z |
// +---------------------+----------------------+---------------------+
// Una lectura secuencial desde MPU6050_ACCEL_XOUT_H devuelve automáticamente los 6 bytes correspondientes
// a los valores crudos de los tres ejes (X, Y y Z), en una única operación I2C.
// Este diseño simplifica el acceso a los datos del acelerómetro y mejora el rendimiento,
// al reducir la cantidad de operaciones I2C necesarias.
#define MPU6050_ACCEL_XOUT_H 0x3B

// El registro TEMP_OUT_H (0x41) define la dirección del byte alto de la temperatura del sensor.
// Este registro sirve como punto inicial para leer los datos crudos de la temperatura.
#define MPU6050_TEMP_OUT_H 0x41

// El registro GYRO_CONFIG (0x1B) permite configurar la escala del giroscopio.
// Este registro incluye los bits para seleccionar el rango de medición:
// +----------------------+----------------------+
// | Bits [4:3]           | Rango                |
// +----------------------+----------------------+
// | 00                   | ±250 °/s             |
// | 01                   | ±500 °/s             |
// | 10                   | ±1000 °/s            |
// | 11                   | ±2000 °/s            |
// +----------------------+----------------------+
// Cambiar el rango afecta la sensibilidad del giroscopio:
// Sensibilidad (LSB/°/s): 131.0 (±250 °/s), 65.5 (±500 °/s), 32.8 (±1000 °/s), 16.4 (±2000 °/s).
#define MPU6050_GYRO_CONFIG_REG_ADDR 0x1B

// El registro MPU6050_GYRO_XOUT_H (0x43) define la dirección del byte alto del eje X del giroscopio.
// Este registro sirve como punto inicial para leer los datos crudos del giroscopio.
// El giroscopio del MPU9250 organiza sus datos en 6 bytes consecutivos, distribuidos de la siguiente manera:
// +---------------------+----------------------+---------------------+
// | Registro            | Dirección (Hex)      | Descripción         |
// +---------------------+----------------------+---------------------+
// | GYRO_XOUT_H         | 0x43                 | Byte alto del eje X |
// | GYRO_XOUT_L         | 0x44                 | Byte bajo del eje X |
// | GYRO_YOUT_H         | 0x45                 | Byte alto del eje Y |
// | GYRO_YOUT_L         | 0x46                 | Byte bajo del eje Y |
// | GYRO_ZOUT_H         | 0x47                 | Byte alto del eje Z |
// | GYRO_ZOUT_L         | 0x48                 | Byte bajo del eje Z |
// +---------------------+----------------------+---------------------+
// Una lectura secuencial desde MPU6050_GYRO_XOUT_H devuelve automáticamente los 6 bytes correspondientes
// a los valores crudos de los tres ejes (X, Y y Z), en una única operación I2C.
#define MPU6050_GYRO_XOUT_H 0x43

typedef enum
{
    MPU6050_USER_FIFO_EN = 0x40,       // Habilitar el uso del FIFO
    MPU6050_USER_I2C_MST_EN = 0x20,    // Habilitar el bus I2C auxiliar
    MPU6050_USER_I2C_IF_DIS = 0x10,    // Deshabilitar el bus I2C auxiliar
    MPU6050_USER_FIFO_RESET = 0x04,    // Reiniciar el FIFO
    MPU6050_USER_I2C_MST_RESET = 0x02, // Reiniciar el bus I2C auxiliar
    MPU6050_USER_SIG_COND_RESET = 0x01 // Reiniciar las señales de condición
} mpu6050_user_ctrl_bit_t;

typedef enum
{
    MPU6050_FIFO_EN_TEMP = 0x80,  // Habilitar la lectura de la temperatura
    MPU6050_FIFO_EN_GX = 0x40,    // Habilitar la lectura del giroscopio X
    MPU6050_FIFO_EN_GY = 0x20,    // Habilitar la lectura del giroscopio Y
    MPU6050_FIFO_EN_GZ = 0x10,    // Habilitar la lectura del giroscopio Z
    MPU6050_FIFO_EN_ACCEL = 0x08, // Habilitar la lectura del acelerómetro
    MPU6050_FIFO_EN_SLV2 = 0x04,  // Habilitar la lectura del esclavo 2
    MPU6050_FIFO_EN_SLV1 = 0x02,  // Habilitar la lectura del esclavo 1
    MPU6050_FIFO_EN_SLV0 = 0x01   // Habilitar la lectura del esclavo 0
} mpu6050_fifo_en_bit_t;

typedef enum
{
    MPU6050_PWR_MGMT_1_RESET = 0x80,        // Reiniciar el dispositivo
    MPU6050_PWR_MGMT_1_SLEEP = 0x40,        // Modo de suspensión
    MPU6050_PWR_MGMT_1_CYCLE = 0x20,        // Modo de ciclo de baja potencia
    MPU6050_PWR_MGMT_1_GYRO_STANDBY = 0x10, // Modo de espera del giroscopio
    MPU6050_PWR_MGMT_1_TEMP_DIS = 0x08,     // Deshabilitar el sensor de temperatura
    MPU6050_PWR_MGMT_1_CLKSEL_0 = 0x01,     // Fuente de reloj interno de 8 MHz
    MPU6050_PWR_MGMT_1_CLKSEL_1 = 0x02,     // Fuente de reloj PLL con eje X del giroscopio
    MPU6050_PWR_MGMT_1_CLKSEL_2 = 0x03,     // Fuente de reloj PLL con eje Y del giroscopio
    MPU6050_PWR_MGMT_1_CLKSEL_3 = 0x04,     // Fuente de reloj PLL con eje Z del giroscopio
    MPU6050_PWR_MGMT_1_CLKSEL_4 = 0x05,     // Fuente de reloj PLL con 32.768 kHz
    MPU6050_PWR_MGMT_1_CLKSEL_5 = 0x06,     // Fuente de reloj PLL con 19.2 MHz
    MPU6050_PWR_MGMT_1_CLKSEL_6 = 0x07      // Modo de suspensión
} mpu6050_pwr_mgmt_1_bit_t;

typedef enum
{
    MPU6050_PWR_MGMT_2_STBY_XA = 0x20, // Modo de espera del acelerómetro X
    MPU6050_PWR_MGMT_2_STBY_YA = 0x10, // Modo de espera del acelerómetro Y
    MPU6050_PWR_MGMT_2_STBY_ZA = 0x08, // Modo de espera del acelerómetro Z
    MPU6050_PWR_MGMT_2_STBY_XG = 0x04, // Modo de espera del giroscopio X
    MPU6050_PWR_MGMT_2_STBY_YG = 0x02, // Modo de espera del giroscopio Y
    MPU6050_PWR_MGMT_2_STBY_ZG = 0x01  // Modo de espera del giroscopio Z
} mpu6050_pwr_mgmt_2_bit_t;

typedef enum
{
    MPU6050_ACCEL_RANGE_2G = 0x00, // ±2g
    MPU6050_ACCEL_RANGE_4G = 0x08, // ±4g
    MPU6050_ACCEL_RANGE_8G = 0x10, // ±8g
    MPU6050_ACCEL_RANGE_16G = 0x18 // ±16g
} mpu6050_accel_range_t;

typedef enum
{
    MPU6050_GYRO_RANGE_250DPS = 0x00,  // ±250°/s
    MPU6050_GYRO_RANGE_500DPS = 0x08,  // ±500°/s
    MPU6050_GYRO_RANGE_1000DPS = 0x10, // ±1000°/s
    MPU6050_GYRO_RANGE_2000DPS = 0x18  // ±2000°/s
} mpu6050_gyro_range_t;

typedef enum
{
    MPU6050_CLOCK_INTERNAL = 0x00,   // Reloj interno de 8 MHz
    MPU6050_CLOCK_PLL_XGYRO = 0x01,  // Reloj PLL con eje X del giroscopio
    MPU6050_CLOCK_PLL_YGYRO = 0x02,  // Reloj PLL con eje Y del giroscopio
    MPU6050_CLOCK_PLL_ZGYRO = 0x03,  // Reloj PLL con eje Z del giroscopio
    MPU6050_CLOCK_PLL_EXT32K = 0x04, // Reloj PLL con 32.768 kHz
    MPU6050_CLOCK_PLL_EXT19M = 0x05, // Reloj PLL con 19.2 MHz
    MPU6050_CLOCK_STOP = 0x07        // Modo de suspensión
} mpu6050_clock_source_t;

/**
 * @brief Inicializa el sensor MPU6050 y verifica WHO_AM_I.
 * @param mpu6050_handle Manejador del dispositivo
 * @return esp_err_t ESP_OK si la ID (0x68) es correcta
 */
esp_err_t mpu6050_init(i2c_master_dev_handle_t mpu6050_handle);

/**
 * @brief Configura el rango del acelerómetro.
 */
esp_err_t mpu6050_set_accel_range(i2c_master_dev_handle_t mpu6050_handle, mpu6050_accel_range_t range);

/**
 * @brief Configura el rango del giroscopio.
 */
esp_err_t mpu6050_set_gyro_range(i2c_master_dev_handle_t mpu6050_handle, mpu6050_gyro_range_t range);

/**
 * @brief Lee los valores crudos del acelerómetro.
 * @return esp_err_t ESP_OK si la lectura I2C fue exitosa
 */
esp_err_t mpu6050_read_accelerometer_raw(i2c_master_dev_handle_t dev_handle, int16_t *accel_x_raw, int16_t *accel_y_raw, int16_t *accel_z_raw);

/**
 * @brief Lee los valores del acelerómetro y los convierte a g.
 * @return esp_err_t ESP_OK si la lectura I2C fue exitosa
 */
esp_err_t mpu6050_read_accelerometer(i2c_master_dev_handle_t dev_handle, float *a_x, float *a_y, float *a_z, mpu6050_accel_range_t range);

/**
 * @brief Lee el valor crudo de la temperatura.
 * @return esp_err_t ESP_OK si la lectura I2C fue exitosa
 */
esp_err_t mpu6050_read_temperature_raw(i2c_master_dev_handle_t dev_handle, int16_t *temp_raw);

/**
 * @brief Lee el valor de la temperatura y la convierte a °C.
 * @return esp_err_t ESP_OK si la lectura I2C fue exitosa
 */
esp_err_t mpu6050_read_temperature(i2c_master_dev_handle_t dev_handle, float *temperature);

/**
 * @brief Lee los valores crudos del giroscopio.
 * @return esp_err_t ESP_OK si la lectura I2C fue exitosa
 */
esp_err_t mpu6050_read_gyroscope_raw(i2c_master_dev_handle_t dev_handle, int16_t *gyro_x_raw, int16_t *gyro_y_raw, int16_t *gyro_z_raw);

/**
 * @brief Lee los valores del giroscopio y los convierte a °/s.
 * @return esp_err_t ESP_OK si la lectura I2C fue exitosa
 */
esp_err_t mpu6050_read_gyroscope(i2c_master_dev_handle_t dev_handle, float *g_x, float *g_y, float *g_z, mpu6050_gyro_range_t range);

/**
 * @brief Lee todos los valores crudos del sensor en una sola operación I2C.
 * @return esp_err_t ESP_OK si la lectura I2C fue exitosa
 */
esp_err_t mpu6050_read_all_raw(i2c_master_dev_handle_t dev_handle, int16_t *accel_x, int16_t *accel_y, int16_t *accel_z, int16_t *temp, int16_t *gyro_x, int16_t *gyro_y, int16_t *gyro_z);