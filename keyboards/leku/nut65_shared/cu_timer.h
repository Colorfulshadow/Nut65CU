// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Start marks for timers that use 0 as "not running".
//
// `timer_read32() | 1` is not one: on an even millisecond it is a millisecond in the
// future, and a timer_elapsed32() in the same millisecond wraps to ~4.29e9, so the
// wait was skipped about half the time (found on the keyboard 2026-10-31: a
// BOOT_REQUEST timed out at once). Only 0 itself is moved, to UINT32_MAX, i.e. 1 ms
// earlier: it can never be in the future.
#pragma once

#include <stdint.h>
#include "timer.h"

static inline uint32_t cu_timer_start(void) {
    uint32_t t = timer_read32();
    return t ? t : UINT32_MAX;
}
