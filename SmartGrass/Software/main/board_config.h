// ===============================================================================================================================================================
// MARK: Definiciones
// ===============================================================================================================================================================
// I2C
#define I2C_MASTER_SCL 22
#define I2C_MASTER_SDA 21
#define I2C_MASTER_NUM I2C_NUM_0

// Pines Vehículo (A4988)
#define VEHICLE_STEP_PIN GPIO_NUM_23
#define VEHICLE_DIR_LEFT GPIO_NUM_32
#define VEHICLE_DIR_RIGHT GPIO_NUM_19
#define VEHICLE_ENABLE_PIN GPIO_NUM_18

// Pines Cuchilla (Brushless)
#define BLADE_GPIO_PIN GPIO_NUM_25 // La señal ESC

// Configuración Hardware
// Timer 0 para Ruedas (A4988), Timer 1 para Cuchilla (Brushless)
#define VEHICLE_LEDC_TIMER LEDC_TIMER_0
#define VEHICLE_LEDC_CHANNEL LEDC_CHANNEL_0
#define BLADE_LEDC_TIMER LEDC_TIMER_1
#define BLADE_LEDC_CHANNEL LEDC_CHANNEL_1

#define VEHICLE_STEPS_PER_REV 200
#define VEHICLE_MICROSTEPS 2
#define VEHICLE_TOTAL_STEPS (VEHICLE_STEPS_PER_REV * VEHICLE_MICROSTEPS)