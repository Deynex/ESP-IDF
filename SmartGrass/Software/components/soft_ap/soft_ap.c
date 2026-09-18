/**
 * @file soft_ap.c
 * @brief Implementación de funciones para gestión del WiFi en modo SoftAP
 *
 * Este archivo contiene la implementación completa del WiFi en modo punto de acceso,
 * incluyendo configuración de red, manejo de eventos y gestión de clientes.
 * @author DEYNEX
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "soft_ap.h"
#include "lwip/err.h"
#include "lwip/sys.h"

/* ========================================================================
 * MARK: DEFINICIONES
 * ======================================================================== */

/** @brief SSID del punto de acceso WiFi */
#define ESP_WIFI_SSID "SmartGrass_AP"

/** @brief Contraseña del punto de acceso (mínimo 8 caracteres para WPA2) */
#define ESP_WIFI_PASS "12345678"

/** @brief Canal WiFi (1-11 en la banda de 2.4GHz) */
#define ESP_WIFI_CHANNEL 6

/** @brief Número máximo de estaciones que pueden conectarse simultáneamente */
#define ESP_MAX_STA_CONN 4

/**
 * @brief Intervalo de rekeying GTK en segundos (0 = deshabilitado)
 *
 * GTK (Group Temporal Key) es la clave temporal de grupo usada para cifrar
 * tráfico de broadcast/multicast. Un valor de 0 deshabilita el rekeying.
 * Rango válido: 0 o 60-65535 segundos
 */
#define GTK_REKEY_INTERVAL 0

/**
 * @brief Dirección IP del punto de acceso
 *
 * Esta IP también se usa como gateway. La máscara de red es 255.255.255.0,
 * por lo que el rango DHCP será 192.168.2.2 - 192.168.2.254
 */
#define ESP_WIFI_IP "192.168.2.1"

/** @brief Máscara de red para el punto de acceso */
#define ESP_WIFI_NETMASK "255.255.255.0"

/** @brief Tag para logs del módulo WiFi */
static const char *TAG = "WIFI_SOFTAP";

/** @brief Contador de estaciones conectadas */
static uint8_t s_connected_stations = 0;

/* ========================================================================
 * MARK: EVENTOS WIFI
 * ======================================================================== */

