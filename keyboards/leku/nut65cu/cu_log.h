// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Battery / sleep / connection event log in the external flash (CU_STORE_LOG_BASE to the
// end of the chip), a ring of 16-byte records. Record seq N always sits in slot
// N % capacity, so a record is found by its seq and checked by it.

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CU_LOG_BOOT = 1,  // arg: 0
    CU_LOG_SLEEP,     // arg: cu_log_sleep_reason_t
    CU_LOG_WAKE,      // arg: low byte lpwr_wakeupcd_t, high byte = wake-ups that went straight back to sleep
    CU_LOG_BATTERY,   // battery level changed (the level is in every record)
    CU_LOG_CABLE,     // arg: 1 plugged in, 0 unplugged
    CU_LOG_CHARGED,   // charge complete
    CU_LOG_DEVICE,    // arg: new device (DEVS_*)
    CU_LOG_LINK,      // arg: wireless MD_STATE_*, or 0x10 + USB driver state while on USB
    CU_LOG_TIME,      // bytes 12-15 hold the Unix time the host sent, instead of bat/state/arg
    CU_LOG_MARK,      // arg: from the host (tools/cu_hid.py mark)
    CU_LOG_MODE,      // arg: cu_mode_t (normal / power save / game)
} cu_log_type_t;

typedef enum {
    CU_SLEEP_IDLE = 0, // no input for the sleep timeout
    CU_SLEEP_REQUEST,  // asked for: 2.4G host asleep, pairing / reconnect timed out
    CU_SLEEP_BATTERY,  // battery empty: deep sleep until a cable is plugged in
} cu_log_sleep_reason_t;

typedef struct __attribute__((packed)) {
    uint8_t  check; // see record_check()
    uint8_t  type;  // cu_log_type_t
    uint16_t boot;  // boot counter
    uint32_t seq;
    uint32_t ms;    // time awake since boot (the clock stops while asleep)
    uint8_t  bat;   // battery %, 0xFF unknown
    uint8_t  state; // bits 0-2 device (DEVS_*), 3 linked to the host, 4 cable in, 5 charge full
    uint16_t arg;
} cu_log_record_t;

_Static_assert(sizeof(cu_log_record_t) == 16, "log record must be 16 bytes");

void     cu_log_init(void);
void     cu_log_task(void);
void     cu_log_event(cu_log_type_t type, uint16_t arg);
void     cu_log_time(uint32_t unix_time);
bool     cu_log_ready(void);
uint16_t cu_log_boot(void);
uint32_t cu_log_capacity(void);
uint32_t cu_log_next_seq(void);    // seq the next record gets (= records written so far)
bool     cu_log_get(uint32_t seq, cu_log_record_t *rec);
bool     cu_log_check(const cu_log_record_t *rec, uint32_t slot);

// Sleep bookkeeping (lowpower hooks)
void cu_log_before_sleep(void);
void cu_log_stop_returned(uint8_t wakeupcd); // every return from MCU stop mode
void cu_log_after_wake(void);
