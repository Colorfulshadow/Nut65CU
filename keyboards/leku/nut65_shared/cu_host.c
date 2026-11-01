// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// CU raw HID commands, the bootloader request and the typed status report (Fn+I).

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
#include "cu_timer.h"
#include "cu_mode.h"
#include "cu_debounce.h"
#include "cu_overlay.h"
#include "cu_stage.h"

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

// [0] CU_HID_CMD [1] CU_HID_EVENT [2] 0 [3] event [4] arg, see cu_host.h.
static void send_event(cu_hid_event_t event, uint8_t arg) {
    uint8_t ev[32] = {CU_HID_CMD, CU_HID_EVENT, CU_HID_OK, event, arg};
    replaced_hid_send(ev, sizeof(ev));
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

static cu_hid_status_t boot_request(uint8_t *p); // bootloader request, below
static void            boot_cancel(void);

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
    p[27] = (hs_rl_mode_get() & 15) | hs_rl_level_get() << 4 | (hs_rl_mode_get() >> 4) << 7; // mode bit 4 in bit 7 (protocol 5)
}

// INFO page 1 (protocol 4): the model this firmware was built for and its LEDs.
// [0] protocol [1] page (1) [2] model (1 NUT65 CU, 2 NUT65 with per-key RGB) [3..4] LED count
// [5] key LEDs a host may set with KEY_LIGHT (0: none) [6] first bar LED [7] bar LEDs
// [8] host light flags: bit0 IND (Q-T), bit1 KEY_LIGHT, bit2 BAR. The rest is 0.
static void info_model(uint8_t *p) {
    memset(p, 0, 28);
    p[0] = CU_HID_PROTOCOL;
    p[1] = 1;
    p[2] = NUT65_MODEL_ID;
    put16(&p[3], RGB_MATRIX_LED_COUNT);
    p[5] = NUT65_KEY_LEDS;
    p[6] = HS_RGB_STRIP_START;
    p[7] = RGBLED_NUM;
    p[8] = 1 << 0 | (NUT65_KEY_LEDS ? 1 << 1 : 0) | 1 << 2;
}

