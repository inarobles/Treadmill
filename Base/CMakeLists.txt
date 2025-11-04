# ESP-IDF Project for Sala de Maquinas (Slave)
cmake_minimum_required(VERSION 3.16)

# Include only cm_protocol component (avoid bsp_extra which is ESP32-P4 only)
set(EXTRA_COMPONENT_DIRS "../common_components/cm_protocol" "components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sala_maquinas)
