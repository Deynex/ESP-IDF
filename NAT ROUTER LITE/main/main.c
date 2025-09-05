#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_tls_crypto.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <sys/param.h>
#include <stdbool.h>
#if IP_NAPT
#include "lwip/lwip_napt.h"
#endif

// Definiciones de pines
#define BUILD_LED GPIO_NUM_2 // LED conectado al pin GPIO 2

// Definiciones de NVS
#define WIFI_NAMESPACE "wifi"

// Definiciones WiFi AP
#define WIFI_AP_SSID "ESP32-NAT"
#define WIFI_AP_PASS "12345678"
#define WIFI_AP_MAX_STA_CONN 3
#define WIFI_AP_CHANNEL 0
#define WIFI_AP_IP "192.168.2.1" // También se usa como gateway (Netmask: 255.255.255.0)

// Definiciones WiFi STA
#define WIFI_STA_SSID_DEFAULT "SSID"
#define WIFI_STA_PASS_DEFAULT "PASS"
#define WIFI_STA_MAX_RETRY 2
#define WIFI_STA_RECONNECT_TIMEOUT 3

// Tags para logging
static const char *TAG_GPIO = "GPIO";
static const char *TAG_TIMER = "TIMER";
// static const char *TAG_ISR = "ISR";
// static const char *TAG_QUEUE = "QUEUE";
static const char *TAG_NVS = "NVS";
static const char *TAG_WIFI = "WIFI";
static const char *TAG_AP = "WIFI_AP";
static const char *TAG_STA = "WIFI_STA";
static const char *TAG_HTTP = "WEB_SERVER";

// Estructuras
typedef struct
{
    uint8_t num;
    gpio_mode_t mode;
    uint8_t state;
} digital_pin;

// Variables globales
static esp_timer_handle_t reconnect_timer = NULL;
static esp_netif_t *esp_netif_ap = NULL;
static esp_netif_t *esp_netif_sta = NULL;
static char *ssid = NULL;
static char *password = NULL;
static int auth_mode_index = 6;
static bool esp_connected = 0;
static httpd_handle_t server_handle = NULL;

// Declaración de funciones principales
static void configure_digital_pin(digital_pin *pin);                                                             // Configura un pin GPIO
static void configure_timer(const char *name, esp_timer_handle_t *timer_handle, esp_timer_cb_t reconnect_timer); // Inicia un timer
static void nvs_start(void);                                                                                     // Inicializa el almacenamiento no volátil
static void wifi_start(void);                                                                                    // Inicializa el WiFi
esp_netif_t *wifi_ap_start(void);                                                                                // Inicializa el punto de acceso WiFi
esp_netif_t *wifi_sta_start(void);                                                                               // Inicializa el cliente WiFi
static void get_wifi_credentials(char **ssid, char **password);                                                  // Guarda las credenciales de WiFi en el almacenamiento no volátil
static void configure_http_server(void);                                                                         // Configura el servidor HTTP
static void toggle_pin(digital_pin *pin);                                                                        // Cambia el estado de un pin GPIO

// Declaración de funciones de eventos en el WiFi
static void sta_disconnected_event_handler(wifi_event_sta_disconnected_t *event);   // Manejador de eventos de desconexión del cliente WiFi
static void change_sta_authmode_threshold(void);                                    // Manejador de eventos de no encontrar AP en el umbral de autenticación
static void update_wifi_credentials(void);                                          // Manejador de eventos comunes de desconexión del cliente WiFi
static void wifi_reconnect(void);                                                   // Reintenta la conexión WiFi
static void ap_set_dns_addr(esp_netif_t *esp_netif_ap, esp_netif_t *esp_netif_sta); // Establece la dirección DNS en el punto de acceso

// Declaración de manejadores del web server
static esp_err_t main_handler(httpd_req_t *req);    // Manejador de la página principal
static esp_err_t favicon_handler(httpd_req_t *req); // Manejador del favicon
static esp_err_t post_handler(httpd_req_t *req);    // Manejador de la petición POST