static cu_hid_status_t handle(uint8_t sub, const uint8_t *arg, uint8_t *p) {
    switch (sub) {
        case CU_HID_INFO:
            if (arg[0] == 1) info_model(p);
            else info(p); // page 0, also what hosts before protocol 4 get (they send 0)
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
        case CU_HID_DEBOUNCE_SET:
            if (!cu_debounce_set(arg[0], arg[1], arg[2])) return CU_HID_BAD_ARGUMENT;
            // SET and GET have identical readback payloads.
            __attribute__((fallthrough));
        case CU_HID_DEBOUNCE_GET: {
            uint8_t normal, game;
            if (!cu_debounce_get(arg[0], &normal, &game)) return CU_HID_BAD_ARGUMENT;
            p[0] = arg[0]; p[1] = normal; p[2] = game;
            p[3] = cu_debounce_slot(); p[4] = cu_debounce_effective(); p[5] = cu_mode_get();
            memcpy(p + 6, arg + 3, 4); // opaque request nonce, for late-reply rejection
            return CU_HID_OK;
        }
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
        case CU_HID_BAR: {
            _Static_assert(sizeof(cu_bar_layer_t) <= 28, "cu_bar_layer_t must fit the arguments");
            cu_bar_layer_t layer;
            memcpy(&layer, arg, sizeof(layer));
            return cu_overlay_bar_set(&layer) ? CU_HID_OK : CU_HID_BAD_ARGUMENT;
        }
        case CU_HID_IND: {
            cu_host_ind_t ind;
            memcpy(&ind, arg, sizeof(ind));
            return cu_overlay_ind_set(&ind) ? CU_HID_OK : CU_HID_BAD_ARGUMENT;
        }
#if NUT65_KEY_LEDS
        case CU_HID_KEY_LIGHT: {
            cu_host_ind_t key;
            memcpy(&key, arg, sizeof(key));
            return cu_overlay_key_set(&key) ? CU_HID_OK : CU_HID_BAD_ARGUMENT;
        }
#endif
        case CU_HID_LIGHT_CLEAR:
            cu_overlay_clear();
            return CU_HID_OK;
        case CU_HID_STAGE:
            cu_stage_command(arg,p);
            return CU_HID_OK;
        case CU_HID_BOOT_REQUEST:
            if (get32(arg) != CU_HID_BOOT_MAGIC) return CU_HID_BAD_ARGUMENT;
            return boot_request(p);
        case CU_HID_BOOT_CANCEL:
            boot_cancel();
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

/* ---------- link to the computer ---------- */

// Host lights belong to the computer on the other end of the link: they go when the link
// drops (device switched, computer asleep, keyboard asleep), and once it is back the host
// is told, so a host program can put its lights back without polling a sleeping keyboard.
#define LINK_EVENT_DELAY_MS 500 // let a fresh 2.4G / USB link settle first

static bool     link_was_up;
static uint8_t  link_devs = 0xFF;
static uint32_t link_up_since; // LINK_UP not sent yet, 0 = nothing to send

static bool link_up(void) {
    return wireless_get_current_devs() == DEVS_USB ? USB_DRIVER.state == USB_ACTIVE : *md_getp_state() == MD_STATE_CONNECTED;
}

static void link_task(void) {
    uint8_t devs = wireless_get_current_devs();
    bool    up   = link_up() && devs == link_devs;
    link_devs    = devs;

    if (!up) {
        if (link_was_up) cu_overlay_clear();
        link_was_up   = false;
        link_up_since = 0;
        return;
    }
    if (!link_was_up) {
        link_was_up   = true;
        link_up_since = cu_timer_start();
    }
    if (link_up_since && timer_elapsed32(link_up_since) >= LINK_EVENT_DELAY_MS) {
        link_up_since = 0;
        send_event(CU_HID_EVENT_LINK_UP, devs);
    }
}

/* ---------- bootloader request ---------- */

// CU_HID_BOOT_REQUEST: the computer wants to update the firmware, but the keyboard only
// goes into the bootloader once the user agrees on the keyboard itself. The light bar
// blinks amber (cu_draw_bar) and the next key press decides: the top-left key, Esc,
// enters the bootloader, any other key says no. It goes by the matrix position, so a
// remapped Esc still works, and that press (and its release) never reaches the
// computer. The computer is told the answer with a BOOT event; if the link drops
// (device switched, USB suspended or unplugged) or the keyboard sleeps, the request is
// dropped without one. USB only: the WB32 bootloader (DFU) only talks USB.
#define BOOT_ANSWER_MS     10000
#define BOOT_JUMP_DELAY_MS 100 // let the BOOT event go out first
#define BOOT_KEY_ROW       0   // Esc
#define BOOT_KEY_COL       0

static uint32_t boot_since;      // request waiting for the user since, 0 = none
static uint32_t boot_jump_since; // Esc pressed: bootloader soon, 0 = not
static bool     boot_key_down;   // the key that answered is still held: drop its release too
static keypos_t boot_key;

static bool boot_usb_ok(void) {
    return wireless_get_current_devs() == DEVS_USB && USB_DRIVER.state == USB_ACTIVE;
}

static cu_hid_status_t boot_request(uint8_t *p) {
    if (!boot_usb_ok() || boot_jump_since) return CU_HID_FAILED;
    boot_since = cu_timer_start(); // a new request starts the time again
    p[0]       = BOOT_ANSWER_MS / 1000;
    return CU_HID_OK;
}

static void boot_cancel(void) {
    boot_since      = 0;
    boot_jump_since = 0; // also in the last 100 ms: staying out of the bootloader is the safe side
}

uint32_t cu_host_boot_waiting(void) {
    return boot_since;
}

bool cu_host_boot_key(keyrecord_t *record) {
    keypos_t k    = record->event.key;
    bool     same = boot_key_down && k.row == boot_key.row && k.col == boot_key.col;

    if (!record->event.pressed) {
        if (!same) return true;
        boot_key_down = false;
        return false;
    }
    if (same) boot_key_down = false; // its release went missing: don't eat the next one
    if (!boot_since) return true;

    bool yes      = IS_KEYEVENT(record->event) && k.row == BOOT_KEY_ROW && k.col == BOOT_KEY_COL;
    boot_since    = 0;
    boot_key_down = true;
    boot_key      = k;
    send_event(CU_HID_EVENT_BOOT, yes ? CU_HID_BOOT_YES : CU_HID_BOOT_NO);
    if (yes) boot_jump_since = cu_timer_start();
    return false;
}

static void boot_task(void) {
    if (!boot_usb_ok()) { // nobody left to answer, and no USB for the bootloader
        boot_cancel();
        return;
    }
    if (boot_jump_since && timer_elapsed32(boot_jump_since) >= BOOT_JUMP_DELAY_MS) {
        // Keep the key counts since the last snapshot, as before sleep.
        if (cu_keys_dirty()) cu_keys_save();
        cu_store_wait();
        // As QK_BOOT does (nut65cu.c): no eeconfig_disable(), the settings stay.
        bootloader_jump();
    }
    if (boot_since && timer_elapsed32(boot_since) >= BOOT_ANSWER_MS) {
        boot_since = 0;
        send_event(CU_HID_EVENT_BOOT, CU_HID_BOOT_TIMEOUT);
    }
}

/* ---------- sleep ---------- */

void cu_host_before_sleep(void) {
    // Tell the computer first, so a host program stops asking a sleeping keyboard. Best
    // effort: over the air it is only queued for the module (md_send_raw, which never
    // blocks), and the module has until the MCU stops (LPWR_STOP_DELAY, 200 ms) to send it;
    // on USB the board never sleeps with the link up. Not for a requested sleep: with the
    // link up that is the 2.4G receiver reporting its computer asleep, and a report then
    // could wake the computer.
    cu_log_sleep_reason_t reason = cu_log_sleep_reason();
    if (link_was_up && link_up() && reason != CU_SLEEP_REQUEST) send_event(CU_HID_EVENT_SLEEP, reason);

    boot_cancel();
    cu_overlay_clear(); // the clock stops while asleep: lights would outlive their time
    link_was_up   = false; // announce the link again after waking
    link_up_since = 0;
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
    link_task();
    boot_task();

    if (!report_len || !timer_expired32(timer_read32(), report_next)) return;
    // Wireless reports go through the module's UART queue: leave it time to drain.
    report_next = timer_read32() + (wireless_get_current_devs() == DEVS_USB ? 4 : 15);
    send_char(report[report_pos++]);
    if (report_pos >= report_len) report_end();
}
