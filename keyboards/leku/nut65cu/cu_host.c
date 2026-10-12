// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// CU raw HID commands and the typed status report (Fn+I).

#include QMK_KEYBOARD_H
#include <string.h>
#include "version.h"
#include "wireless.h"
#include "usb_main.h"
#include "cu_store.h"
#include "cu_log.h"
#include "cu_keys.h"
#include "cu_profile.h"
#include "cu_host.h"
#include "cu_mode.h"

// nut65cu.c / wls.c
extern bool     charging_state;
extern bool     bat_full_flag;
extern bool     hs_os_is_mac(void);
extern bool     hs_f_row_get(void);
extern uint8_t  hs_rl_mode_get(void);
extern uint8_t  hs_rl_level_get(void);
extern uint8_t  hs_get_sleep_timeout(void);
void            replaced_hid_send(uint8_t *data, uint8_t length); // md_raw.c: USB or wireless

#define KEY_COUNT (MATRIX_ROWS * MATRIX_COLS)

static void put16(uint8_t *p, uint16_t v) {
    memcpy(p, &v, 2);
}

static void put32(uint8_t *p, uint32_t v) {
    memcpy(p, &v, 4);
}

static uint16_t get16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static uint32_t get32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint8_t link_state(void) {
    return wireless_get_current_devs() == DEVS_USB ? 0x10 + USB_DRIVER.state : *md_getp_state();
}

static uint8_t log2_size(void) {
    uint8_t n = 0;
    for (uint32_t s = cu_store_size(); s > 1; s >>= 1) n++;
    return n;
}

/* ---------- raw HID ---------- */

// Fills p[0..27] (reply bytes 3-30).
static void info(uint8_t *p) {
    p[0] = CU_HID_PROTOCOL;
    p[1] = wireless_get_current_devs();
    p[2] = link_state();
    p[3] = *md_getp_bat();
    p[4] = charging_state | (charging_state && bat_full_flag) << 1 | keymap_config.nkro << 2 | hs_os_is_mac() << 3 | keymap_config.no_gui << 4 | hs_f_row_get() << 5 | cu_profile_enabled() << 6;
    p[5] = hs_get_sleep_timeout() | cu_mode_get() << 4;
    p[6] = cu_store_state();
    p[7] = log2_size();
    put32(&p[8], timer_read32());
    put16(&p[12], cu_log_boot());
    put32(&p[14], cu_log_next_seq());
    put32(&p[18], cu_keys_total());
    uint32_t id = cu_store_jedec_id();
    p[22] = id >> 16;
    p[23] = id >> 8;
    p[24] = id;
    p[25] = cu_profile_active();
    p[26] = cu_mode_debounce();
    p[27] = hs_rl_mode_get() | hs_rl_level_get() << 4;
}

