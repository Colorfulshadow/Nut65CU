// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Per-device settings: each connection (USB, BT1-5, 2.4G) remembers its own Win/Mac
// mode, Win lock, NKRO, number-row mode and light bar. Changing one of them while on a
// device stores it for that device; switching devices applies the other one's.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CU_PROFILE_SLOTS 8 // indexed by DEVS_* (USB 0, BT1-5 1-5, 2.4G 6)

typedef struct __attribute__((packed)) {
    uint8_t valid : 1;    // captured from this device at least once
    uint8_t mac : 1;      // Mac base layer
    uint8_t win_lock : 1; // GUI key disabled (GU_TOGG)
    uint8_t nkro : 1;
    uint8_t f_row : 1;    // number row sends F1-F12 / media (KC_FTOG)
    uint8_t mode : 2;     // cu_mode_t
    uint8_t reserved_bits : 1;
    uint8_t bar_mode;     // light bar mode 0-12
    uint8_t bar_level;    // light bar brightness 0-4
    uint8_t reserved;
} cu_profile_t;

// EEPROM: the CU extension block at the start of the user datablock (config.h).
typedef struct __attribute__((packed)) {
    uint8_t      magic;   // CU_EXT_MAGIC
    uint8_t      version; // CU_EXT_VERSION
    uint8_t      profiles_on : 1;
    uint8_t      reserved_bits : 7;
    uint8_t      mode;    // cu_mode_t in use (also kept per device while profiles are on)
    cu_profile_t profile[CU_PROFILE_SLOTS];
} cu_ext_t;

_Static_assert(sizeof(cu_profile_t) == 4, "cu_profile_t is 4 bytes in the HID protocol");
_Static_assert(sizeof(cu_ext_t) <= EECONFIG_CU_EXT_SIZE, "cu_ext_t must fit EECONFIG_CU_EXT_SIZE");

void cu_profile_init(void);
void cu_profile_task(void);
void cu_profile_switch(uint8_t devs); // the board switched to this device
void cu_profile_invalidate(void);     // factory reset: back to defaults on the next init
void cu_profile_save_mode(uint8_t mode); // cu_mode.c: remember the mode in use
bool cu_profile_enabled(void);
void cu_profile_set_enabled(bool on);
uint8_t cu_profile_active(void);      // slot in use, 0xFF none
bool cu_profile_get(uint8_t slot, cu_profile_t *p);
bool cu_profile_set(uint8_t slot, const cu_profile_t *p);
