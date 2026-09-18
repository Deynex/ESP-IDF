/**
 * @file mks_servo42c.c
 * @brief Implementación de la API para control del servo motor MKS SERVO42C con encoder absoluto
 * @author DEYNEX
 */

#include <stdio.h>
#include <string.h>
#include "mks_servo42c.h"
#include "uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

// ============================================================================================================================================================
// MARK: Definiciones
// ============================================================================================================================================================

// Dirección del dispositivo y encabezado de paquetes
#define MKS_SLAVE_ADDR 0xE0    // Dirección del servo en el bus de comunicación
#define MKS_PACKET_HEADER 0xE0 // Byte de inicio de cada paquete de respuesta

// Comandos de Lectura
#define MKS_CMD_READ_ENCODER 0x30     // Lee la posición del encoder absoluto
#define MKS_CMD_READ_PULSES 0x33      // Lee el contador de pulsos recibidos
#define MKS_CMD_READ_ERROR_ANGLE 0x39 // Lee el error angular del eje

// Comandos de Configuración
#define MKS_CMD_CALIBRATE 0x80    // Inicia calibración del encoder
#define MKS_CMD_SET_CURRENT 0x83  // Configura límite de corriente
#define MKS_CMD_SET_MSTEP 0x84    // Configura microstepping
#define MKS_CMD_SET_EN_LOGIC 0x85 // Configura lógica del pin ENABLE

// Comandos de Movimiento
#define MKS_CMD_SET_STATE 0xF3  // Habilita/deshabilita el motor
#define MKS_CMD_RUN 0xF6        // Inicia movimiento continuo
#define MKS_CMD_RUN_PULSES 0xFD // Inicia movimiento relativo por pulsos
#define MKS_CMD_STOP 0xF7       // Detiene el motor

// Códigos de respuesta del dispositivo
#define MKS_ACK_SUCCESS 0x01      // Comando ejecutado exitosamente
#define MKS_ACK_FAIL 0x00         // Comando falló
#define MKS_ACK_RUN_STARTING 0x01 // Respuesta: Movimiento iniciado
#define MKS_ACK_RUN_COMPLETE 0x02 // Respuesta: Movimiento completado

// Timeouts y configuración de comunicación
#define MKS_DEFAULT_UART_TIMEOUT_MS 300 // Timeout para comandos normales
#define MKS_CALIBRATE_TIMEOUT_MS 120000 // Timeout para calibración (2 minutos)
#define MKS_CALIBRATE_READ_POLL_MS 500  // Intervalo de polling durante calibración
#define MKS_UART_RX_BUF_SIZE 128        // Tamaño del buffer de recepción

// Factores de conversión
#define MKS_RAW_TO_DEG_FACTOR (360.0f / 65536.0f)       // Convierte valor crudo a grados (0-360°)
#define MKS_RAW_ERROR_TO_DEG_FACTOR (180.0f / 32768.0f) // Convierte error crudo a grados (-180° a +180°)
#define MKS_MAX_CURRENT_VAL 15                          // Valor máximo de corriente (15 * 200mA = 3000mA)
#define MKS_MAX_SPEED_VAL 127                           // Valor máximo de velocidad en el protocolo

// ============================================================================================================================================================
// MARK: Variables
// ============================================================================================================================================================
static const char *TAG = "MKS_SERVO42C";

static mks_status_t g_current_status = {
    .mstep = 16,
    .current = MKS_CURRENT_600MA,
    .enable = true,
    .speed = 0,
};

static SemaphoreHandle_t g_uart_mutex = NULL; // Mutex para proteger la UART
// ============================================================================================================================================================
// MARK: Utilidades
// ============================================================================================================================================================
/**
 * @brief Calcula el checksum simple (suma de bytes) para un paquete de datos
 *
 * @param data Puntero al buffer de datos
 * @param len Longitud del buffer en bytes
 * @return uint8_t Valor del checksum (8 bits inferiores de la suma)
 */
