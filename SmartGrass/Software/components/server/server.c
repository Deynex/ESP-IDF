#include "server.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "ws.h"
#include <string.h>

static const char *TAG = "HTTP_SERVER";

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Definiciones
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

#define HTTP_CACHE_MAX_AGE "max-age=31536000, immutable"
#define HTTP_NO_CACHE "no-cache, no-store, must-revalidate"

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Recursos embebidos
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");
extern const uint8_t favicon_ico_start[] asm("_binary_logo_ico_start");
extern const uint8_t favicon_ico_end[] asm("_binary_logo_ico_end");

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Manejadores de URI
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

static esp_err_t main_index_handler(httpd_req_t *req)
{
    if (req == NULL) {
        ESP_LOGE(TAG, "Solicitud NULL en main_index_handler");
        return ESP_ERR_INVALID_ARG;
    }

    size_t size = index_html_end - index_html_start;
    
    // Validar que el tamaño sea razonable
    if (size == 0 || size > 1024 * 1024) { // Máximo 1MB
        ESP_LOGE(TAG, "Tamaño de HTML inválido: %zu bytes", size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error interno del servidor");
        return ESP_FAIL;
    }

    // Configurar headers de seguridad
    httpd_resp_set_hdr(req, "Cache-Control", HTTP_NO_CACHE);
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "X-XSS-Protection", "1; mode=block");
    httpd_resp_set_type(req, "text/html; charset=utf-8");

    char content_length[32];
    snprintf(content_length, sizeof(content_length), "%zu", size);
    httpd_resp_set_hdr(req, "Content-Length", content_length);

    httpd_resp_send(req, (const char *)index_html_start, size);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    if (req == NULL) {
        ESP_LOGE(TAG, "Solicitud NULL en favicon_handler");
        return ESP_ERR_INVALID_ARG;
    }

    size_t size = favicon_ico_end - favicon_ico_start;
    
    // Validar tamaño del favicon
    if (size == 0 || size > 100 * 1024) { // Máximo 100KB para un favicon
        ESP_LOGE(TAG, "Tamaño de favicon inválido: %zu bytes", size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error interno del servidor");
        return ESP_FAIL;
    }

    // Configurar headers de caché agresivo para recursos estáticos
    httpd_resp_set_hdr(req, "Cache-Control", HTTP_CACHE_MAX_AGE);
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "image/x-icon");
    
    esp_err_t ret = httpd_resp_send(req, (const char *)favicon_ico_start, size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error enviando favicon: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "Favicon servido exitosamente (%zu bytes)", size);
    return ESP_OK;
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Eventos HTTP
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

static void http_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base != ESP_HTTP_SERVER_EVENT) {
        return;
    }

    switch (event_id) {
    case HTTP_SERVER_EVENT_ERROR:
        ESP_LOGE(TAG, "Error en el servidor HTTP");
        break;
        
    case HTTP_SERVER_EVENT_START:
        ESP_LOGI(TAG, "Servidor HTTP iniciado correctamente");
        break;
        
    case HTTP_SERVER_EVENT_ON_CONNECTED:
        if (event_data != NULL) {
            int *fd = (int *)event_data;
            ESP_LOGI(TAG, "Cliente conectado (socket: %d)", *fd);
        }
        break;
        
    case HTTP_SERVER_EVENT_DISCONNECTED:
        if (event_data != NULL) {
            int *fd = (int *)event_data;
            ESP_LOGI(TAG, "Cliente desconectado (socket: %d)", *fd);
        }
        break;
        
    case HTTP_SERVER_EVENT_STOP:
        ESP_LOGI(TAG, "Servidor HTTP detenido");
        break;

    // Eventos de bajo nivel (solo en modo debug)
    case HTTP_SERVER_EVENT_ON_HEADER:
        ESP_LOGV(TAG, "Cabecera HTTP recibida");
        break;
        
    case HTTP_SERVER_EVENT_HEADERS_SENT:
        ESP_LOGV(TAG, "Cabeceras HTTP enviadas");
        break;
        
    case HTTP_SERVER_EVENT_ON_DATA:
        ESP_LOGV(TAG, "Datos HTTP recibidos");
        break;
        
    case HTTP_SERVER_EVENT_SENT_DATA:
        ESP_LOGV(TAG, "Datos HTTP enviados");
        break;

    default:
        ESP_LOGV(TAG, "Evento HTTP no manejado: %ld", event_id);
        break;
    }
}

// ------------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Gestión del servidor
// ------------------------------------------------------------------------------------------------------------------------------------------------------------------

static httpd_handle_t server_handle = NULL;

esp_err_t http_server_init(vehicle_handle_t vehicle)
{
    // Verificar si el servidor ya está iniciado
    if (server_handle != NULL) {
        ESP_LOGW(TAG, "El servidor HTTP ya está en ejecución");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Inicializando servidor HTTP...");

    // Configuración del servidor con valores optimizados
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.core_id = tskNO_AFFINITY;

    // Configurar coincidencia de URI con comodines
    //config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Puerto: %d", config.server_port);
    ESP_LOGI(TAG, "Max URIs: %d, Max sockets: %d", config.max_uri_handlers, config.max_open_sockets);
    ESP_LOGI(TAG, "Stack size: %d bytes", config.stack_size);

    // Registrar manejador de eventos antes de iniciar el servidor
    esp_err_t ret = esp_event_handler_register(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID, &http_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando manejador de eventos: %s", esp_err_to_name(ret));
        return ret;
    }

    // Iniciar el servidor HTTP
    ret = httpd_start(&server_handle, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error iniciando servidor HTTP: %s", esp_err_to_name(ret));
        esp_event_handler_unregister(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID, &http_event_handler);
        return ret;
    }

    // Registrar manejadores de URI
    const httpd_uri_t uri_main_index = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = main_index_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server_handle, &uri_main_index);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando handler para '/': %s", esp_err_to_name(ret));
        http_server_stop();
        return ret;
    }

    const httpd_uri_t uri_favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
        .user_ctx = NULL
    };
    ret = httpd_register_uri_handler(server_handle, &uri_favicon);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando handler para '/favicon.ico': %s", esp_err_to_name(ret));
        http_server_stop();
        return ret;
    }

    // Registrar handlers de WebSocket
    // **************************************************
    // ** REGISTRAR HANDLER WEBSOCKET         **
    // **************************************************
    // Le pasamos el handle del vehículo al módulo WebSocket
    ret = ws_server_register_handlers(server_handle, vehicle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error registrando handler de WebSocket: %s", esp_err_to_name(ret));
        httpd_stop(server_handle); // Detener si falla
        return ret;
    }

    ESP_LOGI(TAG, "Servidor HTTP y WebSocket iniciados correctamente");
    
    return ESP_OK;
}

void http_server_stop(void)
{
    if (server_handle == NULL) {
        ESP_LOGD(TAG, "El servidor HTTP no está en ejecución");
        return;
    }

    ESP_LOGI(TAG, "Deteniendo servidor HTTP...");

    // Detener el servidor HTTP
    esp_err_t ret = httpd_stop(server_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error deteniendo servidor HTTP: %s", esp_err_to_name(ret));
    }

    // Desregistrar el manejador de eventos
    ret = esp_event_handler_unregister(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID, &http_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error desregistrando manejador de eventos: %s", esp_err_to_name(ret));
    }

    // Limpiar el handle
    server_handle = NULL;
    
    ESP_LOGI(TAG, "Servidor HTTP detenido correctamente");
}