// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Lifetime press count of every key (by matrix position, whatever it is mapped to).
// Counts only, no order or timing. Kept in RAM and saved to the external flash
// (CU_STORE_KEYS_BASE) every few minutes and before sleep.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "action.h"

void     cu_keys_init(void);
void     cu_keys_task(void);
void     cu_keys_record(keyrecord_t *record);
uint32_t cu_keys_get(uint8_t row, uint8_t col);
uint32_t cu_keys_total(void);
uint32_t cu_keys_saved_seq(void); // snapshots written so far, 0 = none
bool     cu_keys_dirty(void);
bool     cu_keys_save(void);
void     cu_keys_clear(void);
