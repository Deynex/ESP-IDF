# NAT Router Lite

Router Wi-Fi IPv4 para **ESP32 y ESP32-S3**, desarrollado con **ESP-IDF 6.1**. El dispositivo crea un punto de acceso (AP) para los clientes y se conecta como estación (STA) al módem. Comparte esa conexión mediante NAPT y ofrece una página local para configurar las credenciales y consultar el estado.

La aplicación incluye almacenamiento de credenciales en NVS, reconexión automática, diagnóstico de errores Wi-Fi, proxy DNS e indicador LED. Las páginas se incorporan al firmware: no hace falta una tarjeta SD ni un sistema de archivos adicional.

## Requisitos

- ESP32 o ESP32-S3 compatible con ESP-IDF 6.1, cable USB y alimentación adecuada.
- ESP-IDF 6.1 instalado y una terminal con su entorno activado; también puede usarse la extensión ESP-IDF de VS Code.
- Una red Wi-Fi de **2,4 GHz** con seguridad compatible con la configuración WPA2/WPA3 del firmware.
- Un teléfono o equipo con navegador y JavaScript para configurar la conexión y ver sus actualizaciones automáticas.

Los perfiles incluidos describen las placas usadas por el proyecto, no todos los módulos de cada familia:

| Perfil | Flash declarada | Indicador predeterminado |
|---|---|---|
| `esp32` | 2 MB, DIO, 40 MHz | LED convencional en GPIO2, activo bajo |
| `esp32s3` | 16 MB, DIO, 80 MHz | LED RGB en GPIO48 |

En el **[ESP32-S3-DevKitC-1 v1.1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)**, el LED RGB corresponde a **GPIO38**; ajústalo en `menuconfig`. Confirma la revisión de tu placa, la flash física y el tipo de LED antes de programarla. La capacidad de PSRAM tampoco se deduce únicamente del nombre del chip.

## Compilar y programar

Abre una terminal de ESP-IDF en la raíz del repositorio. Selecciona **uno** de los targets:

```powershell
# Para ESP32:
idf.py set-target esp32

# O, para ESP32-S3:
idf.py set-target esp32s3
```

`set-target` limpia el directorio de compilación seleccionado y regenera la configuración del nuevo chip. Si la terminal tiene definida la variable `IDF_TARGET`, debe coincidir con la selección. Después:

```powershell
idf.py menuconfig
idf.py build
idf.py -p COM3 flash monitor
```

`COM3` es un ejemplo: sustitúyelo por el puerto de tu placa. En `menuconfig`, revisa la flash en **Serial flasher config** y el indicador en **NAT Router**. Para salir del monitor, usa `Ctrl+]`.

La configuración se organiza así:

| Archivo | Uso |
|---|---|
| `sdkconfig.defaults` | Opciones comunes: forwarding IPv4, NAPT y soporte de port mapping |
| `sdkconfig.defaults.esp32` | Preferencias de la placa ESP32 |
| `sdkconfig.defaults.esp32s3` | Preferencias de la placa ESP32-S3 |
| `sdkconfig` | Configuración activa completa, generada por ESP-IDF |
| `sdkconfig.old` | Respaldo automático de una configuración anterior |

ESP-IDF carga el archivo común y después el sufijo del target seleccionado. **`.old` no identifica al ESP32-S3**; comprueba `CONFIG_IDF_TARGET` dentro del archivo. No copies un `sdkconfig` completo entre chips para elegir la placa.

Consulta [Configuración para ESP32 y ESP32-S3](CONFIGURACION.md) para conservar cambios, entender la prioridad de los defaults y mantener compilaciones separadas por target.

## Primer uso