// Declaración de funciones del web server
static void url_decode(char *dst, const char *src);            // Decodifica una URL
static void save_wifi_credentials(char *ssid, char *password); // Obtiene las credenciales de WiFi del almacenamiento no volátil

// Paginas web
extern const uint8_t main_html_start[] asm("_binary_main_html_start");
extern const uint8_t main_html_end[] asm("_binary_main_html_end");

// Imagenes e iconos
extern const uint8_t router_ico_start[] asm("_binary_router_ico_start");
extern const uint8_t router_ico_end[] asm("_binary_router_ico_end");

// MARK: CALLBACKS ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void reconnect_cb(void *arg)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TIMER, "Error al conectar al WiFi. Error %s", esp_err_to_name(err));
    }
}

// MARK: EVENTOS -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_HOME_CHANNEL_CHANGE:
        {
            wifi_event_home_channel_change_t *event = (wifi_event_home_channel_change_t *)event_data;
            ESP_LOGI(TAG_WIFI, "Cambio de canal. Anterior: %d, Nuevo: %d", event->old_chan, event->new_chan);
            break;
        }
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG_AP, "Punto de acceso WiFi iniciado");
            break;
        case WIFI_EVENT_AP_PROBEREQRECVED:
        {
            wifi_event_ap_probe_req_rx_t *event = (wifi_event_ap_probe_req_rx_t *)event_data;
            ESP_LOGI(TAG_AP, "Solicitud de sondeo recibida. MAC: " MACSTR ", RSSI: %d", MAC2STR(event->mac), event->rssi);
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG_AP, "Cliente conectado al ESP. MAC: " MACSTR ", AID: %d", MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG_AP, "Cliente desconectado del ESP. MAC: " MACSTR ", AID: %d, Razon: %d", MAC2STR(event->mac), event->aid, event->reason);
            break;
        }
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI(TAG_AP, "Punto de acceso WiFi detenido");
            break;
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG_STA, "Cliente WiFi iniciado, conectando a la red...");
            esp_err_t err = esp_wifi_connect();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG_STA, "Error al conectar al WiFi. Error %s", esp_err_to_name(err));
            }
            break;
        case WIFI_EVENT_STA_CONNECTED:
        {
            wifi_event_sta_connected_t *event = (wifi_event_sta_connected_t *)event_data;
            ESP_LOGI(TAG_STA, "Conectado a la red. MAC: " MACSTR ", AID: %d", MAC2STR(event->bssid), event->aid);
            esp_connected = 1;
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG_STA, "Desconectado de la red o fallo en conexión. MAC: " MACSTR ", Razon: %d", MAC2STR(event->bssid), event->reason);
            esp_connected = 0;

            sta_disconnected_event_handler(event);
            break;
        }
        case WIFI_EVENT_STA_BEACON_TIMEOUT:
            ESP_LOGW(TAG_STA, "Tiempo de espera de beacon agotado");
            break;
        case WIFI_EVENT_STA_STOP:
            ESP_LOGI(TAG_STA, "Cliente WiFi detenido");
            break;
        default:
            ESP_LOGW(TAG_WIFI, "Evento no manejado en WIFI: %ld", event_id);
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG_STA, "Dirección IP asignada. IP: " IPSTR ", Máscara: " IPSTR ", Gateway: " IPSTR, IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.netmask), IP2STR(&event->ip_info.gw));

            esp_err_t err = esp_netif_set_default_netif(esp_netif_sta);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG_WIFI, "Error al establecer la interfaz de red por defecto. Error %s", esp_err_to_name(err));
            }

            err = esp_netif_napt_enable(esp_netif_ap);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG_WIFI, "Error al habilitar el NAT en la interfaz de red del punto de acceso. Error %s", esp_err_to_name(err));
            }

            ap_set_dns_addr(esp_netif_ap, esp_netif_sta);
            break;
        }
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGW(TAG_STA, "Dirección IP perdida");
            break;
        case IP_EVENT_ASSIGNED_IP_TO_CLIENT:
        {
            ip_event_assigned_ip_to_client_t *event = (ip_event_assigned_ip_to_client_t *)event_data;
            ESP_LOGI(TAG_AP, "Dirección IP asignada al cliente. IP: " IPSTR ", MAC: " MACSTR, IP2STR(&event->ip), MAC2STR(event->mac));
            break;
        }
        default:
            ESP_LOGW(TAG_WIFI, "Evento no manejado en IP: %ld", event_id);
            break;
        }
    }
}

