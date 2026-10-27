#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

// IPv4 addresses use network byte order. Start after assigning the AP address.
esp_err_t dns_proxy_start(uint32_t listen_addr);
// A zero address disables forwarding until the station obtains a DNS server.
void dns_proxy_set_upstream(uint32_t address);
bool dns_proxy_is_ready(void);
