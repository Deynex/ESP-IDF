# Componentes del router

`main/main.c` solo inicia el LED, NVS, Wi-Fi y el servidor, y actualiza el indicador cada 500 ms.

| Componente | Responsabilidad | Dependencias del proyecto |
|---|---|---|
| `nvs` | Inicialización y lectura/guardado de las credenciales | Ninguna |
| `wifi` | AP+STA, DHCP, reconexión, NAT, estado y diagnóstico de conexión; parser del formulario | `nvs`, `dns_proxy` |
| `server` | HTTP, HTML y favicon embebidos, formulario y API de estado | `wifi` |
| `dns_proxy` | Reenvío de consultas DNS para los clientes del AP | Ninguna |
| `status_led` | LED convencional o RGB mediante RMT | Ninguna |

Cada componente declara sus dependencias de ESP-IDF en su `CMakeLists.txt`. Sus encabezados públicos están en `include`; `wifi_internal.h` queda privado al componente Wi-Fi.

## Wi-Fi

- `wifi.c`: eventos, estado, temporizador de reconexión, NAT y selección del DNS del módem.
- `ap.c`: configuración del AP y DHCP, incluido el DNS estable del AP desde el arranque.
- `sta.c`: configuración de la estación, aplicación de credenciales y clasificación de errores.
- `wifi_form.c`: decodificación y validación del formulario; su API pública es `wifi_form_parse()`.
- `wifi_internal.h`: constantes del AP/STA, incluida `WIFI_AP_IP`, y funciones internas.

La API de `include/wifi.h` permite iniciar Wi-Fi, leer su estado y guardar/aplicar credenciales. El servidor usa esta API y no accede a las variables de Wi-Fi ni a NVS directamente. Guardar y aplicar siguen separados para conservar las respuestas HTTP de error de cada etapa.

## Persistencia, servidor e indicadores

La API de almacenamiento está en `nvs/include/router_nvs.h`. Se usa ese nombre para evitar que un `nvs.h` del proyecto oculte al encabezado del SDK. Se conservan el namespace `wifi`, las claves `ssid`/`password` y los tamaños de los datos, por lo que las credenciales ya guardadas siguen siendo válidas.

`server/server.c` conserva las rutas `GET /`, `POST /`, `GET /saved`, `GET /api/status` y `GET /favicon.ico`. Su CMake embebe los recursos de la carpeta raíz `app`; los HTML y el icono no se duplican en `main`.

DNS y LED conservan su código y sus interfaces anteriores, ahora como componentes independientes. DNS mantiene su listener en la IP del AP antes de que pueda conectarse el primer cliente; el LED recibe únicamente el estado de conexión.

El menú **NAT Router** sigue definido por `main/Kconfig.projbuild`, que incluye los fragmentos `status_led/Kconfig.options` y `wifi/Kconfig.options`. Los símbolos de configuración existentes se conservan.

La configuración común y los perfiles estándar para ESP32/ESP32-S3 están descritos en [CONFIGURACION.md](../CONFIGURACION.md).

## Verificación

Las pruebas de host y sus comandos están en [tests/README.md](../tests/README.md). La compilación completa comprueba además las dependencias, la resolución del encabezado NVS del SDK y el enlace de los recursos web.

Después de programar la placa, las comprobaciones en hardware son: arranque con credenciales guardadas, primer guardado desde NVS vacía, error de SSID/contraseña y recuperación con datos correctos, navegación a través del AP y reconexión al reiniciar el módem.
