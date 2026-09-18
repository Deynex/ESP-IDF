/**
 * @file soft_ap.h
 * @brief API para la gestión del WiFi en modo SoftAP (Punto de Acceso)
 * 
 * Este archivo proporciona las funciones necesarias para inicializar y configurar
 * el ESP32 como un punto de acceso WiFi (SoftAP).
 * @author DEYNEX
 */

#ifndef SOFT_AP_H
#define SOFT_AP_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa el almacenamiento no volátil (NVS)
 * 
 * Esta función debe ser llamada antes de inicializar el WiFi.
 * Si el NVS está corrupto o es una versión incompatible, se borrará y reinicializará.
 * 
 * @return
 *     - ESP_OK: Inicialización exitosa
 *     - ESP_FAIL: Error en la inicialización
 * 
 * @note Esta función hace abort() del programa si la inicialización falla
 */
esp_err_t nvs_init(void);

/**
 * @brief Inicializa y arranca el WiFi en modo Punto de Acceso (SoftAP)
 * 
 * Esta función realiza las siguientes operaciones:
 * - Inicializa el stack de red (netif)
 * - Crea el loop de eventos por defecto
 * - Registra los manejadores de eventos WiFi
 * - Configura el WiFi en modo AP
 * - Configura la dirección IP estática del AP
 * - Inicia el servidor DHCP para asignar IPs a los clientes
 * - Arranca el controlador WiFi
 * 
 * @return
 *     - ESP_OK: Inicialización exitosa
 *     - ESP_FAIL: Error en la inicialización
 * 
 * @note Debe llamarse nvs_init() antes de esta función
 * @note Esta función hace abort() del programa si hay errores críticos
 */
esp_err_t wifi_init(void);

/**
 * @brief Detiene el WiFi y libera los recursos
 * 
 * @return
 *     - ESP_OK: WiFi detenido correctamente
 *     - ESP_FAIL: Error al detener el WiFi
 */
esp_err_t wifi_stop(void);

/**
 * @brief Obtiene el número de estaciones conectadas al SoftAP
 * 
 * @return Número de estaciones conectadas (0-4 por defecto)
 */
uint8_t wifi_get_connected_stations(void);

#ifdef __cplusplus
}
#endif

#endif /* SOFT_AP_H */