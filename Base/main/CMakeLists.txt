idf_component_register(
    SRCS "main.c"
         "vfd_driver.c"
         "speed_sensor.c"
    INCLUDE_DIRS "."

    # Dependencias públicas del proyecto
    REQUIRES cm_protocol driver nvs_flash esp_timer freertos

    # Dependencias privadas (solo para implementación interna)
    PRIV_REQUIRES esp-modbus
)