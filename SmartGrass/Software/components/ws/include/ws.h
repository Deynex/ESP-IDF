/**
 * @file ws.h
 * @brief API para el servidor WebSocket.
 * @author DEYNEX
 */

#pragma once

#include "esp_http_server.h"
#include "vehicle.h" // Incluimos para el tipo vehicle_handle_t

/**
 * @brief Registra los handlers de WebSocket en un servidor HTTP existente.
 *
 * @param server El handle del servidor HTTP (obtenido de httpd_start).
 * @param vehicle El handle del vehículo (creado en main.c) para enviar comandos.
 * @return ESP_OK si se registra correctamente, o un error en caso contrario.
 */
esp_err_t ws_server_register_handlers(httpd_handle_t server, vehicle_handle_t vehicle);

/**
 * @brief Envía un mensaje de texto a todos los clientes WebSocket conectados.
 *
 * Esta función es thread-safe y se puede llamar desde cualquier tarea.
 * Utiliza la cola de trabajo del servidor HTTP para enviar los datos de forma asíncrona.
 *
 * @param data Puntero al string de texto (JSON) a enviar.
 * @return ESP_OK si el trabajo de envío se encoló correctamente.
 */
esp_err_t ws_server_send_text_all(const char* data);