static void sta_disconnected_event_handler(wifi_event_sta_disconnected_t *event)
{
    switch (event->reason)
    {
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        change_sta_authmode_threshold();
        break;
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_CONNECTION_FAIL:
        update_wifi_credentials();
        break;
    case WIFI_REASON_AUTH_EXPIRE:
        ESP_LOGW(TAG_STA, "La autenticación ha expirado");
        wifi_reconnect();
        break;
    case WIFI_REASON_UNSUPP_RSN_IE_VERSION:
        ESP_LOGW(TAG_STA, "Versión de RSN IE no soportada");
        wifi_reconnect();
        break;
    case WIFI_REASON_BEACON_TIMEOUT:
        ESP_LOGW(TAG_STA, "Tiempo de espera de beacon agotado");
        wifi_reconnect();
        break;
    case WIFI_REASON_ASSOC_LEAVE:
        ESP_LOGW(TAG_STA, "El cliente ha dejado la red");
        wifi_reconnect();
        break;
    case WIFI_REASON_AUTH_FAIL:
        ESP_LOGW(TAG_STA, "Fallo en la autenticación");
        wifi_reconnect();
        break;
    default:
        ESP_LOGW(TAG_STA, "Razón de desconexión no manejada: %d", event->reason);
        wifi_reconnect();
        break;
    }
}

static void change_sta_authmode_threshold(void)
{
    if (auth_mode_index < WIFI_AUTH_MAX)
    {
        wifi_config_t wifi_config;
        ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));

        wifi_config.sta.threshold.authmode = (wifi_auth_mode_t)auth_mode_index++;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
    else
    {
        ESP_LOGW(TAG_STA, "Todos los modos de autenticación han sido probados.");
        auth_mode_index = 0; // Reiniciar el índice para futuros intentos
    }
}

static void update_wifi_credentials(void)
{
    wifi_config_t wifi_config;
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));

    get_wifi_credentials(&ssid, &password);
    if (ssid && password)
    {
        strcpy((char *)wifi_config.sta.ssid, ssid);
        strcpy((char *)wifi_config.sta.password, password);
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    }

    wifi_reconnect();
}

static void wifi_reconnect(void)
{
    ESP_ERROR_CHECK(esp_wifi_disconnect());

    ESP_LOGI(TAG_WIFI, "Reconectando al WiFi...");

    esp_err_t err = esp_timer_start_once(reconnect_timer, WIFI_STA_RECONNECT_TIMEOUT * 1000000);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TIMER, "Error al iniciar el timer. Error %s", esp_err_to_name(err));
    }
}

static void ap_set_dns_addr(esp_netif_t *esp_netif_ap, esp_netif_t *esp_netif_sta)
{
    esp_netif_dns_info_t dns;
    esp_netif_get_dns_info(esp_netif_sta, ESP_NETIF_DNS_MAIN, &dns);
    uint8_t dhcps_offer_option = 0x02;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(esp_netif_ap));
}

