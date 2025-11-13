# Instrucciones para Claude Code

## Procedimiento de Trabajo con el Usuario

### Antes de Hacer Cualquier Cambio

**SIEMPRE seguir este procedimiento:**

1. **Explicar lo que entendí** de la solicitud del usuario
2. **Describir EXACTAMENTE qué cambios voy a hacer** (qué archivos, qué líneas, qué valores)
3. **ESPERAR aprobación explícita** del usuario ("si", "apruebo", "adelante")
4. **SOLO ENTONCES ejecutar los cambios**

### Cómo Actuar

- **No adivinar**: Si hay ambigüedad en la solicitud, PREGUNTAR antes de asumir
- **No hacer suposiciones sobre el hardware**: El usuario conoce el comportamiento real, yo solo veo código
- **Ser conciso**: Respuestas estructuradas pero directas, sin divagaciones innecesarias

### Errores que NO Repetir

- ❌ Hacer cambios sin aprobación del usuario
- ❌ Proponer soluciones antes de entender completamente el problema
- ❌ Asumir que entendí cuando aún tengo dudas

### Buena Práctica al Finalizar

- ✅ **Siempre proporcionar el link del PR** cuando se hayan hecho cambios
  - Formato: `https://github.com/inarobles/Treadmill/compare/<nombre-rama>`

## Flujo de Trabajo Git

### Principios Fundamentales

1. **El usuario solo trabaja con la rama `main`**
   - Todos los cambios finales deben estar en `main`
   - El usuario acepta Pull Requests en GitHub manualmente
   - No intentar hacer merge directamente, solo crear PRs

2. **Claude trabaja en ramas feature**
   - Crear ramas con el patrón: `claude/<descripcion>-<session-id>`
   - Hacer commits descriptivos en español
   - Pushear a origin para que el usuario pueda ver el PR

### Proceso de Trabajo

#### 1. Iniciar nueva tarea
```bash
# Asegurarse de estar en main actualizado
git checkout main
git pull origin main

# Crear nueva rama desde main
git checkout -b claude/<descripcion-tarea>-<session-id>
```

#### 2. Desarrollar y commitear
```bash
# Hacer cambios en archivos
# ...

# Commitear con mensaje descriptivo en español
git add <archivos>
git commit -m "Descripción clara del cambio

- Detalle 1
- Detalle 2
- Detalle 3
"
```

#### 3. Pushear para crear PR
```bash
# IMPORTANTE: La rama debe empezar con 'claude/' y terminar con el session-id
# o el push fallará con error 403
git push -u origin claude/<nombre-rama>-<session-id>
```

#### 4. Esperar merge del usuario
- **NO** hacer merge automático
- **NO** usar `gh pr create` (está deshabilitado)
- El usuario creará y aceptará el PR en GitHub manualmente
- El usuario te avisará cuando haya hecho el merge

#### 5. Sincronizar main después del merge
```bash
git checkout main
git fetch origin main
git reset --hard origin/main
```

## Estructura del Proyecto

### Proyecto Treadmill - Consola para Cinta de Correr

#### Directorios Principales
- `/Base` - Componentes base y esp-modbus
- `/Consola` - Código principal de la consola (ESP32 con pantalla táctil)
  - `/main` - Código fuente principal
    - `ui.c` - Interfaz de usuario con LVGL
    - `cm_master.c` - Comunicación maestro-esclavo (protocolo ASCII/UART)
    - `treadmill_state.c` - Estado global de la cinta
    - `button_handler.c` - Manejo de botones físicos
    - `touch_driver.c` - Driver pantalla táctil
    - `wifi_client.c` - Cliente WiFi
    - `ble_client.h` - Cliente BLE para monitor cardíaco

#### Funcionalidades Clave
- Control de velocidad (km/h con decimales)
- Control de inclinación (% en unidades enteras, sin decimales)
- Comunicación UART con placa esclavo (motor VFD, actuador inclinación)
- Pantalla táctil con LVGL
- Monitor BLE de frecuencia cardíaca
- Cálculo de calorías, distancia, pace

## Convenciones de Código

### Mensajes de Commit
- En español
- Primera línea: resumen conciso (50-72 caracteres)
- Líneas siguientes: detalles con viñetas si es necesario
- Usar verbos en presente: "Añade", "Modifica", "Elimina", "Corrige"

Ejemplo:
```
Cambia inclinación a unidades enteras (sin decimales)

- Incremento/decremento de 1% en lugar de 0.1%
- Visualización sin decimales en pantalla principal
- Modo SET ajustado para 2 dígitos (como WEIGHT)
- Actualiza todos los labels relacionados con climb y grados
```

### Estilo de Código C
- Comentarios en español
- Nombres de variables en inglés siguiendo convención snake_case
- Logs con ESP_LOGI, ESP_LOGW, ESP_LOGE
- Respetar indentación existente (espacios, no tabs)

## Comandos Git Importantes

### Verificar estado
```bash
git status
git log --oneline -5
git log origin/main..HEAD --oneline  # Ver commits que faltan en main
```

### Manejo de ramas
```bash
git branch -a                         # Ver todas las ramas
git checkout <rama>                   # Cambiar de rama
git branch -d <rama>                  # Eliminar rama local (después de merge)
```

### Push con reintentos
Si el push falla por red, reintentar hasta 4 veces con backoff exponencial (2s, 4s, 8s, 16s).

**IMPORTANTE**: Solo las ramas que empiezan con `claude/` y terminan con el session-id pueden hacer push.

## Resolución de Problemas

### Push rechazado con 403
- Verificar que la rama empiece con `claude/`
- Verificar que la rama termine con el session-id correcto
- No intentar push a `main` directamente

### Ramas divergentes
```bash
# Si main local diverge de origin/main después de un merge
git checkout main
git reset --hard origin/main
```

### Conflictos de merge
- El usuario resolverá conflictos en GitHub si es necesario
- No intentar resolver automáticamente

## Notas Adicionales

- El proyecto usa ESP-IDF v5.5.1
- Compilación: `idf.py build` (desde directorio `/Consola`)
- El sistema tiene optimistic updates deshabilitados para inclinación
- La inclinación funciona con valores enteros (sin decimales)
- La velocidad funciona con un decimal (ej: 5.1 km/h)

## Recordatorios

1. ✅ Siempre trabajar en ramas `claude/*`
2. ✅ Pushear para que el usuario vea el PR
3. ✅ Esperar confirmación del usuario antes de continuar
4. ✅ Sincronizar `main` después de cada merge
5. ❌ No hacer merge automático
6. ❌ No usar `gh pr create`
7. ❌ No pushear directamente a `main`
