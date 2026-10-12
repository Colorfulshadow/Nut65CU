// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Talking to the computer: the CU raw HID commands (next to VIA's, over USB, 2.4G or
// Bluetooth; host side tools/cu_hid.py) and the status report typed by Fn+I.
//
// Request:  [0] CU_HID_CMD  [1] sub-command  [2..30] arguments
// Reply:    [0] CU_HID_CMD  [1] sub-command  [2] cu_hid_status_t  [3..30] payload (28 bytes)
// Multi-byte values are little-endian. Byte 31 is never used: over Bluetooth (BLE) the
// module carries 31-byte reports, the last byte of ours doesn't make it.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "action.h"

#define CU_HID_CMD      0xC5 // not a VIA command id
#define CU_HID_PROTOCOL 1

typedef enum {
    CU_HID_OK = 0,
    CU_HID_BAD_COMMAND,
    CU_HID_BAD_ARGUMENT,
    CU_HID_NO_STORE, // external flash missing or locked
    CU_HID_FAILED,
} cu_hid_status_t;

enum cu_hid_command {
    CU_HID_INFO           = 0x01, // state snapshot, see cu_host.c
    CU_HID_BUILD          = 0x02, // build date string
    CU_HID_TIME           = 0x03, // u32 Unix time -> log record
    CU_HID_ECHO           = 0x04, // payload back unchanged
    CU_HID_DIAG           = 0x05, // md_diag_t (module.h), module firmware version, link state
    CU_HID_LOG_INFO       = 0x10,
    CU_HID_LOG_READ       = 0x11, // u32 seq -> found, 16-byte record
    CU_HID_LOG_MARK       = 0x12, // u16 arg -> CU_LOG_MARK record
    CU_HID_KEYS_INFO      = 0x20,
    CU_HID_KEYS_READ      = 0x21, // u8 first index (row * cols + col) -> up to 6 u32 counts
    CU_HID_KEYS_SAVE      = 0x22,
    CU_HID_KEYS_CLEAR     = 0x23, // u16 CU_HID_KEYS_CLEAR_MAGIC
    CU_HID_PROFILE_GET    = 0x30, // u8 slot
    CU_HID_PROFILE_SET    = 0x31, // u8 slot, 4-byte cu_profile_t
    CU_HID_PROFILE_ENABLE = 0x32, // u8 on
    CU_HID_FLASH_READ     = 0x40, // u32 address -> u32 address, 24 bytes (whole chip, read only)
    CU_HID_FLASH_FORMAT   = 0x41, // u32 CU_HID_FORMAT_MAGIC: erase the log and counter regions
};

#define CU_HID_KEYS_CLEAR_MAGIC 0x5AA5
#define CU_HID_FORMAT_MAGIC     0x21544D46 // "FMT!"

bool cu_host_hid(uint8_t *data, uint8_t length); // true: it was ours and the reply is sent
void cu_host_report(void);                       // start typing the status report
void cu_host_key_event(uint16_t keycode, keyrecord_t *record);
void cu_host_task(void);
