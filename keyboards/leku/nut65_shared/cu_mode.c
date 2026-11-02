// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Normal / power save / game mode.
//
// ECO always cuts both LED rails, including indicators and music.
// RGB settings stay intact; the task/flush hook suppresses all LED traffic.

#include QMK_KEYBOARD_H
#include "cu_mode.h"
#include "cu_debounce.h"
#include "cu_timer.h"
#include "cu_log.h"
#include "cu_profile.h"
#include "cu_stage.h"
#include "wireless.h"

extern uint16_t  hs_deb;                     // nut65cu.c: debounce from DEB_TOG
extern bool      hs_os_is_mac(void);         // nut65cu.c
extern void      hs_music_power(bool on);    // transient music routing, not settings
extern bool      charging_state;             // cable present, sampled before cu_mode_task

#define ECO_SLEEP_MAX_MS    60000
#define ECO_IDLE_AFTER_MS   1000
#define ECO_ACTIVE_WAIT_MS  4    // CPU WFI between scans; not the radio/USB polling rate
#define ECO_IDLE_WAIT_MS    8
#define ECO_BATTERY_MS      15000

static cu_mode_t mode = CU_MODE_NORMAL;
static uint32_t  matrix_changed_at;
static bool      power_polled;
static uint32_t  power_polled_at;
static uint8_t   power_status, power_device;

static void led_power_set(bool on) {
    gpio_write_pin(LED_POWER_EN_PIN, on);
    gpio_write_pin(HS_LED_BOOSTING_PIN, on);
    hs_music_power(on);
}

void cu_mode_init(uint8_t saved) {
    mode = saved < CU_MODE_COUNT ? saved : CU_MODE_NORMAL;
    led_power_set(mode != CU_MODE_ECO);
    matrix_changed_at = timer_read32();
    power_polled = false;
}

cu_mode_t cu_mode_get(void) {
    return mode;
}

void cu_mode_set(cu_mode_t m) {
    if (m >= CU_MODE_COUNT || m == mode) return;
    mode = m;
    led_power_set(mode != CU_MODE_ECO);
    power_polled = false;
    matrix_changed_at = timer_read32();
    cu_profile_save_mode(mode);
    cu_log_event(CU_LOG_MODE, mode);
}

void cu_mode_task(void) {
    // Leave USB, cable-powered use, UART ACK/retry traffic and image reception at full
    // speed. Never delay the low-power state machine once sleep/wake has started.
    if (mode != CU_MODE_ECO || charging_state || wireless_get_current_devs() == DEVS_USB ||
        lpwr_get_state() != LPWR_NORMAL || smsg_is_busy() || cu_stage_awake()) return;

    // Raw matrix changes bring scanning back to the active cadence BEFORE debounce
    // accepts the key. These are minimum waits, not a promise of 250/125 Hz reports.
    bool idle = last_input_activity_elapsed() > ECO_IDLE_AFTER_MS &&
                timer_elapsed32(matrix_changed_at) > ECO_IDLE_AFTER_MS;
    uint16_t delay = idle ? ECO_IDLE_WAIT_MS : ECO_ACTIVE_WAIT_MS;
    wait_ms(delay);
}

void cu_mode_matrix_changed(void) {
    matrix_changed_at = timer_read32();
}

bool wireless_battery_poll_enabled(void) {
    // ECO has one owner for voltage queries (below), including cable/state changes.
    // Other keyboards retain wireless.c's weak default, and other modes retain 3 s.
    return mode != CU_MODE_ECO;
}

void cu_mode_power_task(uint8_t status) {
    bool eco = mode == CU_MODE_ECO;
    if (eco && lpwr_get_state() != LPWR_NORMAL) return;
    uint8_t device = wireless_get_current_devs();
    bool changed = !power_polled || status != power_status || device != power_device;
    uint32_t interval = eco ? ECO_BATTERY_MS : 1000;
    if (!changed && timer_elapsed32(power_polled_at) < interval) return;
    // Periodic management traffic should not compete with keys or HID replies.
    // A cable/charge transition is still queued immediately, even when UART is busy.
    if (eco && !changed && smsg_is_busy()) return;
    md_send_devctrl(status);
    md_send_devctrl(MD_SND_CMD_DEVCTRL_INQVOL);
    power_polled = true;
    power_polled_at = timer_read32();
    power_status = status;
    power_device = device;
}

bool cu_mode_process_record(uint16_t keycode, keyrecord_t *record) {
    if (mode == CU_MODE_GAME && !hs_os_is_mac() && (keycode == KC_LGUI || keycode == KC_RGUI)) return false;
    return true;
}

bool cu_mode_bar_dark(void) {
    return mode == CU_MODE_ECO;
}

void cu_mode_clear_key_lights(void) {
#if NUT65_KEY_LEDS
    // Hide native per-key effects without changing RGB settings/EEPROM. Status
    // indicators and explicit host lights are drawn afterwards by nut65_core.c.
    if (mode == CU_MODE_ECO) {
        for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) rgb_matrix_set_color(i, 0, 0, 0);
    }
#endif
}

uint16_t cu_mode_debounce(void) {
    return cu_debounce_effective();
}

uint32_t cu_mode_sleep_timeout(uint32_t timeout) {
    if (mode == CU_MODE_ECO && (timeout == 0 || timeout > ECO_SLEEP_MAX_MS)) return ECO_SLEEP_MAX_MS;
    return timeout;
}

bool rgb_matrix_power_enabled(void) {
    return mode != CU_MODE_ECO;
}

void cu_mode_indicators(void) {
    if (mode == CU_MODE_GAME && !hs_os_is_mac()) {
        rgb_matrix_set_color(HS_RGB_INDEX_WIN_LOCK, 0xFF, 0x00, 0x00);
    }
}

void cu_mode_after_wake(void) {
    led_power_set(mode != CU_MODE_ECO);
    matrix_changed_at = timer_read32();
    power_polled = false;
}