/**
 * @brief Manejador de eventos WiFi para el modo SoftAP
 *
 * Esta función es llamada automáticamente cuando ocurre un evento WiFi.
 * Procesa todos los eventos relacionados con el punto de acceso.
 *
 * @param arg Argumento definido por el usuario (no usado)
 * @param event_base Base del evento (debe ser WIFI_EVENT)
 * @param event_id ID del evento específico
 * @param event_data Puntero a los datos del evento (varía según el tipo)
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    switch (event_id)
    {
        /* ------------------------------------------------------------------
         * Eventos de ciclo de vida del AP
         * ------------------------------------------------------------------ */

    case WIFI_EVENT_AP_START:
        ESP_LOGI(TAG, "Punto de acceso WiFi iniciado correctamente");
        ESP_LOGI(TAG, "SSID: %s, Canal: %d, IP: %s", ESP_WIFI_SSID, ESP_WIFI_CHANNEL, ESP_WIFI_IP);
        esp_err_t ret = esp_wifi_set_max_tx_power(80);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Potencia de transmisión Tx establecida a 20 dBm (80)");
        }
        else
        {
            ESP_LOGW(TAG, "No se pudo establecer la potencia de transmisión Tx: %s", esp_err_to_name(ret));
        }
        break;

    case WIFI_EVENT_AP_STOP:
        ESP_LOGI(TAG, "Punto de acceso WiFi detenido");
        s_connected_stations = 0;
        break;

        /* ------------------------------------------------------------------
         * Eventos de gestión de clientes
         * ------------------------------------------------------------------ */

    case WIFI_EVENT_AP_PROBEREQRECVED:
    {
        /* Un dispositivo está escaneando y detectó nuestro AP */
        wifi_event_ap_probe_req_rx_t *event = (wifi_event_ap_probe_req_rx_t *)event_data;
        ESP_LOGD(TAG, "Probe Request recibida - MAC: " MACSTR ", RSSI: %d dBm",
                 MAC2STR(event->mac), event->rssi);
        break;
    }

    case WIFI_EVENT_AP_STACONNECTED:
    {
        /* Un cliente se conectó exitosamente al AP */
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        s_connected_stations++;
        ESP_LOGI(TAG, "Cliente conectado - MAC: " MACSTR ", AID: %d, Total conectados: %d/%d",
                 MAC2STR(event->mac), event->aid, s_connected_stations, ESP_MAX_STA_CONN);
        break;
    }

    case WIFI_EVENT_AP_STADISCONNECTED:
    {
        /* Un cliente se desconectó del AP */
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        if (s_connected_stations > 0)
        {
            s_connected_stations--;
        }
        ESP_LOGI(TAG, "Cliente desconectado - MAC: " MACSTR ", AID: %d, Razón: %d, Total conectados: %d/%d",
                 MAC2STR(event->mac), event->aid, event->reason, s_connected_stations, ESP_MAX_STA_CONN);
        break;
    }

        /* ------------------------------------------------------------------
         * Eventos de seguridad
         * ------------------------------------------------------------------ */

    case WIFI_EVENT_AP_WRONG_PASSWORD:
    {
        /* Un cliente intentó conectarse con contraseña incorrecta */
        wifi_event_ap_wrong_password_t *event = (wifi_event_ap_wrong_password_t *)event_data;
        ESP_LOGW(TAG, "Intento de conexión con contraseña incorrecta - MAC: " MACSTR,
                 MAC2STR(event->mac));
        break;
    }

        /* ------------------------------------------------------------------
         * Eventos WPS (Wi-Fi Protected Setup)
         * ------------------------------------------------------------------ */

    case WIFI_EVENT_AP_WPS_RG_SUCCESS:
    {
        /* Registro WPS completado exitosamente */
        wifi_event_ap_wps_rg_success_t *event = (wifi_event_ap_wps_rg_success_t *)event_data;
        ESP_LOGI(TAG, "WPS: Registro exitoso - MAC del cliente: " MACSTR,
                 MAC2STR(event->peer_macaddr));
        break;
    }

    case WIFI_EVENT_AP_WPS_RG_FAILED:
    {
        /* Fallo en el registro WPS */
        wifi_event_ap_wps_rg_fail_reason_t *event = (wifi_event_ap_wps_rg_fail_reason_t *)event_data;
        ESP_LOGW(TAG, "WPS: Registro fallido - MAC: " MACSTR ", Razón: %d",
                 MAC2STR(event->peer_macaddr), event->reason);
        break;
    }

    case WIFI_EVENT_AP_WPS_RG_TIMEOUT:
        ESP_LOGW(TAG, "WPS: Timeout en el registro");
        break;

    case WIFI_EVENT_AP_WPS_RG_PIN:
    {
        /* Se recibió un PIN WPS del cliente */
        wifi_event_ap_wps_rg_pin_t *event = (wifi_event_ap_wps_rg_pin_t *)event_data;
        ESP_LOGI(TAG, "WPS: PIN recibido: %s", event->pin_code);
        break;
    }

    case WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP:
        /* Se detectaron múltiples dispositivos en modo PBC simultáneamente */
        ESP_LOGW(TAG, "WPS: PBC overlap detectado (múltiples dispositivos en modo push-button)");
        break;

        /* ------------------------------------------------------------------
         * Eventos de configuración
         * ------------------------------------------------------------------ */

    case WIFI_EVENT_HOME_CHANNEL_CHANGE:
    {
        /* El canal del AP cambió (puede ocurrir en modo APSTA) */
        wifi_event_home_channel_change_t *event = (wifi_event_home_channel_change_t *)event_data;
        ESP_LOGI(TAG, "Cambio de canal - Anterior: %d, Nuevo: %d, Secundario: %d -> %d",
                 event->old_chan, event->new_chan, event->old_snd, event->new_snd);
        break;
    }

        /* ------------------------------------------------------------------
         * Eventos no reconocidos
         * ------------------------------------------------------------------ */

    default:
        ESP_LOGW(TAG, "Evento WiFi no manejado: %ld", event_id);
        break;
    }
}

/* ========================================================================
 * MARK: CONFIGURACIÓN
 * ======================================================================== */

/**
 * @brief Configura e inicia el punto de acceso WiFi
 *
 * Esta función configura todos los parámetros del AP incluyendo:
 * - Dirección IP estática y puerta de enlace
 * - Servidor DHCP para asignación de IPs a clientes
 * - SSID, contraseña y canal
 * - Modo de autenticación (WPA2/WPA3)
 * - Protected Management Frames (PMF)
 * - BSS Max Idle para desconexión de clientes inactivos
 * - Group Temporal Key (GTK) rekeying
 *
 * @note Esta función se llama internamente desde wifi_init()
 */
