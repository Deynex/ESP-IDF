#include "wifi_internal.h"

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "lwip/inet.h"

static const char *TAG_AP = "WIFI_AP";

esp_netif_t *router_wifi_ap_start(void)
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
