/**
 * @file i2c.h
 * @brief API de alto nivel para comunicación I2C en ESP32
 *
 * Este módulo proporciona funciones simplificadas para trabajar con el
 * bus I2C del ESP32, abstrayendo la complejidad de la configuración y
 * las operaciones básicas de lectura/escritura de registros.
 *
 * Funcionalidades principales:
 * - Inicialización y configuración del bus I2C maestro
 * - Gestión de múltiples dispositivos I2C en el mismo bus
 * - Lectura y escritura de registros de dispositivos I2C
 * - Manejo robusto de errores con logging detallado
 * @author DEYNEX
 */

#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Inicialización del bus I2C maestro con configuración predeterminada
 *
 * Configura e inicializa un bus I2C en modo maestro con los siguientes parámetros:
 * - Fuente de reloj: Por defecto del sistema
 * - Resistencias pull-up internas: Activadas
 * - Filtro anti-glitch: 7 ciclos de reloj
 *
 * El bus debe ser inicializado antes de poder agregar dispositivos.
 *
 * @param[out] bus_handle Puntero donde se almacenará el handle del bus I2C creado.
 *                        Este handle se usará para agregar dispositivos al bus.
 * @param[in] sda_pin Número de pin GPIO para la línea de datos (SDA).
 *                    Debe ser un GPIO válido del ESP32.
 * @param[in] scl_pin Número de pin GPIO para la línea de reloj (SCL).
 *                    Debe ser un GPIO válido del ESP32.
 * @param[in] port_num Puerto I2C a utilizar (I2C_NUM_0 o I2C_NUM_1).
 *                     El ESP32 tiene dos controladores I2C independientes.
 *
 * @return esp_err_t
 *         - ESP_OK: Éxito en la inicialización
 *         - ESP_ERR_INVALID_ARG: Parámetros inválidos
 *         - ESP_FAIL: Error en la inicialización del hardware
 *
 * @note Las resistencias pull-up internas pueden no ser suficientes para
 *       buses largos o con múltiples dispositivos. En esos casos, considere
 *       usar resistencias pull-up externas (típicamente 4.7kΩ).
 *
 * @warning El bus debe ser desinicializado correctamente con i2c_del_master_bus()
 *          antes de reinicializar o al finalizar la aplicación.
 */
esp_err_t i2c_master_bus_init(i2c_master_bus_handle_t *bus_handle, int sda_pin, int scl_pin, i2c_port_t port_num);

/**
 * @brief Registra un dispositivo esclavo en el bus I2C maestro
 *
 * Añade un dispositivo I2C al bus previamente inicializado, permitiendo
 * realizar operaciones de lectura/escritura con ese dispositivo específico.
 * Cada dispositivo puede tener su propia velocidad de reloj.
 *
 * Un mismo bus I2C puede tener múltiples dispositivos con diferentes
 * direcciones y velocidades.
 *
 * @param[in] bus_handle Handle del bus I2C maestro obtenido de i2c_master_bus_init().
 *                       El bus debe estar previamente inicializado.
 * @param[out] dev_handle Puntero donde se almacenará el handle del dispositivo.
 *                        Este handle se usará para las operaciones de lectura/escritura.
 * @param[in] dev_addr Dirección I2C de 7 bits del dispositivo esclavo (0x00 - 0x7F).
 *                     Consulte la hoja de datos del dispositivo para conocer su dirección.
 * @param[in] freq_hz Frecuencia del reloj SCL para este dispositivo en Hz.
 *                    Valores típicos:
 *                    - 100000 (100 kHz): Modo estándar
 *                    - 400000 (400 kHz): Modo rápido
 *                    - 1000000 (1 MHz): Modo rápido plus (si es compatible)
 *
 * @return esp_err_t
 *         - ESP_OK: Dispositivo agregado exitosamente
 *         - ESP_ERR_INVALID_ARG: Parámetros inválidos (dirección fuera de rango, etc.)
 *         - ESP_ERR_NO_MEM: No hay memoria suficiente para agregar el dispositivo
 *
 * @note La dirección I2C del dispositivo suele estar documentada en su datasheet
 *       y puede ser fija o configurable mediante pines de hardware.
 *
 * @warning Asegúrese de que la frecuencia especificada sea compatible con el
 *          dispositivo. Una frecuencia demasiado alta puede causar errores de comunicación.
 */
