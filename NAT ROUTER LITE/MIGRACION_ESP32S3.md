# Migración de NAT ROUTER LITE a ESP32-S3

Comparación de `sdkconfig.old` (ESP32) con `sdkconfig` (ESP32-S3), usando el ESP-IDF 6.1 instalado. El archivo antiguo conserva `CONFIG_IDF_INIT_VERSION="6.0.0"`; algunas diferencias provienen de nuevos valores predeterminados de IDF, y no se puede atribuir cada una a un cambio manual.

## Configuración necesaria

Desde una terminal de ESP-IDF ejecuta `idf.py reconfigure` y después `idf.py menuconfig`. El target actual ya es `esp32s3`; no hace falta ejecutar `set-target` otra vez.

En **Component config → LWIP**, comprueba estas opciones y actívalas si hace falta:

| Opción | Símbolo | Antiguo | Actual antes de esta migración |
|---|---|---|---|
| Enable IP forwarding | `CONFIG_LWIP_IP_FORWARD` | `y` | `n` |
| Enable NAT | `CONFIG_LWIP_IPV4_NAPT` | `y` | No aparece mientras forwarding esté desactivado |
| Enable NAT Port Mapping | `CONFIG_LWIP_IPV4_NAPT_PORTMAP` | `y` | No aparece mientras NAT esté desactivado |

Las dos primeras son indispensables para compartir Internet. Port Mapping es opcional: mantiene la capacidad del archivo antiguo, pero esta aplicación todavía no crea reglas para reenviar puertos. Las opciones aparecen en ese orden según se activan sus dependencias.

`sdkconfig.defaults` incorpora estas tres preferencias. ESP-IDF 6.1 distingue opciones predeterminadas mediante comentarios `# default:` en `sdkconfig`: al reconfigurar, los nuevos valores predeterminados pueden aplicarse a esas opciones aunque ya aparezcan en el archivo. Las selecciones explícitas guardadas mediante `menuconfig` se conservan, sujetas a las dependencias de Kconfig. En la copia usada para verificar este proyecto, forwarding estaba marcado como predeterminado y las tres opciones quedaron activadas al cargar `sdkconfig.defaults`.

No se editaron los archivos originales `sdkconfig` ni `sdkconfig.old` durante esta migración; tu próxima reconfiguración sí podrá actualizar `sdkconfig` con los valores resueltos por IDF. Después de reconfigurar, confirma que los tres símbolos anteriores tengan `=y`, tanto en `menuconfig` como en el archivo generado. No copies `sdkconfig.old` completo sobre el archivo S3.

