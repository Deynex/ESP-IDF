#include "server.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "wifi.h"
#include "wifi_form.h"

static const char *TAG_HTTP = "WEB_SERVER";
static httpd_handle_t server_handle = NULL;

static esp_err_t main_handler(httpd_req_t *req);
static esp_err_t favicon_handler(httpd_req_t *req);
static esp_err_t post_handler(httpd_req_t *req);
static esp_err_t saved_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);

extern const uint8_t main_html_start[] asm("_binary_main_html_start");
extern const uint8_t main_html_end[] asm("_binary_main_html_end");
extern const uint8_t saved_html_start[] asm("_binary_saved_html_start");
extern const uint8_t saved_html_end[] asm("_binary_saved_html_end");
extern const uint8_t router_ico_start[] asm("_binary_router_ico_start");
extern const uint8_t router_ico_end[] asm("_binary_router_ico_end");

static void http_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // httpd_handle_t server = (httpd_handle_t)arg;

    if (event_base == ESP_HTTP_SERVER_EVENT)
    {
        switch (event_id)
        {
        case HTTP_SERVER_EVENT_ERROR:
            ESP_LOGE(TAG_HTTP, "Error en el servidor web");
            break;
        case HTTP_SERVER_EVENT_START:
            ESP_LOGI(TAG_HTTP, "Servidor web iniciado");
            break;
        case HTTP_SERVER_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG_HTTP, "Cliente conectado al servidor web");
            break;
        case HTTP_SERVER_EVENT_ON_HEADER:
            ESP_LOGI(TAG_HTTP, "Cabecera recibida");
            break;
        case HTTP_SERVER_EVENT_HEADERS_SENT:
            ESP_LOGI(TAG_HTTP, "Cabeceras enviadas");
            break;
        case HTTP_SERVER_EVENT_ON_DATA:
            ESP_LOGI(TAG_HTTP, "Datos recibidos");
            break;
        case HTTP_SERVER_EVENT_SENT_DATA:
            ESP_LOGI(TAG_HTTP, "Datos enviados");
            break;
        case HTTP_SERVER_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_HTTP, "Cliente desconectado del servidor web");
            break;
        case HTTP_SERVER_EVENT_STOP:
            ESP_LOGW(TAG_HTTP, "Servidor web detenido");
            break;
        default:
            ESP_LOGW(TAG_HTTP, "Evento no manejado: %ld", event_id);
            break;
        }
    }
}

void router_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    // HTTP usa tres sockets internos; quedan dos para el proxy DNS.
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;

    ESP_LOGI(TAG_HTTP, "Iniciando servidor web en puerto: %d", config.server_port);

    // Registra el manejador de eventos del servidor HTTP
    esp_event_handler_register(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID, &http_event_handler, server_handle);

    // Inicia el servidor HTTP con las URL y manejadores de eventos
    if (httpd_start(&server_handle, &config) == ESP_OK)
    {
        // Pagina principal
        httpd_uri_t uri_main = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = main_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server_handle, &uri_main);

        // Favicon
        httpd_uri_t uri_favicon = {
            .uri = "/favicon.ico",
            .method = HTTP_GET,
            .handler = favicon_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server_handle, &uri_favicon);

        httpd_uri_t uri_post = {
            .uri = "/",
            .method = HTTP_POST,
            .handler = post_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server_handle, &uri_post);

        const httpd_uri_t uri_saved = {
            .uri = "/saved",
            .method = HTTP_GET,
            .handler = saved_handler,
        };
        ESP_ERROR_CHECK(httpd_register_uri_handler(server_handle, &uri_saved));
        const httpd_uri_t uri_status = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = status_handler,
        };
        ESP_ERROR_CHECK(httpd_register_uri_handler(server_handle, &uri_status));
    }
    else
    {
        ESP_LOGE(TAG_HTTP, "Error al iniciar el servidor web");
    }
}

static esp_err_t main_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");         // No almacenar en caché
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff"); // No permitir la interpretación de MIME
    httpd_resp_set_type(req, "text/html; charset=utf-8");         // Tipo de contenido
    return httpd_resp_send(req, (const char *)main_html_start, main_html_end - main_html_start - 1);
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=31536000, immutable"); // Almacenar en caché por un año, no modificar
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)router_ico_start, router_ico_end - router_ico_start);
    return ESP_OK;
}

static esp_err_t saved_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    router_wifi_status_t status;
    router_wifi_get_status(&status);
    if (!status.credentials_saved)
    {
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, NULL, 0);
    }
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)saved_html_start, saved_html_end - saved_html_start - 1);
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char response[320];
    router_wifi_status_t status;
    router_wifi_get_status(&status);
    snprintf(response, sizeof(response),
             "{\"credentials_saved\":%s,\"connected\":%s,\"nat_enabled\":%s,\"dns_ready\":%s,\"subnet_conflict\":%s,\"wifi_reason\":%u,\"connection_error\":\"%s\"}",
             status.credentials_saved ? "true" : "false",
             status.connected ? "true" : "false",
             status.nat_enabled ? "true" : "false",
             status.dns_ready ? "true" : "false",
             status.subnet_conflict ? "true" : "false",
             status.wifi_reason, status.connection_error);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, response);
}

static esp_err_t post_handler(httpd_req_t *req)
{
    // 32 + 64 bytes pueden ocupar 3 veces su tamaño al codificarse como %xx.
    char buf[320] = {0};
    if (req->content_len == 0 || req->content_len >= sizeof(buf))
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Formulario vacío o demasiado largo");
    }
    size_t received = 0;
    while (received < req->content_len)
    {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        received += ret;
    }
    char ssid[33] = {0};
    char password[65] = {0};
    if (memchr(buf, '\0', received) || !wifi_form_parse(buf, ssid, password))
    {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID o clave WPA inválidos");
    }

    if (router_wifi_store_credentials(ssid, password) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudieron guardar las credenciales");
    }
    esp_err_t err = router_wifi_apply_credentials();
    if (err != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Credenciales guardadas; reinicie el equipo para aplicarlas");
    }
    ESP_LOGI(TAG_HTTP, "Nuevas credenciales guardadas. Reiniciando conexión STA...");
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/saved");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

