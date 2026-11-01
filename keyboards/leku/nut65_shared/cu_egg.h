// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Easter eggs: short light shows (cu_egg.c has what each one looks like).
//   Typed (the keys still go to the computer; nothing is sent to the computer about it):
//     Up Up Down Down Left Right Left Right B A   the Konami code show
//     z h a n g t i a n y i                      an ice-blue heart around K
//     q i u z i                                  a bright red heart around K
//     m a t r i x                                digital rain
//     6 6 6                                      a triple gold flash
//     s o s                                      SOS in Morse code
//   Achievements:
//     the lifetime key count reaching 500,000 and every million   fireworks (cu_egg_task)
//     typing faster than ever before                               a gold "new record" sweep (cu_features.c)
//     four pomodoros finished in one run                           a tomato harvest (cu_features.c)
#pragma once

#include QMK_KEYBOARD_H

typedef enum {
    CU_EGG_NONE,
    CU_EGG_KONAMI,
    CU_EGG_ICE,
    CU_EGG_RED,
    CU_EGG_MATRIX,
    CU_EGG_666,
    CU_EGG_SOS,
    CU_EGG_MILESTONE,
    CU_EGG_WPM,
    CU_EGG_POMO,
} cu_egg_t;

void cu_egg_record(uint16_t keycode, keyrecord_t *record); // every key event, never consumes it
void cu_egg_task(void);                                     // key count milestones
void cu_egg_play(cu_egg_t egg);                             // starts a show, replacing the one playing
void cu_egg_draw(void);                                     // over the bar (and keys), under the bootloader request
