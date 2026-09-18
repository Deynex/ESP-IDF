/**
 * @file mks_servo42c.h
 * @brief API para control del servo motor MKS SERVO42C con encoder absoluto
 * @author DEYNEX
 */

#ifndef MKS_SERVO42C_H
#define MKS_SERVO42C_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Estructura de datos del encoder absoluto del servo MKS SERVO42C
 *
 * Esta estructura contiene toda la información de posición del encoder,
 * incluyendo vueltas completas y posición angular precisa.
 */
typedef struct
{
    int32_t carry;     /**< Contador de vueltas completas (puede ser negativo) */
    uint16_t value;    /**< Valor crudo del encoder (rango: 0-65535) */
    float angle_deg;   /**< Ángulo actual dentro de la vuelta (0-360°) */
    float total_angle; /**< Ángulo acumulado total = (carry * 360°) + angle_deg */
} mks_encoder_data_t;

/**
 * @brief Estructura de estado actual del servo
 *
 * Contiene la configuración y estado operativo del motor.
 */
typedef struct
{
    uint8_t mstep;    /**< Configuración de microstepping (1, 2, 4, 8, 16, 32, etc.) */
    uint16_t speed;   /**< Velocidad actual en RPM */
    uint16_t current; /**< Corriente configurada en mA */
    bool enable;      /**< Estado del motor: true = habilitado, false = deshabilitado */
} mks_status_t;

/**
 * @brief Modos de operación del pin ENABLE
 *
 * Define cómo responde el motor a la señal del pin ENABLE.
 */
typedef enum
{
    MKS_ENABLE_ACTIVE_LOW = 0x00,   /**< Nivel bajo (0V) activa el motor */
    MKS_ENABLE_ACTIVE_HIGH = 0x01,  /**< Nivel alto (3.3V/5V) activa el motor */
    MKS_ENABLE_ACTIVE_ALWAYS = 0x02 /**< Motor siempre activo, ignora el pin ENABLE */
} mks_enable_mode_t;

/**
 * @brief Estados de habilitación del motor
 *
 * Controla si el motor está energizado y puede moverse.
 */
typedef enum
{
    MKS_DISABLE = 0x00, /**< Desactiva el motor (sin torque, libre para girar) */
    MKS_ENABLE = 0x01   /**< Activa el motor (con torque, mantiene posición) */
} mks_state_t;

/**
 * @brief Valores de corriente máxima disponibles
 *
 * Define los límites de corriente configurables para el motor.
 * Mayor corriente = mayor torque, pero también mayor calentamiento.
 * Rango: 200mA - 3000mA en incrementos de 200mA.
 */
typedef enum
{
    MKS_CURRENT_200MA = 200,   /**< 200mA - Torque mínimo */
    MKS_CURRENT_400MA = 400,   /**< 400mA */
    MKS_CURRENT_600MA = 600,   /**< 600mA */
    MKS_CURRENT_800MA = 800,   /**< 800mA */
    MKS_CURRENT_1000MA = 1000, /**< 1000mA (1A) */
    MKS_CURRENT_1200MA = 1200, /**< 1200mA */
    MKS_CURRENT_1400MA = 1400, /**< 1400mA */
    MKS_CURRENT_1600MA = 1600, /**< 1600mA */
    MKS_CURRENT_1800MA = 1800, /**< 1800mA */
    MKS_CURRENT_2000MA = 2000, /**< 2000mA (2A) */
    MKS_CURRENT_2200MA = 2200, /**< 2200mA */
    MKS_CURRENT_2400MA = 2400, /**< 2400mA */
    MKS_CURRENT_2600MA = 2600, /**< 2600mA */
    MKS_CURRENT_2800MA = 2800, /**< 2800mA */
    MKS_CURRENT_3000MA = 3000  /**< 3000mA (3A) - Torque máximo */
} mks_current_t;

// ============================================================================================================================================
// FUNCIONES DE LECTURA
// ============================================================================================================================================

/**
 * @brief Lee la posición actual del encoder absoluto
 *
 * @param[out] data Puntero a estructura donde se almacenarán los datos del encoder
 * @return true si la lectura fue exitosa, false en caso de error de comunicación
 */
bool mks_read_encoder(mks_encoder_data_t *data);

/**
 * @brief Lee el número total de pulsos recibidos por el motor
 *
 * Este contador se incrementa/decrementa según los pulsos de STEP recibidos.
 * Útil para verificar sincronización entre comandos enviados y pulsos procesados.
 *
 * @param[out] pulses Puntero donde se almacenará el contador de pulsos
 * @return true si la lectura fue exitosa, false en caso de error
 */
bool mks_read_pulses_received(int32_t *pulses);

