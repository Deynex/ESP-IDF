#include "wifi.h"
#include "wifi_internal.h"

#include <stdatomic.h>
#include <stddef.h>
#include "dns_proxy.h"
#include "router_nvs.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "lwip/inet.h"

static const char *TAG_TIMER = "TIMER";
static const char *TAG_WIFI = "WIFI";
static const char *TAG_AP = "WIFI_AP";
static const char *TAG_STA = "WIFI_STA";

// Conexión y cambios de credenciales se serializan en la tarea de eventos.
ESP_EVENT_DEFINE_BASE(ROUTER_EVENT);
enum { ROUTER_CONNECT, ROUTER_UPDATE_CREDENTIALS };

static esp_timer_handle_t reconnect_timer = NULL;
static esp_netif_t *esp_netif_ap = NULL;
static esp_netif_t *esp_netif_sta = NULL;
static atomic_bool esp_connected = false;
static atomic_bool credentials_saved = false;
static atomic_bool nat_enabled = false;
static atomic_bool subnet_conflict = false;
static atomic_uint wifi_disconnect_reason = 0;
static bool credentials_pending = false;
static bool credentials_disconnect_expected = false;

static void configure_timer(const char *name, esp_timer_handle_t *timer_handle, esp_timer_cb_t callback);
static void sta_disconnected_event_handler(wifi_event_sta_disconnected_t *event);
static void wifi_reconnect(void);
static void update_dns_upstream(void);
static void router_mark_offline(void);

static void reconnect_cb(void *arg)
{
    esp_err_t err = esp_event_post(ROUTER_EVENT, ROUTER_CONNECT, NULL, 0, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TIMER, "Error al programar conexión: %s", esp_err_to_name(err));
        wifi_reconnect();
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == ROUTER_EVENT)
    {
        if (event_id == ROUTER_UPDATE_CREDENTIALS)
        {
            atomic_store(&wifi_disconnect_reason, 0);
            router_mark_offline();
            esp_timer_stop(reconnect_timer);
            credentials_pending = true;
            credentials_disconnect_expected = true;
            esp_err_t err = esp_wifi_disconnect();
            if (err != ESP_OK)
            {
                credentials_disconnect_expected = false;
                ESP_LOGW(TAG_STA, "Desconexión para actualizar credenciales: %s", esp_err_to_name(err));
            }
            if (err == ESP_OK)
            {
                credentials_pending = !router_wifi_sta_apply_credentials();
            }
            wifi_reconnect();
        }
        else if (event_id == ROUTER_CONNECT && !atomic_load(&esp_connected))
        {
            if (credentials_pending)
            {
                credentials_disconnect_expected = true;
                esp_err_t err = esp_wifi_disconnect();
                if (err != ESP_OK)
                {
                    credentials_disconnect_expected = false;
                }
                if (err != ESP_OK || !router_wifi_sta_apply_credentials())
                {
                    wifi_reconnect();
                    return;
                }
                credentials_pending = false;
                wifi_reconnect();
                return;
            }
            // El siguiente fallo ya pertenece al intento con la configuración actual.
            credentials_disconnect_expected = false;
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
            atomic_store(&wifi_disconnect_reason, 0);
            credentials_disconnect_expected = false;
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
            // Cambiar credenciales provoca una desconexión local, no un rechazo de clave.
            bool local_update = credentials_disconnect_expected &&
                (event->reason == WIFI_REASON_STA_LEAVING || event->reason == WIFI_REASON_ASSOC_LEAVE);
            credentials_disconnect_expected = false;
            if (!local_update)
            {
                atomic_store(&wifi_disconnect_reason, event->reason);
            }
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
            atomic_store(&wifi_disconnect_reason, 0);
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

void router_wifi_start(void)
{
#if !CONFIG_LWIP_IPV4_NAPT
    ESP_LOGW(TAG_WIFI, "Active LWIP_IP_FORWARD y LWIP_IPV4_NAPT en menuconfig para compartir Internet");
#endif
    configure_timer("reconnect_timer", &reconnect_timer, reconnect_cb);

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
    esp_netif_ap = router_wifi_ap_start();

    // Inicia el cliente STA
    bool saved = false;
    esp_netif_sta = router_wifi_sta_start(&saved);
    atomic_store(&credentials_saved, saved);

    // El listener existe antes de que el primer cliente pueda obtener un lease.
    ESP_ERROR_CHECK(dns_proxy_start(ipaddr_addr(WIFI_AP_IP)));

    // Inicia el controlador WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
    // No conectar en STA_START: esp_wifi_start() debe haber terminado en APSTA.
    ESP_ERROR_CHECK(esp_event_post(ROUTER_EVENT, ROUTER_CONNECT, NULL, 0, portMAX_DELAY));
}

bool router_wifi_is_connected(void)
{
    return atomic_load(&esp_connected);
}

void router_wifi_get_status(router_wifi_status_t *status)
{
    if (status == NULL)
    {
        return;
    }
    unsigned reason = atomic_load(&wifi_disconnect_reason);
    *status = (router_wifi_status_t){
        .credentials_saved = atomic_load(&credentials_saved),
        .connected = atomic_load(&esp_connected),
        .nat_enabled = atomic_load(&nat_enabled),
        .dns_ready = dns_proxy_is_ready(),
        .subnet_conflict = atomic_load(&subnet_conflict),
        .wifi_reason = reason,
        .connection_error = router_wifi_error_for_reason(reason),
    };
}

esp_err_t router_wifi_store_credentials(const char *ssid, const char *password)
{
    esp_err_t err = router_nvs_save_wifi_credentials(ssid, password);
    if (err == ESP_OK)
    {
        atomic_store(&credentials_saved, true);
        atomic_store(&wifi_disconnect_reason, 0);
    }
    return err;
}

esp_err_t router_wifi_apply_credentials(void)
{
    return esp_event_post(ROUTER_EVENT, ROUTER_UPDATE_CREDENTIALS, NULL, 0, pdMS_TO_TICKS(100));
}
