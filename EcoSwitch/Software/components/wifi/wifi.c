#include "wifi.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include <stdio.h>

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Eventos
// -----------------------------------------------------------------------------------------------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_HOME_CHANNEL_CHANGE:
        {
            wifi_event_home_channel_change_t *event = (wifi_event_home_channel_change_t *)event_data;
            ESP_LOGI("WIFI", "Cambio de canal. Anterior: %d, Nuevo: %d", event->old_chan, event->new_chan);
            break;
        }
        case WIFI_EVENT_AP_START:
            ESP_LOGI("WIFI", "Punto de acceso WiFi iniciado");
            break;
        case WIFI_EVENT_AP_PROBEREQRECVED:
        {
            wifi_event_ap_probe_req_rx_t *event = (wifi_event_ap_probe_req_rx_t *)event_data;
            ESP_LOGI("WIFI", "Solicitud de sondeo recibida. MAC: " MACSTR ", RSSI: %d", MAC2STR(event->mac), event->rssi);
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI("WIFI", "Cliente conectado al ESP. MAC: " MACSTR ", AID: %d", MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI("WIFI", "Cliente desconectado del ESP. MAC: " MACSTR ", AID: %d, Razon: %d", MAC2STR(event->mac), event->aid, event->reason);
            break;
        }
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI("WIFI", "Punto de acceso WiFi detenido");
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_ASSIGNED_IP_TO_CLIENT:
        {
            ip_event_assigned_ip_to_client_t *event = (ip_event_assigned_ip_to_client_t *)event_data;
            ESP_LOGI("AP", "Dirección IP asignada al cliente. IP: " IPSTR ", MAC: " MACSTR, IP2STR(&event->ip), MAC2STR(event->mac));
            break;
        }
        default:
            ESP_LOGW("WIFI", "Evento no manejado en IP: %ld", event_id);
            break;
        }
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------------------------------------
// MARK: Funciones
// -----------------------------------------------------------------------------------------------------------------------------------------------------------------
void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // Si no hay espacio libre o hay una nueva versión, reinicializa NVS
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Error al inicializar el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(err);
}

esp_netif_t *wifi_ap_start(void)
{
    // Crea una interfaz de red por defecto para el modo AP
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    // Configura la dirección IP del punto de acceso
    esp_netif_ip_info_t ip_info = {
        .ip = {ipaddr_addr(WIFI_AP_IP)},
        .gw = {ipaddr_addr(WIFI_AP_IP)},
        .netmask = {ipaddr_addr(WIFI_AP_NETMASK)},
    };

    // Detiene y reinicia el servidor DHCP
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));

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
                .ftm_responder = false,
                .pmf_cfg =
                    {
                        .required = true,
                    },
            },
    };

    if (strlen(WIFI_AP_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI("AP", "WiFi AP iniciado. SSID: %s, PASS: %s, CANAL: %d", WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL);

    return esp_netif_ap;
}

static esp_netif_t *esp_netif_ap = NULL;

void wifi_init(void)
{
    // Inicializa el stack de red
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Registra el manejador de eventos de WiFi
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    // Inicializa el controlador WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configura el modo WiFi en modo AP-STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    // Inicia el punto de acceso WiFi
    esp_netif_ap = wifi_ap_start();

    // Inicia el controlador WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
}