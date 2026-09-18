#include "server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "switch.h"
#include <stdio.h>

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: WebSocket Client Management
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

#define MAX_WS_CLIENTS 5

typedef struct
{
    httpd_handle_t server_handle;
    int fd;
    bool is_active;
} ws_client_t;

static ws_client_t ws_clients[MAX_WS_CLIENTS];
static TimerHandle_t current_timer = NULL;

// Función para agregar un cliente WebSocket
static void add_ws_client(httpd_handle_t server, int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (!ws_clients[i].is_active)
        {
            ws_clients[i].server_handle = server;
            ws_clients[i].fd = fd;
            ws_clients[i].is_active = true;
            ESP_LOGI("WS", "Cliente agregado en posición %d, fd: %d", i, fd);
            return;
        }
    }
    ESP_LOGW("WS", "No hay espacio para más clientes WebSocket");
}

// Función para remover un cliente WebSocket
static void remove_ws_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].is_active && ws_clients[i].fd == fd)
        {
            ws_clients[i].is_active = false;
            ESP_LOGI("WS", "Cliente removido de posición %d, fd: %d", i, fd);
            return;
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Current Monitoring
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Función para enviar datos de corriente a todos los clientes conectados
void send_switch_current(void)
{
    float current = switch_get_current_rms();

    // Crear payload: 1 byte para tipo de mensaje + 4 bytes para float
    uint8_t *payload = malloc(5);
    if (payload == NULL)
    {
        ESP_LOGE("WS", "Error al asignar memoria para payload de corriente");
        return;
    }

    payload[0] = 0xFF; // Identificador para mensaje de corriente solamente
    memcpy(&payload[1], &current, sizeof(float));

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    ws_pkt.payload = payload;
    ws_pkt.len = 5;

    // Enviar a todos los clientes conectados
    for (int i = 0; i < MAX_WS_CLIENTS; i++)
    {
        if (ws_clients[i].is_active)
        {
            // Verificar si la conexión sigue activa
            if (httpd_ws_get_fd_info(ws_clients[i].server_handle, ws_clients[i].fd))
            {
                esp_err_t ret = httpd_ws_send_frame_async(ws_clients[i].server_handle, ws_clients[i].fd, &ws_pkt);
                if (ret != ESP_OK)
                {
                    ESP_LOGW("WS", "Error enviando corriente al cliente %d: %s", i, esp_err_to_name(ret));
                    // Remover cliente si falló el envío
                    ws_clients[i].is_active = false;
                }
            }
            else
            {
                // Cliente desconectado, remover de la lista
                ws_clients[i].is_active = false;
                ESP_LOGI("WS", "Cliente %d desconectado, removido de la lista", i);
            }
        }
    }

    free(payload);
    ESP_LOGD("WS", "Corriente enviada: %.3f A", current);
}

// Timer callback para envío periódico de corriente
static void current_timer_callback(TimerHandle_t xTimer)
{
    send_switch_current();
}

// Función para inicializar el monitoreo de corriente
static void init_current_monitoring(void)
{
    if (current_timer == NULL)
    {
        current_timer = xTimerCreate("CurrentTimer",
                                     pdMS_TO_TICKS(1000), // Enviar cada 1 segundo
                                     pdTRUE,              // Repetir automáticamente
                                     NULL,
                                     current_timer_callback);

        if (current_timer != NULL)
        {
            xTimerStart(current_timer, 0);
            ESP_LOGI("WS", "Monitoreo de corriente iniciado");
        }
        else
        {
            ESP_LOGE("WS", "Error creando timer de corriente");
        }
    }
}

// Función para detener el monitoreo de corriente
static void stop_current_monitoring(void)
{
    if (current_timer != NULL)
    {
        xTimerStop(current_timer, 0);
        xTimerDelete(current_timer, 0);
        current_timer = NULL;
        ESP_LOGI("WS", "Monitoreo de corriente detenido");
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: WebSocket
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// Estructura de datos de respuesta asíncrona para comunicación WebSocket
struct arg_resp_asinc
{
    httpd_handle_t hd; // Instancia del servidor
    int fd;            // Descriptor de archivo de socket de sesión
};

// Función de envío asíncrono, que colocamos en la cola de trabajo de httpd
static void ws_async_send(void *arg)
{
    printf("Envío de datos asíncronos\n");
    static const char *datos = "Async data";
    struct arg_resp_asinc *resp_asinc = arg;
    httpd_handle_t hd = resp_asinc->hd;
    int fd = resp_asinc->fd;
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)datos;
    ws_pkt.len = strlen(datos);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    httpd_ws_send_frame_async(hd, fd, &ws_pkt);
    free(resp_asinc);
}

// Función para activar el envío de datos asíncronos
static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    printf("Activación de envío de datos asíncronos\n");
    struct arg_resp_asinc *resp_asinc = malloc(sizeof(struct arg_resp_asinc));
    if (resp_asinc == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    resp_asinc->hd = req->handle;
    resp_asinc->fd = httpd_req_to_sockfd(req);
    esp_err_t ret = httpd_queue_work(handle, ws_async_send, resp_asinc);
    if (ret != ESP_OK)
    {
        free(resp_asinc);
    }
    return ret;
}

esp_err_t send_switch_state(httpd_req_t *req, bool led_state)
{
    // Verificar que la conexión sea válida
    if (!httpd_ws_get_fd_info(req->handle, httpd_req_to_sockfd(req)))
    {
        ESP_LOGE("WS", "Conexión WebSocket no válida");
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
    
    // Crear estructura de datos: 1 byte para estado + 4 bytes para float
    uint8_t *payload = malloc(5);
    if (payload == NULL)
    {
        ESP_LOGE("WS", "Error al asignar memoria para payload");
        return ESP_ERR_NO_MEM;
    }

    // Byte 0: estado del switch (1 o 0)
    payload[0] = led_state ? 1 : 0;
    
    // Bytes 1-4: valor de corriente como float
    float current = switch_get_current_rms();
    memcpy(&payload[1], &current, sizeof(float));
    
    ws_pkt.payload = payload;
    ws_pkt.len = 5;

    esp_err_t ret = httpd_ws_send_frame(req, &ws_pkt);
    if (ret != ESP_OK)
    {
        ESP_LOGE("WS", "Error enviando frame: %s", esp_err_to_name(ret));
    }

    free(payload);
    return ret;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        // Agregar cliente a la lista
        add_ws_client(req->handle, httpd_req_to_sockfd(req));

        // Enviar estado inicial
        send_switch_state(req, switch_get_state());

        // Iniciar monitoreo de corriente si es el primer cliente
        bool has_active_clients = false;
        for (int i = 0; i < MAX_WS_CLIENTS; i++)
        {
            if (ws_clients[i].is_active)
            {
                has_active_clients = true;
                break;
            }
        }
        if (has_active_clients)
        {
            init_current_monitoring();
        }

        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret != ESP_OK)
    {
        ESP_LOGE("WS", "httpd_ws_recv_frame falló al obtener la longitud del marco con %d", ret);
        // Remover cliente si hay error
        remove_ws_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE("WS", "Error al asignar memoria para buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE("WS", "httpd_ws_recv_frame falló con %d", ret);
            free(buf);
            // Remover cliente si hay error
            remove_ws_client(httpd_req_to_sockfd(req));
            return ret;
        }

        // Actualizar el estado del interruptor para mensajes binarios
        if (ws_pkt.type == HTTPD_WS_TYPE_BINARY && ws_pkt.len > 0)
        {
            bool led_state = (ws_pkt.payload[0] != 0);
            switch_set_state(led_state);

            // Enviar confirmación del nuevo estado a todos los clientes
            for (int i = 0; i < MAX_WS_CLIENTS; i++)
            {
                if (ws_clients[i].is_active)
                {
                    // Crear una solicitud temporal para enviar el estado
                    httpd_req_t temp_req = *req;
                    temp_req.handle = ws_clients[i].server_handle;
                    // Nota: Esto es una simplificación, en producción sería mejor usar httpd_ws_send_frame_async
                }
            }
        }

        // Verificar mensaje de trigger async solo para mensajes de texto
        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_pkt.len > 0)
        {
            // Asegurar que el string esté terminado en null
            ws_pkt.payload[ws_pkt.len] = '\0';
            ESP_LOGI("WS", "Mensaje de texto recibido: %s", (char *)ws_pkt.payload);

            if (strcmp((char *)ws_pkt.payload, "Trigger async") == 0)
            {
                free(buf);
                return trigger_async_send(req->handle, req);
            }
        }

        free(buf);
    }
    else
    {
        ESP_LOGI("WS", "Frame sin payload recibido");
    }

    return ret;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: URI
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

extern const uint8_t main_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t main_index_html_end[] asm("_binary_index_html_end");
extern const uint8_t settings_index_html_start[] asm("_binary_config_html_start");
extern const uint8_t settings_index_html_end[] asm("_binary_config_html_end");
extern const uint8_t favicon_ico_start[] asm("_binary_logo_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_logo_ico_end");

static esp_err_t main_index_handler(httpd_req_t *req)
{
    size_t size = main_index_html_end - main_index_html_start;

    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    char content_length[32];
    snprintf(content_length, sizeof(content_length), "%zu", size);
    httpd_resp_set_hdr(req, "Content-Length", content_length);

    httpd_resp_send(req, (const char *)main_index_html_start, size);
    return ESP_OK;
}

static esp_err_t settings_index_handler(httpd_req_t *req)
{
    size_t size = settings_index_html_end - settings_index_html_start;

    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    char content_length[32];
    snprintf(content_length, sizeof(content_length), "%zu", size);
    httpd_resp_set_hdr(req, "Content-Length", content_length);

    httpd_resp_send(req, (const char *)settings_index_html_start, size);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=31536000, immutable"); // Almacenar en caché por un año, no modificar
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_end - favicon_ico_start);
    return ESP_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Eventos
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void http_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == ESP_HTTP_SERVER_EVENT)
    {
        switch (event_id)
        {
        case HTTP_SERVER_EVENT_ERROR:
            ESP_LOGE("HTTP", "Error en el servidor web");
            break;
        case HTTP_SERVER_EVENT_START:
            ESP_LOGI("HTTP", "Servidor web iniciado");
            break;
        case HTTP_SERVER_EVENT_ON_CONNECTED:
            ESP_LOGI("HTTP", "Cliente conectado al servidor web");
            break;
        case HTTP_SERVER_EVENT_ON_HEADER:
            ESP_LOGI("HTTP", "Cabecera recibida");
            break;
        case HTTP_SERVER_EVENT_HEADERS_SENT:
            ESP_LOGI("HTTP", "Cabeceras enviadas");
            break;
        case HTTP_SERVER_EVENT_ON_DATA:
            ESP_LOGI("HTTP", "Datos recibidos");
            break;
        case HTTP_SERVER_EVENT_SENT_DATA:
            ESP_LOGI("HTTP", "Datos enviados");
            break;
        case HTTP_SERVER_EVENT_DISCONNECTED:
            ESP_LOGI("HTTP", "Cliente desconectado del servidor web");
            break;
        case HTTP_SERVER_EVENT_STOP:
            ESP_LOGW("HTTP", "Servidor web detenido");
            break;
        default:
            ESP_LOGW("HTTP", "Evento no manejado: %ld", event_id);
            break;
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Funciones
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

static httpd_handle_t server_handle = NULL;

void http_server_init(void)
{
    if (server_handle != NULL)
    {
        ESP_LOGW("HTTP", "Servidor ya iniciado");
        return;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true; // Habilita la purga LRU

    ESP_LOGI("HTTP", "Iniciando servidor web en puerto: %d", config.server_port);

    // Registra el manejador de eventos del servidor HTTP
    esp_err_t ret = esp_event_handler_register(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID, &http_event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE("HTTP", "Error registrando manejador de eventos: %s", esp_err_to_name(ret));
        return;
    }

    // Inicia el servidor HTTP
    ret = httpd_start(&server_handle, &config);
    if (ret != ESP_OK)
    {
        ESP_LOGE("HTTP", "Error iniciando servidor: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID, &http_event_handler);
        return;
    }

    // Registro de URIs
    // Pagina principal
    httpd_uri_t uri_main_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = main_index_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_main_index);

    // Configuración del interruptor
    httpd_uri_t uri_settings_index = {
        .uri = "/settings",
        .method = HTTP_GET,
        .handler = settings_index_handler, // Debes definir esta función en tu código
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_settings_index);

    // WebSocket
    httpd_uri_t uri_ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true, // Habilita el WebSocket
    };
    httpd_register_uri_handler(server_handle, &uri_ws);

    // Favicon
    httpd_uri_t uri_favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(server_handle, &uri_favicon);
}

void http_server_stop(void)
{
    if (server_handle != NULL)
    {
        ESP_LOGI("HTTP", "Deteniendo servidor web");

        // Detener monitoreo de corriente
        stop_current_monitoring();

        // Limpiar lista de clientes
        for (int i = 0; i < MAX_WS_CLIENTS; i++)
        {
            ws_clients[i].is_active = false;
        }

        esp_event_handler_unregister(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID, &http_event_handler);
        httpd_stop(server_handle);
        server_handle = NULL;
    }
}

void http_server_restart(void)
{
    ESP_LOGI("HTTP", "Reiniciando servidor web");
    http_server_stop();
    http_server_init();
}