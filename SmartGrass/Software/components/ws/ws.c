/**
 * @file ws.c
 * @brief Implementación del servidor WebSocket.
 * @author DEYNEX
 */

#include "ws.h"
#include <esp_log.h>
#include <string.h>
#include "a4988.h"
#include "brushless.h"

static const char *TAG = "WEBSOCKET";

// ==========================================================================================================================================================
// MARK: Definiciones
// ==========================================================================================================================================================
#define CMD_STOP 0x00
#define CMD_FORWARD 0x01
#define CMD_BACKWARD 0x02
#define CMD_LEFT 0x03
#define CMD_RIGHT 0x04
#define CMD_BLADE_ON 0x05
#define CMD_BLADE_OFF 0x06
#define CMD_ENABLE 0x07
#define CMD_DISABLE 0x08

#define VEHICLE_CONTROL_SPEED 100.0f
#define BLADE_CUT_SPEED 30.0f // 30% Potencia para corte estándar

// ==========================================================================================================================================================
// MARK: Gestión de Clientes
// ==========================================================================================================================================================
#define MAX_CLIENTS 5

typedef struct
{
    httpd_handle_t handle;
    int fds[MAX_CLIENTS];
} ws_clients_t;

static ws_clients_t s_clients = {
    .handle = NULL,
    .fds = {-1, -1, -1, -1, -1}};

// Handle del vehículo, pasado desde main.c
static vehicle_handle_t s_vehicle_handle = NULL;

static void add_client(httpd_handle_t handle, int fd)
{
    if (s_clients.handle == NULL)
    {
        s_clients.handle = handle;
    }
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (s_clients.fds[i] == -1)
        {
            ESP_LOGI(TAG, "Cliente conectado, fd=%d, asignado al slot %d", fd, i);
            s_clients.fds[i] = fd;
            return;
        }
    }
    ESP_LOGW(TAG, "No hay slots libres para el cliente fd=%d", fd);
}

static void remove_client(int fd)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (s_clients.fds[i] == fd)
        {
            ESP_LOGI(TAG, "Cliente desconectado, fd=%d, liberado del slot %d", fd, i);
            s_clients.fds[i] = -1;
            break;
        }
    }
    bool any_client_left = false;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (s_clients.fds[i] != -1)
        {
            any_client_left = true;
            break;
        }
    }
    if (!any_client_left)
    {
        s_clients.handle = NULL;
    }
}

// ==========================================================================================================================================================
// MARK: Telelemetría
// ==========================================================================================================================================================

struct async_send_arg
{
    char *data;
    size_t len;
};

static void ws_async_send_task(void *arg)
{
    struct async_send_arg *args = (struct async_send_arg *)arg;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)args->data;
    ws_pkt.len = args->len;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (s_clients.fds[i] != -1)
        {
            httpd_ws_send_frame_async(s_clients.handle, s_clients.fds[i], &ws_pkt);
        }
    }
    free(args->data);
    free(args);
}

esp_err_t ws_server_send_text_all(const char *data)
{
    if (data == NULL)
        return ESP_ERR_INVALID_ARG;
    if (s_clients.handle == NULL)
        return ESP_FAIL; // No hay clientes

    size_t len = strlen(data);
    if (len == 0)
        return ESP_OK;

    struct async_send_arg *args = malloc(sizeof(struct async_send_arg));
    if (args == NULL)
        return ESP_ERR_NO_MEM;

    args->data = malloc(len + 1); // +1 para NUL
    if (args->data == NULL)
    {
        free(args);
        return ESP_ERR_NO_MEM;
    }

    memcpy(args->data, data, len + 1);
    args->len = len;

    esp_err_t ret = httpd_queue_work(s_clients.handle, ws_async_send_task, args);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_queue_work falló: %s", esp_err_to_name(ret));
        free(args->data);
        free(args);
    }
    return ret;
}

// =========================================================================================================================================================
// MARK: Comandos de Control
// =========================================================================================================================================================

static void handle_control_command(uint8_t cmd)
{
    if (s_vehicle_handle == NULL)
        return;

    switch (cmd)
    {
    case CMD_FORWARD:
        vehicle_move(s_vehicle_handle, A4988_DUAL_FORWARD, VEHICLE_CONTROL_SPEED);
        break;

    case CMD_BACKWARD:
        vehicle_move(s_vehicle_handle, A4988_DUAL_BACKWARD, VEHICLE_CONTROL_SPEED);
        break;

    case CMD_LEFT:
        vehicle_move(s_vehicle_handle, A4988_DUAL_LEFT, VEHICLE_CONTROL_SPEED);
        break;

    case CMD_RIGHT:
        vehicle_move(s_vehicle_handle, A4988_DUAL_RIGHT, VEHICLE_CONTROL_SPEED);
        break;

    case CMD_STOP:
        vehicle_stop(s_vehicle_handle);
        break;

    case CMD_BLADE_ON:
        vehicle_control_blade(s_vehicle_handle, BLADE_CUT_SPEED);
        break;

    case CMD_BLADE_OFF:
        vehicle_control_blade(s_vehicle_handle, 0.0f);
        break;

    case CMD_ENABLE:
        vehicle_enable(s_vehicle_handle);
        break;

    case CMD_DISABLE:
        vehicle_disable(s_vehicle_handle); // Esto corta torque y apaga cuchilla
        break;
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "Handshake solicitado por cliente, fd=%d", httpd_req_to_sockfd(req));
        add_client(req->handle, httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame (len) falló: %s", esp_err_to_name(ret));
        remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
    {
        ESP_LOGI(TAG, "Cliente envió paquete CLOSE, fd=%d", httpd_req_to_sockfd(req));
        remove_client(httpd_req_to_sockfd(req));
        httpd_ws_send_frame(req, &ws_pkt); // Enviar ACK de cierre
        return ESP_OK;
    }

    if (ws_pkt.len > 0)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Fallo al reservar memoria para el buffer del WS");
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame (payload) falló: %s", esp_err_to_name(ret));
            free(buf);
            return ret;
        }

        // --- Procesar el paquete recibido ---
        if (ws_pkt.type == HTTPD_WS_TYPE_BINARY)
        {
            // Este es un comando de control
            if (ws_pkt.len == 1)
            {
                handle_control_command(ws_pkt.payload[0]);
            }
            else
            {
                ESP_LOGW(TAG, "Paquete binario de longitud inesperada: %d", ws_pkt.len);
            }
        }
        else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
        {
            // El cliente no debería enviar texto, pero lo logueamos si lo hace
            ESP_LOGI(TAG, "Texto recibido de fd=%d: [%s]", httpd_req_to_sockfd(req), ws_pkt.payload);
        }

        free(buf);
    }

    return ESP_OK;
}

// =========================================================================================================================================================
// MARK: Registro de Handlers
// =========================================================================================================================================================

esp_err_t ws_server_register_handlers(httpd_handle_t server, vehicle_handle_t vehicle)
{
    // Guardar el handle del vehículo para que ws_handler pueda usarlo
    s_vehicle_handle = vehicle;

    static const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true};

    s_clients.handle = server;
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        s_clients.fds[i] = -1;
    }

    ESP_LOGI(TAG, "Registrando URI /ws para WebSockets");
    return httpd_register_uri_handler(server, &ws_uri);
}