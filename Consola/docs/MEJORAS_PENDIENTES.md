# 📋 MEJORAS PENDIENTES - Sistema de Control de Cinta de Correr

**Documento generado:** 2025-11-05
**Última actualización:** 2025-11-05 (Incorporadas recomendaciones del informe exhaustivo de velocidad)

Este documento registra las mejoras planificadas para el sistema de control de la cinta de correr, tanto en **Consola** como en **Base**.

---

## 🔴 PRIORIDAD ALTA

### 1. Calibración del Sistema de Velocidad del Motor

**Descripción:**
Calibrar el parámetro `g_calibration_factor` que convierte los pulsos del sensor de velocidad a velocidad real en km/h. El valor actual (0.00875) es un placeholder teórico que necesita verificación con hardware real.

**Requisitos:**
- Configurar VFD a una frecuencia conocida (ej: 30 Hz)
- Medir velocidad real de la cinta con método físico:
  - Marcar un punto en la cinta
  - Medir tiempo de 1 vuelta completa
  - Calcular: `velocidad_real = (perímetro_cinta_m × 3.6) / tiempo_segundos`
- Leer pulsos del sensor durante 1 segundo
- Calcular nuevo factor: `g_calibration_factor = velocidad_real_medida / pulsos_por_segundo`
- Actualizar valor en código

**Parámetro a calibrar:**
- **`g_calibration_factor`** en `Base/main/main.c:44`
- Valor actual: `0.00875` (placeholder)
- Fórmula: `velocidad_kmh = (pulsos/segundo) × g_calibration_factor`

**Proceso de calibración:**
1. Establecer VFD a frecuencia fija (ej: 30 Hz mediante comando SET_SPEED)
2. Medir velocidad real de cinta físicamente
3. Registrar pulsos/segundo del sensor (GPIO 34)
4. Calcular y actualizar `g_calibration_factor`
5. Verificar con al menos 3 velocidades diferentes (baja, media, alta)
6. Documentar resultados de calibración

**Archivos afectados:**
- `Base/main/main.c` - Actualizar `g_calibration_factor` (línea 44)
- Posiblemente `Base/main/vfd_driver.c` - Verificar `KPH_TO_HZ_RATIO` (línea 53) si es necesario
- Documentación: `Base/README.md` - Actualizar sección de calibración (líneas 215-241)

**Relación con otros parámetros:**
- **Sensor de velocidad:** GPIO 34 con PCNT (Pulse Counter)
- **Intervalo de medición:** 500ms (`SPEED_UPDATE_INTERVAL_MS`)
- **Ratio VFD:** 60 Hz = 20 km/h (definido en `KPH_TO_HZ_RATIO`)

**Beneficio:**
Asegurar que la velocidad mostrada en pantalla coincida exactamente con la velocidad real de la cinta, crítico para seguridad y experiencia del usuario.

---

### 2. Sistema de Retorno a 0% al Salir de la Aplicación

**Descripción:**
Implementar un sistema automático que lleve la cinta a 0% de inclinación cuando el usuario sale de la pantalla principal.

**Requisitos:**
- Detectar evento de salida de la pantalla principal (cierre de aplicación, apagado, etc.)
- Enviar comando para llevar motor a 0% de forma automática
- Mostrar indicador visual (opcional) de que el motor está retornando a posición inicial
- Asegurar que el proceso se complete antes de permitir el apagado completo

**Archivos afectados:**
- `Consola/main/ui.c` - Agregar hook de salida de pantalla principal
- `Consola/main/cm_master.c` - Posiblemente añadir comando de "return to home"

**Beneficio:**
Garantiza que la cinta siempre quede en posición plana al apagar, evitando sorpresas al encender.

---

### 3. Sistema de Calibración Automática al Encender

**Descripción:**
Al encender la Consola, verificar si la inclinación está en 0%. Si no lo está, bloquear todas las funciones y forzar el retorno a 0% antes de permitir el uso.

**Requisitos:**
- Al arrancar, solicitar posición actual a Base mediante `GET_INCLINE_POSITION`
- Si posición != 0%, iniciar secuencia de retorno automático
- Mostrar pantalla de "Calibrando..." o "Retornando a posición inicial..."
- Bloquear todos los botones excepto STOP/emergencia durante el proceso
- Una vez alcanzado 0%, desbloquear funciones y mostrar pantalla principal

