// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Talking to the computer: the CU raw HID commands (next to VIA's, over USB, 2.4G or
// Bluetooth; host side tools/cu_hid.py) and the status report typed by Fn+I.
//
// Request:  [0] CU_HID_CMD  [1] sub-command  [2..30] arguments
// Reply:    [0] CU_HID_CMD  [1] sub-command  [2] cu_hid_status_t  [3..30] payload (28 bytes)
// Event:    [0] CU_HID_CMD  [1] CU_HID_EVENT  [2] 0  [3] cu_hid_event_t  [4..30] payload
//           sent by the keyboard on its own (not a reply); hosts must match replies on
//           byte 1 as well as byte 0, and ignore events they don't know.
// Multi-byte values are little-endian. Byte 31 is never used: over Bluetooth (BLE) the
// module carries 31-byte reports, the last byte of ours doesn't make it.
//
// Protocol versions (INFO payload byte 0):
//   1  state, log, key counters, per-device settings, flash read
//   2  + host lights (BAR / IND / LIGHT_CLEAR) and events (LINK_UP)
//   3  + bootloader request confirmed on the keyboard (BOOT_REQUEST / BOOT_CANCEL,
//        event BOOT), event SLEEP; the VIA keymap and macros survive firmware updates
//        (fixed VIA EEPROM magic, config.h)
//   4  + INFO page 1 (request byte 2 = 1): which keyboard model this firmware is for and
//        its LEDs, so hosts no longer guess the model from the USB PID; KEY_LIGHT for
//        models with per-key LEDs (the regular NUT65). Built from the shared code in
//        keyboards/leku/nut65_shared for both models.

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "action.h"

#define CU_HID_CMD      0xC5 // not a VIA command id
#define CU_HID_PROTOCOL 5    // 4: INFO page 1 (model), KEY_LIGHT; 5: light bar modes 13-24 (INFO p[27] bit 7 = mode bit 4)

typedef enum {
    CU_HID_OK = 0,
    CU_HID_BAD_COMMAND,
    CU_HID_BAD_ARGUMENT,
    CU_HID_NO_STORE, // external flash missing or locked
    CU_HID_FAILED,
} cu_hid_status_t;

enum cu_hid_command {
    CU_HID_INFO           = 0x01, // u8 page: 0 state snapshot, 1 model and LEDs (protocol 4), see cu_host.c
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
    CU_HID_BAR            = 0x50, // cu_bar_layer_t (cu_overlay.h): a light bar layer
    CU_HID_IND            = 0x51, // cu_host_ind_t: an indicator LED under Q W E R T
    CU_HID_LIGHT_CLEAR    = 0x52, // remove every host light
    CU_HID_KEY_LIGHT      = 0x53, // cu_host_ind_t with index = a key's LED number; models with per-key LEDs
                                  // only (others answer BAD_COMMAND)
    CU_HID_BOOT_REQUEST   = 0x60, // u32 CU_HID_BOOT_MAGIC -> u8 seconds to answer; USB only (else FAILED).
                                  // The bar blinks amber, the next key press decides: Esc (matrix 0,0)
                                  // enters the bootloader, any other key cancels; event BOOT tells which
    CU_HID_BOOT_CANCEL    = 0x61, // withdraw a pending BOOT_REQUEST (OK even if none is pending)
    CU_HID_EVENT          = 0x80, // keyboard -> host only
};

typedef enum {
    CU_HID_EVENT_LINK_UP = 1, // [4] device (DEVS_*): the link to this computer is (back) up, host lights are clear
    CU_HID_EVENT_SLEEP   = 2, // [4] cu_log_sleep_reason_t: about to sleep (best effort, may not make it out over the air)
    CU_HID_EVENT_BOOT    = 3, // [4] cu_hid_boot_answer_t: the answer to a BOOT_REQUEST
} cu_hid_event_t;

typedef enum {
    CU_HID_BOOT_NO = 0,  // another key was pressed
    CU_HID_BOOT_YES,     // Esc: the keyboard enters the bootloader ~100 ms later (USB DFU 342D:DFA0)
    CU_HID_BOOT_TIMEOUT, // no key within the time given
} cu_hid_boot_answer_t;

#define CU_HID_KEYS_CLEAR_MAGIC 0x5AA5
#define CU_HID_FORMAT_MAGIC     0x21544D46 // "FMT!"
#define CU_HID_BOOT_MAGIC       0x544F4F42 // "BOOT"

bool     cu_host_hid(uint8_t *data, uint8_t length); // true: it was ours and the reply is sent
void     cu_host_report(void);                       // start typing the status report
void     cu_host_key_event(uint16_t keycode, keyrecord_t *record);
bool     cu_host_boot_key(keyrecord_t *record); // pre_process_record_kb: false = the key answered a BOOT_REQUEST, drop it
uint32_t cu_host_boot_waiting(void);            // a BOOT_REQUEST waits for the user since (timer), 0 = none
void     cu_host_task(void);
void     cu_host_before_sleep(void);
