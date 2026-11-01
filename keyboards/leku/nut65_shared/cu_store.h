// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The external SPI flash beyond the wear-leveled EEPROM: key counters and the event
// log (layout in config.h, CU_STORE_*).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CU_STORE_SECTOR 4096u

typedef enum {
    CU_STORE_NONE = 0, // no usable flash found
    CU_STORE_LOCKED,   // a region holds data this firmware didn't write: left alone until formatted
    CU_STORE_OK,
} cu_store_state_t;

void             cu_store_init(void); // after the EEPROM (wear leveling) is up
void             cu_store_task(void);
cu_store_state_t cu_store_state(void);
uint32_t         cu_store_jedec_id(void); // manufacturer << 16 | type << 8 | capacity
uint32_t         cu_store_size(void);     // bytes, 0 if no usable flash

// The log and key counter regions call this when a sector of theirs holds foreign data.
void cu_store_lock(void);

// Reads work anywhere on the chip; writes and erases only inside the CU_STORE_* regions.
bool cu_store_read(uint32_t addr, void *buf, uint32_t len);
bool cu_store_write(uint32_t addr, const void *buf, uint32_t len);
bool cu_store_is_blank(uint32_t addr, uint32_t len);

// A sector erase takes ~50 ms (up to ~400 ms). The async one returns at once and the
// flash finishes on its own; anything touching the flash meanwhile waits for it.
bool cu_store_erase_async(uint32_t addr);
bool cu_store_erase(uint32_t addr);
void cu_store_wait(void);

// Erase both regions (blocking, a few seconds); the caller re-initialises the users.
bool cu_store_format(void);
