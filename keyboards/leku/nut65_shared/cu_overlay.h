// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Lights the computer controls (raw HID, cu_host.c): up to CU_BAR_LAYERS layers on the
// light bar, the five indicator LEDs under Q W E R T, which otherwise only blink while
// connecting, and on models with per-key LEDs (NUT65_KEY_LEDS > 0) any key's LED. The computer sends what to show and for how long; the animation runs here,
// and everything goes away by itself when its time is up, so a host program that dies or
// a computer that sleeps can't leave a light stuck on. They are also cleared when the
// link to the computer drops and before the keyboard sleeps.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CU_BAR_LAYERS 4
#define CU_HOST_INDICATORS 5 // Q W E R T

typedef enum {
    CU_FX_OFF = 0, // removes the layer / indicator
    CU_FX_SOLID,
    CU_FX_BREATHE,
    CU_FX_BLINK,
    CU_FX_FLOW,     // a comet running from start to end, one pass per period
    CU_FX_SCAN,     // a dot sweeping back and forth, one round trip per period
    CU_FX_PROGRESS, // filled to value / 255 of the segment
    CU_FX_COUNT,
} cu_fx_t;

// CU_HID_BAR arguments, as sent.
typedef struct __attribute__((packed)) {
    uint8_t  layer;             // 0-3, higher layers draw over lower ones
    uint8_t  fx;                // cu_fx_t
    uint8_t  start, end;        // LEDs [start, end) of the bar, 0-80 from the left
    uint8_t  hue, sat, val;     // val scales the bar brightness: 255 = as bright as the bar
    uint8_t  bg_hue, bg_sat, bg_val; // the rest of the segment (val 0 = black)
    uint8_t  period;            // animation period in 20 ms units, 0 = 1 s
    uint8_t  value;             // PROGRESS fill, 0-255
    uint8_t  dir;               // 0 left to right, 1 right to left, 2 from the centre out
    uint16_t ttl;               // 100 ms units, 0 removes the layer
} cu_bar_layer_t;

// CU_HID_IND arguments, as sent.
typedef struct __attribute__((packed)) {
    uint8_t  index;         // 0-4 = Q W E R T
    uint8_t  fx;            // CU_FX_OFF / SOLID / BREATHE / BLINK
    uint8_t  hue, sat, val; // val is the LED brightness itself
    uint8_t  period;        // 20 ms units, 0 = 1 s
    uint16_t ttl;           // 100 ms units, 0 turns it off
} cu_host_ind_t;

bool cu_overlay_bar_set(const cu_bar_layer_t *layer); // false: bad arguments
bool cu_overlay_ind_set(const cu_host_ind_t *ind);
#if NUT65_KEY_LEDS
// CU_HID_KEY_LIGHT: same arguments as IND, index = the physical LED number of a key (0 to
// NUT65_KEY_LEDS - 1). Drawn over the RGB matrix effect, under the status indicators.
bool cu_overlay_key_set(const cu_host_ind_t *key);
void cu_overlay_draw_keys(void);
#endif
void cu_overlay_clear(void);
void cu_overlay_draw_bar(void);        // over the bar's own content, under alerts and feedback
void cu_overlay_draw_indicators(void); // under the connection blinks
