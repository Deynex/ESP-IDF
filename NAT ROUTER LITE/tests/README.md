# Pruebas del router

Las pruebas usan los archivos de producción en sus componentes. Los ejecutables, objetos y registros se guardan bajo `build`.

## Formulario Wi-Fi

Desde una terminal **x64 Native Tools de Visual Studio** en la raíz del proyecto:

```powershell
New-Item -ItemType Directory -Force build\host-modularized\wifi-form | Out-Null
cl /nologo /std:c11 /W4 /WX /Icomponents\wifi\include components\wifi\wifi_form.c tests\test_wifi_form.c /Febuild\host-modularized\wifi-form\test_wifi_form.exe /Fobuild\host-modularized\wifi-form\
.\build\host-modularized\wifi-form\test_wifi_form.exe
```

Comprueba límites de SSID/contraseña, UTF-8, caracteres codificados, claves duplicadas y entradas inválidas.

## DNS

Sigue las [instrucciones de la prueba DNS](support/dns_host/README.md). Compila `components/dns_proxy/dns_proxy.c` y comprueba siete escenarios con tráfico UDP real en loopback y puerto de prueba 1053; el firmware mantiene el puerto 53.

## Página de estado

Con Node.js:

```powershell
node tests/test_connection_status_page.mjs
```

Ejecuta el JavaScript real de `app/saved.html` con un navegador simulado. Comprueba la secuencia de error de contraseña, reintentos, nuevas credenciales y conexión correcta, además de las otras causas de fallo.

## Compilación y hardware

Desde una terminal de ESP-IDF, ejecuta `idf.py build`. Para cambiar de chip usa una configuración independiente del target correspondiente. Las pruebas de host no emulan la radio Wi-Fi ni el enrutamiento NAPT de una placa física.
