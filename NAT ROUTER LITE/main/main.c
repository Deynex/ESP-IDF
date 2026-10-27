#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dns_proxy.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "wifi_form.h"

// Definiciones de NVS
#define WIFI_NAMESPACE "wifi"

// Definiciones WiFi AP
#define WIFI_AP_SSID "ESP32-NAT"
#define WIFI_AP_PASS "12345678"
#define WIFI_AP_MAX_STA_CONN 3
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_IP "192.168.2.1" // También se usa como gateway (Netmask: 255.255.255.0)

// Definiciones WiFi STA
#define WIFI_STA_SSID_DEFAULT "SSID"
#define WIFI_STA_PASS_DEFAULT "PASS"
#define WIFI_STA_MAX_RETRY 2
#define WIFI_STA_RECONNECT_TIMEOUT 3

// Tags para logging
static const char *TAG_TIMER = "TIMER";
static const char *TAG_NVS = "NVS";
static const char *TAG_WIFI = "WIFI";
static const char *TAG_AP = "WIFI_AP";
static const char *TAG_STA = "WIFI_STA";
static const char *TAG_HTTP = "WEB_SERVER";

// Serializa conexión y cambios de credenciales en la tarea de eventos.
ESP_EVENT_DEFINE_BASE(ROUTER_EVENT);
enum { ROUTER_CONNECT, ROUTER_UPDATE_CREDENTIALS };

// Variables globales
static esp_timer_handle_t reconnect_timer = NULL;
static esp_netif_t *esp_netif_ap = NULL;
static esp_netif_t *esp_netif_sta = NULL;
static atomic_bool esp_connected = false;
static atomic_bool credentials_saved = false;
static atomic_bool nat_enabled = false;
static atomic_bool subnet_conflict = false;
static bool credentials_pending = false; // Solo se accede desde la tarea de eventos.
static httpd_handle_t server_handle = NULL;

// Declaración de funciones principales
static void configure_timer(const char *name, esp_timer_handle_t *timer_handle, esp_timer_cb_t reconnect_timer); // Inicia un timer
static void nvs_start(void);                                                                                     // Inicializa el almacenamiento no volátil
static void wifi_start(void);                                                                                    // Inicializa el WiFi
esp_netif_t *wifi_ap_start(void);                                                                                // Inicializa el punto de acceso WiFi
esp_netif_t *wifi_sta_start(void);                                                                               // Inicializa el cliente WiFi
static bool get_wifi_credentials(char ssid[33], char password[65]);
static void configure_http_server(void);                                                                         // Configura el servidor HTTP

// Declaración de funciones de eventos en el WiFi
static void sta_disconnected_event_handler(wifi_event_sta_disconnected_t *event);   // Manejador de eventos de desconexión del cliente WiFi
static bool update_wifi_credentials(void);
static void wifi_reconnect(void);                                                   // Reintenta la conexión WiFi
static void update_dns_upstream(void);
static void router_mark_offline(void);

// Declaración de manejadores del web server
static esp_err_t main_handler(httpd_req_t *req);    // Manejador de la página principal
static esp_err_t favicon_handler(httpd_req_t *req); // Manejador del favicon
static esp_err_t post_handler(httpd_req_t *req);    // Manejador de la petición POST
static esp_err_t saved_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);

// Declaración de funciones del web server
static esp_err_t save_wifi_credentials(const char *ssid, const char *password);

// Paginas web
extern const uint8_t main_html_start[] asm("_binary_main_html_start");
extern const uint8_t main_html_end[] asm("_binary_main_html_end");
extern const uint8_t saved_html_start[] asm("_binary_saved_html_start");
extern const uint8_t saved_html_end[] asm("_binary_saved_html_end");

// Imagenes e iconos
extern const uint8_t router_ico_start[] asm("_binary_router_ico_start");
extern const uint8_t router_ico_end[] asm("_binary_router_ico_end");

// MARK: CALLBACKS ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void reconnect_cb(void *arg)
{
    esp_err_t err = esp_event_post(ROUTER_EVENT, ROUTER_CONNECT, NULL, 0, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TIMER, "Error al programar conexión: %s", esp_err_to_name(err));
        wifi_reconnect();
    }
}

