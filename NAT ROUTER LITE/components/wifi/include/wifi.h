#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct
{
    bool credentials_saved;
    bool connected;
    bool nat_enabled;
    bool dns_ready;
    bool subnet_conflict;
    unsigned wifi_reason;
    const char *connection_error; // Cadena constante; no requiere liberación.
} router_wifi_status_t;

// Inicializa AP+STA, DNS y reconexión. Llamar una vez después de inicializar NVS.
void router_wifi_start(void);
bool router_wifi_is_connected(void);
void router_wifi_get_status(router_wifi_status_t *status);

// Guardar y aplicar son pasos separados para informar los errores HTTP originales.
esp_err_t router_wifi_store_credentials(const char *ssid, const char *password);
esp_err_t router_wifi_apply_credentials(void);
