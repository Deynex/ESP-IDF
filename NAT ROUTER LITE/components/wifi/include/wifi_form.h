#pragma once

#include <stdbool.h>

/* Decode one complete URL-encoded form. Outputs are unchanged on failure.
 * SSID: 1..32 bytes; WPA passphrase: 8..63 bytes, or a 64-digit hex PSK.
 * Both fields must occur exactly once. Unknown fields are ignored.
 */
bool wifi_form_parse(const char *form, char ssid[33], char password[65]);
