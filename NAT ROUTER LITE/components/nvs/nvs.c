#include "router_nvs.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define WIFI_NAMESPACE "wifi"

static const char *TAG_NVS = "NVS";

esp_err_t router_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err == ESP_OK)
        {
            err = nvs_flash_init();
        }
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al inicializar el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }
    return err;
}

bool router_nvs_load_wifi_credentials(char ssid[33], char password[65])
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

esp_err_t router_nvs_save_wifi_credentials(const char *ssid, const char *password)
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
