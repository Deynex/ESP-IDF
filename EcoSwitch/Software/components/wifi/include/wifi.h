// Definiciones de NVS
#define WIFI_NAMESPACE "wifi"

// Definiciones WiFi AP
#define WIFI_AP_SSID "EcoSwitch"
#define WIFI_AP_PASS "12345678"
#define WIFI_AP_MAX_STA_CONN 3
#define WIFI_AP_CHANNEL 6
#define WIFI_AP_IP "192.168.2.1" // También se usa como gateway
#define WIFI_AP_NETMASK "255.255.255.0"

void nvs_init(void);
void wifi_init(void);