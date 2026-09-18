/**
 * @file uart.c
 * @brief Implementación del módulo de comunicación UART para ESP32
 * 
 * Este archivo implementa las funciones de comunicación serie UART
 * para la interfaz con el servo motor MKS SERVO42C. Utiliza el
 * driver UART estándar de ESP-IDF.
 * 
 * @author DEYNEX
 */

#include "uart.h"
#include <string.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// DEFINICIONES DE CONFIGURACIÓN
// ------------------------------------------------------------------------------------------------------------------------------------------------------

/**
 * @def UART_PORT
 * @brief Puerto UART utilizado para la comunicación
 * 
 * ESP32 tiene 3 puertos UART (UART_NUM_0, UART_NUM_1, UART_NUM_2).
 * UART_NUM_0 generalmente se usa para consola/debug, por lo que
 * usamos UART_NUM_1 para la comunicación con el servo.
 */
#define UART_PORT UART_NUM_1

/**
 * @def UART_BAUD_RATE
 * @brief Velocidad de comunicación en baudios
 * 
 * 115200 baudios es una velocidad estándar para comunicación
 * serie, proporcionando un buen balance entre velocidad y
 * confiabilidad para distancias cortas (<3 metros típicamente).
 */
#define UART_BAUD_RATE 115200

/**
 * @def TXD_PIN
 * @brief Pin GPIO usado para transmisión (TX)
 * 
 * GPIO 17 es el pin de transmisión de datos del ESP32 hacia
 * el dispositivo externo (servo MKS SERVO42C).
 */
#define TXD_PIN GPIO_NUM_17

/**
 * @def RXD_PIN
 * @brief Pin GPIO usado para recepción (RX)
 * 
 * GPIO 16 es el pin de recepción de datos del dispositivo
 * externo (servo MKS SERVO42C) hacia el ESP32.
 */
#define RXD_PIN GPIO_NUM_16

/**
 * @def RX_BUF_SIZE
 * @brief Tamaño del buffer de recepción UART en bytes
 * 
 * Define el tamaño base del buffer. El driver UART usará
 * el doble de este valor (512 bytes) para el buffer de
 * recepción interno, permitiendo almacenar temporalmente
 * datos recibidos antes de ser procesados.
 */
#define RX_BUF_SIZE 256

// ------------------------------------------------------------------------------------------------------------------------------------------------------
// IMPLEMENTACIÓN DE FUNCIONES PÚBLICAS
// ------------------------------------------------------------------------------------------------------------------------------------------------------

/**
 * @brief Inicializa el hardware y driver UART
 * 
 * Esta función realiza la inicialización completa del módulo UART:
 * 1. Crea la estructura de configuración UART
 * 2. Instala el driver UART con los buffers necesarios
 * 3. Aplica la configuración al puerto UART
 * 4. Configura los pines GPIO para TX y RX
 * 
 * @note Debe ser llamada una sola vez durante la inicialización del sistema
 * @note No verifica errores, asume que la configuración es válida
 */
