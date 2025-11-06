#ifndef VFD_DRIVER_H
#define VFD_DRIVER_H

#include "esp_err.h"

// Estados públicos del VFD que el main.c puede consultar
typedef enum {
    VFD_STATUS_DISCONNECTED,
    VFD_STATUS_OK,
    VFD_STATUS_FAULT
} vfd_status_t;

/**
 * @brief Inicializa el UART2 (Modbus), el nodo Modbus y crea la tarea de control del VFD.
 */
void vfd_driver_init(void);

/**
 * @brief Fija la velocidad objetivo.
 * Esta función es segura para llamar desde cualquier tarea (ej. uart_rx_task).
 * La tarea del VFD se encargará de enviar el comando al VFD.
 *
 * @param kph Velocidad objetivo en km/h.
 */
void vfd_driver_set_speed(float kph);

/**
 * @brief Envía un comando de paro inmediato al VFD.
 * Esta función es segura para llamar desde cualquier tarea o ISR (ej. watchdog).
 */
void vfd_driver_emergency_stop(void);

/**
 * @brief Obtiene el estado de salud actual del controlador del VFD.
 *
 * @return vfd_status_t Estado actual.
 */
vfd_status_t vfd_driver_get_status(void);

/**
 * @brief Obtiene la última frecuencia objetivo enviada al VFD.
 *
 * @return float Frecuencia en Hz.
 */
float vfd_driver_get_target_freq_hz(void);

/**
 * @brief Obtiene la frecuencia real del VFD leída del registro 0x2103.
 *
 * Esta es la frecuencia que el VFD está aplicando actualmente al motor,
 * útil para monitorear el progreso de rampas de aceleración/desaceleración.
 *
 * @return float Frecuencia real en Hz (0.0 si no se ha leído aún).
 */
float vfd_driver_get_real_freq_hz(void);


#endif // VFD_DRIVER_H