// MARK: EVENTOS -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == ROUTER_EVENT)
    {
        if (event_id == ROUTER_UPDATE_CREDENTIALS)
        {
            router_mark_offline();
            esp_timer_stop(reconnect_timer);
            credentials_pending = true;
            esp_err_t err = esp_wifi_disconnect();
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG_STA, "Desconexión para actualizar credenciales: %s", esp_err_to_name(err));
            }
            if (err == ESP_OK)
            {
                credentials_pending = !update_wifi_credentials();
            }
            wifi_reconnect();
        }
        else if (event_id == ROUTER_CONNECT && !atomic_load(&esp_connected))
        {
            if (credentials_pending)
            {
                esp_err_t err = esp_wifi_disconnect();
                if (err != ESP_OK || !update_wifi_credentials())
                {
                    wifi_reconnect();
                    return;
                }
                credentials_pending = false;
                wifi_reconnect();
                return;
            }
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG_STA, "No se pudo iniciar la conexión: %s; reintentando", esp_err_to_name(err));
                wifi_reconnect();
            }
        }
        return;
    }

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_HOME_CHANNEL_CHANGE:
        {
            wifi_event_home_channel_change_t *event = (wifi_event_home_channel_change_t *)event_data;
            ESP_LOGI(TAG_WIFI, "Cambio de canal. Anterior: %d, Nuevo: %d", event->old_chan, event->new_chan);
            break;
        }
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG_AP, "Punto de acceso WiFi iniciado");
            break;
        case WIFI_EVENT_AP_PROBEREQRECVED:
        {
            wifi_event_ap_probe_req_rx_t *event = (wifi_event_ap_probe_req_rx_t *)event_data;
            ESP_LOGI(TAG_AP, "Solicitud de sondeo recibida. MAC: " MACSTR ", RSSI: %d", MAC2STR(event->mac), event->rssi);
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG_AP, "Cliente conectado al ESP. MAC: " MACSTR ", AID: %d", MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG_AP, "Cliente desconectado del ESP. MAC: " MACSTR ", AID: %d, Razon: %d", MAC2STR(event->mac), event->aid, event->reason);
            break;
        }
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG_AP, "Punto de acceso WiFi detenido");
            break;
        case WIFI_EVENT_STA_START:
            // En APSTA este evento puede llegar antes de terminar esp_wifi_start().
            ESP_LOGI(TAG_STA, "Cliente WiFi iniciado");
            break;
        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG_STA, "Conectado a la red. MAC: " MACSTR ", AID: %d", MAC2STR(event->bssid), event->aid);
            if (!credentials_pending)
            {
                esp_timer_stop(reconnect_timer);
            }
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG_STA, "Desconectado de la red o fallo en conexión. MAC: " MACSTR ", Razon: %d", MAC2STR(event->bssid), event->reason);
            router_mark_offline();

            sta_disconnected_event_handler(event);
            break;
        }
        case WIFI_EVENT_STA_BEACON_TIMEOUT:
            ESP_LOGW(TAG_STA, "Tiempo de espera de beacon agotado");
            break;
        case WIFI_EVENT_STA_STOP:
            router_mark_offline();
            esp_timer_stop(reconnect_timer);
            ESP_LOGI(TAG_STA, "Cliente WiFi detenido");
            break;
        default:
            ESP_LOGW(TAG_WIFI, "Evento no manejado en WIFI: %ld", event_id);
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            if (credentials_pending)
            {
                wifi_reconnect();
                break;
            }
            ESP_LOGI(TAG_STA, "Dirección IP asignada. IP: " IPSTR ", Máscara: " IPSTR ", Gateway: " IPSTR, IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));
            atomic_store(&esp_connected, true);
            esp_timer_stop(reconnect_timer);

            esp_netif_ip_info_t ap_ip;
            ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif_ap, &ap_ip));
            bool overlap = (event->ip_info.ip.addr & ap_ip.netmask.addr) == (ap_ip.ip.addr & ap_ip.netmask.addr)
                || (event->ip_info.ip.addr & event->ip_info.netmask.addr) == (ap_ip.ip.addr & event->ip_info.netmask.addr);
            atomic_store(&subnet_conflict, overlap);
            if (overlap)
            {
                ESP_LOGE(TAG_WIFI, "Las subredes STA y AP se superponen; cambie WIFI_AP_IP antes de usar NAT");
                atomic_store(&nat_enabled, false);
                dns_proxy_set_upstream(0);
#if CONFIG_LWIP_IPV4_NAPT
                ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_napt_disable(esp_netif_ap));
#endif
                break;
            }

            esp_err_t err = esp_netif_set_default_netif(esp_netif_sta);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG_WIFI, "Error al establecer la interfaz de red por defecto. Error %s", esp_err_to_name(err));
            }

