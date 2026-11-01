// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Event log for checking battery life and sleep behaviour: what the keyboard did and
// when, with the battery level and connection state in every record. Read it with
// tools/cu_hid.py log.

#include QMK_KEYBOARD_H
#include <string.h>
#include "wireless.h"
#include "usb_main.h"
#include "lowpower.h"
#include "cu_store.h"
#include "cu_log.h"

extern bool charging_state; // nut65cu.c: USB cable present
extern bool bat_full_flag;  // nut65cu.c: charger reports full
extern bool lower_sleep;    // nut65cu.c: battery empty

#define SLOTS_PER_SECTOR  (CU_STORE_SECTOR / sizeof(cu_log_record_t))
#define PRE_ERASE_LEFT    16    // erase the next sector this many records before it is needed
#define SAMPLE_MS         1000
#define BAT_SETTLE_MS     10000 // the module reports the level a little after boot / wake
#define BAT_MIN_GAP_MS    60000 // at most one battery record a minute, unless it moved 5 %
#define NO_SECTOR         UINT32_MAX

static bool     ready;
static uint32_t capacity; // records
static uint32_t sectors;
static uint32_t next_seq;
static uint16_t boot;
static uint32_t erased_sector = NO_SECTOR; // erased ahead of the head, not written yet

static uint8_t  stop_returns, last_wakeupcd;
static uint32_t settle_from; // battery readings are trusted from here on

static uint32_t slot_addr(uint32_t slot) {
    return CU_STORE_LOG_BASE + slot * sizeof(cu_log_record_t);
}

static uint8_t record_check(const cu_log_record_t *rec) {
    const uint8_t *p = (const uint8_t *)rec;
    uint8_t        c = 0x5A;
    for (uint8_t i = 1; i < sizeof(*rec); i++) {
        c = (uint8_t)((c << 1) | (c >> 7)) ^ p[i];
    }
    return c;
}

bool cu_log_check(const cu_log_record_t *rec, uint32_t slot) {
    return rec->type != 0xFF && rec->check == record_check(rec) && rec->seq % capacity == slot;
}

static bool read_slot(uint32_t slot, cu_log_record_t *rec) {
    return cu_store_read(slot_addr(slot), rec, sizeof(*rec)) && cu_log_check(rec, slot);
}

static uint8_t state_bits(void) {
    uint8_t devs   = wireless_get_current_devs();
    bool    linked = devs == DEVS_USB ? USB_DRIVER.state == USB_ACTIVE : *md_getp_state() == MD_STATE_CONNECTED;
    return (devs & 0x07) | (linked << 3) | (charging_state << 4) | ((charging_state && bat_full_flag) << 5);
}

static void write_record(cu_log_record_t *rec) {
    if (!ready || cu_store_state() != CU_STORE_OK) return;

    uint32_t slot   = next_seq % capacity;
    uint32_t sector = slot / SLOTS_PER_SECTOR;
    uint32_t pos    = slot % SLOTS_PER_SECTOR;

    if (pos == 0) {
        if (erased_sector != sector && !cu_store_erase(slot_addr(slot))) return;
        erased_sector = NO_SECTOR;
    }

    rec->seq   = next_seq;
    rec->boot  = boot;
    rec->check = record_check(rec);
    if (!cu_store_write(slot_addr(slot), rec, sizeof(*rec))) return;
    next_seq++;

    if (pos >= SLOTS_PER_SECTOR - PRE_ERASE_LEFT && erased_sector == NO_SECTOR) {
        uint32_t next = (sector + 1) % sectors;
        if (cu_store_erase_async(slot_addr(next * SLOTS_PER_SECTOR))) erased_sector = next;
    }
}

void cu_log_event(cu_log_type_t type, uint16_t arg) {
    cu_log_record_t rec = {
        .type  = type,
        .ms    = timer_read32(),
        .bat   = *md_getp_bat(),
        .state = state_bits(),
        .arg   = arg,
    };
    write_record(&rec);
}

void cu_log_time(uint32_t unix_time) {
    cu_log_record_t rec = {.type = CU_LOG_TIME, .ms = timer_read32()};
    memcpy(&rec.bat, &unix_time, sizeof(unix_time)); // bat, state, arg
    write_record(&rec);
}

