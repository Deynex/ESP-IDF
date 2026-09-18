#pragma once

#include "esp_err.h"
#include "vehicle.h"

/**
 * @brief Inicializa el servidor HTTP y los handlers de WebSocket.
 *
 * @param vehicle El handle del vehículo (creado en main.c).
 * Es necesario para pasarlo al módulo de WebSocket.
 * @return ESP_OK si el servidor se inició correctamente.
 */
esp_err_t http_server_init(vehicle_handle_t vehicle);

/**
 * @brief Detiene el servidor HTTP
 * 
 * Esta función detiene el servidor HTTP y libera todos los recursos asociados.
 * Desregistra el manejador de eventos y limpia el handle del servidor.
 * 
 * @note Es seguro llamar esta función múltiples veces
 * @note Esta función es idempotente
 */
void http_server_stop(void);