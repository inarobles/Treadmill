# ✅ Checklist para Primera Prueba - Sistema Cinta de Correr

**Fecha de creación:** 2025-11-06
**Versión del software:** Commit `da6ce70c`
**Estado:** ✅ LISTO PARA PRUEBA

---

## 📋 Índice

- [Pre-requisitos](#pre-requisitos)
- [Compilación y Flasheo](#compilación-y-flasheo)
- [Verificaciones Iniciales](#verificaciones-iniciales)
- [Pruebas de Funcionalidad](#pruebas-de-funcionalidad)
- [Validación de Calibración](#validación-de-calibración)
- [Logs Esperados](#logs-esperados)
- [Troubleshooting](#troubleshooting)

---

## 🔧 Pre-requisitos

### Hardware Requerido

- [ ] **Base (ESP32):**
  - [ ] ESP32 conectado vía USB
  - [ ] VFD SU300 conectado a UART2 (TX=GPIO19, RX=GPIO18)
  - [ ] Sensor Hall en GPIO 15 (corona de 12 dientes)
  - [ ] Motor de inclinación conectado a relés
  - [ ] Comunicación RS485 a Consola (TX=GPIO17, RX=GPIO16)

- [ ] **Consola (ESP32-P4):**
  - [ ] ESP32-P4 conectado vía USB
  - [ ] Pantalla LCD 10.1" (1280x800) funcionando
  - [ ] Touch GT911 configurado
  - [ ] Comunicación RS485 a Base (TX=GPIO4, RX=GPIO5)
  - [ ] Botones físicos (10 botones, 5 por lado)

- [ ] **VFD SU300:**
  - [ ] Alimentado correctamente
  - [ ] Motor conectado
  - [ ] Parámetros configurados:
    - [ ] F0-01 = 2 (comando vía RS485)
    - [ ] F0-02 = 9 (frecuencia vía comunicación)
    - [ ] F5-00 = 3 (9600 baud)
    - [ ] F5-01 = 1 (dirección Modbus)
    - [ ] F5-02 = 0 (sin paridad)

### Software Requerido

- [ ] ESP-IDF v5.5.1 instalado
- [ ] Python 3.11 (para idf.py)
- [ ] Driver USB-Serial instalado
- [ ] Terminal serial (minicom, screen, o PuTTY)

---

## 🔨 Compilación y Flasheo

### Base (ESP32)

```bash
cd /home/user/Treadmill/Base

# Limpiar build anterior
idf.py fullclean

# Configurar (si es necesario)
idf.py menuconfig
# Verificar:
# - Flash size: 4MB (o según tu módulo)
# - Partition table: Default
# - UART para logs: UART0

# Compilar
idf.py build

# Flashear
idf.py -p /dev/ttyUSB0 flash

# Monitorear logs
idf.py -p /dev/ttyUSB0 monitor
```

**Checklist de compilación Base:**
- [ ] Compilación sin errores
- [ ] Compilación sin warnings críticos
- [ ] Tamaño del binario < 1MB
- [ ] Flasheo exitoso
- [ ] Logs visibles en monitor

### Consola (ESP32-P4)

```bash
cd /home/user/Treadmill/Consola

# Limpiar build anterior
idf.py fullclean

# Compilar
idf.py build

# Flashear
idf.py -p /dev/ttyUSB1 flash

# Monitorear logs
idf.py -p /dev/ttyUSB1 monitor
```

**Checklist de compilación Consola:**
- [ ] Compilación sin errores
- [ ] Compilación sin warnings críticos
- [ ] Pantalla LCD enciende
- [ ] Touch responde
- [ ] Logs visibles en monitor

---

## 🔍 Verificaciones Iniciales

### 1. Comunicación RS485 (Base ↔ Consola)

**En logs de Base, buscar:**
```
I (xxxx) SLAVE: Configuración UART completada
I (xxxx) SLAVE: Sistema listo - Esperando comandos...
```

**En logs de Consola, buscar:**
```
I (xxxx) CM_MASTER: Master started successfully
I (xxxx) CM_MASTER: Connected to slave
```

- [ ] ✅ Base reporta "Sistema listo"
- [ ] ✅ Consola reporta "Connected to slave"
- [ ] ✅ No hay errores de timeout en RS485

---

### 2. Comunicación Modbus (Base ↔ VFD)

**En logs de Base, buscar:**
```
I (xxxx) VFD_DRIVER_MODBUS: Inicializando driver Modbus Master...
I (xxxx) VFD_DRIVER_MODBUS: VFD configurado para control por Modbus.
I (xxxx) VFD_DRIVER_MODBUS: Configuración VFD exitosa. Iniciando bucle de control.
```

- [ ] ✅ VFD responde a comandos Modbus
- [ ] ✅ No hay errores de comunicación
- [ ] ✅ Registro 0x2104 (fault code) = 0

**Test manual:**
```
Presionar SPEED + en la Consola
→ Verificar que el VFD cambia de frecuencia
```

---

### 3. Sensor Hall (GPIO 15)

**En logs de Base, buscar:**
```
I (xxxx) SPEED_SENSOR: Inicializando sensor de velocidad (PCNT) en GPIO 15
I (xxxx) SPEED_SENSOR: GPIO 15 configurado con pull-down para evitar ruido
```

**Verificación con motor parado:**
```
I (xxxx) SLAVE: Velocidad: 0.00 km/h
```

**Verificación con motor en marcha:**
```
I (xxxx) SLAVE: Velocidad: X.XX km/h (Pulsos/seg: YYY)
```

- [ ] ✅ Sensor inicializa correctamente
- [ ] ✅ Reporta 0 km/h con motor parado
- [ ] ✅ Reporta velocidad > 0 con motor en marcha

---

## 🧪 Pruebas de Funcionalidad

### Test 1: Arranque y Parada Básica

**Procedimiento:**
1. Presionar **SPEED +** (1 vez) → Objetivo: 0.1 km/h
2. Esperar 5 segundos
3. Verificar que motor arranca
4. Presionar **STOP**
5. Verificar que motor se detiene con rampa

**Checklist:**
- [ ] Motor arranca suavemente (rampa del VFD)
- [ ] Pantalla muestra velocidad incrementando
- [ ] Motor se detiene suavemente al presionar STOP
- [ ] Velocidad en pantalla llega a 0.0 km/h

**Logs esperados en Base:**
```
I (xxxx) VFD_DRIVER_MODBUS: VFD Real: 7.81 Hz | Target: 7.81 Hz | Speed: 1.0 km/h
I (xxxx) SLAVE: SET_SPEED recibido: 1.0 km/h
```

---

### Test 2: Incremento de Velocidad

**Procedimiento:**
1. Arrancar motor a 5.0 km/h
2. Presionar **SPEED +** varias veces hasta 10.0 km/h
3. Observar rampa de aceleración
4. Verificar velocidad en pantalla

**Checklist:**
- [ ] Aceleración suave (rampa del VFD)
- [ ] Sin saltos bruscos
- [ ] Velocidad en pantalla se actualiza continuamente
- [ ] Velocidad objetivo alcanzada en ~6-8 segundos

**Logs esperados:**
```
I (xxxx) VFD_DRIVER_MODBUS: VFD Real: 39.06 Hz | Target: 78.13 Hz | Speed: 10.0 km/h
I (xxxx) VFD_DRIVER_MODBUS: VFD Real: 54.60 Hz | Target: 78.13 Hz | Speed: 10.0 km/h
I (xxxx) VFD_DRIVER_MODBUS: VFD Real: 68.00 Hz | Target: 78.13 Hz | Speed: 10.0 km/h
I (xxxx) VFD_DRIVER_MODBUS: VFD Real: 78.10 Hz | Target: 78.13 Hz | Speed: 10.0 km/h ✅
```

---

### Test 3: Modo COOLDOWN

**Procedimiento:**
1. Motor a 10.0 km/h
2. Presionar **COOL DOWN**
3. Observar desaceleración gradual
4. Verificar que llega a 0 km/h

**Checklist:**
- [ ] Rampa de desaceleración activa (software)
- [ ] Velocidad decrece gradualmente
- [ ] Pantalla muestra velocidad actualizándose
- [ ] Motor se detiene completamente
- [ ] Modo COOLDOWN se desactiva al llegar a 0

**Logs esperados:**
```
I (xxxx) CM_MASTER: COOLDOWN activado
I (xxxx) CM_MASTER: SET_SPEED: 8.5 km/h
I (xxxx) CM_MASTER: SET_SPEED: 7.0 km/h
...
I (xxxx) CM_MASTER: SET_SPEED: 0.0 km/h
```

---

### Test 4: Control de Inclinación

**Procedimiento:**
1. Presionar **CLIMB +** (1 vez) → Objetivo: 0.1%
2. Verificar que motor de inclinación se activa
3. Presionar **CLIMB -** → Verificar retorno

**Checklist:**
- [ ] Motor de inclinación responde
- [ ] Relés se activan correctamente
- [ ] Porcentaje en pantalla se actualiza
- [ ] Motor se detiene al alcanzar objetivo

**Logs esperados:**
```
I (xxxx) SLAVE: SET_INCLINE recibido: 0.5%
I (xxxx) SLAVE: Motor inclinación: SUBIENDO
I (xxxx) SLAVE: Inclinación actual: 0.3%
I (xxxx) SLAVE: Motor inclinación: DETENIDO (objetivo alcanzado)
```

---

### Test 5: Ventiladores

**Procedimiento:**
1. Presionar **HEAD** → Ciclar entre OFF/LOW/HIGH
2. Presionar **CHEST** → Ciclar entre OFF/LOW/HIGH

**Checklist:**
- [ ] Ventilador HEAD responde a comandos
- [ ] Ventilador CHEST responde a comandos
- [ ] Relés cambian de estado correctamente
- [ ] Estado en pantalla se actualiza

---

## 📊 Validación de Calibración

### Test de Precisión de Velocidad

**Velocidades a probar:** 5, 10, 15 km/h

| Velocidad Objetivo | Frecuencia VFD Esperada | Pulsos/seg Esperados | Tolerancia |
|--------------------|-------------------------|----------------------|------------|
| 5.0 km/h | 39.06 Hz | ~287 | ±2% |
| 10.0 km/h | 78.13 Hz | ~575 | ±2% |
| 15.0 km/h | 117.19 Hz | ~862 | ±2% |

**Procedimiento:**
1. Establecer velocidad objetivo
2. Esperar estabilización (5-10 seg)
3. Anotar:
   - Frecuencia VFD real (del log)
   - Pulsos/seg (del log)
   - Velocidad mostrada en pantalla

**Checklist de validación:**
- [ ] Error de velocidad < ±2%
- [ ] Frecuencia VFD coincide con esperada (±1 Hz)
- [ ] Pulsos/seg coinciden con esperados (±5%)
- [ ] Linealidad verificada (relación constante)

**Ejemplo de log correcto:**
```
I (xxxx) VFD_DRIVER_MODBUS: VFD Real: 78.10 Hz | Target: 78.13 Hz | Speed: 10.0 km/h
I (xxxx) SLAVE: Velocidad: 10.0 km/h (Pulsos/seg: 575)
```

✅ **Criterio de éxito:** Velocidad real ± 0.2 km/h del objetivo

---

## 📝 Logs Esperados

### Arranque Normal de Base

```
I (xxxx) cpu_start: Starting scheduler on CPU0
I (xxxx) SLAVE: Inicializando sistema Base...
I (xxxx) SPEED_SENSOR: Inicializando sensor de velocidad (PCNT) en GPIO 15
I (xxxx) SPEED_SENSOR: GPIO 15 configurado con pull-down
I (xxxx) VFD_DRIVER_MODBUS: Inicializando driver Modbus Master...
I (xxxx) VFD_DRIVER_MODBUS: VFD configurado para control por Modbus.
I (xxxx) SLAVE: Configuración UART completada
I (xxxx) SLAVE: Sistema listo - Esperando comandos...
```

### Arranque Normal de Consola

```
I (xxxx) cpu_start: Starting scheduler on CPU0
I (xxxx) MAIN: Inicializando sistema Consola...
I (xxxx) BSP: Display inicializado (1280x800)
I (xxxx) BSP: Touch GT911 configurado
I (xxxx) CM_MASTER: Inicializando maestro RS485...
I (xxxx) CM_MASTER: Master started successfully
I (xxxx) CM_MASTER: Heartbeat iniciado (300ms)
I (xxxx) CM_MASTER: Connected to slave
I (xxxx) UI: Interfaz gráfica cargada
```

### Durante Operación Normal (cada 2 seg)

```
// Base
I (xxxx) VFD_DRIVER_MODBUS: VFD Real: 78.10 Hz | Target: 78.13 Hz | Speed: 10.0 km/h
I (xxxx) SLAVE: Velocidad: 10.0 km/h (Pulsos/seg: 575)

// Consola
I (xxxx) CM_MASTER: GET_SENSOR_SPEED → 10.0 km/h
I (xxxx) UI: Pantalla actualizada: 10.0 km/h, 0.5%, 0 kcal
```

---

## 🔧 Troubleshooting

### Problema: VFD no responde

**Síntomas:**
```
E (xxxx) VFD_DRIVER_MODBUS: Error al LEER registro 0x2103: timeout
W (xxxx) VFD_DRIVER_MODBUS: Estado VFD: DISCONNECTED
```

**Soluciones:**
1. [ ] Verificar alimentación del VFD
2. [ ] Verificar cableado RS485 (A, B, GND)
3. [ ] Verificar parámetros F5-00, F5-01, F5-02 del VFD
4. [ ] Probar con baudrate diferente temporalmente
5. [ ] Medir señales con osciloscopio

---

### Problema: Sensor Hall no cuenta pulsos

**Síntomas:**
```
I (xxxx) SLAVE: Velocidad: 0.00 km/h (Pulsos/seg: 0)
```
*(motor está girando pero no se detectan pulsos)*

**Soluciones:**
1. [ ] Verificar conexión GPIO 15
2. [ ] Verificar alimentación del sensor Hall
3. [ ] Verificar polaridad del sensor
4. [ ] Medir señal del sensor con multímetro
5. [ ] Verificar que la corona tiene 12 dientes
6. [ ] Verificar distancia sensor-corona (debe ser < 5mm)

---

### Problema: Consola no conecta con Base

**Síntomas:**
```
W (xxxx) CM_MASTER: Slave disconnected (no responses for 1000ms)
W (xxxx) CM_MASTER: Timeout for SEQ=42, retry 1/3
```

**Soluciones:**
1. [ ] Verificar cableado RS485 entre módulos
2. [ ] Verificar que ambos usan mismo baudrate (115200)
3. [ ] Verificar que pines TX/RX no están cruzados
4. [ ] Verificar GND común entre módulos
5. [ ] Revisar logs de Base para ver si recibe tramas

---

### Problema: Velocidad incorrecta en pantalla

**Síntomas:**
```
VFD: 78.10 Hz
Pantalla: 3.84 km/h (debería ser ~10 km/h)
```

**Verificar:**
1. [ ] `g_calibration_factor = 0.0174` en Base/main/main.c:46
2. [ ] `KPH_TO_HZ_RATIO = 7.8125` en Base/main/vfd_driver.c:57
3. [ ] Línea 290 de vfd_driver.c usa `kph * KPH_TO_HZ_RATIO`
4. [ ] Corona tiene 12 dientes (no 6 ni 24)
5. [ ] Recompilar y flashear

---

### Problema: Motor acelera/desacelera bruscamente

**Síntomas:**
- Saltos bruscos de velocidad
- Vibración excesiva

**Verificar:**
1. [ ] Parámetros de rampa del VFD (F1-14, F1-15)
2. [ ] Valores recomendados: 6-8 segundos
3. [ ] Configurar en panel frontal del VFD
4. [ ] Si aún es brusco, aumentar a 10 segundos

---

### Problema: Pantalla congelada durante COOLDOWN

**Síntomas:**
- Velocidad en pantalla no se actualiza durante rampa

**Esto es esperado (ver Tarea #6 en MEJORAS_PENDIENTES.md)**
- No es crítico
- La velocidad se actualiza al finalizar rampa
- Mejora planificada pero opcional

---

## ✅ Criterios de Éxito para Primera Prueba

### Mínimo Viable (DEBE funcionar)

- [x] ✅ Base compila sin errores
- [x] ✅ Consola compila sin errores
- [ ] ✅ Comunicación RS485 funcional
- [ ] ✅ Comunicación Modbus VFD funcional
- [ ] ✅ Motor arranca y para correctamente
- [ ] ✅ Sensor Hall lee velocidad
- [ ] ✅ Pantalla muestra información

### Deseable (Debería funcionar)

- [ ] ✅ Precisión de velocidad < ±2%
- [ ] ✅ Rampas de aceleración suaves
- [ ] ✅ Control de inclinación funcional
- [ ] ✅ Ventiladores responden
- [ ] ✅ Modo COOLDOWN funcional

### Opcional (Puede fallar sin impedir prueba)

- [ ] Calibración automática al encender
- [ ] Retorno a 0% al apagar
- [ ] Sensor de fin de carrera (hardware pendiente)

---

## 📊 Tabla de Registro de Pruebas

| Test | Esperado | Resultado | Notas |
|------|----------|-----------|-------|
| Comunicación RS485 | Conectado | ⬜ | |
| Comunicación VFD | OK | ⬜ | |
| Sensor Hall GPIO 15 | Funciona | ⬜ | |
| Arranque motor | Suave | ⬜ | |
| Parada motor | Suave | ⬜ | |
| Velocidad 5 km/h | ±0.1 km/h | ⬜ | |
| Velocidad 10 km/h | ±0.2 km/h | ⬜ | |
| Velocidad 15 km/h | ±0.3 km/h | ⬜ | |
| Modo COOLDOWN | Funciona | ⬜ | |
| Inclinación +0.5% | Funciona | ⬜ | |
| Inclinación -0.5% | Funciona | ⬜ | |
| Ventilador HEAD | Funciona | ⬜ | |
| Ventilador CHEST | Funciona | ⬜ | |

---

## 🎉 Conclusión

**El sistema está listo para la primera prueba si:**

✅ Todos los ítems de "Mínimo Viable" están verificados
✅ Al menos 80% de "Deseable" funciona
✅ No hay errores críticos en logs

**Después de la primera prueba:**
1. Anotar observaciones en tabla de registro
2. Documentar problemas encontrados
3. Priorizar fixes según criticidad
4. Repetir prueba hasta alcanzar criterios de éxito

---

**Última actualización:** 2025-11-06
**Versión del documento:** 1.0
**Commit base:** `da6ce70c`

✅ **SISTEMA LISTO PARA PRIMERA PRUEBA**
