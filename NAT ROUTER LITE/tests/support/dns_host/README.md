# Prueba de integración DNS en Windows

Compila **el archivo de producción `components/dns_proxy/dns_proxy.c`** con adaptadores mínimos de FreeRTOS, logging y sockets Windows. Las consultas y respuestas viajan por sockets UDP reales en loopback; no se consulta Internet.

El adaptador `include/host_socket.h` traduce los endpoints de puerto 53 a **1053 únicamente durante la prueba**. El firmware conserva UDP/53. El puerto 53 de Windows puede estar reservado o filtrado por el entorno de ejecución y no es necesario acceder a él para comprobar el protocolo.

Desde una terminal **x64 Native Tools de Visual Studio**, situada en la raíz del proyecto, con MSVC que admita atomics de C11 y Python 3:

```powershell
New-Item -ItemType Directory -Force build\host-dns | Out-Null
cl /nologo /std:c11 /experimental:c11atomics /W4 /WX /D_WINSOCK_DEPRECATED_NO_WARNINGS /D_CRT_SECURE_NO_WARNINGS /Itests\support\dns_host\include /Icomponents\dns_proxy\include components\dns_proxy\dns_proxy.c tests\support\dns_host\host_main.c /Febuild\host-dns\dns_proxy_host.exe /Fobuild\host-dns\ /link Ws2_32.Lib
python tests\test_dns_proxy_host.py build\host-dns\dns_proxy_host.exe
```

Verificado con MSVC 14.51 y Windows SDK 10.0.26100.0. El harness escucha en `127.0.0.1:1053`, los clientes usan `127.0.0.2` y `127.0.0.3`, y los upstreams simulados usan `127.0.1.1:1053` y `127.0.2.1:1053`. Deben estar libres esos endpoints.

Se comprueban siete casos: SERVFAIL sin upstream, conservación de respuesta e ID original, dos clientes concurrentes con el mismo ID, cambio de upstream, rechazo de ID desconocido, timeout acotado y pérdida de conexión. `runtime.log` se guarda junto al ejecutable.

Estas pruebas verifican el protocolo y la asociación de solicitudes y respuestas. No emulan el enrutamiento de lwIP, DHCP, NAPT ni el comportamiento de un teléfono conectado a la placa.