#if CONFIG_LWIP_IPV4_NAPT
            err = esp_netif_napt_enable(esp_netif_ap);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG_WIFI, "Error al habilitar el NAT en la interfaz de red del punto de acceso. Error %s", esp_err_to_name(err));
                atomic_store(&nat_enabled, false);
            }
            else
            {
                ESP_LOGI(TAG_WIFI, "NAPT habilitado en el punto de acceso");
                atomic_store(&nat_enabled, true);
            }
#else
            ESP_LOGW(TAG_WIFI, "El soporte NAPT no está habilitado en la configuración de lwIP");
#endif

            update_dns_upstream();
            break;
        }
        case IP_EVENT_STA_LOST_IP:
            router_mark_offline();
            ESP_LOGW(TAG_STA, "Dirección IP perdida");
            break;
        case IP_EVENT_ASSIGNED_IP_TO_CLIENT:
        {
            ip_event_assigned_ip_to_client_t *event = (ip_event_assigned_ip_to_client_t *)event_data;
            ESP_LOGI(TAG_AP, "Dirección IP asignada al cliente. IP: " IPSTR ", MAC: " MACSTR, IP2STR(&event->ip), MAC2STR(event->mac));
            break;
        }
        case IP_EVENT_GOT_IP6:
            ESP_LOGI(TAG_STA, "Dirección IPv6 asignada (ignorada para NAT IPv4)");
            break;
        case IP_EVENT_NETIF_UP:
            ESP_LOGI(TAG_WIFI, "Interfaz de red levantada (Netif Up)");
            break;
        case IP_EVENT_NETIF_DOWN:
            ESP_LOGI(TAG_WIFI, "Interfaz de red bajada (Netif Down)");
            break;
        default:
            ESP_LOGW(TAG_WIFI, "Evento no manejado en IP: %ld", event_id);
            break;
        }
    }
}

static void sta_disconnected_event_handler(wifi_event_sta_disconnected_t *event)
{
    switch (event->reason)
    {
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        ESP_LOGW(TAG_STA, "La red no cumple el umbral WPA2/WPA3 configurado");
        wifi_reconnect();
        break;
    case WIFI_REASON_NO_AP_FOUND:
        ESP_LOGW(TAG_STA, "Punto de acceso no encontrado");
        wifi_reconnect();
        break;
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        ESP_LOGW(TAG_STA, "Tiempo de espera del handshake de 4 vías agotado");
        wifi_reconnect();
        break;
    case WIFI_REASON_CONNECTION_FAIL:
        ESP_LOGW(TAG_STA, "Fallo en la conexión");
        wifi_reconnect();
        break;
    case WIFI_REASON_STA_LEAVING:
        ESP_LOGI(TAG_STA, "Desconexión local, programando reconexión...");
        wifi_reconnect();
        break;
    case WIFI_REASON_AUTH_EXPIRE:
        ESP_LOGW(TAG_STA, "La autenticación ha expirado");
        wifi_reconnect();
        break;
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
        ESP_LOGW(TAG_STA, "Versión de RSN IE no soportada");
        wifi_reconnect();
        break;
    case WIFI_REASON_BEACON_TIMEOUT:
        ESP_LOGW(TAG_STA, "Tiempo de espera de beacon agotado");
        wifi_reconnect();
        break;
    case WIFI_REASON_ASSOC_LEAVE:
        ESP_LOGW(TAG_STA, "El cliente ha dejado la red");
        wifi_reconnect();
        break;
    case WIFI_REASON_AUTH_FAIL:
        ESP_LOGW(TAG_STA, "Fallo en la autenticación");
        wifi_reconnect();
        break;
    default:
        ESP_LOGW(TAG_STA, "Razón de desconexión no manejada: %d", event->reason);
        wifi_reconnect();
        break;
    }
}

static bool update_wifi_credentials(void)
{
    wifi_config_t wifi_config = {0};
    char ssid[33] = {0};
    char password[65] = {0};
    if (get_wifi_credentials(ssid, password))
    {
        esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG_STA, "No se pudo leer la configuración WiFi: %s", esp_err_to_name(err));
            return false;
        }
        memset(wifi_config.sta.ssid, 0, sizeof(wifi_config.sta.ssid));
        memset(wifi_config.sta.password, 0, sizeof(wifi_config.sta.password));
        memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
        memcpy(wifi_config.sta.password, password, strlen(password));
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG_STA, "No se pudieron aplicar las credenciales: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    return false;
}

static void wifi_reconnect(void)
{
    ESP_LOGI(TAG_WIFI, "Reconectando al WiFi...");

    // Detener el timer si ya está corriendo
    esp_timer_stop(reconnect_timer);

    esp_err_t err = esp_timer_start_once(reconnect_timer, WIFI_STA_RECONNECT_TIMEOUT * 1000000);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TIMER, "Error al iniciar el timer. Error %s", esp_err_to_name(err));
    }
}

