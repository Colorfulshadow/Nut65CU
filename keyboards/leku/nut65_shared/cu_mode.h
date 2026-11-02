// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Normal / power save / game mode (Fn+P cycles, VIA Settings -> Devices). The mode is
// part of the per-device settings, so e.g. the gaming PC can stay in game mode while a
// laptop on Bluetooth saves power.
//
//   power save: all LEDs and boost power OFF, including status/host/music lights;
//               RGB rendering/transmission paused; sleep after at most 1 min;
//               on battery wireless, WFI waits 4 ms active / 8 ms idle, with UART and
//               staging bypasses; one battery query/charge heartbeat every 15 s
//   game:       separately configured debounce, Win key disabled (Win mode only), Win LED red

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "action.h"

typedef enum {
    CU_MODE_NORMAL = 0,
    CU_MODE_ECO,
    CU_MODE_GAME,
    CU_MODE_COUNT,
} cu_mode_t;

void      cu_mode_init(uint8_t saved);
cu_mode_t cu_mode_get(void);
void      cu_mode_set(cu_mode_t mode); // applies, logs and remembers it
void      cu_mode_task(void);
void      cu_mode_matrix_changed(void);                  // raw edge, before debounce
void      cu_mode_power_task(uint8_t status);            // sampled cable/charge command
void      cu_mode_clear_key_lights(void);                // before status/host indicators
bool      cu_mode_process_record(uint16_t keycode, keyrecord_t *record); // false: key swallowed
bool      cu_mode_bar_dark(void);                       // hide the light bar's own animations
uint16_t  cu_mode_debounce(void);                       // ms, for hs_debounce.c
uint32_t  cu_mode_sleep_timeout(uint32_t timeout);      // ms, 0 = never
void      cu_mode_indicators(void);                     // game mode indicator (ECO never renders)
void      cu_mode_after_wake(void);