static void http_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // httpd_handle_t server = (httpd_handle_t)arg;

    if (event_base == ESP_HTTP_SERVER_EVENT)
    {
        switch (event_id)
        {
        case HTTP_SERVER_EVENT_ERROR:
            ESP_LOGE(TAG_HTTP, "Error en el servidor web");
            break;
        case HTTP_SERVER_EVENT_START:
            ESP_LOGI(TAG_HTTP, "Servidor web iniciado");
            break;
        case HTTP_SERVER_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG_HTTP, "Cliente conectado al servidor web");
            break;
        case HTTP_SERVER_EVENT_ON_HEADER:
            ESP_LOGI(TAG_HTTP, "Cabecera recibida");
            break;
        case HTTP_SERVER_EVENT_HEADERS_SENT:
            ESP_LOGI(TAG_HTTP, "Cabeceras enviadas");
            break;
        case HTTP_SERVER_EVENT_ON_DATA:
            ESP_LOGI(TAG_HTTP, "Datos recibidos");
            break;
        case HTTP_SERVER_EVENT_SENT_DATA:
            ESP_LOGI(TAG_HTTP, "Datos enviados");
            break;
        case HTTP_SERVER_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_HTTP, "Cliente desconectado del servidor web");
            break;
        case HTTP_SERVER_EVENT_STOP:
            ESP_LOGW(TAG_HTTP, "Servidor web detenido");
            break;
        default:
            ESP_LOGW(TAG_HTTP, "Evento no manejado: %ld", event_id);
            break;
        }
    }
}

// MARK: MAIN ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void app_main(void)
{
    // Configura los pines GPIO
    digital_pin build_led = {BUILD_LED, GPIO_MODE_OUTPUT, 1};
    configure_digital_pin(&build_led);

    // Inicia el timer
    configure_timer("reconnect_timer", &reconnect_timer, reconnect_cb);

    // Inicia el almacenamiento no volátil
    nvs_start();

    // Inicia el WiFi
    wifi_start();

    // Inicia el servidor HTTP
    configure_http_server();

    // Bucle principal
    while (1)
    {
        if (esp_connected)
        {
            if (build_led.state == 1)
            {
                toggle_pin(&build_led);
            }
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        else
        {
            toggle_pin(&build_led);
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }
}

// MARK: FUNCIONES -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

// Configura un pin GPIO
static void configure_digital_pin(digital_pin *pin)
{
    esp_err_t err = gpio_reset_pin(pin->num);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_GPIO, "Error al reiniciar el pin GPIO %d. Error %s", pin->num, esp_err_to_name(err));
    }

    err = gpio_set_direction(pin->num, pin->mode);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_GPIO, "Error al configurar el pin GPIO %d. Error %s", pin->num, esp_err_to_name(err));
    }
}

static void configure_timer(const char *name, esp_timer_handle_t *timer_handle, esp_timer_cb_t reconnect_timer)
{
    esp_timer_create_args_t config = {
        .callback = reconnect_timer,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = name,
    };

    esp_err_t err = esp_timer_create(&config, timer_handle); // Pasar la dirección de timer_handle
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TIMER, "Error al crear el timer. Error %s", esp_err_to_name(err));
    }
}

static void nvs_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al inicializar el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    ESP_ERROR_CHECK(err);
}

static void wifi_start(void)
{
    // Inicializa el stack de red
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Registra el manejador de eventos de WiFi
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));

    // Inicializa el controlador WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configura el modo WiFi en modo AP-STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Inicia el punto de acceso WiFi
    esp_netif_ap = wifi_ap_start();

    // Inicia el cliente STA
    esp_netif_sta = wifi_sta_start();

    // Inicia el controlador WiFi
    ESP_ERROR_CHECK(esp_wifi_start());
}

