#pragma once

#include <stdbool.h>

#include "esp_err.h"

// Inicializa la partición NVS usada por el router y el controlador Wi-Fi.
esp_err_t router_nvs_init(void);

// Recupera las claves "ssid" y "password" del namespace "wifi".
bool router_nvs_load_wifi_credentials(char ssid[33], char password[65]);

// Confirma ambas credenciales en NVS antes de devolver ESP_OK.
esp_err_t router_nvs_save_wifi_credentials(const char *ssid, const char *password);
