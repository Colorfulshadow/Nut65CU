// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Normal / power save / game mode.
//
// Why the power save mode cuts the LED power: the 88 WS2812 / WS2811 LEDs and the boost
// converter feeding the bar draw current even when every LED is black, and the board
// only cut that power for sleep. Here it is cut once the whole frame has been black for
// a moment and switched back on as soon as anything lights (the frame is redrawn every
// few ms, so the LEDs catch up straight away).

#include QMK_KEYBOARD_H
#include "cu_mode.h"
#include "cu_timer.h"
#include "cu_log.h"
#include "cu_profile.h"

extern rgb_led_t rgb_matrix_ws2812_array[];  // rgb_matrix_drivers.c
extern uint16_t  hs_deb;                     // nut65cu.c: debounce from DEB_TOG
extern bool      hs_os_is_mac(void);         // nut65cu.c
extern bool      hs_music_active(void);      // nut65cu.c: the music chip drives the bar

#define ECO_SLEEP_MAX_MS    60000
#define ECO_DARK_MS         500  // all LEDs black this long: cut their power
#define ECO_IDLE_AFTER_MS   1000 // no input this long: idle 1 ms between scans
#define GAME_DEBOUNCE_MS    1

static cu_mode_t mode = CU_MODE_NORMAL;
static bool      led_power = true;
static uint32_t  dark_since;

static void led_power_set(bool on) {
    if (on == led_power) return;
    led_power = on;
    gpio_write_pin(LED_POWER_EN_PIN, on);
    gpio_write_pin(HS_LED_BOOSTING_PIN, on);
}

void cu_mode_init(uint8_t saved) {
    mode = saved < CU_MODE_COUNT ? saved : CU_MODE_NORMAL;
}

cu_mode_t cu_mode_get(void) {
    return mode;
}

void cu_mode_set(cu_mode_t m) {
    if (m >= CU_MODE_COUNT || m == mode) return;
    mode = m;
    if (mode != CU_MODE_ECO) led_power_set(true);
    cu_profile_save_mode(mode);
    cu_log_event(CU_LOG_MODE, mode);
}

void cu_mode_task(void) {
    // Let the MCU sleep (WFI) for a tick between scans; a key still registers within ~1 ms.
    if (mode == CU_MODE_ECO && last_input_activity_elapsed() > ECO_IDLE_AFTER_MS) wait_ms(1);
}

bool cu_mode_process_record(uint16_t keycode, keyrecord_t *record) {
    if (mode == CU_MODE_GAME && !hs_os_is_mac() && (keycode == KC_LGUI || keycode == KC_RGUI)) return false;
    return true;
}

bool cu_mode_bar_dark(void) {
    return mode == CU_MODE_ECO;
}

uint16_t cu_mode_debounce(void) {
    return mode == CU_MODE_GAME ? GAME_DEBOUNCE_MS : hs_deb;
}

uint32_t cu_mode_sleep_timeout(uint32_t timeout) {
    if (mode == CU_MODE_ECO && (timeout == 0 || timeout > ECO_SLEEP_MAX_MS)) return ECO_SLEEP_MAX_MS;
    return timeout;
}

void cu_mode_indicators(void) {
    if (mode == CU_MODE_GAME && !hs_os_is_mac()) {
        rgb_matrix_set_color(HS_RGB_INDEX_WIN_LOCK, 0xFF, 0x00, 0x00);
    }

    if (mode != CU_MODE_ECO || hs_music_active()) {
        led_power_set(true);
        return;
    }
    for (uint8_t i = 0; i < RGB_MATRIX_LED_COUNT; i++) {
        rgb_led_t *l = &rgb_matrix_ws2812_array[i];
        if (l->r | l->g | l->b) {
            dark_since = 0;
            led_power_set(true);
            return;
        }
    }
    if (!dark_since) dark_since = cu_timer_start();
    if (timer_elapsed32(dark_since) >= ECO_DARK_MS) led_power_set(false);
}

void cu_mode_after_wake(void) {
    led_power  = true; // waking turned the LED power back on
    dark_since = 0;
}