static cu_hid_status_t handle(uint8_t sub, const uint8_t *arg, uint8_t *p) {
    switch (sub) {
        case CU_HID_INFO:
            info(p);
            return CU_HID_OK;
        case CU_HID_BUILD:
            strncpy((char *)p, QMK_BUILDDATE, 27);
            return CU_HID_OK;
        case CU_HID_TIME:
            cu_log_time(get32(arg));
            return CU_HID_OK;
        case CU_HID_DIAG:
            _Static_assert(sizeof(md_diag) <= 26, "md_diag_t must fit the payload");
            p[0] = md_get_version();
            p[1] = link_state();
            memcpy(&p[2], &md_diag, sizeof(md_diag));
            return CU_HID_OK;
        case CU_HID_ECHO:
            return CU_HID_OK; // p already holds the request's bytes 3-30
        case CU_HID_LOG_INFO:
            p[0] = cu_store_state();
            put32(&p[1], cu_log_capacity());
            put32(&p[5], cu_log_next_seq());
            put16(&p[9], cu_log_boot());
            return CU_HID_OK;
        case CU_HID_LOG_READ: {
            cu_log_record_t rec;
            p[0] = cu_log_get(get32(arg), &rec);
            memcpy(&p[1], &rec, p[0] ? sizeof(rec) : 0);
            return CU_HID_OK;
        }
        case CU_HID_LOG_MARK:
            if (!cu_log_ready()) return CU_HID_NO_STORE;
            cu_log_event(CU_LOG_MARK, get16(arg));
            return CU_HID_OK;
        case CU_HID_KEYS_INFO:
            p[0] = MATRIX_ROWS;
            p[1] = MATRIX_COLS;
            put32(&p[2], cu_keys_total());
            put32(&p[6], cu_keys_saved_seq());
            p[10] = cu_keys_dirty();
            return CU_HID_OK;
        case CU_HID_KEYS_READ: {
            uint8_t first = arg[0];
            if (first >= KEY_COUNT) return CU_HID_BAD_ARGUMENT;
            uint8_t n = MIN(6, KEY_COUNT - first);
            p[0]      = first;
            p[1]      = n;
            for (uint8_t i = 0; i < n; i++) {
                uint8_t k = first + i;
                put32(&p[2 + i * 4], cu_keys_get(k / MATRIX_COLS, k % MATRIX_COLS));
            }
            return CU_HID_OK;
        }
        case CU_HID_KEYS_SAVE:
            return cu_keys_save() ? CU_HID_OK : CU_HID_NO_STORE;
        case CU_HID_KEYS_CLEAR:
            if (get16(arg) != CU_HID_KEYS_CLEAR_MAGIC) return CU_HID_BAD_ARGUMENT;
            cu_keys_clear();
            return CU_HID_OK;
        case CU_HID_PROFILE_GET: {
            cu_profile_t prof;
            if (!cu_profile_get(arg[0], &prof)) return CU_HID_BAD_ARGUMENT;
            p[0] = arg[0];
            p[1] = cu_profile_active();
            p[2] = cu_profile_enabled();
            memcpy(&p[3], &prof, sizeof(prof));
            return CU_HID_OK;
        }
        case CU_HID_PROFILE_SET: {
            cu_profile_t prof;
            memcpy(&prof, &arg[1], sizeof(prof));
            return cu_profile_set(arg[0], &prof) ? CU_HID_OK : CU_HID_BAD_ARGUMENT;
        }
        case CU_HID_PROFILE_ENABLE:
            cu_profile_set_enabled(arg[0] != 0);
            return CU_HID_OK;
        case CU_HID_FLASH_READ: {
            uint32_t addr = get32(arg);
            put32(&p[0], addr);
            return cu_store_read(addr, &p[4], 24) ? CU_HID_OK : CU_HID_BAD_ARGUMENT;
        }
        case CU_HID_FLASH_FORMAT:
            if (get32(arg) != CU_HID_FORMAT_MAGIC) return CU_HID_BAD_ARGUMENT;
            cu_keys_save(); // no-op while locked
            if (!cu_store_format()) return CU_HID_NO_STORE;
            cu_log_init();
            cu_keys_init();
            return CU_HID_OK;
        default:
            return CU_HID_BAD_COMMAND;
    }
}

bool cu_host_hid(uint8_t *data, uint8_t length) {
    if (data[0] != CU_HID_CMD || length < 32) return false;

    // The payload starts out as the request's bytes 3-30, which is what ECHO returns.
    uint8_t arg[30];
    memcpy(arg, &data[2], sizeof(arg));
    data[2] = handle(data[1], arg, &data[3]);
    replaced_hid_send(data, length);
    return true;
}

/* ---------- typed status report ---------- */

static char     report[480];
static uint16_t report_len, report_pos;
static uint32_t report_next;
static bool     report_caps; // Caps Lock was on: turned off while typing, back on after

static void add(const char *s) {
    while (*s && report_len < sizeof(report) - 1) report[report_len++] = *s++;
}

static void add_u32(uint32_t v) {
    char  buf[11];
    char *p = buf + sizeof(buf) - 1;
    *p      = 0;
    do {
        *--p = '0' + v % 10;
        v /= 10;
    } while (v);
    add(p);
}