**Comportamiento esperado:**
```
ARRANQUE → GET_INCLINE_POSITION → ¿Es 0%?
                                    ├─ SÍ  → Continuar normal
                                    └─ NO  → Mostrar pantalla "Calibrando..."
                                           → Enviar SET_INCLINE(0%)
                                           → Esperar hasta alcanzar 0%
                                           → Continuar normal
```

**Archivos afectados:**
- `Consola/main/main.c` o `Consola/main/ui.c` - Añadir lógica de startup
- Crear nueva pantalla LVGL de "Calibrando" (opcional)
- `Consola/main/cm_master.c` - Quizás añadir función `cm_master_wait_for_zero()`

**Beneficio:**
Siempre partir de una posición conocida (0%), eliminando inconsistencias por apagados abruptos.

---

## 🟠 PRIORIDAD MEDIA

### 4. Conectar Sensor de Fin de Carrera (GPIO 35 en Base)

**Descripción:**
Habilitar el sensor de fin de carrera físico para realizar homing real al arrancar Base.

**Estado actual:**
El código ya tiene soporte para el sensor en `Base/main/main.c`, pero está deshabilitado porque el hardware no está conectado:
```c
case INCLINE_MOTOR_HOMING:
    // TEMPORAL: Sensor de fin de carrera desconectado - anular homing
    // if (gpio_get_level(INCLINE_LIMIT_SWITCH_PIN) == 0) { ... }
```

**Requisitos:**
- Conectar sensor de fin de carrera al GPIO 35 de Base
- Descomentar código de detección de sensor
- Probar homing real al arrancar Base
- Documentar conexión del sensor en `Base/docs/HARDWARE.md`

**Archivos afectados:**
- `Base/main/main.c` - Descomentar código de sensor (líneas ~554-572)
- Hardware físico

**Beneficio:**
Referencia real de posición 0%, evitando acumulación de errores de posición.

---

### 5. Ajustar Intervalo de Repetición de Botones CLIMB

**Descripción:**
Evaluar si el intervalo de repetición actual (6.7 veces/segundo) es adecuado con la nueva velocidad del motor (1.5%/segundo).

**Estado actual:**
- Velocidad de repetición: cada 150ms (6.7 veces/seg)
- Nueva velocidad motor: 1.5%/segundo
- Incremento por pulsación: 0.1%

**Análisis:**
- Cada pulsación cambia objetivo en 0.1%
- Motor tarda ~66ms en alcanzar 0.1%
- Repetición cada 150ms podría acumular comandos

**Opciones:**
1. Reducir tasa de repetición (ej: 300ms en lugar de 150ms)
2. Implementar "debouncing inteligente" que no envíe comando si motor aún no alcanzó objetivo previo
3. Dejar como está y confiar en el sistema de ACK/NAK

**Archivos afectados:**
- `Consola/main/button_handler.c` - Constante `REPEAT_INTERVAL_MS` (línea 23)

**Beneficio:**
Evitar saturación de comandos y comportamiento errático del motor.

---

### 6. Resolver Saturación RS485 Durante Cooldown

**Descripción:**
Durante el modo COOLDOWN (rampa de velocidad hacia 0 km/h), la Consola envía múltiples comandos SET_SPEED consecutivos. Mientras espera el ACK de cada comando, NO se envían comandos GET_SENSOR_SPEED, causando que la velocidad real mostrada en pantalla se "congele" durante la rampa.

**Problema identificado:**
En `Consola/main/cm_master.c:718-736`, cuando hay un comando SET pendiente de ACK:
```c
if (has_pending_set) {
    // Solo polling crítico (status VFD)
    send_get_status();
    vTaskDelay(pdMS_TO_TICKS(CM_MASTER_HEARTBEAT_MS));  // 300ms sin lectura de velocidad
} else {
    // Polling completo solo si no hay SET pendiente
    send_get_sensor_speed();  // ← NO SE EJECUTA durante rampas
}
```

**Impacto:**
- UX deficiente: La velocidad mostrada se "congela" en pantalla durante cooldown
- No es crítico para seguridad, pero confunde al usuario

**Soluciones propuestas:**
1. Reducir timeout de ACK de 100ms a 50ms para liberar el bus más rápido
2. Permitir envío de GET_SENSOR_SPEED incluso con SET pendiente (intercalar comandos)
3. Implementar rampa de velocidad en Consola para reducir frecuencia de comandos SET

**Archivos afectados:**
- `Consola/main/cm_master.c` - Modificar lógica de heartbeat (líneas 663-744)
- `Consola/main/cm_master.c` - Ajustar timeout de ACK (línea 32: `CM_MASTER_TIMEOUT_MS`)