static void router_mark_offline(void)
{
    atomic_store(&esp_connected, false);
    atomic_store(&nat_enabled, false);
    atomic_store(&subnet_conflict, false);
    dns_proxy_set_upstream(0);
#if CONFIG_LWIP_IPV4_NAPT
    if (esp_netif_ap)
    {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_napt_disable(esp_netif_ap));
    }
#endif
}

static void update_dns_upstream(void)
{
    // El DNS anunciado por DHCP siempre es WIFI_AP_IP; solo cambia su destino.
    const esp_netif_dns_type_t types[] = {ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK};
    const uint32_t ap_addr = ipaddr_addr(WIFI_AP_IP);
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
    {
        esp_netif_dns_info_t dns = {0};
        if (esp_netif_get_dns_info(esp_netif_sta, types[i], &dns) == ESP_OK
            && dns.ip.type == ESP_IPADDR_TYPE_V4)
        {
            uint32_t address = dns.ip.u_addr.ip4.addr;
            uint32_t host = ntohl(address);
            if (address != ap_addr && (host >> 24) != 0 && host < 0xe0000000U && (host >> 24) != 127)
            {
                dns_proxy_set_upstream(address);
                ESP_LOGI(TAG_WIFI, "DNS del módem para los clientes: " IPSTR, IP2STR(&dns.ip.u_addr.ip4));
                return;
            }
        }
    }
    dns_proxy_set_upstream(ipaddr_addr("1.1.1.1"));
    ESP_LOGW(TAG_WIFI, "El módem no proporcionó DNS IPv4 válido; usando 1.1.1.1");
}

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

