/**
 * @file uart.h
 * @brief Módulo de comunicación UART para ESP32
 * 
 * Este módulo proporciona funciones para la comunicación serie UART,
 * incluyendo inicialización, transmisión, recepción y gestión de buffers.
 * Está diseñado para comunicarse con el servo MKS SERVO42C.
 * 
 * @note Utiliza UART_NUM_1 del ESP32 con los pines GPIO 16 (RX) y GPIO 17 (TX)
 * @note Velocidad de comunicación configurada a 115200 baudios
 * @author DEYNEX
 */

#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/**
 * @brief Inicializa el módulo UART con la configuración predefinida.
 * 
 * Configura el puerto UART con los siguientes parámetros:
 * - Puerto: UART_NUM_1
 * - Velocidad: 115200 baudios
 * - Bits de datos: 8
 * - Paridad: Ninguna
 * - Bits de parada: 1
 * - Control de flujo: Deshabilitado
 * - Pin TX: GPIO 17
 * - Pin RX: GPIO 16
 * - Buffer de recepción: 512 bytes (256 * 2)
 * 
 * @note Esta función debe ser llamada antes de usar cualquier otra función UART
 * @note Instala el driver UART y configura los pines GPIO correspondientes
 */
void uart_init(void);

/**
 * @brief Envía una cadena de texto a través de UART.
 * 
 * Transmite un bloque de datos a través del puerto UART configurado.
 * La función es bloqueante hasta que todos los datos sean escritos
 * en el buffer de transmisión del hardware.
 *
 * @param data Puntero a la cadena de caracteres o datos binarios a enviar.
 *             No necesita ser null-terminated para data_size específico.
 * @param data_size Tamaño exacto de los datos a enviar en bytes.
 * 
 * @return El número de bytes escritos exitosamente en el buffer UART.
 *         Debería ser igual a data_size en operación normal.
 * 
 * @note La función no agrega caracteres adicionales (como '\n' o '\r')
 * @note Es seguro enviar datos binarios (no solo texto)
 * 
 * @example
 * const char *msg = "Hola";
 * uart_tx_data(msg, strlen(msg));
 */
int uart_tx_data(const char *data, size_t data_size);

/**
 * @brief Lee bytes desde UART con timeout.
 * 
 * Lee datos disponibles del buffer de recepción UART hasta alcanzar
 * el tamaño máximo especificado o hasta que expire el timeout.
 * La función es bloqueante durante el tiempo de timeout.
 *
 * @param buffer Buffer de destino donde se almacenarán los datos recibidos.
 *               Debe tener al menos max_len bytes de espacio disponible.
 * @param max_len Tamaño máximo del buffer en bytes. Determina cuántos bytes
 *                como máximo se pueden leer en esta llamada.
 * @param timeout_ms Timeout en milisegundos. La función esperará este tiempo
 *                   para recibir datos. Si es 0, retorna inmediatamente.
 * 
 * @return Número de bytes leídos y almacenados en el buffer.
 *         Puede ser menor que max_len si:
 *         - Se reciben menos bytes que max_len
 *         - Expira el timeout antes de llenar el buffer
 *         - No hay datos disponibles (retorna 0)
 * 
 * @note La función no agrega null-terminator al final de los datos
 * @note Los datos no leídos permanecen en el buffer UART para la siguiente lectura
 * 
 * @example
 * uint8_t buffer[64];
 * int bytes_read = uart_rx_data(buffer, 64, 1000); // Espera hasta 1 segundo
 * if (bytes_read > 0) {
 *     // Procesar datos recibidos
 * }
 */
int uart_rx_data(uint8_t *buffer, int max_len, int timeout_ms);

/**
 * @brief Limpia el buffer de recepción de UART.
 * 
 * Descarta todos los datos pendientes en el buffer de recepción del
 * puerto UART. Útil para eliminar datos antiguos o no deseados antes
 * de iniciar una nueva transacción de comunicación.
 * 
 * @return esp_err_t Código de error de la operación:
 *         - ESP_OK: Buffer limpiado exitosamente
 *         - ESP_FAIL: Error al limpiar el buffer
 *         - ESP_ERR_INVALID_ARG: Puerto UART inválido
 * 
 * @note Después de llamar esta función, cualquier dato no leído se perderá
 * @note Es recomendable llamar esta función antes de enviar comandos críticos
 *       para asegurar que la respuesta no contenga datos antiguos
 * 
 * @example
 * uart_flush_rx_buffer(); // Limpia datos antiguos
 * uart_tx_data("CMD", 3); // Envía comando
 * uart_rx_data(buffer, 64, 1000); // Lee respuesta limpia
 */
esp_err_t uart_flush_rx_buffer(void);

#endif // UART_H