void uart_init(void)
{
    /* Estructura de configuración UART con parámetros estándar */
    const uart_config_t cfg = {
        .baud_rate = UART_BAUD_RATE,           // 115200 baudios
        .data_bits = UART_DATA_8_BITS,         // 8 bits de datos por byte
        .parity = UART_PARITY_DISABLE,         // Sin bit de paridad
        .stop_bits = UART_STOP_BITS_1,         // 1 bit de parada
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, // Sin control de flujo (RTS/CTS)
        .source_clk = UART_SCLK_DEFAULT,       // Reloj fuente por defecto (APB)
    };
    
    /* 
     * Instala el driver UART en el sistema
     * Parámetros:
     * - UART_PORT: Puerto UART a usar (UART_NUM_1)
     * - RX_BUF_SIZE * 2: Tamaño del buffer de recepción (512 bytes)
     * - 0: Tamaño del buffer de transmisión (0 = sin buffer, transmisión directa)
     * - 0: Tamaño de la cola de eventos (0 = sin cola de eventos)
     * - NULL: Handle de la cola de eventos (no usado)
     * - 0: Flags de interrupción (ninguno)
     */
    uart_driver_install(UART_PORT, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    
    /* Aplica la configuración de parámetros UART al puerto */
    uart_param_config(UART_PORT, &cfg);
    
    /* 
     * Configura los pines GPIO para UART
     * Parámetros:
     * - UART_PORT: Puerto UART configurado
     * - TXD_PIN: Pin GPIO para transmisión (GPIO 17)
     * - RXD_PIN: Pin GPIO para recepción (GPIO 16)
     * - UART_PIN_NO_CHANGE: No usar pin RTS (control de flujo deshabilitado)
     * - UART_PIN_NO_CHANGE: No usar pin CTS (control de flujo deshabilitado)
     */
    uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/**
 * @brief Transmite datos a través del puerto UART
 * 
 * Envía un bloque de datos de tamaño específico a través del puerto UART.
 * La función utiliza el driver UART de ESP-IDF para escribir los datos
 * en el buffer de transmisión del hardware.
 * 
 * @param data Puntero al buffer de datos a transmitir
 * @param data_size Cantidad de bytes a enviar
 * @return Número de bytes escritos en el buffer UART
 * 
 * @note La función es bloqueante hasta que los datos se copien al buffer TX
 * @note El retorno indica bytes escritos en buffer, no bytes transmitidos físicamente
 */
int uart_tx_data(const char *data, size_t data_size)
{
    /* 
     * uart_write_bytes copia los datos al buffer de transmisión UART
     * La transmisión física real ocurre de forma asíncrona por hardware
     */
    return uart_write_bytes(UART_PORT, data, data_size);
}

/**
 * @brief Recibe datos desde el puerto UART con timeout
 * 
 * Lee datos del buffer de recepción UART. Si no hay suficientes datos
 * disponibles, la función esperará hasta el timeout especificado antes
 * de retornar con los datos disponibles hasta ese momento.
 * 
 * @param buffer Buffer de destino para almacenar los datos recibidos
 * @param max_len Cantidad máxima de bytes a leer
 * @param timeout_ms Tiempo máximo de espera en milisegundos
 * @return Número de bytes leídos (puede ser 0 si no hay datos disponibles)
 * 
 * @note pdMS_TO_TICKS convierte milisegundos a ticks de FreeRTOS
 * @note La función retorna antes si se recibe la cantidad max_len de bytes
 * @note Un timeout_ms de 0 hace que la función retorne inmediatamente
 */
int uart_rx_data(uint8_t *buffer, int max_len, int timeout_ms)
{
    /* 
     * uart_read_bytes lee datos del buffer de recepción UART
     * pdMS_TO_TICKS(timeout_ms) convierte milisegundos a ticks del sistema
     * para que FreeRTOS pueda manejar el timeout correctamente
     */
    return uart_read_bytes(UART_PORT, buffer, max_len, pdMS_TO_TICKS(timeout_ms));
}

/**
 * @brief Limpia el buffer de recepción UART
 * 
 * Descarta todos los datos pendientes en el buffer de recepción del
 * puerto UART. Esta operación es útil para eliminar datos antiguos
 * o corruptos antes de iniciar una nueva transacción de comunicación,
 * asegurando que las lecturas posteriores solo contengan datos nuevos.
 * 
 * @return esp_err_t
 *         - ESP_OK: Buffer limpiado exitosamente
 *         - ESP_FAIL: Error al limpiar el buffer
 * 
 * @note Después de esta operación, cualquier dato no leído se pierde
 * @note Esta función no afecta al buffer de transmisión
 * @warning Use con precaución para no descartar datos importantes sin procesar
 */
esp_err_t uart_flush_rx_buffer(void)
{
    /* 
     * uart_flush_input elimina todos los datos en el buffer de recepción
     * Es una operación inmediata que no espera por transmisiones pendientes
     */
    return uart_flush_input(UART_PORT);
}