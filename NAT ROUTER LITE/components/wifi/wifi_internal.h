#pragma once

#include <stdbool.h>
#include "esp_netif.h"

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

// Interfaces privadas del componente Wi-Fi.
esp_netif_t *router_wifi_ap_start(void);
esp_netif_t *router_wifi_sta_start(bool *credentials_saved);
bool router_wifi_sta_apply_credentials(void);
const char *router_wifi_error_for_reason(unsigned reason);
