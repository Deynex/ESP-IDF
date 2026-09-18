#include <stdbool.h>

#define SWITCH_GPIO GPIO_NUM_5 // Define el GPIO utilizado para el interruptor
#define ACS712_OFFSET 2.49f     // Offset del sensor ACS712, ajusta según tu calibración
#define ACS712_SCALE 0.100f    // Escala del sensor ACS712, ajusta según tu calibración (100 mV/A para ACS712-20B)
#define NUM_SAMPLES 200        // Número de muestras para promediar la lectura de corriente

void switch_init(void);
void switch_set_state(bool state);
bool switch_get_state(void);
float switch_get_current(void);
float switch_get_current_rms(void);
void switch_start_adc(void);
void switch_stop_adc(void);
void switch_deinit(void);