static const char *devs_name(uint8_t devs) {
    static const char *const names[] = {"USB", "BT1", "BT2", "BT3", "BT4", "BT5", "2.4G"};
    return devs < ARRAY_SIZE(names) ? names[devs] : "?";
}

static void build_report(void) {
    static const char *const sleep_names[] = {"never", "1 min", "3 min", "5 min", "10 min", "20 min", "30 min", "5 min"};
    uint8_t                  devs          = wireless_get_current_devs();
    uint32_t                 up            = timer_read32() / 60000;

    report_len = 0;
    static const char *const mode_names[] = {"normal", "power save", "game"};

    add("NUT65 CU | firmware ");
    add(QMK_BUILDDATE);
    add(" | boot ");
    add_u32(cu_log_boot());
    add(" | awake ");
    add_u32(up / 60);
    add("h ");
    add_u32(up % 60);
    add("m | ");
    add(mode_names[cu_mode_get()]);
    add(" mode\n");

    add("Link: ");
    add(devs_name(devs));
    if (devs == DEVS_USB) {
        add(USB_DRIVER.state == USB_ACTIVE ? " connected" : USB_DRIVER.state == USB_SUSPENDED ? " suspended" : " no host");
    } else {
        uint8_t st = *md_getp_state();
        add(st == MD_STATE_CONNECTED ? " connected" : st == MD_STATE_PAIRING ? " pairing" : " disconnected");
    }
    add(" | battery ");
    add_u32(*md_getp_bat());
    add(charging_state ? (bat_full_flag ? "% (charged)" : "% (charging)") : "% (on battery)");
    add(" | sleep after ");
    add(sleep_names[hs_get_sleep_timeout() & 7]);
    add("\n");

    add("Keys: ");
    add(hs_os_is_mac() ? "Mac" : "Win");
    add(keymap_config.no_gui ? " | Win key locked" : "");
    add(" | NKRO ");
    add(keymap_config.nkro ? "on" : "off");
    add(" | number row ");
    add(hs_f_row_get() ? "F1-F12" : "digits");
    add(" | debounce ");
    add_u32(cu_mode_debounce());
    add(" ms | per-device settings ");
    if (cu_profile_enabled() && cu_profile_active() < CU_PROFILE_SLOTS) {
        add("on (");
        add(devs_name(cu_profile_active()));
        add(")\n");
    } else {
        add(cu_profile_enabled() ? "on\n" : "off\n");
    }

    add("Light bar: mode ");
    add_u32(hs_rl_mode_get());
    add(", brightness ");
    add_u32(hs_rl_level_get() + 1);
    add("/5\n");

    add("Stats: ");
    add_u32(cu_keys_total());
    add(" key presses | ");
    switch (cu_store_state()) {
        case CU_STORE_OK:
            add_u32(cu_log_next_seq());
            add(" log events | flash ");
            add_u32(cu_store_size() / 1024);
            add(" KB\n");
            break;
        case CU_STORE_LOCKED:
            add("flash holds unknown data, not logging (tools/cu_hid.py format)\n");
            break;
        default:
            add("no flash, not logging\n");
            break;
    }
    report[report_len] = 0;
}

void cu_host_report(void) {
    build_report();
    report_pos  = 0;
    report_next = timer_read32();
    report_caps = host_keyboard_led_state().caps_lock;
    if (report_caps) tap_code(KC_CAPS);
}

static void report_end(void) {
    if (report_caps) tap_code(KC_CAPS);
    report_len = report_pos = 0;
}

void cu_host_key_event(uint16_t keycode, keyrecord_t *record) {
    // Any other key stops the report.
    if (report_len && record->event.pressed && keycode != CU_INFO) report_end();
}

void cu_host_task(void) {
    if (!report_len || !timer_expired32(timer_read32(), report_next)) return;
    // Wireless reports go through the module's UART queue: leave it time to drain.
    report_next = timer_read32() + (wireless_get_current_devs() == DEVS_USB ? 4 : 15);
    send_char(report[report_pos++]);
    if (report_pos >= report_len) report_end();
}
