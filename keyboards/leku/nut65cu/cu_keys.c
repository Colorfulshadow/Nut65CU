// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Key press counters. Snapshots of all counters go round a ring of 512-byte slots
// (8 per sector); the newest one with a good CRC is loaded at boot. With a save every
// 5 minutes of typing, each sector is erased about once every 10 hours of typing.

#include QMK_KEYBOARD_H
#include <string.h>
#include "cu_store.h"
#include "cu_log.h"
#include "cu_keys.h"

#define SNAP_MAGIC       0x434B5543 // "CUKC"
#define SLOT_SIZE        512u
#define SLOTS_PER_SECTOR (CU_STORE_SECTOR / SLOT_SIZE)
#define SLOTS            (CU_STORE_KEYS_SIZE / SLOT_SIZE)
#define SAVE_EVERY_MS    300000 // while there are new presses
#define KEY_COUNT        (MATRIX_ROWS * MATRIX_COLS)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t seq;
    uint16_t boot;
    uint8_t  rows, cols;
    uint32_t total;
    uint32_t count[KEY_COUNT];
    uint32_t crc;
} snapshot_t;

_Static_assert(sizeof(snapshot_t) <= SLOT_SIZE, "key snapshot must fit a slot");

static snapshot_t snap; // the live counters; seq = seq of the last saved snapshot + 1
static bool       ready, dirty;
static bool       next_erased; // the sector of the next slot was erased ahead of time
static uint32_t   last_save;

static uint32_t crc32(const void *data, uint32_t len) {
    const uint8_t *p   = data;
    uint32_t       crc = 0xFFFFFFFF;
    while (len--) {
        crc ^= *p++;
        for (uint8_t i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

static uint32_t slot_addr(uint32_t slot) {
    return CU_STORE_KEYS_BASE + slot * SLOT_SIZE;
}

static bool load_slot(uint32_t slot, snapshot_t *s) {
    return cu_store_read(slot_addr(slot), s, sizeof(*s)) && s->magic == SNAP_MAGIC && s->seq % SLOTS == slot &&
           s->rows == MATRIX_ROWS && s->cols == MATRIX_COLS && s->crc == crc32(s, offsetof(snapshot_t, crc));
}

void cu_keys_init(void) {
    snap = (snapshot_t){.magic = SNAP_MAGIC, .rows = MATRIX_ROWS, .cols = MATRIX_COLS};
    if (cu_store_state() == CU_STORE_NONE) return;

    // Headers first; the newest slot that also passes its CRC wins.
    struct __attribute__((packed)) {
        uint32_t magic, seq;
    } head;
    uint32_t best_seq = 0, best_slot = 0;
    bool     found = false, foreign = false;
    for (uint32_t slot = 0; slot < SLOTS; slot++) {
        if (!cu_store_read(slot_addr(slot), &head, sizeof(head))) return;
        if (head.magic == SNAP_MAGIC && head.seq % SLOTS == slot) {
            if (!found || head.seq > best_seq) {
                best_seq  = head.seq;
                best_slot = slot;
            }
            found = true;
        } else if (slot % SLOTS_PER_SECTOR == 0 && (head.magic != 0xFFFFFFFF || head.seq != 0xFFFFFFFF)) {
            foreign = true;
        }
    }
    if (!found && foreign) {
        cu_store_lock();
        return;
    }

    snapshot_t s;
    for (uint32_t tries = 0; found && tries < SLOTS; tries++) {
        uint32_t slot = (best_slot + SLOTS - tries) % SLOTS;
        if (load_slot(slot, &s) && s.seq == best_seq - tries) {
            snap = s;
            break;
        }
    }
    snap.seq  = found ? best_seq + 1 : 0; // a torn newest slot is overwritten after its sector is erased
    ready     = true;
    last_save = timer_read32();
}

void cu_keys_record(keyrecord_t *record) {
    if (!record->event.pressed || !IS_KEYEVENT(record->event)) return;
    uint8_t row = record->event.key.row, col = record->event.key.col;
    if (row >= MATRIX_ROWS || col >= MATRIX_COLS) return;
    snap.count[row * MATRIX_COLS + col]++;
    snap.total++;
    dirty = true;
}

uint32_t cu_keys_get(uint8_t row, uint8_t col) {
    return row < MATRIX_ROWS && col < MATRIX_COLS ? snap.count[row * MATRIX_COLS + col] : 0;
}

uint32_t cu_keys_total(void) {
    return snap.total;
}

uint32_t cu_keys_saved_seq(void) {
    return snap.seq;
}

bool cu_keys_dirty(void) {
    return dirty;
}

bool cu_keys_save(void) {
    if (!ready || cu_store_state() != CU_STORE_OK) return false;

    uint32_t slot = snap.seq % SLOTS;
    if (slot % SLOTS_PER_SECTOR == 0) {
        if (!next_erased && !cu_store_erase(slot_addr(slot))) return false;
        next_erased = false;
    }

    snap.boot = cu_log_boot();
    snap.crc  = crc32(&snap, offsetof(snapshot_t, crc));
    if (!cu_store_write(slot_addr(slot), &snap, sizeof(snap))) return false;
    snap.seq++;
    dirty     = false;
    last_save = timer_read32();

    // Get the next sector ready while nothing needs the flash.
    if (snap.seq % SLOTS_PER_SECTOR == 0) next_erased = cu_store_erase_async(slot_addr(snap.seq % SLOTS));
    return true;
}

void cu_keys_task(void) {
    if (dirty && timer_elapsed32(last_save) >= SAVE_EVERY_MS) cu_keys_save();
}

void cu_keys_clear(void) {
    memset(snap.count, 0, sizeof(snap.count));
    snap.total = 0;
    dirty      = true;
    cu_keys_save();
}
