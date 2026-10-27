#include "wifi_internal.h"

#include <string.h>
#include "router_nvs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG_STA = "WIFI_STA";

esp_netif_t *router_wifi_sta_start(bool *credentials_saved)
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
    *credentials_saved = router_nvs_load_wifi_credentials(ssid, password);
    if (!*credentials_saved)
    {
        strncpy((char *)wifi_config.sta.ssid, WIFI_STA_SSID_DEFAULT, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, WIFI_STA_PASS_DEFAULT, sizeof(wifi_config.sta.password) - 1);
    }
    else
    {
        memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
        memcpy(wifi_config.sta.password, password, strlen(password));
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG_STA, "WiFi STA configurado. SSID: %.*s", (int)sizeof(wifi_config.sta.ssid), wifi_config.sta.ssid);

    return esp_netif_sta;
}

bool router_wifi_sta_apply_credentials(void)
{
    wifi_config_t wifi_config = {0};
    char ssid[33] = {0};
    char password[65] = {0};
    if (router_nvs_load_wifi_credentials(ssid, password))
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

const char *router_wifi_error_for_reason(unsigned reason)
{
    switch (reason)
    {
    case 0:
        return "none";
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "handshake_timeout";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_802_1X_AUTH_FAILED:
    case WIFI_REASON_MIC_FAILURE:
    case WIFI_REASON_INVALID_PMKID:
    case WIFI_REASON_ASSOC_NOT_AUTHED:
        return "authentication_failed";
    case WIFI_REASON_AUTH_EXPIRE:
        return "authentication_timeout";
    case WIFI_REASON_NO_AP_FOUND:
        return "network_not_found";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_GROUP_CIPHER_INVALID:
    case WIFI_REASON_PAIRWISE_CIPHER_INVALID:
    case WIFI_REASON_AKMP_INVALID:
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
    case WIFI_REASON_INVALID_RSN_IE_CAP:
    case WIFI_REASON_CIPHER_SUITE_REJECTED:
    case WIFI_REASON_BAD_CIPHER_OR_AKM:
        return "security_mismatch";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "signal_too_weak";
    case WIFI_REASON_ASSOC_FAIL:
        return "association_failed";
    case WIFI_REASON_ASSOC_TOOMANY:
        return "modem_busy";
    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_AUTH_LEAVE:
    case WIFI_REASON_DISASSOC_DUE_TO_INACTIVITY:
    case WIFI_REASON_ASSOC_LEAVE:
    case WIFI_REASON_STA_LEAVING:
        return "connection_lost";
    default:
        return "connection_failed";
    }
}
