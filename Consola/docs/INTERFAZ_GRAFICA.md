# Interfaz Gráfica - LVGL v8

Documentación completa del sistema de interfaz gráfica basado en LVGL v8.

## Índice

- [Descripción General](#descripción-general)
- [Pantallas del Sistema](#pantallas-del-sistema)
- [Arquitectura y Tareas](#arquitectura-y-tareas)
- [Sistema de Rampas](#sistema-de-rampas)
- [Gestión de Estado](#gestión-de-estado)
- [Fuentes Personalizadas](#fuentes-personalizadas)
- [Integración con Hardware](#integración-con-hardware)
- [Ejemplos de Código](#ejemplos-de-código)

## Descripción General

La interfaz gráfica utiliza LVGL (Light and Versatile Graphics Library) v8 con buffers en PSRAM para renderizado suave en pantalla de 10.1" 1280x800.

**Características**:
- Pantallas múltiples con transiciones
- Teclado virtual para entrada de datos
- Fuentes personalizadas (Chivo Mono)
- Actualización en tiempo real de métricas
- Integración con touch GT911

**Archivos Principales**:
- `ui.c/h`: Pantallas principales y lógica
- `ui_wifi.c`: Pantallas de configuración WiFi
- `display_driver.c/h`: Driver MIPI-DSI
- `touch_driver.c/h`: Driver táctil GT911

## Pantallas del Sistema

### 1. Pantalla de Selección de Entrenamiento

**Nombre**: `scr_training_select`

**Elementos**:
- 5 botones de entrenamiento:
  - Free (Libre)
  - Itsaso
  - Ina
  - Alain
  - Urko
- Botón de configuración WiFi
- Logo/título

**Funcionalidad**:
```c
void ui_select_training(int training_number) {
    g_treadmill_state.selected_training = training_number;
    // Transición a pantalla de peso o principal
}
```

**Layout**:
```
┌───────────────────────────────────────┐
│  SELECCIÓN DE ENTRENAMIENTO          │
│                                       │
│  ┌─────┐  ┌─────┐  ┌─────┐          │
│  │Free │  │Itsaso│ │ Ina │          │
│  └─────┘  └─────┘  └─────┘          │
│                                       │
│  ┌─────┐  ┌─────┐                    │
│  │Alain│  │Urko │                    │
│  └─────┘  └─────┘                    │
│                                       │
│  [WiFi Config]                        │
└───────────────────────────────────────┘
```

### 2. Pantalla de Entrada de Peso

**Nombre**: `scr_weight_entry`

**Mostrada cuando**:
- Primera vez que se usa la cinta
- Usuario no ha ingresado peso previamente

**Elementos**:
- Teclado numérico (0-9)
- Display del peso ingresado
- Botón "Confirmar"
- Botón "Saltar"

**Funcionalidad**:
```c
void ui_weight_entry(void) {
    // Mostrar teclado
    // Usuario ingresa peso (kg)
    // Guardar en g_treadmill_state.user_weight_kg
    // Transición a pantalla principal
}
```

### 3. Pantalla Principal

**Nombre**: `scr_main`

**Elementos**:
- **Velocidad actual**: Grande, centro-superior (Chivo Mono 100)
- **Inclinación actual**: Derecha
- **Tiempo transcurrido**: HH:MM:SS
- **Distancia**: km acumulados
- **BPM**: Frecuencia cardíaca (si BLE conectado)
- **Calorías**: Estimadas según peso + velocidad
- **Objetivos**:
  - Velocidad objetivo (flash)
  - Inclinación objetivo (flash)
- **Estado de ventiladores**:
  - Indicador HEAD fan
  - Indicador CHEST fan

**Layout**:
```
┌───────────────────────────────────────┐
│  Tiempo: 00:15:30      BPM: 145 ♥    │
│                                       │
│            SPEED                      │
│             10.5                      │
│            km/h                       │
│                                       │
│  CLIMB    DISTANCE    CALORIES        │
│   5.0%     2.5 km      180 kcal      │
│                                       │
│  Target: 12.0 km/h  (parpadeando)    │
│  Target Climb: 7.5%  (parpadeando)   │
│                                       │
│  [HEAD] [CHEST]  Ventiladores         │
└───────────────────────────────────────┘
```

**Colores**:
- Velocidad: Verde si en movimiento, Rojo si parado
- BPM: Rojo si < 60 o > 180, Verde normal
- Calorías: Amarillo

### 4. Pantalla de Entrada Manual

**Nombre**: `scr_numpad`

**Mostrada cuando**:
- Usuario presiona botón físico "SET SPEED"
- Usuario presiona botón físico "SET CLIMB"

**Elementos**:
- Teclado numérico (0-9)
- Display del valor ingresado
- Etiqueta: "Set Speed (km/h)" o "Set Climb (%)"
- Botón "Confirmar"
- Botón "Cancelar"

**Funcionalidad**:
```c
void ui_set_speed(void) {
    g_treadmill_state.set_mode = SET_MODE_SPEED;
    lv_scr_load(scr_numpad);
    // Usuario ingresa valor
    // Al confirmar: g_treadmill_state.target_speed = value
}
```

### 5. Pantallas WiFi

**Archivos**: `ui_wifi.c`

#### 5.1. Lista de Redes

**Elementos**:
- Lista scrollable de SSIDs encontrados
- Indicador de señal (RSSI)
- Icono de seguridad (candado)
- Botón "Scan Again"
- Botón "Volver"

**Funcionalidad**:
```c
void ui_open_wifi_list(void) {
    // Iniciar escaneo
    wifi_manager_scan_networks(...);
    // Poblar lista
    // Usuario selecciona red
    // → Pantalla de contraseña
}
```

#### 5.2. Entrada de Contraseña WiFi

**Elementos**:
- Teclado QWERTY virtual (LVGL)
- Campo de texto (ocultar contraseña)
- SSID seleccionado (label)
- Botón "Conectar"
- Botón "Cancelar"

**Funcionalidad**:
```c
// Usuario ingresa contraseña
// Al presionar "Conectar":
wifi_manager_save_credentials(ssid, password);
// Iniciar conexión
// → Pantalla de estado
```

#### 5.3. Estado de Conexión WiFi

**Elementos**:
- Spinner (animación)
- Texto: "Conectando a [SSID]..."
- Progreso: "Obteniendo IP...", "Verificando Internet..."
- Botón "Cancelar" (durante conexión)

**Estados**:
- Conectando → Conectado → Internet OK
- Conectando → Fallo → Reintentar o Volver

### 6. Avisos de Mantenimiento

**Mostrado cuando**:
- `g_treadmill_state.total_running_seconds >= 144000` (40 horas)

**Elementos**:
- Icono de alerta
- Texto: "Es hora de aplicar cera lubricante"
- Instrucciones
- Botón "OK" (reconocer)
- Botón "Posponer"

## Arquitectura y Tareas

### Tarea Principal de UI

```c
void ui_update_task(void *pvParameter) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));  // 10 Hz

        // Actualizar elementos de UI según estado
        if (ui_is_main_screen_active()) {
            update_speed_label();
            update_time_label();
            update_distance_label();
            update_bpm_label();
            update_calories_label();
            update_fan_indicators();
        }

        // Procesar rampas
        process_speed_ramp();
        process_incline_ramp();

        // Actualizar parpadeo de objetivos
        process_blink_timers();
    }
}
```

**Frecuencia**: 100ms (10 Hz)
**Stack**: 8192 bytes
**Prioridad**: 5

### Sincronización con LVGL

LVGL maneja su propia tarea de renderizado (creada por `bsp_display_start()`):

```c
// En BSP
static void lvgl_port_task(void *arg) {
    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}
```

**Importante**: Todos los accesos a objetos LVGL desde `ui_update_task` deben usar `lvgl_port_lock()`:

```c
if (lvgl_port_lock(0)) {
    lv_label_set_text_fmt(label_speed, "%.1f", speed);
    lvgl_port_unlock();
}
```

## Sistema de Rampas

### Rampas de Velocidad

El sistema implementa cambios suaves de velocidad para seguridad:

**Tipos de Rampa**:
```c
typedef enum {
    RAMP_MODE_NORMAL,           // 5 km/h/s
    RAMP_MODE_STOP_STOP,        // Parada rápida
    RAMP_MODE_COOLDOWN_STOP,    // Cool-down lento (2 min)
    RAMP_MODE_STOP_RESUME,      // Reanudación desde parada
    RAMP_MODE_COOLDOWN_RESUME,  // Reanudación desde cool-down
} ramp_mode_t;
```

**Constantes**:
```c
const float STOP_RAMP_RATE_KMH_S = 5.0f;              // 5 km/h por segundo
const float COOLDOWN_RAMP_RATE_KMH_S = 10.0f / 120.0f; // 2 minutos
```

**Implementación**:
```c
void process_speed_ramp(void) {
    float delta_time_s = 0.1f;  // 100ms

    if (g_treadmill_state.speed_kmh < g_treadmill_state.target_speed) {
        // Acelerar
        g_treadmill_state.speed_kmh += STOP_RAMP_RATE_KMH_S * delta_time_s;
        if (g_treadmill_state.speed_kmh > g_treadmill_state.target_speed) {
            g_treadmill_state.speed_kmh = g_treadmill_state.target_speed;
        }
    } else if (g_treadmill_state.speed_kmh > g_treadmill_state.target_speed) {
        // Decelerar
        float ramp_rate = (g_treadmill_state.ramp_mode == RAMP_MODE_COOLDOWN_STOP)
                          ? COOLDOWN_RAMP_RATE_KMH_S
                          : STOP_RAMP_RATE_KMH_S;

        g_treadmill_state.speed_kmh -= ramp_rate * delta_time_s;
        if (g_treadmill_state.speed_kmh < g_treadmill_state.target_speed) {
            g_treadmill_state.speed_kmh = g_treadmill_state.target_speed;
        }
    }

    // Actualizar maestro RS485
    cm_master_set_speed(g_treadmill_state.speed_kmh);
}
```

### Rampas de Inclinación

La inclinación se controla por el módulo Base, pero el objetivo se actualiza:

```c
void process_incline_ramp(void) {
    if (g_treadmill_state.climb_percent != g_treadmill_state.target_climb_percent) {
        g_treadmill_state.climb_percent = g_treadmill_state.target_climb_percent;

        // Enviar comando al Base
        cm_master_set_incline(g_treadmill_state.climb_percent);
    }
}
```

**Nota**: El motor de inclinación en Base tiene su propia rampa (0.05%/segundo).

## Gestión de Estado

### Estructura TreadmillState

```c
typedef struct {
    // Velocidad
    float speed_kmh;                // Actual
    float target_speed;             // Objetivo
    float speed_before_stop;        // Para reanudar

    // Inclinación
    float climb_percent;            // Actual
    float target_climb_percent;     // Objetivo

    // Tiempo y distancia
    uint32_t elapsed_seconds;
    double total_distance_km;

    // Estados
    bool is_stopped;
    bool is_cooling_down;
    bool is_resuming;

    // BLE
    uint16_t real_pulse;            // BPM
    bool ble_connected;

    // Métricas simuladas
    float sim_kcal;                 // Calorías estimadas

    // UI
    set_mode_t set_mode;            // SET_MODE_SPEED, SET_MODE_CLIMB, etc.
    char set_buffer[4];             // Buffer de entrada
    int set_digit_index;            // Índice de dígito actual

    // Entrenamiento
    int selected_training;          // 1-5
    float user_weight_kg;           // Peso del usuario
    bool weight_entered;

    // Mantenimiento
    uint32_t total_running_seconds; // Contador para aviso de cera

    // Control
    ramp_mode_t ramp_mode;
} TreadmillState;
```

### Mutex de Protección

```c
extern SemaphoreHandle_t g_state_mutex;

// Acceso thread-safe
if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    g_treadmill_state.speed_kmh = new_speed;
    xSemaphoreGive(g_state_mutex);
}
```

## Fuentes Personalizadas

### Fuentes Incluidas

**Chivo Mono 70**:
- Uso: Inclinación, distancia
- Archivo: `main/fonts/chivo_mono_70.c`
- Generada con: LVGL Font Converter

**Chivo Mono 100**:
- Uso: Velocidad principal (grande)
- Archivo: `main/fonts/chivo_mono_100.c`

**DejaVu Mono 50**:
- Uso: Tiempo transcurrido
- Archivo: `main/fonts/dejavu_mono_50.c`

### Declaración y Uso

```c
// En ui.c
LV_FONT_DECLARE(chivo_mono_70);
LV_FONT_DECLARE(chivo_mono_100);

// Crear label con fuente personalizada
lv_obj_t *label = lv_label_create(parent);
lv_obj_set_style_text_font(label, &chivo_mono_100, 0);
lv_label_set_text(label, "10.5");
```

### Generar Fuentes Personalizadas

1. Ir a: https://lvgl.io/tools/fontconverter
2. Seleccionar fuente TTF
3. Configurar:
   - Name: `chivo_mono_100`
   - Size: 100 px
   - BPP: 4 (anti-aliasing)
   - Range: 0x20-0x7E (ASCII básico)
4. Descargar archivo `.c`
5. Copiar a `main/fonts/`
6. Agregar a `CMakeLists.txt`

## Integración con Hardware

### Botones Físicos

Los botones físicos son manejados por `button_handler.c` y llaman funciones de UI:

```c
// En button_handler.c
static const button_config_t buttons[] = {
    { BSP_BUTTON_SPEED_UP_MASK,   ui_speed_inc,   "SPEED+" },
    { BSP_BUTTON_SPEED_DOWN_MASK, ui_speed_dec,   "SPEED-" },
    { BSP_BUTTON_CLIMB_UP_MASK,   ui_climb_inc,   "CLIMB+" },
    { BSP_BUTTON_CLIMB_DOWN_MASK, ui_climb_dec,   "CLIMB-" },
    { BSP_BUTTON_STOP_MASK,       ui_stop_resume, "STOP" },
    { BSP_BUTTON_COOLDOWN_MASK,   ui_cool_down,   "COOLDOWN" },
    // ...
};
```

**Implementación en UI**:
```c
void ui_speed_inc(void) {
    if (g_treadmill_state.target_speed < MAX_SPEED_KMH) {
        g_treadmill_state.target_speed += 0.5f;
        audio_play_beep();
    }
}
```

### Touch

El touch GT911 es manejado por LVGL automáticamente a través del BSP. Los eventos táctiles son procesados por callbacks LVGL:

```c
// Ejemplo: botón táctil
lv_obj_t *btn = lv_btn_create(parent);
lv_obj_add_event_cb(btn, button_event_handler, LV_EVENT_CLICKED, NULL);

static void button_event_handler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Acción
    }
}
```

### Audio

El sistema reproduce sonidos en eventos clave:

```c
// En ui.c
#include "audio.h"

void ui_speed_inc(void) {
    // Incrementar velocidad...

    // Reproducir beep
    audio_play_beep();
}

void on_training_start(void) {
    audio_play_start();
}

void on_training_stop(void) {
    audio_play_stop();
}
```

## Ejemplos de Código

### Ejemplo 1: Crear Pantalla Simple

```c
// Crear nueva pantalla
lv_obj_t *scr = lv_obj_create(NULL);

// Agregar label
lv_obj_t *label = lv_label_create(scr);
lv_label_set_text(label, "Hola Mundo");
lv_obj_center(label);

// Cargar pantalla
lv_scr_load(scr);
```

### Ejemplo 2: Botón con Callback

```c
// Crear botón
lv_obj_t *btn = lv_btn_create(scr_main);
lv_obj_set_size(btn, 120, 50);
lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);

// Agregar label al botón
lv_obj_t *label = lv_label_create(btn);
lv_label_set_text(label, "Start");
lv_obj_center(label);

// Agregar callback
lv_obj_add_event_cb(btn, start_button_callback, LV_EVENT_CLICKED, NULL);

// Callback
static void start_button_callback(lv_event_t *e) {
    g_treadmill_state.target_speed = 5.0f;
    audio_play_start();
}
```

### Ejemplo 3: Lista Scrollable

```c
// Crear lista
lv_obj_t *list = lv_list_create(scr);
lv_obj_set_size(list, 300, 400);
lv_obj_center(list);

// Agregar items
for (int i = 0; i < num_networks; i++) {
    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, networks[i].ssid);
    lv_obj_add_event_cb(btn, network_selected_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
}
```

### Ejemplo 4: Timer Periódico

```c
// Crear timer LVGL (1 segundo)
lv_timer_t *timer = lv_timer_create(timer_callback, 1000, NULL);

// Callback
static void timer_callback(lv_timer_t *timer) {
    g_treadmill_state.elapsed_seconds++;
    update_time_label();
}
```

## Troubleshooting

### UI no responde al touch

1. Verificar logs del GT911
2. Confirmar calibración táctil
3. Probar con stylus o dedo limpio
4. Revisar que `lvgl_port_lock/unlock` se usa correctamente

### Parpadeo o tearing

1. Verificar double buffer habilitado
2. Confirmar `buff_spiram = true`
3. Aumentar `buffer_size` en `bsp_display_cfg_t`

### Fuentes no se cargan

1. Verificar declaración `LV_FONT_DECLARE()`
2. Confirmar que el archivo `.c` está en `CMakeLists.txt`
3. Recompilar: `idf.py fullclean && idf.py build`

### UI se congela

1. Verificar que no hay deadlock en mutex
2. Confirmar que `ui_update_task` no está bloqueada
3. Revisar que LVGL task tiene suficiente stack

---

**Última actualización**: 2025-11-05