esp_netif_t *wifi_ap_start(void)
{
    // Crea una interfaz de red por defecto para el modo AP
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    // Configura la dirección IP del punto de acceso
    esp_netif_ip_info_t ip_info = {
        .ip = {ipaddr_addr(WIFI_AP_IP)},
        .gw = {ipaddr_addr(WIFI_AP_IP)},
        .netmask = {ipaddr_addr("255.255.255.0")},
    };

    // Detiene y reinicia el servidor DHCP
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));

    // Configura el punto de acceso WiFi
    wifi_config_t wifi_config = {
        .ap =
            {
                .ssid = WIFI_AP_SSID,
                .ssid_len = strlen(WIFI_AP_SSID),
                .password = WIFI_AP_PASS,
                .max_connection = WIFI_AP_MAX_STA_CONN,
                .channel = WIFI_AP_CHANNEL,
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
                .authmode = WIFI_AUTH_WPA3_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
#else
                .authmode = WIFI_AUTH_WPA2_PSK,
#endif
                .ftm_responder = 0,
                .pmf_cfg =
                    {
                        .required = 1,
                    },
            },
    };

    if (strlen(WIFI_AP_PASS) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI(TAG_AP, "WiFi AP iniciado. SSID: %s, PASS: %s, CANAL: %d", WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL);

    return esp_netif_ap;
}

esp_netif_t *wifi_sta_start(void)
{
    // Crea una interfaz de red por defecto para el modo STA
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    // Configura el cliente WiFi
    wifi_config_t wifi_config = {
        .sta =
            {
                .ssid = "",
                .password = "",
                .scan_method = WIFI_ALL_CHANNEL_SCAN,
                .failure_retry_cnt = WIFI_STA_MAX_RETRY,
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
                .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
            },
    };

    // Obtiene las credenciales de WiFi del almacenamiento no volátil
    get_wifi_credentials(&ssid, &password);

    // Si no se encuentran credenciales, se usan las predeterminadas
    if (ssid == NULL || password == NULL)
    {
        strncpy((char *)wifi_config.sta.ssid, WIFI_STA_SSID_DEFAULT, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, WIFI_STA_PASS_DEFAULT, sizeof(wifi_config.sta.password) - 1);
    }
    else
    {
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG_STA, "WiFi STA iniciado. SSID: %s, PASS: %s", wifi_config.sta.ssid, wifi_config.sta.password);

    return esp_netif_sta;
}

static void get_wifi_credentials(char **ssid, char **password)
{
    nvs_handle_t nvs_handle_wifi;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle_wifi);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al abrir el almacenamiento no volátil. Error %s", esp_err_to_name(err));
        if (err == ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_LOGW(TAG_NVS, "Creando espacio de almacenamiento no volátil...");
            err = nvs_flash_init_partition(WIFI_NAMESPACE);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG_NVS, "Error al inicializar el espacio de almacenamiento no volátil. Error %s", esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG_NVS, "Espacio de almacenamiento no volátil creado correctamente");
                err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle_wifi);
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG_NVS, "Error al abrir el almacenamiento no volátil después de la creación. Error %s", esp_err_to_name(err));
                }
            }
        }
    }

    size_t ssid_len = 0;
    err = nvs_get_str(nvs_handle_wifi, "ssid", NULL, &ssid_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) // Si no se encuentra la clave, no se muestra el error
    {
        ESP_LOGE(TAG_NVS, "Error al obtener la longitud del SSID del almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    size_t password_len = 0;
    err = nvs_get_str(nvs_handle_wifi, "password", NULL, &password_len);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGE(TAG_NVS, "Error al obtener la longitud de la contraseña del almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    if (ssid_len > 0 && password_len > 0)
    {
        *ssid = malloc(ssid_len);
        *password = malloc(password_len);

        err = nvs_get_str(nvs_handle_wifi, "ssid", *ssid, &ssid_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG_NVS, "Error al obtener el SSID del almacenamiento no volátil. Error %s", esp_err_to_name(err));
        }

        err = nvs_get_str(nvs_handle_wifi, "password", *password, &password_len);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG_NVS, "Error al obtener la contraseña del almacenamiento no volátil. Error %s", esp_err_to_name(err));
        }
    }
    else
    {
        ESP_LOGW(TAG_NVS, "No se encontraron credenciales de WiFi en el almacenamiento no volátil");
    }

    nvs_close(nvs_handle_wifi);
}