static void wifi_ap_start(void)
{
    /* Crea la interfaz de red por defecto para el AP */
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();
    if (esp_netif_ap == NULL)
    {
        ESP_LOGE(TAG, "Error al crear la interfaz de red por defecto para el punto de acceso");
        return;
    }

    /* ------------------------------------------------------------------
     * Configuración de la dirección IP estática del AP
     * ------------------------------------------------------------------ */

    esp_netif_ip_info_t ip_info;

    /* Configura la IP del AP */
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(ESP_WIFI_IP, &ip_info.ip));

    /* El gateway es la misma IP del AP */
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(ESP_WIFI_IP, &ip_info.gw));

    /* Máscara de red: 255.255.255.0 (red /24) */
    ESP_ERROR_CHECK(esp_netif_str_to_ip4(ESP_WIFI_NETMASK, &ip_info.netmask));

    /* Detiene el servidor DHCP temporalmente para cambiar la configuración */
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(esp_netif_ap));

    /* Aplica la nueva configuración IP */
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ip_info));

    /* Reinicia el servidor DHCP con la nueva configuración */
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));

    ESP_LOGI(TAG, "Configuración IP del AP establecida: IP=%s, Gateway=%s, Máscara=%s",
             ESP_WIFI_IP, ESP_WIFI_IP, ESP_WIFI_NETMASK);

    /* ------------------------------------------------------------------
     * Configuración de los parámetros del AP
     * ------------------------------------------------------------------ */

    wifi_config_t wifi_config = {
        .ap = {
            /* SSID: nombre de la red WiFi */
            .ssid = ESP_WIFI_SSID,
            .ssid_len = strlen(ESP_WIFI_SSID),

            /* Canal WiFi (1-13 en 2.4GHz) */
            .channel = ESP_WIFI_CHANNEL,

            /* Contraseña (mínimo 8 caracteres para WPA2) */
            .password = ESP_WIFI_PASS,

            /* Número máximo de clientes simultáneos */
            .max_connection = ESP_MAX_STA_CONN,

#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
            /* WPA3-PSK: más seguro que WPA2, usa SAE en lugar de 4-way handshake */
            .authmode = WIFI_AUTH_WPA3_PSK,

            /* Soporta tanto H2E como hunting-and-pecking para compatibilidad */
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else
            /* WPA2-PSK: modo estándar ampliamente compatible */
            .authmode = WIFI_AUTH_WPA2_PSK,
#endif

            /* Protected Management Frames: protege frames de gestión contra ataques */
            .pmf_cfg = {
                .required = true, /* PMF obligatorio (más seguro, menos compatible) */
            },

#ifdef CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT
            /* BSS Max Idle: desconecta clientes inactivos para liberar recursos */
            .bss_max_idle_cfg = {
                .period = WIFI_AP_DEFAULT_MAX_IDLE_PERIOD, /* 292 * 1000 TUs ≈ 300 segundos */
                .protected_keep_alive = 1,                 /* Keep-alive protegido con PMF */
            },
#endif

            /* Intervalo de rekeying para la clave GTK (0 = deshabilitado) */
            .gtk_rekey_interval = GTK_REKEY_INTERVAL,
        },
    };

    /* Si no hay contraseña, usa modo abierto */
    if (strlen(ESP_WIFI_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
        ESP_LOGW(TAG, "Modo abierto activado (sin contraseña)");
    }

    /* Aplica la configuración al driver WiFi */
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI(TAG, "AP configurado: SSID='%s', Canal=%d, Autenticación=%s, Max Clientes=%d",
             ESP_WIFI_SSID,
             ESP_WIFI_CHANNEL,
             (wifi_config.ap.authmode == WIFI_AUTH_OPEN) ? "OPEN" : (wifi_config.ap.authmode == WIFI_AUTH_WPA2_PSK) ? "WPA2-PSK"
                                                                                                                    : "WPA3-PSK",
             ESP_MAX_STA_CONN);
}

/* ========================================================================
 * MARK: FUNCIONES
 * ======================================================================== */

esp_err_t nvs_init(void)
{
    /* Intenta inicializar el NVS */
    esp_err_t err = nvs_flash_init();

    /* Si no hay páginas libres o la versión es incompatible, borra y reinicia */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS requiere ser borrado (razón: %s). Borrando...", esp_err_to_name(err));

        esp_err_t erase_err = nvs_flash_erase();
        if (erase_err != ESP_OK)
        {
            ESP_LOGE(TAG, "Error al borrar NVS: %s", esp_err_to_name(erase_err));
            return erase_err; // Devuelve el error
        }

        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al inicializar NVS: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t wifi_init(void)
{
    /* ------------------------------------------------------------------
     * 1. Inicializar el stack TCP/IP (LwIP)
     * ------------------------------------------------------------------ */

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al inicializar netif: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ------------------------------------------------------------------
     * 2. Crear el loop de eventos por defecto
     * ------------------------------------------------------------------ */

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al crear event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ------------------------------------------------------------------
     * 3. Registrar el manejador de eventos WiFi
     * ------------------------------------------------------------------ */

    ret = esp_event_handler_instance_register(
        WIFI_EVENT,          /* Base de eventos WiFi */
        ESP_EVENT_ANY_ID,    /* Todos los eventos WiFi */
        &wifi_event_handler, /* Función manejadora */
        NULL,                /* Argumento de usuario (no usado) */
        NULL                 /* Instancia del handler (no necesitamos guardarla) */
    );

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al registrar event handler: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ------------------------------------------------------------------
     * 4. Inicializar el driver WiFi con configuración por defecto
     * ------------------------------------------------------------------ */

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al inicializar WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ------------------------------------------------------------------
     * 5. Configurar el modo WiFi (solo AP, no STA)
     * ------------------------------------------------------------------ */

    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al establecer modo AP: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ------------------------------------------------------------------
     * 6. Configurar el punto de acceso (IP, SSID, contraseña, etc.)
     * ------------------------------------------------------------------ */

    wifi_ap_start();

    /* ------------------------------------------------------------------
     * 7. Arrancar el driver WiFi
     * ------------------------------------------------------------------ */

    ret = esp_wifi_start();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al arrancar WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t wifi_stop(void)
{
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Error al detener WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    s_connected_stations = 0;
    return ESP_OK;
}

uint8_t wifi_get_connected_stations(void)
{
    return s_connected_stations;
}