**Beneficio:**
Mejora la experiencia de usuario mostrando actualización continua de velocidad durante rampas.

---

### 7. Documentar y Configurar Parámetros de Rampa del VFD

**Descripción:**
El VFD SU300 recibe cambios de frecuencia directos sin rampa gradual implementada en software. El VFD aplica su rampa interna mediante parámetros F1-14 (tiempo de aceleración) y F1-15 (tiempo de desaceleración), pero se desconocen los valores configurados actualmente.

**Problema identificado:**
En `Base/main/vfd_driver.c:285-290`, los cambios de velocidad se envían directamente:
```c
// Cambio directo de frecuencia (sin rampa software)
float freq_hz = (kph / 20.0f) * 60.0f;
uint16_t freq_centi_hz = (uint16_t)(freq_hz * 100.0f);
vfd_write_register(VFD_REG_FREQ, freq_centi_hz);  // ← Sin rampa software
```

**Impacto:**
- Aceleración/desaceleración depende del VFD, no del software
- No hay control explícito de la suavidad de la rampa
- Dificulta debugging (no sabemos cuánto tarda el VFD en alcanzar objetivo)

**Tareas:**
1. Leer parámetros actuales del VFD:
   - **F1-14:** Tiempo de aceleración (segundos)
   - **F1-15:** Tiempo de desaceleración (segundos)
2. Documentar valores en `Base/docs/VFD_CONFIG.md` (nuevo archivo)
3. Evaluar si los valores son adecuados para uso de cinta de correr
4. (Opcional) Implementar rampa en software para mayor control

**Registros Modbus a leer:**
- F1-14 = Dirección 0x010E (lectura mediante vfd_read_register)
- F1-15 = Dirección 0x010F

**Archivos afectados:**
- `Base/main/vfd_driver.c` - Añadir función para leer parámetros F1-XX
- `Base/docs/VFD_CONFIG.md` - Crear documentación de configuración VFD

**Beneficio:**
Control y visibilidad sobre el comportamiento de aceleración/desaceleración del motor.

---

### 8. Implementar Rampa de Aceleración en Software para Velocidad

**Descripción:**
Actualmente, los botones SPEED +/- envían cambios de velocidad directos al VFD sin implementar una rampa gradual en software. La Consola define modos de rampa (`ramp_mode_t`) pero solo se usan para STOP/COOLDOWN/RESUME, no para cambios normales de velocidad.

**Estado actual:**
- ✅ Rampa implementada: STOP → 0 km/h
- ✅ Rampa implementada: COOLDOWN → 0 km/h
- ✅ Rampa implementada: RESUME → velocidad previa
- ❌ NO HAY rampa: Incrementos normales (ej: 5 km/h → 10 km/h)

**Variable definida pero no usada:**
En `treadmill_state.h:39` existe `cooldown_climb_ramp_rate` pero no hay equivalente para velocidad.

**Propuesta:**
1. Añadir nueva variable `speed_ramp_rate_kmh_per_sec` en `treadmill_state.h`
2. Modificar `ui_speed_inc()` y `ui_speed_dec()` en `ui.c:1781-1843` para:
   - No enviar comando inmediato al presionar botón
   - Actualizar solo `target_speed` local
   - Dejar que una tarea periódica ajuste gradualmente hacia el objetivo
3. Crear tarea de rampa de velocidad que incremente/decremente según `speed_ramp_rate`

**Archivos afectados:**
- `Consola/main/treadmill_state.h` - Añadir `speed_ramp_rate_kmh_per_sec`
- `Consola/main/ui.c` - Modificar lógica de `ui_speed_inc/dec()`
- `Consola/main/ui.c` - Crear tarea de rampa de velocidad (similar a rampa de inclinación)

**Beneficio:**
Control unificado de rampas, experiencia de usuario más suave y predecible.

**Prioridad:** Opcional - El VFD ya proporciona rampa interna, pero implementarlo en software da mayor control.

---

## 🟢 PRIORIDAD BAJA

### 9. Limpiar Variable No Usada: cooldown_climb_ramp_rate

**Descripción:**
En `Consola/main/treadmill_state.h:39` se define la variable `cooldown_climb_ramp_rate` que está pensada para "rampa de cooldown de climb", pero actualmente no se usa de manera consistente para el control de velocidad.

**Problema identificado:**
```c
typedef struct {
    ...
    float cooldown_climb_ramp_rate;  // ← Definida pero NO USADA para velocidad
    ...
} TreadmillState;
```

**Impacto:**
- Código confuso (variable definida pero sin uso claro)
- No hay rampa de aceleración normal en Consola para velocidad
- Inconsistencia en la nomenclatura