/**
 * @brief Lee el error angular entre la posición objetivo y la posición real del eje
 *
 * Este valor indica qué tan bien el motor está siguiendo los comandos.
 * Un error grande puede indicar sobrecarga o pérdida de pasos.
 *
 * @param[out] angle_error Puntero donde se almacenará el error en grados (rango: -180° a +180°)
 * @return true si la lectura fue exitosa, false en caso de error
 */
bool mks_read_shaft_error_angle(float *angle_error);

/**
 * @brief Obtiene el estado actual del motor
 *
 * Esta función devuelve la configuración y estado operativo actual del motor,
 * incluyendo microstepping, velocidad, corriente y estado de habilitación.
 *
 * @param[out] status Puntero a estructura donde se almacenará el estado actual
 */
void mks_get_status(mks_status_t *status);

// ============================================================================================================================================
// FUNCIONES DE CONFIGURACIÓN
// ============================================================================================================================================

/**
 * @brief Inicializa el módulo MKS SERVO42C
 *
 * Esta función debe ser llamada una vez al inicio para preparar la comunicación
 * y el estado interno del controlador.
 */
void mks_init(void);

/**
 * @brief Calibra el encoder absoluto del motor
 *
 * ADVERTENCIA: El motor girará 360° durante la calibración.
 * Asegúrate de que el eje pueda moverse libremente.
 * El proceso puede tomar hasta 2 minutos.
 *
 * @return true si la calibración fue exitosa, false si hubo timeout o error
 */
bool mks_calibrate_encoder(void);

/**
 * @brief Configura el límite de corriente máxima del motor
 *
 * La corriente determina el torque disponible. Mayor corriente = mayor torque,
 * pero también mayor calentamiento. Configura según tu aplicación.
 *
 * @param current_ma Corriente deseada (usar valores de mks_current_t)
 * @return true si la configuración fue exitosa, false en caso de error
 */
bool mks_set_current_limit(mks_current_t current_ma);

/**
 * @brief Configura el microstepping del motor
 *
 * El microstepping divide cada paso completo en pasos más pequeños para
 * mayor suavidad y precisión. Valores típicos: 1, 2, 4, 8, 16, 32, 64, 128, 256.
 *
 * @param mstep Valor de microstepping (debe ser potencia de 2)
 * @return true si la configuración fue exitosa, false en caso de error
 */
bool mks_set_mstep(uint8_t mstep);

/**
 * @brief Configura la lógica del pin ENABLE
 *
 * Define cómo el pin ENABLE controla el motor: activo bajo, activo alto,
 * o siempre activo (ignorando el pin).
 *
 * @param mode Modo de operación del pin ENABLE
 * @return true si la configuración fue exitosa, false en caso de error
 */
bool mks_set_enable_pin_logic(mks_enable_mode_t mode);

/**
 * @brief Habilita o deshabilita el motor
 *
 * Cuando está deshabilitado, el motor no tiene torque y puede girar libremente.
 * Cuando está habilitado, mantiene la posición actual con torque.
 *
 * @param state Estado deseado: MKS_ENABLE o MKS_DISABLE
 * @return true si el comando fue exitoso, false en caso de error
 */
bool mks_set_state(mks_state_t state);

// ============================================================================================================================================
// FUNCIONES DE MOVIMIENTO
// ============================================================================================================================================

/**
 * @brief Inicia el movimiento continuo del motor
 *
 * Envía el comando y espera ACK. El motor gira hasta llamar a mks_stop().
 *
 * @param dir 0 = sentido horario, 1 = antihorario
 * @param speed_rpm RPM solicitadas (se escalan internamente al protocolo)
 * @return true si ACK recibido, false en error
 */
bool mks_run(uint8_t dir, uint16_t speed_rpm);

/**
 * @brief Mueve el motor un número específico de pulsos (movimiento relativo)
 *
 * Envía el comando 0xFD y espera dos ACKs:
 * 1. ACK de "Inicio de movimiento" (Status=1)
 * 2. ACK de "Movimiento completo" (Status=2)
 *
 * @param dir 0 = sentido horario, 1 = antihorario
 * @param speed_rpm RPM a las que se realizará el movimiento
 * @param pulses Número de pulsos a mover (uint32_t)
 * @return true si el movimiento se completó exitosamente, false en caso de error o timeout
 */
bool mks_move_relative_pulses(uint8_t dir, uint16_t speed_rpm, uint32_t pulses);

/**
 * @brief Detiene el motor inmediatamente
 *
 * Detiene cualquier movimiento en curso. El motor mantiene torque si está habilitado.
 *
 * @return true si el comando fue exitoso, false en caso de error
 */
bool mks_stop(void);

#endif // MKS_SERVO42C_H