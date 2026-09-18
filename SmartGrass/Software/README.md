# SmartGrass - ESP32 Lawn Mower

Firmware para cortacésped robótico basado en ESP32 con control vía web y telemetría en tiempo real.

## 🔌 Pinout y Conexiones

### Esquema de Conexión

| Componente | Función | Pin ESP32 | Notas |
| :--- | :--- | :--- | :--- |
| **IMU (MPU6050)** | I2C SCL | GPIO 22 | Reloj I2C |
| | I2C SDA | GPIO 21 | Datos I2C |
| **Tracción (MKS/A4988)** | STEP | GPIO 23 | Pulso de paso (ambos motores) |
| | DIR Izquierda | GPIO 32 | Dirección Motor Izquierdo |
| | DIR Derecha | GPIO 19 | Dirección Motor Derecho |
| | ENABLE | GPIO 18 | Habilitar Motores (Active Low) |
| **Telemetría MKS** | UART TX | GPIO 17 | TX ESP32 -> RX MKS |
| | UART RX | GPIO 16 | RX ESP32 -> TX MKS |
| **Cuchilla (ESC)** | Señal PWM | GPIO 25 | Control de velocidad ESC |

## 📂 Estructura del Proyecto

El proyecto sigue la estructura estándar de ESP-IDF:

- **`app/`**: Contiene la interfaz web (`index.html`) que se sirve al cliente.
- **`components/`**: Librerías y drivers modulares para el hardware:
  - `a4988`: Driver para controladores de motores paso a paso.
  - `brushless`: Control de motor brushless mediante señal PWM/ESC.
  - `i2c`: Manejo del bus I2C.
  - `mks_servo42c`: Protocolo de comunicación UART para servos MKS Servo42C.
  - `mpu6050`: Driver para el acelerómetro/giroscopio.
  - `server`: Servidor HTTP para servir la web de control.
  - `soft_ap`: Configuración del Punto de Acceso WiFi.
  - `uart`: Abstracción del puerto serie.
  - `vehicle`: Lógica de alto nivel para el movimiento y control del vehículo.
  - `ws`: Servidor WebSocket para comunicación en tiempo real.
- **`main/`**: Punto de entrada de la aplicación (`main.c`), configuración de la placa (`board_config.h`) y gestión de tareas (FreeRTOS).

## ⚙️ Funcionamiento

### 1. Sistema de Control Web
El ESP32 crea un punto de acceso WiFi (o se conecta a una red existente). Al acceder a la IP del ESP32, se carga una interfaz web que permite:
- **Control Manual**: Joystick virtual para movimiento (Adelante, Atrás, Izquierda, Derecha).
- **Control de Cuchilla**: Activación/Desactivación y monitoreo del motor de corte.
- **Telemetría**: Visualización en tiempo real de datos de sensores.

### 2. Comunicación (WebSockets)
La comunicación entre la web y el ESP32 es bidireccional mediante WebSockets:
- **Comandos (Web -> ESP32)**: Bytes de control para movimiento y acciones.
- **Telemetría (ESP32 -> Web)**: Envío periódico (JSON) de:
  - Datos IMU (Acelerómetro, Giroscopio, Inclinación).
  - Estado de los motores (Posición, Velocidad, Corriente).
  - Temperatura del sistema.

### 3. Control de Motores
- **Tracción**: Utiliza drivers paso a paso. El sistema genera pulsos para el movimiento y controla la dirección mediante pines GPIO dedicados para cada rueda. Soporta telemetría avanzada si se usan motores MKS Servo42C.
- **Cuchilla**: Genera una señal PWM compatible con ESCs (Electronic Speed Controllers) para motores brushless, permitiendo un control preciso de la velocidad de corte.

### 4. Sensores
- **MPU6050**: Monitorea la inclinación del vehículo para detectar vuelcos o pendientes peligrosas.

## 🛠️ Compilación y Flasheo

Este proyecto utiliza el framework **ESP-IDF**.

1. **Configurar el entorno**:
   ```bash
   . $HOME/esp/esp-idf/export.sh
   ```

2. **Configurar el target**:
   ```bash
   idf.py set-target esp32
   ```

3. **Compilar**:
   ```bash
   idf.py build
   ```

4. **Flashear y Monitorear**:
   ```bash
   idf.py flash monitor
   ```
