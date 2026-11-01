// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Per-device settings. Rather than hooking every key and VIA command that changes one
// of these settings, the live state is compared with the active device's profile a
// few times a second and stored when it differs. A device's first profile is captured
// from whatever is set when it is first used, so turning this on changes nothing.

#include QMK_KEYBOARD_H
#include "wireless.h"
#include "cu_profile.h"
#include "cu_barfx.h"
#include "cu_mode.h"
#include "cu_timer.h"

#define CU_EXT_MAGIC   0xC6
#define CU_EXT_VERSION 1
#define POLL_MS        250
#define SAVE_DELAY_MS  1000 // let a run of brightness steps settle first
#define NO_SLOT        0xFF

// nut65cu.c
extern bool    hs_os_is_mac(void);
extern void    hs_os_set_mac(bool mac);
extern bool    hs_f_row_get(void);
extern void    hs_f_row_set(bool on);
extern uint8_t hs_rl_mode_get(void);
extern void    hs_rl_mode_set(uint8_t mode);
extern uint8_t hs_rl_level_get(void);
extern void    hs_rl_level_set(uint8_t level);

static cu_ext_t ext;
static uint8_t  active = NO_SLOT;
static uint32_t changed_at; // unsaved change to the active profile, 0 = none

#define EXT_ADDR(field) ((uint8_t *)CU_EXT_EECONFIG_ADDR + offsetof(cu_ext_t, field))

static void save_header(void) {
    eeprom_update_block(&ext, CU_EXT_EECONFIG_ADDR, offsetof(cu_ext_t, profile));
}

static void save_profile(uint8_t slot) {
    eeprom_update_block(&ext.profile[slot], EXT_ADDR(profile) + slot * sizeof(cu_profile_t), sizeof(cu_profile_t));
}

void cu_profile_init(void) {
    active     = NO_SLOT;
    changed_at = 0;
    eeprom_read_block(&ext, CU_EXT_EECONFIG_ADDR, sizeof(ext));
    if (ext.magic != CU_EXT_MAGIC || ext.version != CU_EXT_VERSION) {
        ext = (cu_ext_t){.magic = CU_EXT_MAGIC, .version = CU_EXT_VERSION, .profiles_on = true};
        eeprom_update_block(&ext, CU_EXT_EECONFIG_ADDR, sizeof(ext));
    }
    cu_mode_init(ext.mode);
}

void cu_profile_save_mode(uint8_t mode) {
    if (ext.mode == mode) return;
    ext.mode = mode;
    save_header();
}

void cu_profile_invalidate(void) {
    eeprom_update_byte(CU_EXT_EECONFIG_ADDR, 0);
}

static cu_profile_t capture(void) {
    return (cu_profile_t){
        .valid     = true,
        .mac       = hs_os_is_mac(),
        .win_lock  = keymap_config.no_gui,
        .nkro      = keymap_config.nkro,
        .f_row     = hs_f_row_get(),
        .mode      = cu_mode_get(),
        .bar_mode  = hs_rl_mode_get(),
        .bar_level = hs_rl_level_get(),
    };
}

static bool same(const cu_profile_t *a, const cu_profile_t *b) {
    return a->mac == b->mac && a->win_lock == b->win_lock && a->nkro == b->nkro && a->f_row == b->f_row && a->mode == b->mode && a->bar_mode == b->bar_mode && a->bar_level == b->bar_level;
}

static void apply(const cu_profile_t *p) {
    cu_profile_t now = capture();

    if (p->mac != now.mac) hs_os_set_mac(p->mac);
    if (p->win_lock != now.win_lock || p->nkro != now.nkro) {
        clear_keyboard(); // nothing may stay pressed across an NKRO change
        keymap_config.no_gui = p->win_lock;
        keymap_config.nkro   = p->nkro;
        eeconfig_update_keymap(keymap_config.raw);
    }
    if (p->f_row != now.f_row) hs_f_row_set(p->f_row);
    if (p->mode != now.mode) cu_mode_set(p->mode);
    if (p->bar_mode != now.bar_mode) hs_rl_mode_set(p->bar_mode);
    if (p->bar_level != now.bar_level) hs_rl_level_set(p->bar_level);
}

static void flush(void) {
    if (changed_at && active != NO_SLOT) save_profile(active);
    changed_at = 0;
}

void cu_profile_switch(uint8_t devs) {
    if (!ext.profiles_on || devs >= CU_PROFILE_SLOTS || devs == active) return;

    flush();
    active = devs;
    if (ext.profile[devs].valid) {
        apply(&ext.profile[devs]);
    } else {
        ext.profile[devs] = capture();
        save_profile(devs);
    }
}

void cu_profile_task(void) {
    static uint32_t last_poll;
    if (!ext.profiles_on || active == NO_SLOT || timer_elapsed32(last_poll) < POLL_MS) return;
    last_poll = timer_read32();

    cu_profile_t now = capture();
    if (!same(&now, &ext.profile[active])) {
        ext.profile[active] = now;
        changed_at          = cu_timer_start();
    }
    if (changed_at && timer_elapsed32(changed_at) >= SAVE_DELAY_MS) flush();
}

bool cu_profile_enabled(void) {
    return ext.profiles_on;
}

void cu_profile_set_enabled(bool on) {
    if (ext.profiles_on == on) return;
    flush();
    ext.profiles_on = on;
    active          = NO_SLOT;
    save_header();
    if (!on) return;
    // The current device keeps what is set now; the others keep what they had.
    active = wireless_get_current_devs();
    if (active >= CU_PROFILE_SLOTS) {
        active = NO_SLOT;
        return;
    }
    ext.profile[active] = capture();
    save_profile(active);
}

uint8_t cu_profile_active(void) {
    return active;
}

bool cu_profile_get(uint8_t slot, cu_profile_t *p) {
    if (slot >= CU_PROFILE_SLOTS) return false;
    *p = ext.profile[slot];
    return true;
}

bool cu_profile_set(uint8_t slot, const cu_profile_t *p) {
    if (slot >= CU_PROFILE_SLOTS || p->bar_mode > CU_BAR_MODE_LAST || p->bar_level > 4 || p->mode >= CU_MODE_COUNT) return false;
    ext.profile[slot]       = *p;
    ext.profile[slot].valid = true;
    save_profile(slot);
    if (slot == active) {
        changed_at = 0;
        apply(&ext.profile[slot]);
    }
    return true;
}
