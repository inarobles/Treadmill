# ⚙️ Configuración del VFD SU300 - Cinta de Correr

**Documento generado:** 2025-11-06
**Modelo VFD:** SU300 (Variador de Frecuencia)
**Estado:** ✅ Configuración documentada con datos reales del hardware

---

## 📋 Tabla de Contenidos

- [Resumen Ejecutivo](#resumen-ejecutivo)
- [Comunicación Modbus RTU](#comunicación-modbus-rtu)
- [Registros de Control](#registros-de-control)
- [Registros de Monitorización](#registros-de-monitorización)
- [Parámetros de Rampa](#parámetros-de-rampa)
- [Conversión de Velocidad](#conversión-de-velocidad)
- [Comandos de Control](#comandos-de-control)
- [Código de Ejemplo](#código-de-ejemplo)

---

## 🎯 Resumen Ejecutivo

El VFD SU300 controla el motor de la cinta de correr mediante comunicación Modbus RTU. Los parámetros críticos fueron identificados mediante pruebas con hardware real (ESP32 + Arduino).

### Parámetros Críticos Confirmados

| Parámetro | Valor | Descripción |
|-----------|-------|-------------|
| **Dirección Modbus** | 1 | ID del dispositivo esclavo |
| **Baudrate** | 9600 | Velocidad de comunicación |
| **Paridad** | None (8N1) | Sin paridad, 8 bits, 1 stop bit |
| **Frecuencia base** | 50 Hz | Frecuencia base del motor |
| **Relación velocidad** | 6.4 / 50.0 | Factor de conversión Hz → km/h |

---

## 📡 Comunicación Modbus RTU

### Configuración del Bus

**Hardware:**
- **Interface:** RS485 (MAX485 transceiver)
- **TX Pin (ESP32):** GPIO 19
- **RX Pin (ESP32):** GPIO 18
- **Velocidad:** 9600 baud
- **Formato:** 8 bits, sin paridad, 1 stop bit (8N1)

**Librería utilizada (Arduino):**
```cpp
#include <ModbusMaster.h>

ModbusMaster node;

void setup() {
  Serial2.begin(9600, SERIAL_8N1, VFD_RX_PIN, VFD_TX_PIN);
  node.begin(VFD_ADDRESS, Serial2);
}
```

**Librería utilizada (ESP-IDF):**
```c
#include "esp_modbus_master.h"

// Configuración en Base/main/vfd_driver.c
#define VFD_SLAVE_ID       1
#define VFD_BAUD_RATE      9600
#define VFD_PARITY         UART_PARITY_DISABLE
```

### Parámetros F5-XX (Comunicación)

Estos parámetros deben estar configurados en el VFD mediante su panel frontal o software de configuración:

| Parámetro | Nombre | Valor Configurado | Descripción |
|-----------|--------|-------------------|-------------|
| **F5-00** | Velocidad comunicación | 9600 | Baudrate Modbus (3 = 9600) |
| **F5-01** | Dirección esclavo | 1 | ID del VFD en el bus |
| **F5-02** | Paridad | 0 (None) | Sin paridad (8N1) |

---

## 🎛️ Registros de Control

### Registro 0x2000 - Comando de Control

**Dirección:** `0x2000` (8192 decimal)
**Tipo:** Escritura (Write Single Register)
**Función Modbus:** `0x06`

**Comandos disponibles:**

| Comando | Valor | Descripción | Notas |
|---------|-------|-------------|-------|
| **RUN_FWD** | 0x0001 | Arrancar motor (sentido adelante) | Motor gira |
| **RUN_FWD (Alt)** | 0x0012 | Arrancar motor (modo alternativo) | Usado en tests |
| **STOP** | 0x0001 | Parada libre | Dejar de aplicar tensión |
| **F-STOP** | 0x0005 | Parada con rampa | Sigue rampa F1-15 |

**Ejemplo de uso (Arduino):**
```cpp
// Arrancar motor
uint8_t result = node.writeSingleRegister(0x2000, 0x0012);
if (result == node.ku8MBSuccess) {
  Serial.println("Motor arrancado");
}
```

**Ejemplo de uso (ESP-IDF):**
```c
// En Base/main/vfd_driver.c
#define VFD_REG_CONTROL    0x2000
#define VFD_CMD_RUN_FWD    0x0001
#define VFD_CMD_STOP       0x0005

vfd_write_register(VFD_REG_CONTROL, VFD_CMD_RUN_FWD);
```

---

### Registro 0x2001 - Frecuencia Objetivo

**Dirección:** `0x2001` (8193 decimal)
**Tipo:** Escritura (Write Single Register)
**Formato:** Frecuencia × 100 (centi-Hz)

**Rango:**
- Mínimo: 0 (0 Hz)
- Máximo: 50000 (500.00 Hz) - limitado por motor
- Típico para cinta: 0 - 15625 (0 - 156.25 Hz para 20 km/h)

**Ejemplos de conversión:**

| Velocidad Deseada | Hz Requerido | Valor Registro | Hex |
|-------------------|--------------|----------------|-----|
| 1.0 km/h | 7.81 Hz | 781 | 0x030D |
| 5.0 km/h | 39.06 Hz | 3906 | 0x0F42 |
| 10.0 km/h | 78.13 Hz | 7813 | 0x1E85 |
| 15.0 km/h | 117.19 Hz | 11719 | 0x2DC7 |
| 20.0 km/h | 156.25 Hz | 15625 | 0x3D09 |

**Ejemplo de uso (Arduino):**
```cpp
// Establecer 10 km/h (78.11 Hz)
uint16_t freq_value = 7811;  // 78.11 Hz × 100
uint8_t result = node.writeSingleRegister(0x2001, freq_value);
```

**Ejemplo de uso (ESP-IDF):**
```c
// En Base/main/vfd_driver.c
float target_kmh = 10.0f;
float freq_hz = target_kmh * (50.0f / 6.4f);  // = 78.125 Hz
uint16_t freq_centi_hz = (uint16_t)(freq_hz * 100.0f);  // = 7813
vfd_write_register(VFD_REG_FREQ, freq_centi_hz);
```

---

## 📊 Registros de Monitorización

### Registro 0x2103 - Frecuencia Real del VFD

**Dirección:** `0x2103` (8451 decimal)
**Tipo:** Lectura (Read Holding Register)
**Función Modbus:** `0x03`
**Formato:** Frecuencia × 100 (centi-Hz)

Este registro devuelve la **frecuencia actual real** que está aplicando el VFD al motor, no la frecuencia objetivo. Útil para monitorear el progreso de la rampa de aceleración/desaceleración.

**Ejemplo de lectura (Arduino):**
```cpp
uint8_t result = node.readHoldingRegisters(0x2103, 1);
if (result == node.ku8MBSuccess) {
  uint16_t freq_centihz = node.getResponseBuffer(0);
  float freq_hz = freq_centihz / 100.0;
  float kmh = freq_hz * (6.4 / 50.0);

  Serial.print("VFD Freq: ");
  Serial.print(freq_hz);
  Serial.print(" Hz | Speed: ");
  Serial.print(kmh);
  Serial.println(" km/h");
}
```

**Diferencia objetivo vs. real durante rampa:**
```
Tiempo | Objetivo | Real (0x2103) | Estado
-------|----------|---------------|--------
t=0    | 78.10 Hz | 0.00 Hz       | Acelerando
t=1    | 78.10 Hz | 14.40 Hz      | Acelerando
t=2    | 78.10 Hz | 27.80 Hz      | Acelerando
t=3    | 78.10 Hz | 41.20 Hz      | Acelerando
t=4    | 78.10 Hz | 54.60 Hz      | Acelerando
t=5    | 78.10 Hz | 68.00 Hz      | Acelerando
t=6    | 78.10 Hz | 78.10 Hz      | ✅ Objetivo alcanzado
```

### Registro 0x2104 - Código de Fallo

**Dirección:** `0x2104` (8452 decimal)
**Tipo:** Lectura (Read Holding Register)
**Formato:** Código de error (0 = sin fallo)

**Valores comunes:**
- `0x0000`: Sin fallo (operación normal)
- `0x0001`: Sobre-corriente
- `0x0002`: Sobre-voltaje
- `0x0003`: Sub-voltaje
- `0x0004`: Sobre-temperatura
- `0x0011`: Fallo de comunicación
- `0x0015`: Sobrecarga

**Ejemplo de uso:**
```c
uint16_t fault_code = 0;
if (vfd_read_register(VFD_REG_FAULT_CODE, &fault_code) == ESP_OK) {
  if (fault_code != 0) {
    ESP_LOGE(TAG, "VFD Fault: 0x%04X", fault_code);
  }
}
```

---

## ⏱️ Parámetros de Rampa

### Observaciones de la Rampa de Aceleración

Basándose en los logs reales del hardware, la rampa de aceleración del VFD muestra el siguiente comportamiento:

**Datos observados (de 0 a 78.10 Hz):**

| Tiempo (seg) | Frecuencia (Hz) | Velocidad (km/h) | Delta Hz/seg |
|--------------|-----------------|------------------|--------------|
| 0 | ~0 | 0.00 | - |
| 1 | 14.40 | 1.84 | 14.4 |
| 2 | 27.80 | 3.56 | 13.4 |
| 3 | 41.20 | 5.27 | 13.4 |
| 4 | 54.60 | 6.99 | 13.4 |
| 5 | 68.00 | 8.70 | 13.4 |
| 6 | 78.10 | 10.00 | 10.1 |
| 7+ | 78.10 | 10.00 | 0 (estable) |

**Análisis:**
- **Tasa de aceleración promedio:** ~13 Hz/seg
- **Tiempo para alcanzar objetivo:** ~6 segundos (0 → 78.10 Hz)
- **Perfil de rampa:** Lineal con suavizado al final

### Parámetros F1-XX (Rampa)

Estos parámetros controlan el comportamiento de aceleración y desaceleración del VFD:

| Parámetro | Nombre | Valor Estimado | Registro Modbus | Descripción |
|-----------|--------|----------------|-----------------|-------------|
| **F1-14** | Tiempo aceleración | 6-8 segundos | 0x010E | Tiempo de 0 Hz a frecuencia base |
| **F1-15** | Tiempo desaceleración | 6-8 segundos | 0x010F | Tiempo de frecuencia base a 0 Hz |

**Nota:** Los valores exactos de F1-14 y F1-15 se pueden leer mediante Modbus:

```cpp
// Leer F1-14 (tiempo de aceleración)
uint8_t result = node.readHoldingRegisters(0x010E, 1);
if (result == node.ku8MBSuccess) {
  uint16_t accel_time = node.getResponseBuffer(0);
  Serial.print("Tiempo aceleración: ");
  Serial.print(accel_time / 10.0);  // Normalmente en décimas de segundo
  Serial.println(" segundos");
}
```

### Implicaciones para el Software

**En Base (ESP-IDF):**
- No se implementa rampa en software
- Los cambios de velocidad se envían directamente al VFD
- El VFD aplica la rampa configurada en F1-14/F1-15
- Tiempo de respuesta: 6-8 segundos para cambios grandes de velocidad

**En Consola:**
- Durante rampas, el sistema debe seguir leyendo velocidad del sensor
- La velocidad mostrada en UI debe actualizarse continuamente
- Ver documento `Consola/docs/MEJORAS_PENDIENTES.md` Tarea #6 sobre saturación RS485

---

## 🔄 Conversión de Velocidad

### Fórmula Maestra del VFD SU300

**Relación confirmada con hardware real:**

```
velocidad_kmh = frecuencia_Hz × (6.4 / 50.0)
velocidad_kmh = frecuencia_Hz × 0.128
```

**Inversa (para comandos de control):**
```
frecuencia_Hz = velocidad_kmh × (50.0 / 6.4)
frecuencia_Hz = velocidad_kmh × 7.8125
```

### Tabla Completa de Conversión

| km/h | Hz | Registro 0x2001 | RPM (12 dientes) | Pulsos/seg |
|------|----|-----------------|--------------------|------------|
| 0.5 | 3.91 | 391 | 144 | 29 |
| 1.0 | 7.81 | 781 | 287 | 57 |
| 2.0 | 15.63 | 1563 | 575 | 115 |
| 3.0 | 23.44 | 2344 | 862 | 172 |
| 4.0 | 31.25 | 3125 | 1150 | 230 |
| 5.0 | 39.06 | 3906 | 1438 | 287 |
| 6.0 | 46.88 | 4688 | 1725 | 345 |
| 7.0 | 54.69 | 5469 | 2013 | 402 |
| 8.0 | 62.50 | 6250 | 2300 | 460 |
| 9.0 | 70.31 | 7031 | 2588 | 518 |
| **10.0** | **78.13** | **7813** | **2875** | **575** |
| 11.0 | 85.94 | 8594 | 3163 | 633 |
| 12.0 | 93.75 | 9375 | 3450 | 690 |
| 13.0 | 101.56 | 10156 | 3738 | 748 |
| 14.0 | 109.38 | 10938 | 4025 | 805 |
| 15.0 | 117.19 | 11719 | 4313 | 863 |
| 16.0 | 125.00 | 12500 | 4600 | 920 |
| 17.0 | 132.81 | 13281 | 4888 | 978 |
| 18.0 | 140.63 | 14063 | 5175 | 1035 |
| 19.0 | 148.44 | 14844 | 5463 | 1093 |
| 20.0 | 156.25 | 15625 | 5750 | 1150 |

**Nota:** Los valores de Pulsos/seg y RPM asumen corona Hall de 12 dientes y `g_calibration_factor = 0.0174`.

### Implementación en Código

**Conversión km/h → Hz (Comando SET_SPEED):**
```c
// Base/main/vfd_driver.c
#define KPH_TO_HZ_RATIO  (50.0f / 6.4f)  // = 7.8125

float target_kmh = 10.0f;
float freq_hz = target_kmh * KPH_TO_HZ_RATIO;  // 10.0 × 7.8125 = 78.125 Hz
uint16_t freq_centi_hz = (uint16_t)(freq_hz * 100.0f);  // 7813
vfd_write_register(VFD_REG_FREQ, freq_centi_hz);
```

**Conversión Hz → km/h (Lectura de monitorización):**
```cpp
// Arduino (para referencia)
float freq_hz = 78.10;
float kmh = freq_hz * (6.4 / 50.0);  // 78.10 × 0.128 = 10.00 km/h
```

---

## 🕹️ Comandos de Control

### Secuencia de Arranque del Motor

**Paso 1: Configurar frecuencia objetivo**
```c
uint16_t freq_centi_hz = 7813;  // 78.13 Hz (10 km/h)
vfd_write_register(VFD_REG_FREQ, freq_centi_hz);
delay_ms(100);  // Esperar confirmación
```

**Paso 2: Enviar comando RUN**
```c
vfd_write_register(VFD_REG_CONTROL, VFD_CMD_RUN_FWD);
// El motor comenzará a acelerar según rampa F1-14
```

**Paso 3: Monitorear frecuencia real (opcional)**
```c
uint16_t real_freq;
while (real_freq < target_freq) {
  vfd_read_register(0x2103, &real_freq);
  printf("Acelerando: %.2f Hz\n", real_freq / 100.0f);
  delay_ms(1000);
}
printf("Objetivo alcanzado\n");
```

### Secuencia de Parada del Motor

**Parada con rampa (recomendado):**
```c
vfd_write_register(VFD_REG_CONTROL, VFD_CMD_STOP);  // 0x0005
// El motor desacelera según rampa F1-15
```

**Parada de emergencia (sin rampa):**
```c
vfd_write_register(VFD_REG_CONTROL, 0x0001);  // Parada libre
// Motor deja de recibir tensión inmediatamente
```

### Cambio de Velocidad en Movimiento

El VFD permite cambiar la frecuencia objetivo mientras el motor está en marcha:

```c
// Motor corriendo a 50 Hz (6.4 km/h)
vfd_write_register(VFD_REG_FREQ, 5000);  // Cambiar a 50 Hz

// Cambiar a 10 km/h (78.13 Hz)
vfd_write_register(VFD_REG_FREQ, 7813);
// El VFD aplicará rampa F1-14 automáticamente
```

**Importante:** No es necesario detener el motor para cambiar la velocidad.

---

## 💻 Código de Ejemplo

### Arduino (Funcional - Probado en Hardware)

```cpp
#include <ModbusMaster.h>
#include <HardwareSerial.h>

#define VFD_TX_PIN 19
#define VFD_RX_PIN 18
#define VFD_ADDRESS 1
#define VFD_BAUDRATE 9600
#define FREQ_REGISTER 0x2001
#define CMD_REGISTER 0x2000
#define STATUS_REGISTER 0x2103

ModbusMaster node;

void setup() {
  Serial.begin(115200);
  Serial2.begin(VFD_BAUDRATE, SERIAL_8N1, VFD_RX_PIN, VFD_TX_PIN);
  node.begin(VFD_ADDRESS, Serial2);

  // Establecer velocidad a 10 km/h
  uint16_t freq_value = 7811;  // 78.11 Hz
  uint8_t result = node.writeSingleRegister(FREQ_REGISTER, freq_value);

  if (result == node.ku8MBSuccess) {
    Serial.println("Frecuencia configurada");
  }

  // Arrancar motor
  result = node.writeSingleRegister(CMD_REGISTER, 0x0012);
  if (result == node.ku8MBSuccess) {
    Serial.println("Motor arrancado");
  }
}

void loop() {
  // Leer frecuencia real cada segundo
  uint8_t result = node.readHoldingRegisters(STATUS_REGISTER, 1);

  if (result == node.ku8MBSuccess) {
    uint16_t freq_centihz = node.getResponseBuffer(0);
    float freq_hz = freq_centihz / 100.0;
    float kmh = freq_hz * (6.4 / 50.0);

    Serial.print("VFD: ");
    Serial.print(freq_hz);
    Serial.print(" Hz | Speed: ");
    Serial.print(kmh);
    Serial.println(" km/h");
  }

  delay(1000);
}
```

### ESP-IDF (Producción)

Ver implementación completa en:
- `Base/main/vfd_driver.c` - Driver Modbus del VFD
- `Base/main/vfd_driver.h` - API pública

**Ejemplo de uso:**
```c
#include "vfd_driver.h"

void app_main(void) {
  // Inicializar driver VFD
  vfd_driver_init();

  // Establecer velocidad
  vfd_set_speed(10.0f);  // 10 km/h

  // El driver maneja:
  // - Conversión km/h → Hz
  // - Envío por Modbus
  // - Gestión de errores
  // - Monitorización de estado
}
```

---

## 🔧 Troubleshooting

### Problema: VFD no responde a comandos Modbus

**Verificar:**
1. Dirección Modbus del VFD (debe ser 1)
2. Baudrate configurado en F5-00 (debe ser 9600)
3. Paridad configurada en F5-02 (debe ser 0 = None)
4. Cableado RS485 (A, B, GND correctamente conectados)
5. Alimentación del VFD

**Comando de diagnóstico:**
```cpp
// Intentar leer registro de estado
uint8_t result = node.readHoldingRegisters(0x2103, 1);
if (result == node.ku8MBSuccess) {
  Serial.println("✅ VFD responde correctamente");
} else {
  Serial.print("❌ Error Modbus: 0x");
  Serial.println(result, HEX);
}
```

### Problema: Motor no acelera/desacelera como esperado

**Revisar parámetros de rampa:**
```cpp
// Leer F1-14 (aceleración)
node.readHoldingRegisters(0x010E, 1);
uint16_t accel_time = node.getResponseBuffer(0);

// Leer F1-15 (desaceleración)
node.readHoldingRegisters(0x010F, 1);
uint16_t decel_time = node.getResponseBuffer(0);

Serial.print("Aceleración: ");
Serial.print(accel_time / 10.0);
Serial.println(" seg");

Serial.print("Desaceleración: ");
Serial.print(decel_time / 10.0);
Serial.println(" seg");
```

### Problema: Velocidad real no coincide con la esperada

**Verificar:**
1. Calibración del sensor Hall (`g_calibration_factor`)
2. Ratio de conversión (`KPH_TO_HZ_RATIO`)
3. Número de dientes de la corona Hall (debe ser 12)
4. Ver documento `Base/docs/CALIBRACION.md`

---

## 📚 Referencias

### Documentación Relacionada
- **Calibración del Sistema:** `Base/docs/CALIBRACION.md`
- **Driver VFD:** `Base/main/vfd_driver.c`
- **Mejoras Pendientes:** `Consola/docs/MEJORAS_PENDIENTES.md` (Tarea #7)
- **Manual SU300:** [Datasheet del fabricante]

### Registros Modbus Útiles

| Registro | Tipo | Descripción |
|----------|------|-------------|
| 0x2000 | RW | Comando de control |
| 0x2001 | RW | Frecuencia objetivo (Hz × 100) |
| 0x2103 | R | Frecuencia real (Hz × 100) |
| 0x2104 | R | Código de fallo |
| 0x010E | RW | F1-14: Tiempo aceleración |
| 0x010F | RW | F1-15: Tiempo desaceleración |
| 0x0001 | RW | F0-01: Fuente de comando |
| 0x0002 | RW | F0-02: Fuente de frecuencia |

### Valores de Configuración Recomendados

| Parámetro | Valor | Justificación |
|-----------|-------|---------------|
| F0-01 | 2 | Fuente de comando: Modbus |
| F0-02 | 2 | Fuente de frecuencia: Modbus |
| F1-14 | 6-8 seg | Aceleración suave para cinta |
| F1-15 | 6-8 seg | Desaceleración suave para cinta |
| F5-00 | 3 (9600) | Baudrate Modbus |
| F5-01 | 1 | Dirección Modbus |
| F5-02 | 0 | Sin paridad (8N1) |

---

**Última actualización:** 2025-11-06
**Estado:** ✅ Documentación completa basada en pruebas reales con hardware