static void toggle_pin(digital_pin *pin)
{
    if (pin->mode == GPIO_MODE_OUTPUT)
    {
        pin->state = !pin->state;
        esp_err_t err = gpio_set_level(pin->num, pin->state);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG_GPIO, "Error al cambiar el estado del pin GPIO %d. Error %s", pin->num, esp_err_to_name(err));
        }
    }
    else
    {
        ESP_LOGW(TAG_GPIO, "El pin GPIO %d no está configurado como salida", pin->num);
    }
}

// MARK: HTTP SERVER -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------

static void configure_http_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    ESP_LOGI(TAG_HTTP, "Iniciando servidor web en puerto: %d", config.server_port);

    // Registra el manejador de eventos del servidor HTTP
    esp_event_handler_register(ESP_HTTP_SERVER_EVENT, ESP_EVENT_ANY_ID, &http_event_handler, server_handle);

    // Inicia el servidor HTTP con las URL y manejadores de eventos
    if (httpd_start(&server_handle, &config) == ESP_OK)
    {
        // Pagina principal
        httpd_uri_t uri_main = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = main_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server_handle, &uri_main);

        // Favicon
        httpd_uri_t uri_favicon = {
            .uri = "/favicon.ico",
            .method = HTTP_GET,
            .handler = favicon_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server_handle, &uri_favicon);

        httpd_uri_t uri_post = {
            .uri = "/",
            .method = HTTP_POST,
            .handler = post_handler,
            .user_ctx = NULL,
        };
        httpd_register_uri_handler(server_handle, &uri_post);
    }
    else
    {
        ESP_LOGE(TAG_HTTP, "Error al iniciar el servidor web");
    }
}

static esp_err_t main_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");         // No almacenar en caché
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff"); // No permitir la interpretación de MIME
    httpd_resp_set_type(req, "text/html; charset=utf-8");         // Tipo de contenido
    httpd_resp_send(req, (const char *)main_html_start, main_html_end - main_html_start);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=31536000, immutable"); // Almacenar en caché por un año, no modificar
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)router_ico_start, router_ico_end - router_ico_start);
    return ESP_OK;
}

static esp_err_t post_handler(httpd_req_t *req)
{
    char buf[100];
    int ret, remaining = req->content_len;

    while (remaining > 0)
    {
        if ((ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf) - 1))) <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        remaining -= ret;
        buf[ret] = '\0'; // Asegurarse de que el buffer esté terminado con un carácter nulo
    }

    char decoded_buf[100];
    url_decode(decoded_buf, buf);

    printf("Datos recibidos: %s\n", decoded_buf);

    // Parsear los datos recibidos para extraer ssid y password
    char ssid[32] = {0};
    char password[64] = {0};
    sscanf(decoded_buf, "ssid=%31[^&]&password=%63s", ssid, password);

    // Guardar las credenciales de WiFi en la memoria no volátil
    save_wifi_credentials(ssid, password);

    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, (const char *)main_html_start, main_html_end - main_html_start);
    return ESP_OK;
}

// Función para decodificar URL
static void url_decode(char *dst, const char *src)
{
    char a, b;
    while (*src)
    {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) && (isxdigit(a) && isxdigit(b)))
        {
            if (a >= 'a')
                a -= 'a' - 'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a')
                b -= 'a' - 'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        }
        else if (*src == '+')
        {
            *dst++ = ' ';
            src++;
        }
        else
        {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void save_wifi_credentials(char *ssid, char *password)
{
    nvs_handle_t nvs_handle_wifi;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &nvs_handle_wifi);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al abrir el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    err = nvs_set_str(nvs_handle_wifi, "ssid", ssid);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al guardar el SSID en el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    err = nvs_set_str(nvs_handle_wifi, "password", password);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al guardar la contraseña en el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    err = nvs_commit(nvs_handle_wifi);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_NVS, "Error al confirmar los cambios en el almacenamiento no volátil. Error %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle_wifi);
}