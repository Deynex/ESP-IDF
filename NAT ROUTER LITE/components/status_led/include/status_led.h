#pragma once

#include <stdbool.h>

// Llamar desde app_main; actualizar cada 500 ms para indicar la conexión.
void status_led_init(void);
void status_led_update(bool connected);
