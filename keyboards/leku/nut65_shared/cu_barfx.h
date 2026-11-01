// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Light bar effects beyond the OEM modes. Bar modes as the rest of the firmware and the
// computer see them (hs_rl_mode_get / hs_rl_mode_set, VIA, INFO, per-device settings):
//   0-11  the OEM modes (hs_rgblight_increase)      12  off
//   13-24 these effects, 1-12 here:
//     1 starlight   a pressed key leaves a star that twinkles out
//     2 ripple      a pressed key sends waves to both sides, a new colour each press
//     3 afterglow   a pressed key lights its part of the bar, which fades slowly
//     4 heat        typing heats the bar under the keys: red, orange, yellow, white; it spreads and cools
//     5 starry sky  stars twinkle over a deep blue night
//     6 meteor      a white-blue head with a long blue-purple tail crosses the bar
//     7 aurora      slow waves of green, teal and purple
//     8 fire        flickering flames
//     9 ocean       blue waves with white crests
//    10 raindrops   drops of random colours appear and fade
//    11 scanner     a red light sweeps back and forth with a tail
//    12 gap light   copper to green, with a soft light passing through
// The key-driven ones place a key on the bar by its position across the keyboard.
// Ambient ones follow the bar speed setting. All at the bar brightness (hs_strip_val()).
#pragma once

#include QMK_KEYBOARD_H

#define CU_BARFX_COUNT    12
#define CU_BAR_MODE_OFF   12
#define CU_BAR_MODE_LAST  (CU_BAR_MODE_OFF + CU_BARFX_COUNT)

void cu_barfx_key(uint8_t fx, keyrecord_t *record); // every key event; fx = the effect on now (0: none)
void cu_barfx_draw(uint8_t fx);                     // fx 1..CU_BARFX_COUNT: draws the whole bar
