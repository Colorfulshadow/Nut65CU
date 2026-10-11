// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// sym_defer_g with a run-time debounce time, so Fn+D (DEB_TOG) takes effect.
// The OEM tree patched quantum/debounce/sym_defer_g.c for this but left both
// branches on the fixed DEBOUNCE, so the toggle never changed anything.
// hs_deb (nut65.c) is DEBOUNCE normally and 1 ms in low-latency mode.

#include <string.h>
#include "debounce.h"
#include "timer.h"

extern uint16_t hs_deb;

static bool         debouncing = false;
static fast_timer_t debouncing_time;

void debounce_init(uint8_t num_rows) {}

bool debounce(matrix_row_t raw[], matrix_row_t cooked[], uint8_t num_rows, bool changed) {
    bool cooked_changed = false;

    if (changed) {
        debouncing      = true;
        debouncing_time = timer_read_fast();
    } else if (debouncing && timer_elapsed_fast(debouncing_time) >= hs_deb) {
        size_t matrix_size = num_rows * sizeof(matrix_row_t);
        if (memcmp(cooked, raw, matrix_size) != 0) {
            memcpy(cooked, raw, matrix_size);
            cooked_changed = true;
        }
        debouncing = false;
    }

    return cooked_changed;
}

void debounce_free(void) {}
