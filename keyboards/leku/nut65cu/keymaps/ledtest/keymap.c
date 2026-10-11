// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// LED chain test for the NUT65 CU: lights one LED at a time so the physical
// position of every index (0-87) can be written down.
//
// Use over USB (Fn+T). The normal layout is unchanged except for these keys:
//   Right / Left   next / previous LED
//   Up / Down      +10 / -10
//   Enter          types the current index, e.g. "led 12" + newline, into the focused window
//   Home (Fn+K)    back to LED 0
//   P              pattern view: indicators 0-7 in rainbow order, bar start (8) red,
//                  bar end (87) blue, every 10th bar LED (18, 28, ... 78) green
//   Esc            test overlay on/off (normal lighting underneath)
// The overlay starts once the boot animation along the bar has finished. While it
// is on it hides the normal indicators (connection blinks, Caps, battery).

#include "../default/keymap.c"
#include <stdio.h>

#define LEDTEST_VAL 0x60

static bool    ledtest_on      = true;
static bool    ledtest_pattern = false;
static uint8_t ledtest_index   = 0;

static void ledtest_step(int delta) {
    int i         = ((int)ledtest_index + delta) % RGB_MATRIX_LED_COUNT;
    ledtest_index = i < 0 ? i + RGB_MATRIX_LED_COUNT : i;
}

// The board owns process_record_user, so the keymap hooks in before it.
bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode == KC_ESC) {
        if (record->event.pressed) ledtest_on = !ledtest_on;
        return false;
    }
    if (!ledtest_on) {
        return true;
    }

    switch (keycode) {
        case KC_RIGHT: if (record->event.pressed) ledtest_step(1);   return false;
        case KC_LEFT:  if (record->event.pressed) ledtest_step(-1);  return false;
        case KC_UP:    if (record->event.pressed) ledtest_step(10);  return false;
        case KC_DOWN:  if (record->event.pressed) ledtest_step(-10); return false;
        case KC_HOME:  if (record->event.pressed) ledtest_index = 0; return false;
        case KC_P:     if (record->event.pressed) ledtest_pattern = !ledtest_pattern; return false;
        case KC_ENTER:
            if (record->event.pressed) {
                char buf[12];
                snprintf(buf, sizeof(buf), "led %u\n", ledtest_index);
                send_string(buf);
            }
            return false;
        default:
            return true;
    }
}

extern bool start_paoma_flag; // nut65cu.c: boot animation still running

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    if (!ledtest_on || start_paoma_flag) {
        return true;
    }

    rgb_matrix_set_color_all(0, 0, 0);

    if (ledtest_pattern) {
        for (uint8_t i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
            RGB rgb = hsv_to_rgb((HSV){.h = i * 32, .s = 255, .v = LEDTEST_VAL});
            rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
        }
        for (uint8_t i = HS_RGB_STRIP_START + 10; i < RGB_MATRIX_LED_COUNT - 1; i += 10) {
            rgb_matrix_set_color(i, 0, LEDTEST_VAL, 0);
        }
        rgb_matrix_set_color(HS_RGB_STRIP_START, LEDTEST_VAL, 0, 0);
        rgb_matrix_set_color(RGB_MATRIX_LED_COUNT - 1, 0, 0, LEDTEST_VAL);
    } else {
        rgb_matrix_set_color(ledtest_index, LEDTEST_VAL, LEDTEST_VAL, LEDTEST_VAL);
    }

    return false;
}
