idf_component_register(
    SRCS "wifi.c" "ap.c" "sta.c" "wifi_form.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_common
    PRIV_REQUIRES esp_event esp_wifi nvs esp_netif esp_timer lwip dns_proxy
)
