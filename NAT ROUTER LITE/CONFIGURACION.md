# Configuración para ESP32 y ESP32-S3

El chip de una configuración generada se identifica por `CONFIG_IDF_TARGET`. El nombre `sdkconfig.old` solo indica un respaldo anterior; no identifica un chip.

## Archivos del proyecto

| Archivo | Función | Contenido identificado al organizar los perfiles |
|---|---|---|
| `sdkconfig.defaults` | Preferencias comunes del router | Forwarding IPv4, NAPT y port mapping para ambos chips |
| `sdkconfig.defaults.esp32` | Preferencias del ESP32 | Flash declarada de 2 MB, DIO/40 MHz y LED GPIO2 activo bajo |
| `sdkconfig.defaults.esp32s3` | Preferencias del ESP32-S3 usado en las pruebas | Flash de 16 MB, DIO/80 MHz, LED RGB GPIO48 y consola USB secundaria |
| `sdkconfig` | Configuración activa completa, generada por ESP-IDF | `CONFIG_IDF_TARGET="esp32"` |
| `sdkconfig.old` | Respaldo automático de la configuración anterior | Actualmente `CONFIG_IDF_TARGET="esp32s3"`; puede cambiar al configurar de nuevo |

Con la configuración estándar del proyecto, ESP-IDF carga primero `sdkconfig.defaults` y después el archivo `sdkconfig.defaults.<IDF_TARGET>` correspondiente. No se cargan los dos perfiles de chip a la vez. Este es el mecanismo estándar de [defaults por target](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/build-system.html#target-dependent-sdkconfig-defaults).

Para consultar el chip de los archivos generados en cualquier momento:

```powershell
Select-String -Path sdkconfig,sdkconfig.old -Pattern '^CONFIG_IDF_TARGET='
```

Los archivos `sdkconfig` y `sdkconfig.old` se conservaron sin editar. ESP-IDF los genera y actualiza; configura sus valores mediante `menuconfig` y no uses `.old` como archivo permanente del S3. Su función de respaldo está descrita en la [estructura de configuración oficial](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/kconfig/configuration_structure.html#sdkconfig-and-sdkconfig-old).

## Cambiar de chip en el flujo habitual

En la extensión ESP-IDF de VS Code, selecciona el target que vas a usar. Desde una terminal de ESP-IDF también puedes ejecutar:

```powershell
idf.py set-target esp32
```

O, para el S3:

```powershell
idf.py set-target esp32s3
```

`set-target` limpia el directorio de compilación seleccionado, respalda la configuración anterior y genera la del nuevo target. Después usa `idf.py menuconfig` para ajustes de placa y `idf.py build` para compilar. Comprueba que la variable de entorno `IDF_TARGET`, si está definida, coincida con el chip seleccionado. La configuración actual de VS Code selecciona `esp32`.

Los defaults son la base para crear configuraciones. Una selección explícita guardada en `sdkconfig` tiene prioridad; cambiar un perfil no fuerza todos sus valores sobre una configuración existente. En IDF 6.1, las opciones marcadas `# default:` pueden tomar los nuevos defaults al reconfigurar.

## Trabajar con ambos chips sin mezclar sus configuraciones

Puedes conservar una compilación y una configuración generada por chip usando las opciones estándar `-B`, `IDF_TARGET` y `SDKCONFIG`, sin modificar CMake. Desde PowerShell en la raíz del proyecto:

```powershell
# Quita la selección de la terminal; los comandos siguientes la indican con -D.
Remove-Item Env:IDF_TARGET -ErrorAction SilentlyContinue

idf.py -B build/esp32 -D IDF_TARGET=esp32 -D SDKCONFIG=build/esp32/sdkconfig build
idf.py -B build/esp32s3 -D IDF_TARGET=esp32s3 -D SDKCONFIG=build/esp32s3/sdkconfig build
```

En ese flujo, las configuraciones quedan identificadas por sus carpetas: `build/esp32/sdkconfig` y `build/esp32s3/sdkconfig`. Cada una tiene su propio respaldo y caché. Para abrir el menú de una de ellas, repite sus mismas opciones y sustituye `build` por `menuconfig`; por ejemplo:

```powershell
idf.py -B build/esp32s3 -D IDF_TARGET=esp32s3 -D SDKCONFIG=build/esp32s3/sdkconfig menuconfig
```

Si ya existe un build con otro target o con una ruta `SDKCONFIG` diferente, usa un directorio nuevo en lugar de reutilizar su caché. Las opciones de compilación deben acompañar también a `flash`/`monitor` para usar los binarios del chip correcto.

## Ajustes conservados y mantenimiento

El perfil S3 conserva también las preferencias de funciones FreeRTOS en IRAM y colores de los logs que estaban personalizadas en el respaldo. El perfil ESP32 mantiene la flash declarada en la configuración actual; ese valor no demuestra la capacidad física del módulo. El GPIO del LED y el tamaño de flash describen estas placas, no todos los módulos que incorporan cada chip. PSRAM y FTM permanecen sujetos a la selección de hardware y a `menuconfig`.

Para conservar cambios futuros, revisa los defaults del chip correspondiente. `idf.py save-defconfig` genera los valores que difieren de los defaults, pero escribe en **`sdkconfig.defaults`**: no lo ejecutes sobre un perfil de chip suponiendo que actualizará automáticamente `sdkconfig.defaults.esp32` o `.esp32s3`. Separa después las opciones comunes y las de placa antes de guardar esos perfiles.

## Validación de los perfiles

Ambos perfiles se generaron desde cero, sin copiar un `sdkconfig` completo previo, y se compilaron correctamente con ESP-IDF 6.1. Los registros confirman que cada chip carga el archivo común y únicamente su sufijo correspondiente.

| Perfil | Valores comprobados |
|---|---|
| ESP32 | Forwarding/NAPT/port mapping, DIO/40 MHz, flash declarada de 2 MB y LED GPIO2 activo bajo; sin las opciones S3 de RGB, 16 MB, IRAM o colores |
| ESP32-S3 | Forwarding/NAPT/port mapping, DIO/80 MHz, flash de 16 MB, RGB GPIO48, USB secundario, FreeRTOS en IRAM y colores de logs |

Logs: `build/config-profile-esp32/build.log` y `build/config-profile-esp32s3/build.log`. Se conservaron sin cambios los archivos raíz `sdkconfig` y `sdkconfig.old`, la selección de VS Code y el CMake del proyecto. No se programaron las placas durante esta validación.