static uint8_t mks_calculate_checksum(uint8_t *data, int len)
{
    uint16_t sum = 0;
    for (int i = 0; i < len; i++)
    {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

/**
 * @brief Envía un comando y espera una respuesta ACK simple
 *
 * Esta función maneja el protocolo estándar de comando-respuesta del MKS SERVO42C.
 * Formato de respuesta esperado: [0xE0, Status, Checksum]
 * - 0xE0: Header del paquete
 * - Status: 0x01 (éxito) o 0x00 (fallo)
 * - Checksum: Suma de los dos bytes anteriores (8 bits inferiores)
 *
 * @param cmd Puntero al buffer del comando a enviar
 * @param cmd_len Longitud del comando en bytes
 * @return true si el motor responde exitosamente (Status=0x01 y CRC válido)
 * @return false en caso de timeout, CRC inválido o status de fallo
 */
static bool mks_send_cmd_and_wait_ack(uint8_t *cmd, int cmd_len)
{
    // Generar un tag local para saber qué comando se está ejecutando
    char local_tag[30];
    sprintf(local_tag, "%s (CMD 0x%02X)", TAG, cmd[1]);

    uart_flush_rx_buffer();
    uart_tx_data((const char *)cmd, cmd_len);

    uint8_t resp[16];
    int rx_len = uart_rx_data(resp, sizeof(resp), pdMS_TO_TICKS(MKS_DEFAULT_UART_TIMEOUT_MS));

    // Imprimir lo que sea que hayamos recibido
    ESP_LOG_BUFFER_HEXDUMP(local_tag, resp, rx_len, ESP_LOG_INFO);

    if (rx_len < 3)
    {
        ESP_LOGE(local_tag, "¡Fallo de ACK! TIMEOUT (solo se recibieron %d bytes)", rx_len);
        return false; // Timeout o respuesta incompleta
    }

    // Buscar el paquete [E0, Status, CRC]
    for (int i = 0; i < rx_len - 2; i++)
    {
        if (resp[i] == MKS_PACKET_HEADER)
        {
            uint8_t *pkt = &resp[i];

            // --- INICIO DE LA CORRECCIÓN ---
            // Un paquete de estado VÁLIDO debe tener 0x01 (Éxito) o 0x00 (Fallo)
            // como su segundo byte (pkt[1]).
            // Cualquier otro valor (0x84, 0xF7, etc.) es un ID de comando de un ECO.
            if (pkt[1] != MKS_ACK_SUCCESS && pkt[1] != MKS_ACK_FAIL)
            {
                // ESP_LOGI(local_tag, "Ignorando paquete de eco (ID=0x%02X)", pkt[1]);
                continue; // Ignorar este paquete, es un eco, seguir buscando.
            }
            // --- FIN DE LA CORRECCIÓN ---

            // Si estamos aquí, es un paquete de estado (0x01 o 0x00).
            // Ahora sí, validamos su CRC.
            uint8_t crc = mks_calculate_checksum(pkt, 2);
            if (crc != pkt[2])
            {
                ESP_LOGW(local_tag, "Paquete de estado encontrado, pero con CRC incorrecto.");
                continue; // CRC incorrecto
            }

            // El paquete es un paquete de estado válido y su CRC es correcto.
            if (pkt[1] == MKS_ACK_SUCCESS)
            {
                // ESP_LOGI(local_tag, "ACK de éxito recibido.");
                return true;
            }
            else // pkt[1] debe ser MKS_ACK_FAIL (0x00)
            {
                ESP_LOGE(local_tag, "Fallo de ACK: El motor reportó un error (Status=0x%02X)", pkt[1]);
                return false;
            }
        }
    }

    ESP_LOGW(local_tag, "Fallo de ACK: No se encontró un paquete de estado válido en la respuesta.");
    return false; // No se encontró ningún paquete válido
}

// ============================================================================================================================================================
// MARK: Lectura de Datos
// ============================================================================================================================================================

bool mks_read_encoder(mks_encoder_data_t *data)
{
    // Proteger acceso a la UART
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    uint8_t cmd[3];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_READ_ENCODER;
    cmd[2] = mks_calculate_checksum(cmd, 2);

    uint8_t resp[MKS_UART_RX_BUF_SIZE];
    bool success = false;

    uart_flush_rx_buffer();
    uart_tx_data((const char *)cmd, sizeof(cmd));
    int rx_len = uart_rx_data(resp, sizeof(resp), pdMS_TO_TICKS(MKS_DEFAULT_UART_TIMEOUT_MS));

    // Formato de respuesta: [E0, Carry(4 bytes), Value(2 bytes), CRC] = 8 bytes
    if (rx_len < 8)
    {
        xSemaphoreGive(g_uart_mutex);
        return success;
    }

    // Buscar el paquete válido en la respuesta
    for (int i = 0; i < rx_len - 7; i++)
    {
        if (resp[i] == MKS_PACKET_HEADER)
        {
            uint8_t *pkt = &resp[i];

            // Un paquete de datos del encoder NUNCA debe tener el código
            // de comando '0x30' como su segundo byte (pkt[1]).
            // Si lo tiene, es un eco de nuestro propio comando.
            if (pkt[1] == MKS_CMD_READ_ENCODER) // pkt[1] es 0x30
            {
                continue; // Ignorar este paquete, es un eco.
            }

            uint8_t crc = mks_calculate_checksum(pkt, 7);
            if (crc != pkt[7])
                continue; // Checksum inválido, seguir buscando

            // Extraer carry (4 bytes, big-endian) - contador de vueltas completas
            data->carry = (pkt[1] << 24) | (pkt[2] << 16) | (pkt[3] << 8) | pkt[4];

            // Extraer value (2 bytes, big-endian) - posición dentro de la vuelta
            data->value = (pkt[5] << 8) | pkt[6];

            // Convertir valor crudo a grados (0-360°)
            data->angle_deg = ((float)data->value * MKS_RAW_TO_DEG_FACTOR);

            // Calcular ángulo total considerando vueltas completas
            data->total_angle = data->carry * 360.0f + data->angle_deg;

            success = true;
            break;
        }
    }

    xSemaphoreGive(g_uart_mutex);
    return success;
}

bool mks_read_pulses_received(int32_t *pulses)
{
    // Proteger acceso a la UART
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    uint8_t cmd[3];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_READ_PULSES;
    cmd[2] = mks_calculate_checksum(cmd, 2);

    uint8_t resp[16];
    bool success = false;

    uart_flush_rx_buffer();
    uart_tx_data((const char *)cmd, sizeof(cmd));
    int rx_len = uart_rx_data(resp, sizeof(resp), pdMS_TO_TICKS(MKS_DEFAULT_UART_TIMEOUT_MS));

    // Formato de respuesta: [E0, Pulses(4 bytes), CRC] = 6 bytes
    if (rx_len < 6)
    {
        xSemaphoreGive(g_uart_mutex);
        return success;
    }

    // Buscar el paquete válido en la respuesta
    for (int i = 0; i < rx_len - 5; i++)
    {
        if (resp[i] == MKS_PACKET_HEADER)
        {
            uint8_t *pkt = &resp[i];

            // Un paquete de datos del encoder NUNCA debe tener el código
            // de comando '0x33' como su segundo byte (pkt[1]).
            // Si lo tiene, es un eco de nuestro propio comando.
            if (pkt[1] == MKS_CMD_READ_PULSES) // pkt[1] es 0x33
            {
                continue; // Ignorar este paquete, es un eco.
            }

            uint8_t crc = mks_calculate_checksum(pkt, 5);
            if (crc != pkt[5])
                continue; // Checksum inválido, seguir buscando

            // Extraer contador de pulsos (4 bytes, big-endian, con signo)
            *pulses = (pkt[1] << 24) | (pkt[2] << 16) | (pkt[3] << 8) | pkt[4];

            success = true;
            break;
        }
    }

    xSemaphoreGive(g_uart_mutex);
    return success;
}

bool mks_read_shaft_error_angle(float *angle_error)
{
    // Proteger acceso a la UART
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    uint8_t cmd[3];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_READ_ERROR_ANGLE;
    cmd[2] = mks_calculate_checksum(cmd, 2);

    uint8_t resp[16];
    bool success = false;

    uart_flush_rx_buffer();
    uart_tx_data((const char *)cmd, sizeof(cmd));
    int rx_len = uart_rx_data(resp, sizeof(resp), pdMS_TO_TICKS(MKS_DEFAULT_UART_TIMEOUT_MS));

    // Formato de respuesta: [E0, Error_H, Error_L, CRC] = 4 bytes
    if (rx_len < 4)
    {
        xSemaphoreGive(g_uart_mutex);
        return success;
    }

    // Buscar el paquete válido en la respuesta
    for (int i = 0; i < rx_len - 3; i++)
    {
        if (resp[i] == MKS_PACKET_HEADER)
        {
            uint8_t *pkt = &resp[i];

            // Un paquete de datos del encoder NUNCA debe tener el código
            // de comando '0x39' como su segundo byte (pkt[1]).
            // Si lo tiene, es un eco de nuestro propio comando.
            if (pkt[1] == MKS_CMD_READ_ERROR_ANGLE) // pkt[1] es 0x39
            {
                continue; // Ignorar este paquete, es un eco.
            }

            uint8_t crc = mks_calculate_checksum(pkt, 3);
            if (crc != pkt[3])
                continue; // Checksum inválido, seguir buscando

            // Extraer error (2 bytes, big-endian, con signo)
            int16_t raw_error = (pkt[1] << 8) | pkt[2];

            // Convertir a grados (rango: -180° a +180°)
            *angle_error = ((float)raw_error * MKS_RAW_ERROR_TO_DEG_FACTOR);

            success = true;
            break;
        }
    }

    xSemaphoreGive(g_uart_mutex);
    return success;
}

void mks_get_status(mks_status_t *status)
{
    if (!status)
        return;
    if (g_uart_mutex && xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdTRUE)
    {
        memcpy(status, &g_current_status, sizeof(mks_status_t));
        xSemaphoreGive(g_uart_mutex);
    }
}

// ============================================================================================================================================================
// MARK: Configuración
// ============================================================================================================================================================

void mks_init(void)
{
    ESP_LOGI(TAG, "Inicializando MKS SERVO42C...");

    if (g_uart_mutex == NULL)
    {
        g_uart_mutex = xSemaphoreCreateMutex();
    }

    if (!mks_set_current_limit(MKS_CURRENT_600MA)) // 600 mA
    {
        ESP_LOGE(TAG, "Error al configurar el límite de corriente");
        return;
    }

    if (!mks_set_mstep(16)) // Microstepping x16
    {
        ESP_LOGE(TAG, "Error al configurar el microstepping");
        return;
    }

    if (!mks_set_enable_pin_logic(MKS_ENABLE_ACTIVE_LOW))
    {
        ESP_LOGE(TAG, "Error al configurar la lógica del pin de enable");
        return;
    }

    if (!mks_set_state(MKS_ENABLE))
    {
        ESP_LOGE(TAG, "Error al habilitar el motor");
        return;
    }

    ESP_LOGI(TAG, "MKS SERVO42C inicializado correctamente");
}

bool mks_calibrate_encoder(void)
{
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    uint8_t cmd[4];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_CALIBRATE;
    cmd[2] = 0x00; // Byte de datos para calibración
    cmd[3] = mks_calculate_checksum(cmd, 3);

    uint8_t resp[MKS_UART_RX_BUF_SIZE];
    bool success = false;

    uart_flush_rx_buffer();
    uart_tx_data((const char *)cmd, sizeof(cmd));

    // Configurar timeouts para polling durante la calibración
    const TickType_t total_wait = pdMS_TO_TICKS(MKS_CALIBRATE_TIMEOUT_MS);
    const TickType_t per_read = pdMS_TO_TICKS(MKS_CALIBRATE_READ_POLL_MS);
    TickType_t start = xTaskGetTickCount();

    // Polling: esperar respuesta de calibración (puede tardar hasta 2 minutos)
    while ((xTaskGetTickCount() - start) < total_wait)
    {
        int rx_len = uart_rx_data(resp, sizeof(resp), per_read);
        if (rx_len <= 0)
        {
            continue; // No hay datos aún, seguir esperando
        }

        ESP_LOG_BUFFER_HEX(TAG, resp, rx_len); // Descomenta para debug de comunicación

        // Buscar paquete de confirmación [E0, 0x01, CRC]
        for (int i = 0; i <= rx_len - 3; i++)
        {
            if (resp[i] != MKS_PACKET_HEADER)
                continue;

            uint8_t *pkt = &resp[i];
            uint8_t crc = mks_calculate_checksum(pkt, 2);

            // Verificar checksum Y que el estado indique éxito
            if (crc == pkt[2] && pkt[1] == MKS_ACK_SUCCESS)
            {
                success = true; // Calibración exitosa
                break;
            }
        }

        if (success)
            break;
    }

    xSemaphoreGive(g_uart_mutex);
    return success;
}

bool mks_set_current_limit(mks_current_t current_ma)
{
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    // Convertir mA a valor de protocolo (cada unidad = 200mA)
    uint8_t ma_val = (uint8_t)(current_ma / 200);
    if (ma_val > MKS_MAX_CURRENT_VAL)
    {
        ma_val = MKS_MAX_CURRENT_VAL; // Limitar al máximo permitido (3000mA)
    }

    uint8_t cmd[4];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_SET_CURRENT;
    cmd[2] = ma_val;
    cmd[3] = mks_calculate_checksum(cmd, 3);

    bool success = mks_send_cmd_and_wait_ack(cmd, sizeof(cmd));

    if (success)
    {
        g_current_status.current = current_ma;
    }

    xSemaphoreGive(g_uart_mutex);
    return success;
}

bool mks_set_mstep(uint8_t mstep)
{
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    uint8_t cmd[4];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_SET_MSTEP;
    cmd[2] = mstep;
    cmd[3] = mks_calculate_checksum(cmd, 3);

    bool success = mks_send_cmd_and_wait_ack(cmd, sizeof(cmd));

    if (success)
    {
        g_current_status.mstep = mstep;
    }
    xSemaphoreGive(g_uart_mutex);
    return success;
}

bool mks_set_enable_pin_logic(mks_enable_mode_t mode)
{
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    uint8_t cmd[4];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_SET_EN_LOGIC;
    cmd[2] = (uint8_t)mode;
    cmd[3] = mks_calculate_checksum(cmd, 3);

    bool success = mks_send_cmd_and_wait_ack(cmd, sizeof(cmd));

    xSemaphoreGive(g_uart_mutex);
    return success;
}

// ============================================================================================================================================================
// MARK: Movimiento
// ============================================================================================================================================================

bool mks_set_state(mks_state_t state)
{
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    uint8_t cmd[4];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_SET_STATE;
    cmd[2] = (uint8_t)state;
    cmd[3] = mks_calculate_checksum(cmd, 3);

    bool success = mks_send_cmd_and_wait_ack(cmd, sizeof(cmd));

    if (success)
    {
        g_current_status.enable = (state == MKS_ENABLE);
    }
    xSemaphoreGive(g_uart_mutex);
    return success;
}

bool mks_run(uint8_t dir, uint16_t speed_rpm)
{
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    // Obtener el microstepping actual
    uint8_t mstep = g_current_status.mstep;

    // Calcular velocidad según fórmula del fabricante: V = (RPM * mstep) / 150
    uint16_t calculated_speed = (speed_rpm * mstep) / 150.0f;

    // Limitar al rango válido (0-127)
    uint8_t speed_val;
    if (calculated_speed > MKS_MAX_SPEED_VAL)
    {
        speed_val = MKS_MAX_SPEED_VAL;
    }
    else
    {
        speed_val = (uint8_t)calculated_speed;
    }

    // Construir byte de datos: [DIR(1bit) | SPEED(7bits)]
    // Bit 7 = dirección (0=CW, 1=CCW), Bits 6-0 = velocidad
    uint8_t data_byte = (dir ? 0x80 : 0x00) | (speed_val & 0x7F);

    uint8_t cmd[4];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_RUN;
    cmd[2] = data_byte;
    cmd[3] = mks_calculate_checksum(cmd, 3);

    bool success = mks_send_cmd_and_wait_ack(cmd, sizeof(cmd));

    if (success)
    {
        g_current_status.speed = speed_rpm;
    }

    xSemaphoreGive(g_uart_mutex);
    return success;
}

bool mks_move_relative_pulses(uint8_t dir, uint16_t speed_rpm, uint32_t pulses)
{
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    // --- 1. Calcular el byte de Velocidad (igual que mks_run) ---
    uint8_t mstep = g_current_status.mstep;
    uint16_t calculated_speed = (speed_rpm * mstep) / 150.0f;
    uint8_t speed_val;
    if (calculated_speed > MKS_MAX_SPEED_VAL)
    {
        speed_val = MKS_MAX_SPEED_VAL;
    }
    else
    {
        speed_val = (uint8_t)calculated_speed;
    }
    // Byte de datos: [DIR(1bit) | SPEED(7bits)]
    uint8_t data_byte = (dir ? 0x80 : 0x00) | (speed_val & 0x7F);

    // --- 2. Construir el comando 0xFD (8 bytes) ---
    // Formato: [Addr, CMD, VAL, P3, P2, P1, P0, CRC]
    uint8_t cmd[8];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_RUN_PULSES;
    cmd[2] = data_byte;
    cmd[3] = (pulses >> 24) & 0xFF; // Pulso (Big-Endian)
    cmd[4] = (pulses >> 16) & 0xFF;
    cmd[5] = (pulses >> 8) & 0xFF;
    cmd[6] = pulses & 0xFF;
    cmd[7] = mks_calculate_checksum(cmd, 7);

    // --- 3. Enviar comando ---
    uart_flush_rx_buffer();
    uart_tx_data((const char *)cmd, sizeof(cmd));

    uint8_t resp[16];
    bool start_ack = false;
    bool complete_ack = false;

    // --- 4. Esperar ACK de "Inicio" (Status=1) ---
    int rx_len = uart_rx_data(resp, sizeof(resp), pdMS_TO_TICKS(MKS_DEFAULT_UART_TIMEOUT_MS));
    if (rx_len >= 3)
    {
        for (int i = 0; i < rx_len - 2; i++)
        {
            if (resp[i] == MKS_PACKET_HEADER)
            {
                uint8_t *pkt = &resp[i];
                if (pkt[1] == MKS_ACK_RUN_STARTING)
                { // 0x01
                    uint8_t crc = mks_calculate_checksum(pkt, 2);
                    if (crc == pkt[2])
                    {
                        start_ack = true;
                        break;
                    }
                }
            }
        }
    }

    if (!start_ack)
    {
        ESP_LOGE(TAG, "mks_move_relative: No se recibió ACK de 'START'");
        xSemaphoreGive(g_uart_mutex);
        return false;
    }

    // --- 5. Esperar ACK de "Completado" (Status=2) ---
    // Este timeout debe ser largo, ya que el movimiento puede tardar.
    TickType_t start = xTaskGetTickCount();
    TickType_t total_wait = pdMS_TO_TICKS(60000); // 60 segundos de timeout máx.

    while ((xTaskGetTickCount() - start) < total_wait)
    {
        rx_len = uart_rx_data(resp, sizeof(resp), pdMS_TO_TICKS(50)); // Poll cada 50ms
        if (rx_len < 3)
        {
            // Ceder tiempo al planificador para evitar el WDT
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        for (int i = 0; i < rx_len - 2; i++)
        {
            if (resp[i] == MKS_PACKET_HEADER)
            {
                uint8_t *pkt = &resp[i];
                if (pkt[1] == MKS_ACK_RUN_COMPLETE)
                { // 0x02
                    uint8_t crc = mks_calculate_checksum(pkt, 2);
                    if (crc == pkt[2])
                    {
                        complete_ack = true;
                        break;
                    }
                }
            }
        }
        if (complete_ack)
            break;
    }

    if (!complete_ack)
    {
        ESP_LOGE(TAG, "mks_move_relative: No se recibió ACK de 'COMPLETE' (timeout)");
    }

    // --- 6. Liberar mutex y retornar ---
    xSemaphoreGive(g_uart_mutex);
    return complete_ack;
}

bool mks_stop(void)
{
    if (xSemaphoreTake(g_uart_mutex, portMAX_DELAY) == pdFALSE)
        return false;

    uint8_t cmd[3];
    cmd[0] = MKS_SLAVE_ADDR;
    cmd[1] = MKS_CMD_STOP;
    cmd[2] = mks_calculate_checksum(cmd, 2);

    bool success = mks_send_cmd_and_wait_ack(cmd, sizeof(cmd));

    if (success)
    {
        g_current_status.speed = 0;
    }

    xSemaphoreGive(g_uart_mutex);
    return success;
}