**Soluciones propuestas:**
1. Renombrar a `cooldown_ramp_rate` (genérica para velocidad e inclinación)
2. O añadir variables separadas: `speed_ramp_rate` y `climb_ramp_rate`
3. Documentar claramente el uso de cada variable de rampa

**Archivos afectados:**
- `Consola/main/treadmill_state.h` - Línea 39
- `Consola/main/ui.c` - Revisar usos de la variable

**Beneficio:**
Código más limpio y mantenible, nomenclatura consistente.

---

### 10. Verificar y Calibrar Ratio KPH_TO_HZ

**Descripción:**
La relación 20 km/h = 60 Hz usada para convertir velocidad a frecuencia del VFD es asumida, no calibrada. Este valor depende del diámetro del rodillo motor, relación de transmisión mecánica y configuración del VFD.

**Constante actual:**
En `Base/main/vfd_driver.c:53`:
```c
#define KPH_TO_HZ_RATIO    (60.0f / 20.0f)  // = 3.0
// Asume: 20 km/h = 60 Hz (NO verificado con hardware)
```

**Impacto:**
Si la relación real es diferente, la velocidad objetivo enviada al VFD será incorrecta.

**Ejemplo de error:**
- Si realmente es 20 km/h = 50 Hz (en lugar de 60 Hz)
- Al solicitar 10 km/h, se envían 30 Hz cuando deberían ser 25 Hz
- Error del 20% en la velocidad objetivo

