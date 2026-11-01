// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "wireless.h"
#include "cu_debounce.h"
#include "cu_profile.h"
#include "cu_mode.h"
extern uint16_t hs_deb; // old DEB_flag decoded by keyboard_post_init_kb

// Independent versioned block in UNUSED bytes of the 180-byte CU extension.
// The original 36-byte profile block and all following EEPROM addresses stay fixed.
#define DEB_ADDR ((uint8_t *)CU_EXT_EECONFIG_ADDR + 36)
#define DEB_MAGIC 0xD6
typedef struct __attribute__((packed)) {
    uint8_t magic, version;
    uint8_t ms[8][2]; // USB, BT1-5, 2.4G, global; normal/eco then game
} deb_settings_t;
static deb_settings_t settings;
_Static_assert(sizeof(cu_ext_t) == 36, "debounce storage follows the v1 profile block");
_Static_assert(36 + sizeof(deb_settings_t) <= EECONFIG_CU_EXT_SIZE, "debounce must fit reserved EEPROM");

static bool valid(uint8_t ms) { return ms >= 1 && ms <= CU_DEBOUNCE_MAX; }

void cu_debounce_init(void) {
    eeprom_read_block(&settings, DEB_ADDR, sizeof settings);
    if (settings.magic != DEB_MAGIC || settings.version != 1) {
        settings.magic = DEB_MAGIC;
        settings.version = 1;
        // Preserve the old Fn+D choice for normal/eco. The new game setting defaults
        // to 8 ms and is never silently forced down to 1 ms.
        for (uint8_t s = 0; s < 8; s++) {
            settings.ms[s][0] = hs_deb == 1 ? 1 : CU_DEBOUNCE_DEFAULT;
            settings.ms[s][1] = CU_DEBOUNCE_DEFAULT;
        }
    }
    for (uint8_t s = 0; s < 8; s++)
        for (uint8_t m = 0; m < 2; m++)
            if (!valid(settings.ms[s][m])) settings.ms[s][m] = CU_DEBOUNCE_DEFAULT;
    eeprom_update_block(&settings, DEB_ADDR, sizeof settings);
}

uint8_t cu_debounce_slot(void) {
    uint8_t s = wireless_get_current_devs();
    return cu_profile_enabled() && s < 7 ? s : CU_DEBOUNCE_GLOBAL;
}
uint8_t cu_debounce_effective(void) {
    uint8_t ms = settings.ms[cu_debounce_slot()][cu_mode_get() == CU_MODE_GAME];
    return valid(ms) ? ms : CU_DEBOUNCE_DEFAULT;
}
bool cu_debounce_get(uint8_t slot, uint8_t *normal, uint8_t *game) {
    if (slot >= 8) return false;
    *normal = settings.ms[slot][0];
    *game = settings.ms[slot][1];
    return true;
}
bool cu_debounce_set(uint8_t slot, uint8_t normal, uint8_t game) {
    if (slot >= 8 || !valid(normal) || !valid(game)) return false;
    settings.ms[slot][0] = normal;
    settings.ms[slot][1] = game;
    eeprom_update_block(settings.ms[slot], DEB_ADDR + offsetof(deb_settings_t, ms) + slot * 2, 2);
    return true;
}
void cu_debounce_toggle(void) {
    uint8_t s = cu_debounce_slot(), m = cu_mode_get() == CU_MODE_GAME;
    settings.ms[s][m] = settings.ms[s][m] == 1 ? CU_DEBOUNCE_DEFAULT : 1;
    cu_debounce_set(s, settings.ms[s][0], settings.ms[s][1]);
}