El registro de esta placa identifica una flash física de **16384 KB (16 MB)**, mientras la cabecera de la imagen está configurada para **2048 KB (2 MB)**. En **Serial flasher config → Flash size**, selecciona **16 MB** (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`). Esto corrige el tamaño declarado; no amplía por sí solo las particiones existentes. Se conserva la selección manual del usuario: esta migración no edita `sdkconfig`.

## LED y funciones del ESP32-S3

En el nuevo menú **NAT Router** puedes seleccionar el LED de estado y su GPIO:

- ESP32-S3: LED RGB direccionable controlado por RMT. Verde cuando la estación tiene IPv4 y ámbar intermitente mientras busca conexión.
- DevKitC-1 original/v1.0: GPIO48, valor predeterminado. DevKitC-1 v1.1: selecciona **GPIO38**. Comprueba la revisión de tu placa; Espressif documenta [GPIO48 para v1.0](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html) y [GPIO38 para v1.1](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).
- ESP32 original: LED sencillo por GPIO2, configurable según la placa.
- También puedes seleccionar la opción sin LED si tu placa no incorpora uno compatible.

Para ofrecer **FTM responder** en el SoftAP del S3, activa **Component config → Wi-Fi → WiFi FTM → FTM Responder support**, y después la opción correspondiente de **NAT Router**:

```ini
CONFIG_ESP_WIFI_FTM_ENABLE=y
CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT=y
CONFIG_NAT_ROUTER_FTM_RESPONDER=y
```

FTM es opcional y permanece desactivado por defecto. Permite que un cliente compatible solicite mediciones RTT al punto de acceso; el router no inicia mediciones ni presenta distancias.

La consola USB Serial/JTAG secundaria **ya estaba activada** en `sdkconfig` (`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y`). Puedes ver los mensajes mediante el conector USB nativo del S3. Para entrada o REPL, selecciona USB Serial/JTAG como consola principal en **Component config → ESP-STDIO → Channel for console output**. Consulta la [guía oficial de USB Serial/JTAG](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/usb-serial-jtag-console.html).

PSRAM sigue desactivada: el tamaño de flash no demuestra que el módulo tenga PSRAM. Confirma la referencia exacta del módulo y el tipo de memoria antes de activarla.

## Otras diferencias de configuración

| Símbolo o grupo | `sdkconfig.old` → `sdkconfig` | Qué hacer |
|---|---|---|
| `CONFIG_FREERTOS_IN_IRAM` | `y` → `n` | Opcional: **Component config → FreeRTOS → Port → Place FreeRTOS functions in IRAM**. Mejora operaciones del RTOS a cambio de IRAM; no es requisito para NAT. |
| `CONFIG_LOG_COLORS` | `y` → `n` | Opcional: **Component config → Log → Format → Color**. |
| `CONFIG_BOOTLOADER_LOG_COLORS` | `y` → `n` | Opcional, solo cambia la presentación de los mensajes de arranque. |
| `CONFIG_VFS_SUPPORT_TERMIOS` | `y` → `n` | No requerido por la aplicación. Se puede reactivar en **Component config → Virtual file system → Provide termios.h functions** si se necesita una consola que lo utilice. |
| `CONFIG_PM_SLEEP_FUNC_IN_IRAM`, `CONFIG_PM_SLP_IRAM_OPT` | `y` → `n` | Opcionales para optimizar el reposo a costa de IRAM. Power Management permanece desactivado en ambos archivos. |
| `CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS` | `y` → `n` | Conservar `n` y corregir las advertencias del código. |
| `CONFIG_COMPILER_ORPHAN_SECTIONS_*` | Advertencia → error | Conservar la comprobación estricta actual del enlazador. |
| `CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE` | `y` → `n` | No hace falta restaurarlo; las aserciones siguen habilitadas en ambas configuraciones. |
| `CONFIG_MBEDTLS_COMPILER_OPTIMIZATION_*` | Sin optimización → tamaño | Conservar la optimización actual; no se necesita desactivarla para este router. |
| `CONFIG_MBEDTLS_ARIA_C` | `y` → `n` | No requerido por esta aplicación. |
| `CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE` | `y` → `n` | No requerido; desactivado reduce el uso de memoria. |
| `CONFIG_MBEDTLS_ECP_FIXED_POINT_OPTIM` | `y` → `n` | Opcional si se necesita acelerar criptografía ECP, a cambio de más flash. |
| `CONFIG_MBEDTLS_THREADING_C` | `n` → `y` | Conservar el valor actual; protege contextos criptográficos usados entre hilos. |
| `CONFIG_LIBC_NEWLIB` / `CONFIG_LIBC_PICOLIBC` | Newlib → Picolibc | Conservar la biblioteca predeterminada de IDF actual; se añaden al código sus inclusiones C explícitas. |
| `CONFIG_ESPTOOLPY_FLASHFREQ` | 40 MHz → 80 MHz | Conservar la frecuencia actual compatible con el S3. Es independiente de corregir la capacidad a 16 MB. |

El offset del bootloader (`0x1000` → `0x0`), el umbral de brownout, el tamaño de pila IPC, los temporizadores y los símbolos SOC/eFuse/revisión cambian con el chip: conserva los del ESP32-S3. No se deben restaurar desde el ESP32 original.

WPA3/SAE, el servidor DHCP, la CPU a 160 MHz y el tick FreeRTOS de 100 Hz ya estaban conservados. WPA3 no es una incorporación exclusiva del S3 en este proyecto.

## Cambios de funcionamiento

La conexión inicial STA se solicita después de que `esp_wifi_start()` retorna correctamente. Así se evita el error `ESP_ERR_WIFI_NOT_STARTED` observado al conectar desde `WIFI_EVENT_STA_START` durante el arranque del modo AP+STA. El estado conectado del LED requiere obtener IPv4, no solo asociarse al punto de acceso.

Se programan reintentos también cuando no se encuentra el AP de origen. La STA conserva su umbral mínimo WPA2 y puede conectarse con WPA3 cuando la red lo permita; se elimina el recorrido por valores de la enumeración de autenticación, que podía elegir modos no apropiados. Las credenciales nuevas se aplican mediante la tarea de eventos y se reintentan si el controlador está ocupado durante una transición.

El servidor web acumula correctamente un cuerpo HTTP recibido en varios fragmentos y decodifica cada campo del formulario por separado. Así, caracteres como `&`, `+` o `=` codificados dentro del SSID o de la contraseña no se confunden con separadores. Valida los tamaños y el formato de la clave WPA antes de guardar. Si NVS no puede abrirse, escribir o confirmar los datos, devuelve HTTP 500; los mensajes de diagnóstico no imprimen contraseñas ni el formulario recibido.

## Verificación

Compilaciones verificadas con ESP-IDF 6.1 en directorios aislados y copias de configuración:

| Target | Resultado | NAT | Tamaño del binario | Espacio libre en partición app de 1 MB |
|---|---|---|---|---|
| ESP32 | Correcto, incluida pasada incremental final | Forwarding, NAPT y port mapping activados | `0xe39a0` bytes | 11 % |
| ESP32-S3 | Correcto, incluida pasada incremental final | Forwarding, NAPT y port mapping activados | `0xd4790` bytes | 17 % |
| ESP32-S3 con FTM y flash declarada de 16 MB | Correcto | Forwarding, NAPT y port mapping activados | `0xd7240` bytes | 16 % |

Logs en `build/validation-esp32/build-final.log` y `build/validation-s3/build-final.log`. Estas compilaciones verifican el código y el enlace; todavía no se ha programado ni probado la placa con estos cambios.

La variante con FTM se verificó con `CONFIG_ESP_WIFI_FTM_ENABLE=y`, `CONFIG_ESP_WIFI_FTM_RESPONDER_SUPPORT=y`, `CONFIG_NAT_ROUTER_FTM_RESPONDER=y` y `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` en una copia aislada. Log: `build/validation-s3/build-features.log`. La imagen cabe en la partición actual de 1 MB.

Las pruebas del parser del formulario (`tests/test_wifi_form.c`) pasaron con MSVC C11 y `/W4 /WX`: límites de SSID/contraseña, UTF-8, delimitadores codificados, claves duplicadas y escapes inválidos. El módulo LED también pasó comprobaciones de sintaxis con GCC para RGB, GPIO activo bajo, GPIO activo alto y LED desactivado.

`main.c` pasó además la comprobación de sintaxis con GCC para el S3 sin NAT ni FTM, usando una cabecera de configuración aislada; esta variante permite acceder al formulario mientras se habilita el soporte de router. Log: `build/test-config-no-nat/syntax.log`.

Tras guardar `menuconfig`, compila y programa desde la terminal de ESP-IDF:

```powershell
idf.py build
idf.py -p COM3 flash monitor
```

Usa el puerto COM que corresponda a tu placa y al conector elegido.

Pruebas en hardware pendientes:

1. Reinicia: comprueba que no aparezca `ESP_ERR_WIFI_NOT_STARTED`, y que la flash declarada corresponda a 16 MB tras recompilar y programar.
2. Conecta un teléfono o PC al AP `ESP32-NAT` con la contraseña configurada (`12345678` en el código actual). Abre `http://192.168.2.1`.
3. Guarda el SSID y la contraseña del router de origen. Comprueba que la STA se reconecte, obtenga IPv4 y habilite NAT; en el S3 el LED debe pasar a verde.
4. Desde el cliente del AP, comprueba acceso a Internet y resolución de nombres. Reinicia el S3 para confirmar que las credenciales se guardaron en NVS.
5. Apaga temporalmente el AP de origen: comprueba los reintentos, el LED ámbar y que la página de configuración siga disponible. Vuelve a encenderlo y verifica la recuperación.
6. Si activas FTM, prueba las solicitudes RTT con un cliente que admita FTM initiator.

## Confirmación y DNS para clientes conectados antes que el módem

Después de guardar las credenciales, el POST redirige con HTTP 303 a `/saved`. La página confirma el guardado y consulta `/api/status` para distinguir entre conexión pendiente, IPv4 obtenida y router preparado. No muestra la contraseña ni afirma haber comprobado Internet desde fuera del equipo.

En el registro de la prueba, el teléfono recibió su lease a los 59 segundos y la STA obtuvo IPv4 a los 147 segundos. DHCP ya había anunciado `192.168.2.1` como DNS, pero el ESP no tenía un servicio que respondiera allí. Cambiar el DNS y reiniciar DHCP después no actualiza el lease que conserva el teléfono. Este comportamiento del DNS predeterminado está documentado en la [migración de redes de ESP-IDF 6](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/migration-guides/release-6.x/6.0/networking.html#dhcp-server-dns-option-behavior).

El AP ahora anuncia su propia IP como DNS desde el arranque y un proxy UDP reenvía las consultas al DNS IPv4 del módem. Al perder la STA, devuelve un error temporal; al recuperar IPv4, actualiza el destino sin cambiar DHCP ni exigir un lease nuevo al teléfono. Si el módem no entrega ningún DNS IPv4 válido, se usa `1.1.1.1` como respaldo y se registra el aviso. El servidor HTTP limita sus conexiones simultáneas para reservar los sockets usados por DNS.

El proxy usa dos sockets y admite seis consultas simultáneas, con identificadores independientes para cada cliente. Trabaja con DNS por UDP hasta 4096 bytes; señala explícitamente las respuestas mayores como truncadas y no implementa DNS por TCP. Las consultas sin conexión o sin respuesta en dos segundos reciben SERVFAIL, sin inventar respuestas de dominio inexistente.

Las compilaciones de esta corrección pasaron con ESP-IDF 6.1: ESP32-S3 `0xd8e00` bytes (15 % libre en la partición de 1 MB) y ESP32 `0xe6f20` bytes (10 % libre). Logs: `build/validation-s3/build-dns.log` y `build/validation-esp32/build-dns.log`. La página pasó las comprobaciones de sintaxis JavaScript y de sus estados de espera, conexión, DNS pendiente, conflicto y pérdida de conexión.

El proxy pasó siete pruebas de tráfico UDP real en el host: SERVFAIL sin conexión, conservación de la respuesta y del ID original, consultas simultáneas con el mismo ID, cambio de upstream, rechazo de IDs ajenos, timeout y pérdida de red. El adaptador de prueba usa el puerto local 1053, porque el entorno de pruebas filtra el 53; el firmware conserva UDP/53. Estas pruebas comprueban el protocolo y la gestión de clientes, sin simular la radio Wi-Fi ni verificar la salida del teléfono a Internet. Los adaptadores e instrucciones están en `tests/support/dns_host` y el escenario en `tests/test_dns_proxy_host.py`.

Después de programar estos cambios, vuelve a conectarte a `ESP32-NAT`, envía el formulario y comprueba la página de confirmación. Prueba Google y YouTube desde ese mismo cliente. Si necesitas aislar un fallo, consulta desde un PC conectado al AP: `nslookup google.com 192.168.2.1`. La respuesta debe llegar a través del proxy; el log muestra qué DNS del módem se eligió. Las pruebas en placa siguen siendo necesarias para confirmar el acceso real a Internet.

## Diagnóstico de errores al conectar al módem

La página muestra el motivo recibido del controlador en lugar de mantener únicamente el aviso de espera: verificación de contraseña agotada, autenticación fallida, red no encontrada, seguridad incompatible, señal insuficiente, asociación rechazada o pérdida de conexión. `/api/status` publica `wifi_reason` y `connection_error` para distinguirlos sin enviar contraseñas al navegador.

En la prueba con contraseña incorrecta, el controlador informó `WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT` (15). La página muestra «No se pudo validar la contraseña» y explica que la contraseña puede ser incorrecta. El código identifica un fallo al verificar la clave; no permite atribuir todos los casos de timeout exclusivamente a una contraseña incorrecta. Consulta la [tabla oficial de motivos Wi-Fi](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/wifi-driver/station-scenarios.html#wi-fi-reason-code-related-to-wrong-password).

El diagnóstico se conserva durante los reintentos y se limpia al cambiar las credenciales o completar la autenticación. Las desconexiones locales 8/36 provocadas por el cambio de credenciales no se presentan como un fallo de la nueva clave. El aviso depende de cuándo el controlador publique el evento: en la prueba llegó unos 25 segundos después de guardar, tras los reintentos internos.

La prueba de regresión `tests/test_connection_status_page.mjs` verifica la secuencia clave incorrecta → error persistente → credenciales corregidas → espera nueva → conexión correcta, además de las otras causas y la validación de los datos. Pasó con el JavaScript real de la página. Las compilaciones finales pasaron para ESP32-S3 (`0xd9cf0`, 15 % libre) y ESP32 (`0xe7e80`, 9 % libre), ambas en la partición de 1 MB. Logs: `build/validation-s3/build-auth.log` y `build/validation-esp32/build-auth.log`. Queda comprobar los nuevos avisos en la placa.
