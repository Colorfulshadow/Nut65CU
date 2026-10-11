// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// NUT65 CU light bar features: pomodoro timer, WPM meter, low-battery alert,
// and switching to USB while a cable is plugged in.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "action.h"

#define CU_SETTINGS_MAGIC 0xC5

// Stored at CU_EECONFIG_ADDR, the end of the user datablock (EECONFIG_CU_USE_SIZE bytes).
typedef struct __attribute__((packed)) {
    uint8_t magic;      // CU_SETTINGS_MAGIC once written
    uint8_t pomo_work;  // minutes, 5-60
    uint8_t pomo_break; // minutes, 1-30
    uint8_t wpm_full;   // WPM that fills the meter, 40-200
    uint8_t wpm_on : 1;        // light bar shows the WPM meter
    uint8_t pomo_show : 1;     // light bar shows pomodoro progress while it runs
    uint8_t auto_usb : 1;      // switch to USB while a cable is plugged in
    uint8_t low_bat_alert : 1; // flash the light bar when the battery runs low
    uint8_t reserved_bits : 4;
    uint8_t auto_prev_devs;    // wireless device to return to after switching to USB on a cable (DEVS_USB = none)
    uint8_t reserved[2];
} cu_settings_t;

_Static_assert(sizeof(cu_settings_t) == EECONFIG_CU_USE_SIZE, "cu_settings_t must match EECONFIG_CU_USE_SIZE");

// VIA custom channel value ids (channel 0); 1-4 are the OEM ones in wls.h.
enum cu_via_value_id {
    id_cu_pomo_work = 0x05,
    id_cu_pomo_break,
    id_cu_auto_usb,
    id_cu_low_bat_alert,
    id_cu_wpm_full,
    id_cu_wpm_on,
    id_cu_pomo_show,
    id_cu_bar_mode,  // light bar mode 0-12 (as stepped by RL_MOD)
    id_cu_bar_level, // light bar brightness 1-5
    id_cu_bar_speed, // rainbow speed 1-5 (RGB matrix speed 55-215)
};

void cu_features_init(void);                                    // after the board's EEPROM init
void cu_features_task(void);                                    // housekeeping, after charging_state is read
bool cu_process_record(uint16_t keycode, keyrecord_t *record); // false: key handled
void cu_draw_bar(void);                                         // over the bar's own mode, under status blinks
bool cu_prevent_sleep(void);                                    // pomodoro running: keep the clock going
bool cu_via_get_value(uint8_t value_id, uint8_t *value_data);
bool cu_via_set_value(uint8_t value_id, uint8_t *value_data);