**Proceso de verificación:**
1. Después de calibrar `g_calibration_factor` (mejora #1)
2. Establecer VFD a 30 Hz mediante comando SET_SPEED
3. Medir velocidad real de la cinta físicamente
4. Comparar con velocidad reportada por sensor
5. Si no coinciden, ajustar `KPH_TO_HZ_RATIO`
6. Documentar valor calibrado

**Archivos afectados:**
- `Base/main/vfd_driver.c` - Línea 53
- `Base/docs/CALIBRATION.md` - Documentar proceso y resultado

**Beneficio:**
Asegurar que la velocidad objetivo se traduce correctamente a frecuencia del VFD.

**Nota:** Esta calibración debe hacerse DESPUÉS de la mejora #1 (calibración del sensor).

---

### 11. Evaluar Intervalo de Repetición de Botones SPEED

**Descripción:**
Los botones SPEED +/- tienen un intervalo de repetición de 150ms (6.7 veces/segundo), similar a los botones CLIMB. Sin embargo, dado que los cambios de velocidad dependen del VFD (con tiempos de rampa desconocidos), debería evaluarse si este intervalo es óptimo.

**Parámetros actuales:**
En `Consola/main/button_handler.c:97`:
```c
#define REPEAT_INTERVAL_MS 150  // Aproximadamente 6.7 veces por segundo
```

**Análisis:**
- Cada pulsación incrementa 0.1 km/h
- Tasa de cambio máxima: 0.67 km/h por segundo
- VFD se actualiza cada 200ms
- RS485 heartbeat cada 300ms
- Cola de comandos pendientes: máximo 4

**Riesgo potencial:**
Durante pulsación larga, podría causar acumulación de comandos si el VFD tarda mucho en responder ACK, aunque el sistema de cola lo maneja correctamente.

**Monitoreo requerido:**
- Revisar logs en uso real para detectar "Pending queue full!"
- Si aparece frecuentemente, considerar aumentar intervalo a 200-300ms

**Archivos afectados:**
- `Consola/main/button_handler.c` - Línea 97 (`REPEAT_INTERVAL_MS`)

**Beneficio:**
Prevenir posible saturación de comandos en escenarios de uso intenso.

**Prioridad:** Muy baja - El funcionamiento actual es aceptable, solo requiere monitoreo.

---

### 12. Añadir Logs de Debugging Detallados

**Descripción:**
Mejorar logs de debugging para facilitar diagnóstico futuro de problemas del sistema de velocidad y motor lineal.

**Logs sugeridos:**
- **En Consola:**
  - Timestamp de cada cambio de `target_speed` y `target_incline_pct`
  - Diferencia entre objetivo y velocidad/posición real recibida
  - Tiempo transcurrido desde último comando enviado
  - Eventos de saturación de cola RS485

- **En Base:**
  - Estado de la máquina de estados del motor con timestamp
  - Cambios de frecuencia del VFD
  - Lecturas del sensor de velocidad (pulsos/segundo)
  - Delta de posición calculado en cada ciclo
  - Eventos de activación/desactivación de relés

**Archivos afectados:**
- `Consola/main/ui.c` - Añadir logs en funciones `ui_speed_inc/dec()` y `ui_climb_inc/dec()`
- `Consola/main/cm_master.c` - Logs en heartbeat y procesamiento de respuestas
- `Base/main/main.c` - Logs en `incline_control_task()` y `speed_update_task()`
- `Base/main/vfd_driver.c` - Logs en `vfd_control_task()`

**Beneficio:**
Facilitar diagnóstico de problemas en producción mediante análisis de logs.

---

### 13. Indicador Visual de Movimiento del Motor

**Descripción:**
Añadir indicador en la UI que muestre cuando el motor lineal está activo (subiendo/bajando).

**Propuestas:**
1. Icono animado de "motor en movimiento" junto al valor de inclinación
2. Cambio de color del texto de inclinación (ej: azul cuando está en movimiento)
3. Barra de progreso mostrando objetivo vs. posición actual

**Archivos afectados:**
- `Consola/main/ui.c` - Añadir elemento visual LVGL
- Posiblemente añadir imagen/icono en assets

**Beneficio:**
Feedback visual inmediato al usuario de que el sistema está respondiendo a sus comandos.

---

## 📊 RESUMEN DE PRIORIDADES

| Prioridad | Mejora | Complejidad | Impacto |
|-----------|--------|-------------|---------|
| 🔴 Alta | 1. Calibración velocidad motor | Media | Crítico |
| 🔴 Alta | 2. Retorno a 0% al salir | Media | Alto |
| 🔴 Alta | 3. Calibración al encender | Media-Alta | Alto |
| 🟠 Media | 4. Sensor fin de carrera | Baja (hardware) | Alto |
| 🟠 Media | 5. Ajustar repetición botones CLIMB | Baja | Medio |
| 🟠 Media | 6. Resolver saturación RS485 en cooldown | Media | Medio-Alto |
| 🟠 Media | 7. Documentar parámetros rampa VFD | Baja | Medio |
| 🟠 Media | 8. Implementar rampa de velocidad en software | Media-Alta | Medio |
| 🟢 Baja | 9. Limpiar variable cooldown_climb_ramp_rate | Baja | Bajo |
| 🟢 Baja | 10. Verificar y calibrar KPH_TO_HZ_RATIO | Media | Medio |
| 🟢 Baja | 11. Evaluar intervalo repetición botones SPEED | Baja | Bajo |
| 🟢 Baja | 12. Logs de debugging detallados | Baja | Medio |
| 🟢 Baja | 13. Indicador visual de movimiento | Media | Bajo |

---

## 📝 NOTAS DE IMPLEMENTACIÓN

### Consideraciones para Retorno a 0% al Salir:
- **¿Cuánto tiempo esperar?** Con velocidad de 1.5%/seg, desde 15% tardaría máximo 10 segundos
- **¿Mostrar pantalla de "Retornando a 0%"?** Sí, recomendado para evitar que usuario piense que está colgado
- **¿Permitir cancelación?** No, es una operación de seguridad

### Consideraciones para Calibración al Encender:
- **¿Timeout?** Máximo 15 segundos (peor caso: desde 15% a 0%)
- **¿Qué hacer si falla?** Mostrar error y no permitir uso hasta resolverlo
- **¿Verificar sensor de fin de carrera si está disponible?** Sí, usarlo como referencia adicional

---

## 🔗 REFERENCIAS

- **Informe exhaustivo de velocidad:** Análisis completo del sistema de control de velocidad (2025-11-05)
- **Informe de análisis del sistema de inclinación:** Documento generado el 2025-11-05
- **Commit de correcciones inclinación:** `b761049` - Corrige sistema de control de inclinación
- **Commit de calibración velocidad:** `3541547` - Añade tarea de calibración de velocidad del motor
- **Documentación del protocolo RS485:** `Consola/docs/COMUNICACION_RS485.md`
- **Documentación de hardware Base:** `Base/README.md`

---

## ✅ MEJORAS YA IMPLEMENTADAS (Historial)

### 2025-11-05 - Correcciones Sistema de Inclinación
- ✅ Corregido bug de lectura de inclinación real en UI
- ✅ Eliminado código duplicado (`g_real_incline_pct`)
- ✅ Optimizada velocidad del motor (0.05%/s → 1.5%/s)
- **Commit:** `b761049`

---

**Documento vivo:** Este archivo se actualizará conforme se implementen las mejoras o surjan nuevas necesidades.