1. Enciende la placa y conéctate a la red **`ESP32-NAT`**, con contraseña **`12345678`**.
2. Si el teléfono avisa que esa red todavía no tiene Internet, elige permanecer conectado. Esa conexión permite acceder a la configuración local.
3. Abre **[http://192.168.2.1](http://192.168.2.1)** en el navegador.
4. Introduce el SSID y la contraseña de la red del módem. El botón del ojo permite mostrar u ocultar la contraseña.
5. Envía el formulario. La página **«Credenciales recibidas»** confirma que los datos se guardaron y muestra el intento de conexión.
6. Cuando aparezca **«Conexión al módem establecida»**, prueba la navegación desde el dispositivo que sigue conectado a `ESP32-NAT`.

El formulario admite un SSID de **1 a 32 bytes** y una contraseña WPA de **8 a 63 bytes**, o una PSK de **64 caracteres hexadecimales**. En UTF-8, un carácter puede ocupar varios bytes. Este formulario requiere contraseña y no configura redes abiertas.

El estado se consulta automáticamente cada **2 segundos** mientras se establece la conexión. Una animación indica la espera; desaparece cuando se confirma la conexión o se muestra un error. Una vez listo, las consultas continúan cada **10 segundos** para detectar cambios. No hay que pulsar un botón de actualización.

El mensaje de conexión confirma que el ESP obtuvo una dirección IPv4 y que pudo preparar NAPT y DNS. **No realiza una prueba de acceso a Internet**: el módem puede estar conectado por Wi-Fi y, aun así, tener su servicio de Internet caído.

### Cambiar datos y reiniciar

Usa **«Cambiar credenciales»** para volver al formulario. Puedes corregir una red inexistente o una contraseña incorrecta sin borrar NVS ni reiniciar el dispositivo.

Las credenciales se guardan en NVS antes de confirmar su recepción. En el siguiente arranque, el ESP las recupera e intenta conectarse automáticamente. No es necesario volver a introducirlas después de apagar y encender la placa.

Si cerraste la confirmación, puedes volver a **[http://192.168.2.1/saved](http://192.168.2.1/saved)**. Ante una pérdida temporal de la conexión con el módem, el firmware vuelve a intentarlo y conserva el diagnóstico para que pueda consultarse.

### Indicador LED

| Placa/configuración | Con dirección IPv4 del módem | Sin conexión IPv4 |
|---|---|---|
| LED RGB del perfil ESP32-S3 | Verde fijo | Ámbar intermitente |
| LED convencional del perfil ESP32 | Encendido fijo | Intermitente |

El indicador se actualiza cada 500 ms. El color verde o el LED fijo representan el estado IPv4 de la estación; no verifican el acceso a Google, YouTube u otro servicio externo.

## Red local y DNS

El AP usa **`192.168.2.1/24`** como dirección y puerta de enlace. Su servidor DHCP también entrega esa dirección como DNS desde el arranque. El proxy local reenvía las consultas al DNS obtenido del módem; si no recibe una dirección IPv4 válida de DNS, usa `1.1.1.1`.

Así, un teléfono conectado antes de configurar el módem conserva una dirección DNS válida cuando llega la conexión externa. En el flujo normal no necesita apagar y encender su Wi-Fi para renovar ese dato.

La red del módem debe pertenecer a otra subred. Por ejemplo, `192.168.1.0/24` para el módem y `192.168.2.0/24` para el AP. Si ambas se superponen, la página muestra **«Las redes coinciden»**. El firmware detecta el conflicto, pero no cambia automáticamente la dirección del AP. Sus constantes están en [wifi_internal.h](components/wifi/wifi_internal.h).

## Diagnóstico

La página traduce las causas reportadas por Wi-Fi a mensajes sobre red no encontrada, autenticación, seguridad incompatible, señal insuficiente, asociación rechazada o pérdida de conexión. Algunos códigos describen un síntoma y no permiten demostrar una única causa.

| Situación | Qué comprobar |
|---|---|
| No abre `192.168.2.1` | Confirma que el teléfono sigue conectado a `ESP32-NAT`; abre la URL con `http://` y acepta permanecer en la red sin Internet. |
| «Red Wi-Fi no encontrada», razón 201 | Revisa el SSID exacto, la disponibilidad de 2,4 GHz, la distancia y que el módem esté encendido. |
| «No se pudo validar la contraseña», razón 15 o 204 | Revisa la clave y la seguridad del módem. Es un timeout del intercambio de autenticación; una clave incorrecta es una causa posible, no una prueba concluyente. |
| Conectado al módem, pero no navega | Consulta `/api/status`: verifica `nat_enabled`, `dns_ready` y `subnet_conflict`; comprueba también que el módem tenga Internet. |
| Una IP externa responde, pero los nombres no | Revisa el DNS del módem y sus restricciones de salida. `dns_ready` indica que el proxy está preparado, no que el DNS remoto haya respondido. |
| «Las redes coinciden» | Separa las subredes del módem y del AP; modifica las constantes del AP y vuelve a compilar si cambias su dirección. |
| El LED no enciende o usa otro pin | Comprueba el tipo de LED, la revisión de placa y el GPIO en `menuconfig`. |
| El arranque declara menos flash que la detectada | Ajusta el tamaño al módulo físico y regenera/programa el firmware con ese perfil. |

**Borrar NVS no forma parte del uso habitual.** Para cambiar las claves basta el formulario. Si necesitas una prueba completamente limpia, `idf.py -p COM3 erase-flash` borra toda la flash, incluidas las credenciales; después debes volver a programar el firmware. La inicialización también recupera NVS automáticamente, mediante borrado, si ESP-IDF detecta falta de páginas libres o una versión incompatible de NVS.

## Interfaz HTTP

Las rutas se sirven en el puerto 80:

| Método y ruta | Función |
|---|---|
| `GET /` | Formulario de credenciales |
| `POST /` | Valida, guarda y aplica los campos `ssid` y `password` |
| `GET /saved` | Confirmación y estado automático |
| `GET /api/status` | Estado actual en JSON |
| `GET /favicon.ico` | Icono de la aplicación |

Un envío correcto responde con una redirección **303** hacia `/saved`, de modo que actualizar la confirmación no vuelve a enviar la contraseña. Los formularios inválidos reciben 400; un timeout al recibir el cuerpo, 408; un fallo al guardar o aplicar los datos, 500.

Ejemplo de respuesta de estado:

```json
{
  "credentials_saved": true,
  "connected": true,
  "nat_enabled": true,
  "dns_ready": true,
  "subnet_conflict": false,
  "wifi_reason": 0,
  "connection_error": "none"
}
```

`connected` significa que la estación tiene IPv4; `nat_enabled`, que se habilitó NAPT; y `dns_ready`, que el proxy tiene preparado su socket hacia el DNS. Los últimos dos campos aportan la razón Wi-Fi numérica y su clasificación para la página. La API no devuelve la contraseña.

## Organización del código

| Ruta | Contenido |
|---|---|
| [`main/`](main/) | Arranque y actualización periódica del indicador |
| [`app/`](app/) | Formulario, confirmación y favicon embebidos |
| [`components/wifi/`](components/wifi/) | AP+STA, DHCP, NAPT, reconexión, diagnósticos y parser del formulario |
| [`components/nvs/`](components/nvs/) | Inicialización y persistencia de credenciales |
| [`components/server/`](components/server/) | Servidor HTTP y recursos de la aplicación |
| [`components/dns_proxy/`](components/dns_proxy/) | Proxy DNS UDP de los clientes del AP |
| [`components/status_led/`](components/status_led/) | Indicador GPIO o RGB mediante RMT |
| [`tests/`](tests/) | Pruebas de host y sus instrucciones |

La [guía de componentes](components/README.md) describe las interfaces y dependencias. `main.c` coordina el inicio; la lógica de red, almacenamiento e interfaz reside en sus componentes.

## Alcance actual

- Hasta **3 clientes** simultáneos en el AP; AP y STA comparten la radio y el canal Wi-Fi.
- Reenvío mediante **IPv4/NAPT**; no implementa enrutamiento IPv6.
- Proxy DNS **UDP**, con mensajes de hasta **4096 bytes**, seis consultas pendientes y timeout de dos segundos. No tiene transporte DNS por TCP, caché propia ni conmutación automática de servidor por timeout.
- El soporte de port mapping está habilitado en la configuración, pero la aplicación no crea reglas de publicación de puertos ni incluye un formulario para ellas.
- La administración usa **HTTP sin autenticación propia**: los equipos con acceso a la interfaz HTTP pueden cambiar las credenciales del módem. El servidor no está limitado a la dirección del AP; según la conectividad de la red, también puede ser accesible por la IP STA desde la red del módem. Los perfiles no habilitan cifrado de NVS o de flash.
- No incluye actualización OTA. Los cambios de firmware se programan mediante las herramientas de ESP-IDF.

La seguridad del enlace Wi-Fi y el acceso a la página local son mecanismos distintos. El AP utiliza la seguridad WPA2/WPA3 seleccionada por las opciones del SDK; eso no agrega inicio de sesión ni HTTPS a la interfaz de administración.

## Verificación y documentación técnica

Se han compilado ambos perfiles desde configuraciones nuevas con ESP-IDF 6.1. Las pruebas de host cubren el parser del formulario, siete escenarios DNS con tráfico UDP local y el JavaScript real de la página de estado. Sus comandos y alcance están en [tests/README.md](tests/README.md); no emulan la radio Wi-Fi ni el NAPT del hardware.

Las pruebas manuales reportadas en ESP32-S3 comprobaron el primer guardado, navegación a través del AP, diagnóstico de SSID y contraseña incorrectos, recuperación con datos correctos y reconexión tras reiniciar sin borrar NVS. No constituyen una medición de rendimiento ni una prueba de carga.

Documentación adicional:

- [Configuración por chip y placa](CONFIGURACION.md).
- [Estructura de componentes](components/README.md).
- [Notas históricas de migración a ESP32-S3](MIGRACION_ESP32S3.md).
- [Informe técnico en LaTeX](docs/funcionamiento_tecnico.tex): arquitectura, flujo de eventos, decisiones de diseño, límites y detalles de implementación.

Para generar el informe en **Overleaf**, crea un proyecto en blanco, carga `docs/funcionamiento_tecnico.tex` y selecciónalo como documento principal, o copia su contenido en `main.tex`. Selecciona **pdfLaTeX** y compila. El documento es autónomo y no necesita imágenes externas, `minted` ni ejecución de comandos externos.