// MARK: MAIN ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void app_main(void)
{
    status_led_init();

#if !CONFIG_LWIP_IPV4_NAPT
    ESP_LOGW(TAG_WIFI, "Active LWIP_IP_FORWARD y LWIP_IPV4_NAPT en menuconfig para compartir Internet");
#endif

    // Inicia el timer
    configure_timer("reconnect_timer", &reconnect_timer, reconnect_cb);

    // Inicia el almacenamiento no volátil
    nvs_start();

    // Inicia el WiFi
    wifi_start();

    // Inicia el servidor HTTP
    configure_http_server();

    // Bucle principal
    while (true)
    {
        status_led_update(atomic_load(&esp_connected));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// MARK: FUNCIONES -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static void configure_timer(const char *name, esp_timer_handle_t *timer_handle, esp_timer_cb_t reconnect_timer)
{
    esp_timer_create_args_t config = {
        .callback = reconnect_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = name,
    };

    ESP_ERROR_CHECK(esp_timer_create(&config, timer_handle));
}

static void nvs_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al inicializar el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(err);
}

static void wifi_start(void)
{
    // Inicializa el stack de red
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Registra el manejador de eventos de WiFi
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(ROUTER_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    // Inicializa el controlador WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configura el modo WiFi en modo AP-STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Inicia el punto de acceso WiFi
    esp_netif_ap = wifi_ap_start();

    // Inicia el cliente STA
    esp_netif_sta = wifi_sta_start();

    // El listener existe antes de que el primer cliente pueda obtener un lease.
    ESP_ERROR_CHECK(dns_proxy_start(ipaddr_addr(WIFI_AP_IP)));

    // Inicia el controlador WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    // No conectar en STA_START: esp_wifi_start() debe haber terminado en APSTA.
    ESP_ERROR_CHECK(esp_event_post(ROUTER_EVENT, ROUTER_CONNECT, NULL, 0, portMAX_DELAY));
}

esp_netif_t *wifi_ap_start(void)
{
    // Crea una interfaz de red por defecto para el modo AP
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    // Configura la dirección IP del punto de acceso
    esp_netif_ip_info_t ip_info = {
        .ip = {ipaddr_addr(WIFI_AP_IP)},
        .gw = {ipaddr_addr(WIFI_AP_IP)},
        .netmask = {ipaddr_addr("255.255.255.0")},
    };

    // Detiene y reinicia el servidor DHCP
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ip_info));
    esp_netif_dns_info_t dns = {
        .ip = {
            .u_addr.ip4 = ip_info.ip,
            .type = ESP_IPADDR_TYPE_V4,
        },
    };
    uint8_t dhcps_offer_dns = 0x02;
    ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET,
                                         ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_offer_dns, sizeof(dhcps_offer_dns)));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));
    ESP_LOGI(TAG_AP, "DHCP anuncia DNS " IPSTR " desde el arranque", IP2STR(&ip_info.ip));

    // Configura el punto de acceso WiFi
    wifi_config_t wifi_config = {
        .ap =
            {
                .ssid = WIFI_AP_SSID,
                .ssid_len = strlen(WIFI_AP_SSID),
                .password = WIFI_AP_PASS,
                .max_connection = WIFI_AP_MAX_STA_CONN,
                .channel = WIFI_AP_CHANNEL,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
                .authmode = WIFI_AUTH_WPA3_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else
                .authmode = WIFI_AUTH_WPA2_PSK,
#endif
#if CONFIG_NAT_ROUTER_FTM_RESPONDER
                .ftm_responder = true,
#endif
                .pmf_cfg =
                    {
                        .required = 1,
                    },
            },
    };

    if (strlen(WIFI_AP_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI(TAG_AP, "WiFi AP configurado. SSID: %s, CANAL inicial: %d", WIFI_AP_SSID, WIFI_AP_CHANNEL);
#if CONFIG_NAT_ROUTER_FTM_RESPONDER
    ESP_LOGI(TAG_AP, "FTM responder habilitado: disponible para clientes WiFi RTT compatibles");
#endif

    return esp_netif_ap;
}

esp_netif_t *wifi_sta_start(void)
{
    // Crea una interfaz de red por defecto para el modo STA
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    // Configura el cliente WiFi
    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = "",
                .password = "",
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
                .failure_retry_cnt = WIFI_STA_MAX_RETRY,
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            },
    };

    char ssid[33] = {0};
    char password[65] = {0};

    // Si no se encuentran credenciales, se usan las predeterminadas
    if (!get_wifi_credentials(ssid, password))
    {
        strncpy((char *)wifi_config.sta.ssid, WIFI_STA_SSID_DEFAULT, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, WIFI_STA_PASS_DEFAULT, sizeof(wifi_config.sta.password) - 1);
    }
    else
    {
        atomic_store(&credentials_saved, true);
        memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
        memcpy(wifi_config.sta.password, password, strlen(password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG_STA, "WiFi STA configurado. SSID: %.*s", (int)sizeof(wifi_config.sta.ssid), wifi_config.sta.ssid);

    return esp_netif_sta;
}

static bool get_wifi_credentials(char ssid[33], char password[65])
{
    nvs_handle_t nvs_handle_wifi;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &nvs_handle_wifi);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG_NVS, "Credenciales no disponibles: %s", esp_err_to_name(err));
        return false;
    }

    size_t ssid_len = 33;
    size_t password_len = 65;
    err = nvs_get_str(nvs_handle_wifi, "ssid", ssid, &ssid_len);
    if (err == ESP_OK)
    {
        err = nvs_get_str(nvs_handle_wifi, "password", password, &password_len);
    }
    nvs_close(nvs_handle_wifi);
    if (err != ESP_OK || ssid_len < 2 || password_len < 9)
    {
        ESP_LOGW(TAG_NVS, "No hay credenciales WiFi válidas guardadas");
        ssid[0] = '\0';
        password[0] = '\0';
        return false;
    }
    return true;
}

// MARK: HTTP SERVER -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static void configure_http_server(void)
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
    if (!atomic_load(&credentials_saved))
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
    char response[192];
    snprintf(response, sizeof(response),
             "{\"credentials_saved\":%s,\"connected\":%s,\"nat_enabled\":%s,\"dns_ready\":%s,\"subnet_conflict\":%s}",
             atomic_load(&credentials_saved) ? "true" : "false",
             atomic_load(&esp_connected) ? "true" : "false",
             atomic_load(&nat_enabled) ? "true" : "false",
             dns_proxy_is_ready() ? "true" : "false",
             atomic_load(&subnet_conflict) ? "true" : "false");
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

    if (save_wifi_credentials(ssid, password) != ESP_OK)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No se pudieron guardar las credenciales");
    }
    atomic_store(&credentials_saved, true);
    esp_err_t err = esp_event_post(ROUTER_EVENT, ROUTER_UPDATE_CREDENTIALS, NULL, 0, pdMS_TO_TICKS(100));
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

static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle_wifi;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle_wifi);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al abrir el almacenamiento no volátil. Error %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle_wifi, "ssid", ssid);
    if (err == ESP_OK)
    {
        err = nvs_set_str(nvs_handle_wifi, "password", password);
    }
    if (err == ESP_OK)
    {
        err = nvs_commit(nvs_handle_wifi);
    }
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al confirmar los cambios en el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle_wifi);
    return err;
}