esp_err_t i2c_add_device(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle, uint8_t dev_addr, uint32_t freq_hz);

/**
 * @brief Lee uno o más bytes desde un registro de un dispositivo I2C
 *
 * Realiza una operación combinada de escritura-lectura (repeated start):
 * 1. Transmite la dirección del registro al dispositivo
 * 2. Inicia una lectura para obtener el contenido del registro
 *
 * Esta función es útil para leer registros consecutivos de sensores y
 * otros dispositivos I2C que soportan lectura secuencial.
 *
 * @param[in] dev_handle Handle del dispositivo obtenido de i2c_add_device().
 *                       El dispositivo debe estar previamente registrado en el bus.
 * @param[in] reg_addr Dirección del registro inicial a leer (8 bits).
 *                     Algunos dispositivos soportan auto-incremento de dirección.
 * @param[out] data Puntero al buffer donde se almacenarán los bytes leídos.
 *                  El buffer debe tener al menos 'len' bytes de capacidad.
 * @param[in] len Número de bytes a leer desde el registro.
 *                Si el dispositivo soporta auto-incremento, se leerán
 *                registros consecutivos.
 *
 * @return esp_err_t
 *         - ESP_OK: Lectura exitosa
 *         - ESP_ERR_INVALID_ARG: Parámetros inválidos (puntero NULL, etc.)
 *         - ESP_ERR_TIMEOUT: El dispositivo no respondió a tiempo
 *         - ESP_FAIL: Error en la comunicación I2C (NACK recibido)
 *
 * @note Si len > 1, el comportamiento depende del dispositivo:
 *       - Algunos dispositivos auto-incrementan el registro
 *       - Otros siempre devuelven el mismo registro
 *       Consulte el datasheet del dispositivo.
 *
 * @warning El buffer 'data' debe tener suficiente espacio asignado.
 *          Leer más bytes de los que el buffer puede contener causará
 *          desbordamiento de memoria.
 */
esp_err_t i2c_register_read(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t *data, size_t len);

/**
 * @brief Escribe un byte en un registro específico de un dispositivo I2C
 *
 * Realiza una operación de escritura I2C enviando dos bytes:
 * 1. La dirección del registro donde escribir
 * 2. El valor a escribir en ese registro
 *
 * Esta función es típicamente usada para configurar parámetros de
 * dispositivos, ajustar modos de operación, o enviar comandos.
 *
 * @param[in] dev_handle Handle del dispositivo obtenido de i2c_add_device().
 *                       El dispositivo debe estar previamente registrado en el bus.
 * @param[in] reg_addr Dirección del registro donde escribir (8 bits).
 *                     Consulte el mapa de registros en el datasheet del dispositivo.
 * @param[in] data Byte a escribir en el registro especificado.
 *                 El significado de cada bit depende del registro específico.
 *
 * @return esp_err_t
 *         - ESP_OK: Escritura exitosa
 *         - ESP_ERR_INVALID_ARG: Parámetros inválidos
 *         - ESP_ERR_TIMEOUT: El dispositivo no respondió a tiempo
 *         - ESP_FAIL: Error en la comunicación I2C (NACK recibido)
 *
 * @note Algunos registros pueden ser de solo lectura. Intente escribir
 *       solo en registros que el datasheet indique como escribibles.
 *
 * @note Para escribir múltiples bytes consecutivos, considere implementar
 *       una función i2c_register_write_bytes() similar.
 *
 * @warning Escribir valores incorrectos en ciertos registros puede poner
 *          el dispositivo en un estado indeseado o dañarlo. Siempre consulte
 *          el datasheet antes de escribir.
 */
esp_err_t i2c_register_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t reg_addr, uint8_t data);