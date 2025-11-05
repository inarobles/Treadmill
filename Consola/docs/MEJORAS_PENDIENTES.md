# 📋 MEJORAS PENDIENTES - Sistema de Control de Inclinación

**Documento generado:** 2025-11-05
**Última actualización:** 2025-11-05

Este documento registra las mejoras planificadas para el sistema de control de inclinación de la cinta de correr, tanto en **Consola** como en **Base**.

---

## 🔴 PRIORIDAD ALTA

### 1. Sistema de Retorno a 0% al Salir de la Aplicación

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

### 2. Sistema de Calibración Automática al Encender

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

### 3. Conectar Sensor de Fin de Carrera (GPIO 35 en Base)

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

### 4. Ajustar Intervalo de Repetición de Botones CLIMB

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

## 🟢 PRIORIDAD BAJA

### 5. Añadir Logs de Debugging Detallados

**Descripción:**
Mejorar logs de debugging para facilitar diagnóstico futuro de problemas del motor lineal.

**Logs sugeridos:**
- **En Consola:**
  - Timestamp de cada cambio de `target_incline_pct`
  - Diferencia entre objetivo y posición real recibida
  - Tiempo transcurrido desde último comando enviado

- **En Base:**
  - Estado de la máquina de estados del motor con timestamp
  - Delta de posición calculado en cada ciclo
  - Eventos de activación/desactivación de relés

**Archivos afectados:**
- `Consola/main/ui.c` - Añadir logs en funciones `ui_climb_inc/dec()`
- `Consola/main/cm_master.c` - Logs en heartbeat y procesamiento de respuestas
- `Base/main/main.c` - Logs en `incline_control_task()`

**Beneficio:**
Facilitar diagnóstico de problemas en producción mediante análisis de logs.

---

### 6. Indicador Visual de Movimiento del Motor

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
| 🔴 Alta | Retorno a 0% al salir | Media | Alto |
| 🔴 Alta | Calibración al encender | Media-Alta | Alto |
| 🟠 Media | Sensor fin de carrera | Baja (hardware) | Alto |
| 🟠 Media | Ajustar repetición botones | Baja | Medio |
| 🟢 Baja | Logs de debugging | Baja | Medio |
| 🟢 Baja | Indicador visual | Media | Bajo |

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

- **Informe de análisis del sistema:** Documento generado el 2025-11-05
- **Commit de correcciones:** `b761049` - Corrige sistema de control de inclinación
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