void cu_log_init(void) {
    if (cu_store_state() == CU_STORE_NONE) return;
    capacity = (cu_store_size() - CU_STORE_LOG_BASE) / sizeof(cu_log_record_t);
    sectors  = capacity / SLOTS_PER_SECTOR;

    // The newest sector is the one whose first record has the highest seq.
    cu_log_record_t rec, newest;
    bool            found = false, foreign = false;
    uint32_t        head  = 0;
    for (uint32_t s = 0; s < sectors; s++) {
        uint32_t slot = s * SLOTS_PER_SECTOR;
        if (read_slot(slot, &rec)) {
            if (!found || rec.seq > newest.seq) {
                newest = rec;
                head   = s;
            }
            found = true;
        } else if (!cu_store_is_blank(slot_addr(slot), sizeof(rec))) {
            foreign = true; // or a torn write of ours: only matters if nothing of ours is here
        }
    }
    if (!found && foreign) {
        cu_store_lock(); // someone else's data: leave it until formatted (cu_hid.py format)
        return;
    }

    if (found) {
        for (uint32_t i = 1; i < SLOTS_PER_SECTOR; i++) {
            if (!read_slot(head * SLOTS_PER_SECTOR + i, &rec) || rec.seq != newest.seq + 1) break;
            newest = rec;
        }
        next_seq = newest.seq + 1;
        boot     = newest.boot + 1;
        // Skip a torn record after the newest one; a new sector is erased before use.
        while (next_seq % SLOTS_PER_SECTOR && !cu_store_is_blank(slot_addr(next_seq % capacity), sizeof(rec))) {
            next_seq++;
        }
    } else {
        next_seq = 0;
        boot     = 1;
    }

    ready       = true;
    settle_from = timer_read32();
    cu_log_event(CU_LOG_BOOT, 0);
}

bool cu_log_ready(void) {
    return ready;
}

uint16_t cu_log_boot(void) {
    return boot;
}

uint32_t cu_log_capacity(void) {
    return capacity;
}

uint32_t cu_log_next_seq(void) {
    return next_seq;
}

bool cu_log_get(uint32_t seq, cu_log_record_t *rec) {
    if (!ready || seq >= next_seq || next_seq - seq > capacity) return false;
    return read_slot(seq % capacity, rec) && rec->seq == seq;
}

void cu_log_task(void) {
    static uint32_t last_sample, last_bat_time;
    static uint8_t  last_bat = 0xFF, last_devs = 0xFF, last_link = 0xFF;
    static bool     last_cable, last_full, started;

    if (!ready || timer_elapsed32(last_sample) < SAMPLE_MS) return;
    last_sample = timer_read32();

    uint8_t devs  = wireless_get_current_devs();
    bool    full  = charging_state && bat_full_flag;
    uint8_t link  = devs == DEVS_USB ? 0x10 + USB_DRIVER.state : *md_getp_state();
    if (!started) { // the boot record already has the state
        started    = true;
        last_devs  = devs;
        last_link  = link;
        last_cable = charging_state;
        last_full  = full;
        return;
    }

    if (devs != last_devs) {
        last_devs = devs;
        last_link = link;
        cu_log_event(CU_LOG_DEVICE, devs);
    } else if (link != last_link) {
        last_link = link;
        cu_log_event(CU_LOG_LINK, link);
    }
    if (charging_state != last_cable) {
        last_cable = charging_state;
        cu_log_event(CU_LOG_CABLE, charging_state);
    }
    if (full != last_full) {
        last_full = full;
        if (full) cu_log_event(CU_LOG_CHARGED, 0);
    }

    uint8_t bat = *md_getp_bat();
    if (timer_elapsed32(settle_from) >= BAT_SETTLE_MS && bat != last_bat) {
        uint8_t diff = bat > last_bat ? bat - last_bat : last_bat - bat;
        if (last_bat == 0xFF || diff >= 5 || timer_elapsed32(last_bat_time) >= BAT_MIN_GAP_MS) {
            last_bat      = bat;
            last_bat_time = timer_read32();
            cu_log_event(CU_LOG_BATTERY, 0);
        }
    }
}

cu_log_sleep_reason_t cu_log_sleep_reason(void) {
    uint32_t timeout = lpwr_timeout_value_read();
    return lower_sleep ? CU_SLEEP_BATTERY : (timeout && last_input_activity_elapsed() >= timeout) ? CU_SLEEP_IDLE : CU_SLEEP_REQUEST;
}

void cu_log_before_sleep(void) {
    cu_log_event(CU_LOG_SLEEP, cu_log_sleep_reason());
    stop_returns = 0;
}

void cu_log_stop_returned(uint8_t wakeupcd) {
    if (stop_returns < 255) stop_returns++;
    last_wakeupcd = wakeupcd;
}

void cu_log_after_wake(void) {
    // Every return but the last went straight back to sleep (UART traffic from the module).
    uint8_t resleeps = stop_returns ? stop_returns - 1 : 0;
    cu_log_event(CU_LOG_WAKE, last_wakeupcd | (resleeps << 8));
    settle_from = timer_read32();
}
