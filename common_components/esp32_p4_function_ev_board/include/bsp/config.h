/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**************************************************************************************************
 * BSP configuration
 **************************************************************************************************/
// By default, this BSP is shipped with LVGL graphical library. Enabling this option will exclude it.
// If you want to use BSP without LVGL, select BSP version with 'noglib' suffix.
#ifdef BSP_CONFIG_NO_GRAPHIC_LIB
#undef BSP_CONFIG_NO_GRAPHIC_LIB
#endif
#define BSP_CONFIG_NO_GRAPHIC_LIB (0)
