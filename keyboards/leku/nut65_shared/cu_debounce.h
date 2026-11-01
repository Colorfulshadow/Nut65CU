// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define CU_DEBOUNCE_DEFAULT 8
#define CU_DEBOUNCE_MAX 30
#define CU_DEBOUNCE_GLOBAL 7

void cu_debounce_init(void);
uint8_t cu_debounce_slot(void);
uint8_t cu_debounce_effective(void);
bool cu_debounce_get(uint8_t slot, uint8_t *normal, uint8_t *game);
bool cu_debounce_set(uint8_t slot, uint8_t normal, uint8_t game);
void cu_debounce_toggle(void); // Fn+D: toggle the active mode between 1 and 8 ms
