// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// NUT65 CU light bar features:
//   Fn+;  (CU_POMO)   pomodoro: tap = start work / skip to the next phase, hold 1 s = stop
//   Fn+'  (CU_PSHW)  pomodoro progress on the light bar on/off
//   Fn+A  (CU_WPM)    WPM meter on the light bar on/off
//   Fn+I  (CU_INFO)   type a status report (cu_host.c)
//   Fn+P  (CU_MODE)   normal -> power save -> game mode (cu_mode.c)
//   plus a low-battery alert on the bar and switching to USB while a cable is plugged in,
//   and it starts and runs the rest: external flash, event log, key counters, per-device
//   settings, the raw HID commands and the computer's lights (cu_store/log/keys/profile/host/overlay.c),
//   and blinks the bar while a bootloader request from the computer waits for Esc (cu_host.c),
//   and draws the added light bar effects 13-24 (cu_barfx.c),
//   and plays the easter eggs (cu_egg.c: typed sequences, key count milestones, WPM records, pomodoro sets).
//
// Everything on the bar is drawn at the bar brightness (hs_strip_val(), at most 75):
// 80 LEDs at full brightness draw enough current to make the LED boost converter whine.

#include QMK_KEYBOARD_H
#include "wireless.h"
#include "usb_main.h"
#include "lib/lib8tion/lib8tion.h"
#include "cu_features.h"
#include "cu_timer.h"
#include "cu_store.h"
#include "cu_log.h"
#include "cu_keys.h"
#include "cu_profile.h"
#include "cu_debounce.h"
#include "cu_host.h"
#include "cu_mode.h"
#include "cu_overlay.h"
#include "cu_egg.h"
#include "cu_barfx.h"

extern bool    charging_state; // nut65cu.c: USB cable present (HS_BAT_CABLE_PIN)
extern uint8_t hs_strip_val(void);
extern uint8_t hs_rl_mode_get(void);
extern void    hs_rl_mode_set(uint8_t mode);
extern uint8_t hs_rl_level_get(void);
extern void    hs_rl_level_set(uint8_t level);

#define BAR_N RGBLED_NUM

#define HUE_RED    0
#define HUE_WORK   6  // tomato
#define HUE_AMBER  22 // bootloader request
#define HUE_YELLOW 36
#define HUE_GREEN  85

#define POMO_HOLD_MS          1000 // hold Fn+; this long to stop the timer
#define WPM_PEAK_HOLD_MS      1500 // peak marker stays this long, then falls
#define WPM_PEAK_FALL_MS      40   // ... one LED per this many ms
#define AUTO_USB_DEBOUNCE_MS  200
#define USB_LINK_WAIT_MS      5000 // a host normally configures the board within a second, but Windows sometimes takes 3-4 s:
                                   // restarting USB then only interrupts it (seen 2026-10-31: 3 s, two restarts, gave up)
#define USB_LINK_RESTARTS     2    // USB restarts before a cable-triggered switch gives up
#define LOW_BAT_GRACE_MS      10000 // battery level isn't known right after boot
#define BAR_SPEED_MIN         55    // the OEM speed keys step 55, 95, ... 215
#define BOOT_BLINK_MS         250   // bootloader request: on / off this long, 2 Hz
#define WPM_RECORD_MIN        40    // a record is only celebrated once the old one is at least this
#define WPM_RECORD_HOLD_MS    1500  // ... and after typing faster than it for this long
#define POMO_SET              4     // pomodoros in a row that earn the harvest show

/* ---------- settings ---------- */

static cu_settings_t cu;

static void settings_save(void) {
    eeprom_update_block(&cu, CU_EECONFIG_ADDR, sizeof(cu));
}

static void settings_load(void) {
    eeprom_read_block(&cu, CU_EECONFIG_ADDR, sizeof(cu));
    if (cu.magic != CU_SETTINGS_MAGIC) {
        cu = (cu_settings_t){
            .magic         = CU_SETTINGS_MAGIC,
            .pomo_work     = 25,
            .pomo_break    = 5,
            .wpm_full      = 100,
            .pomo_show     = true,
            .auto_usb      = true,
            .low_bat_alert = true,
            .auto_prev_devs = DEVS_USB,
        };
        settings_save();
    }
    cu.pomo_work  = MAX(5, MIN(cu.pomo_work, 60));
    cu.pomo_break = MAX(1, MIN(cu.pomo_break, 30));
    cu.wpm_full   = MAX(40, MIN(cu.wpm_full, 200));
    if (cu.auto_prev_devs > DEVS_2G4) cu.auto_prev_devs = DEVS_USB;
    if (cu.bar_fx > CU_BARFX_COUNT) cu.bar_fx = 0;
}

/* ---------- light bar helpers ---------- */

static void bar_set(uint8_t i, RGB rgb) {
    rgb_matrix_set_color(HS_RGB_STRIP_START + i, rgb.r, rgb.g, rgb.b);
}

static void bar_fill(RGB rgb) {
    for (uint8_t i = 0; i < BAR_N; i++) {
        bar_set(i, rgb);
    }
}

static RGB bar_hsv(uint8_t hue, uint8_t sat, uint8_t val) {
    return hsv_to_rgb((HSV){.h = hue, .s = sat, .v = val});
}

// Whole-bar flashes for feedback and alerts; a new one replaces the current one.
static struct {
    bool     active;
    uint32_t start;
    uint16_t interval;
    uint8_t  times, hue, sat;
} flash;

static void flash_start(uint8_t hue, uint8_t sat, uint8_t times, uint16_t interval) {
    flash.active   = true;
    flash.start    = timer_read32();
    flash.interval = interval;
    flash.times    = times;
    flash.hue      = hue;
    flash.sat      = sat;
}

static void flash_draw(void) {
    if (!flash.active) return;

    uint32_t step = timer_elapsed32(flash.start) / flash.interval;
    if (step >= flash.times * 2u) {
        flash.active = false;
        return;
    }
    bar_fill(step % 2 ? (RGB){0} : bar_hsv(flash.hue, flash.sat, hs_strip_val()));
}

// A bootloader request from the computer (cu_host.c) waits for Esc: amber at 2 Hz over
// everything, starting lit, until it is answered, cancelled or times out.
static void boot_request_draw(void) {
    uint32_t since = cu_host_boot_waiting();
    if (!since) return;
    bool lit = timer_elapsed32(since) / BOOT_BLINK_MS % 2 == 0;
    bar_fill(lit ? bar_hsv(HUE_AMBER, 255, hs_strip_val()) : (RGB){0});
}

/* ---------- pomodoro ---------- */

typedef enum { POMO_IDLE, POMO_WORK, POMO_BREAK } pomo_phase_t;

static pomo_phase_t pomo_phase = POMO_IDLE;
static uint32_t     pomo_start, pomo_len;
static uint32_t     pomo_key_down; // when Fn+; went down, 0 when up
static bool         pomo_key_long;
static uint8_t      pomo_done; // work phases finished in a row (skipping one doesn't count; stopping resets)

static void pomo_begin(pomo_phase_t phase) {
    pomo_phase = phase;
    pomo_start = timer_read32();
    pomo_len   = (uint32_t)(phase == POMO_WORK ? cu.pomo_work : cu.pomo_break) * 60000;
    flash_start(phase == POMO_WORK ? HUE_WORK : HUE_GREEN, 255, 1, 200);
}

static void pomo_task(void) {
    if (pomo_key_down && !pomo_key_long && timer_elapsed32(pomo_key_down) >= POMO_HOLD_MS) {
        pomo_key_long = true;
        pomo_done     = 0;
        if (pomo_phase != POMO_IDLE) {
            pomo_phase = POMO_IDLE;
            flash_start(0, 0, 2, 150); // white: stopped
        }
    }

    if (pomo_phase == POMO_IDLE || timer_elapsed32(pomo_start) < pomo_len) return;

    if (pomo_phase == POMO_WORK) {
        pomo_begin(POMO_BREAK);
        if (++pomo_done % POMO_SET == 0) cu_egg_play(CU_EGG_POMO); // a set done: the harvest instead of the flashes
        else flash_start(HUE_GREEN, 255, 6, 300);                  // time for a break
    } else {
        pomo_phase = POMO_IDLE;
        flash_start(HUE_WORK, 255, 6, 300); // break over, Fn+; starts the next round
    }
}

// Remaining time as a bar emptying from the right, tomato while working, green on a break.
static bool pomo_draw(void) {
    if (pomo_phase == POMO_IDLE || !cu.pomo_show) return false;

    // Deciseconds keep remaining * BAR_N * 256 inside 32 bits for up to 60 minutes.
    uint32_t len_ds    = pomo_len / 100;
    uint32_t remain_ds = (pomo_len - MIN(timer_elapsed32(pomo_start), pomo_len)) / 100;
    uint32_t pos       = remain_ds * BAR_N * 256 / len_ds;
    uint8_t  full = pos >> 8, frac = pos & 0xFF;
    uint8_t  hue  = pomo_phase == POMO_WORK ? HUE_WORK : HUE_GREEN;
    uint8_t  v    = hs_strip_val();

    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t val = i < full ? v : (i == full ? scale8(v, frac) : 0);
        bar_set(i, bar_hsv(hue, 255, val));
    }
    return true;
}

/* ---------- WPM meter ---------- */

// Fills outwards from the middle, green to red, full at cu.wpm_full; a dim white
// marker holds the recent peak. The two middle LEDs glow faintly while idle.
static void wpm_draw(void) {
    static uint32_t peak, peak_time, peak_fall;
    const uint8_t   half = BAR_N / 2;
    uint8_t         wpm  = get_current_wpm();
    uint32_t        level = (uint32_t)MIN(wpm, cu.wpm_full) * half * 256 / cu.wpm_full;
    uint8_t         lit = level >> 8, frac = level & 0xFF;
    uint8_t         v = hs_strip_val();

    if (level >= peak) {
        peak      = level;
        peak_time = peak_fall = timer_read32();
    } else if (timer_elapsed32(peak_time) > WPM_PEAK_HOLD_MS && timer_elapsed32(peak_fall) >= WPM_PEAK_FALL_MS) {
        peak      = peak > 256 ? peak - 256 : 0;
        peak_fall = timer_read32();
    }

    for (uint8_t d = 0; d < half; d++) {
        uint8_t val = d < lit ? v : (d == lit ? scale8(v, frac) : 0);
        if (d == 0 && wpm == 0) val = MAX(val, v / 4);
        RGB rgb = bar_hsv(HUE_GREEN - (uint16_t)HUE_GREEN * d / (half - 1), 255, val);
        bar_set(half - 1 - d, rgb);
        bar_set(half + d, rgb);
    }

    uint8_t pd = peak >> 8;
    if (pd > lit && pd < half) {
        RGB rgb = bar_hsv(0, 0, v / 2);
        bar_set(half - 1 - pd, rgb);
        bar_set(half + pd, rgb);
    }
}

// Typing faster than ever: the fastest WPM of each burst of typing is kept once the
// typing stops (one settings write per new record); beating an established record for
// WPM_RECORD_HOLD_MS plays the gold sweep once per burst.
static void wpm_record_task(void) {
    static uint32_t last_check, above_since;
    static uint8_t  peak;
    static bool     shown;

    if (timer_elapsed32(last_check) < 100) return;
    last_check  = timer_read32();
    uint8_t wpm = get_current_wpm();
    if (wpm == 0) {
        if (peak > cu.wpm_best) {
            cu.wpm_best = peak;
            settings_save();
        }
        peak        = 0;
        above_since = 0;
        shown       = false;
        return;
    }
    peak = MAX(peak, wpm);
    if (wpm <= cu.wpm_best || cu.wpm_best < WPM_RECORD_MIN) {
        above_since = 0;
        return;
    }
    if (!above_since) above_since = cu_timer_start();
    if (!shown && timer_elapsed32(above_since) >= WPM_RECORD_HOLD_MS) {
        shown = true;
        cu_egg_play(CU_EGG_WPM);
    }
}

/* ---------- low battery ---------- */

// On battery: 15-30 % two yellow flashes every 30 s, below 15 % four red flashes every 15 s.
static void low_bat_task(void) {
    static uint32_t last_alert;

    if (!cu.low_bat_alert || charging_state || flash.active || timer_read32() < LOW_BAT_GRACE_MS) return;

    uint8_t bat = *md_getp_bat();
    if (bat > 30) {
        last_alert = 0;
        return;
    }

    bool     critical = bat < BATTERY_CAPACITY_LOW;
    uint32_t period   = critical ? 15000 : 30000;
    if (last_alert && timer_elapsed32(last_alert) < period) return;

    last_alert = cu_timer_start();
    flash_start(critical ? HUE_RED : HUE_YELLOW, 255, critical ? 4 : 2, 250);
}

/* ---------- USB while a cable is plugged in ---------- */

// Plugging a cable in switches to USB and unplugging it goes back to the wireless
// device that was in use. If no USB host shows up (a charger), it goes back as well.
// Picking a device by hand while plugged in cancels the switch back. The device to go
// back to is kept in EEPROM: with the power switch off, unplugging cuts the power
// before the unplug can be seen, so the next start on battery switches back instead.
static void set_auto_prev(uint8_t devs) {
    if (cu.auto_prev_devs != devs) {
        cu.auto_prev_devs = devs;
        settings_save();
    }
}

static void auto_usb_task(void) {
    static bool     started, cable;
    static uint32_t cable_change; // debounce start, 0 = stable

    uint8_t devs = wireless_get_current_devs();

    if (!started) {
        started = true;
        if (!charging_state && devs == DEVS_USB && cu.auto_prev_devs != DEVS_USB) {
            wireless_devs_change(DEVS_USB, cu.auto_prev_devs, false);
            set_auto_prev(DEVS_USB);
            return;
        }
    }

    if (cu.auto_prev_devs != DEVS_USB && devs != DEVS_USB) { // changed by hand
        set_auto_prev(DEVS_USB);
    }

    if (charging_state == cable) {
        cable_change = 0;
        return;
    }
    if (!cable_change) cable_change = cu_timer_start();
    if (timer_elapsed32(cable_change) < AUTO_USB_DEBOUNCE_MS) return;
    cable_change = 0;
    cable        = charging_state;

    if (cable) {
        if (cu.auto_usb && devs != DEVS_USB) {
            set_auto_prev(devs);
            wireless_devs_change(devs, DEVS_USB, false);
        }
    } else if (cu.auto_prev_devs != DEVS_USB) {
        wireless_devs_change(DEVS_USB, cu.auto_prev_devs, false);
        set_auto_prev(DEVS_USB);
    }
}

// USB link watchdog, on USB with a cable in. Seen 2026-10-14: after switching from 2.4G
// to USB on a cable, the host never saw the board (no device arrival on the PC). USB sat
// idle (USB_SUSPENDED), and the OEM rule in transport.c "suspended for 7 s -> sleep" then
// put the board to sleep every 7 s, cutting off any enumeration, until the cable was
// replugged. Now, until the host has configured the board once:
//   - it doesn't sleep (cu_usb_link_pending() in lpwr_is_allow_timeout_hook);
//   - after USB_LINK_WAIT_MS the USB connection is restarted, like replugging the cable;
//   - after USB_LINK_RESTARTS restarts, a switch made by plugging the cable in goes back
//     to the wireless device (also what happens on a charger, which never configures it).
// Once configured, a suspended USB means the computer is asleep and the OEM rule applies.
static bool usb_configured; // the host configured the board since this USB + cable session began

bool cu_usb_link_pending(void) {
    return wireless_get_current_devs() == DEVS_USB && charging_state && !usb_configured;
}

static void usb_link_task(void) {
    static uint32_t since; // start of the current wait, 0 = not waiting
    static uint8_t  restarts;

    if (wireless_get_current_devs() != DEVS_USB || !charging_state) {
        usb_configured = false;
        since          = 0;
        restarts       = 0;
        return;
    }
    if (USB_DRIVER.state == USB_ACTIVE) usb_configured = true;
    if (usb_configured) return;

    if (!since) since = cu_timer_start();
    if (timer_elapsed32(since) < USB_LINK_WAIT_MS) return;
    since = cu_timer_start();

    if (restarts < USB_LINK_RESTARTS) {
        restarts++;
        cu_log_event(CU_LOG_USB_RETRY, restarts);
        usb_power_connect();
        restart_usb_driver(&USBD1);
    } else if (cu.auto_prev_devs != DEVS_USB) {
        uint8_t prev = cu.auto_prev_devs;
        cu_log_event(CU_LOG_USB_RETRY, 0xFF);
        set_auto_prev(DEVS_USB);
        wireless_devs_change(DEVS_USB, prev, false);
    }
    // Picked by hand (Fn+T): stay on USB; the next attempt is the user's.
}

/* ---------- hooks ---------- */

void cu_features_init(void) {
    settings_load();
    cu_profile_init();
    cu_debounce_init();

    // A factory reset runs keyboard_post_init_kb again: keep the log and unsaved counts.
    static bool store_started;
    if (!store_started) {
        store_started = true;
        cu_store_init();
        cu_log_init();
        cu_keys_init();
    }
}

void cu_features_task(void) {
    // Let the board restore its saved device first (wireless_post_task, 100 ms after boot).
    if (timer_read32() > 1000) {
        auto_usb_task();
        usb_link_task();
    }
    pomo_task();
    wpm_record_task();
    cu_egg_task();
    low_bat_task();
    cu_store_task();
    cu_log_task();
    cu_keys_task();
    cu_profile_task();
    cu_host_task();
    cu_mode_task();
}

void cu_features_before_sleep(void) {
    cu_host_before_sleep();
    cu_log_before_sleep();
    if (cu_keys_dirty()) cu_keys_save();
    cu_store_wait(); // don't stop the clocks in the middle of an erase
}

void cu_features_after_wake(void) {
    cu_log_after_wake();
    cu_mode_after_wake();
}

bool cu_process_record(uint16_t keycode, keyrecord_t *record) {
    if (!cu_mode_process_record(keycode, record)) return false;

    switch (keycode) {
        case CU_MODE:
            if (record->event.pressed) {
                cu_mode_t next = (cu_mode_get() + 1) % CU_MODE_COUNT;
                cu_mode_set(next);
                // white: normal, green: power save, red: game
                flash_start(next == CU_MODE_GAME ? HUE_RED : HUE_GREEN, next == CU_MODE_NORMAL ? 0 : 255, 2, 150);
            }
            return false;
        case CU_WPM:
            if (record->event.pressed) {
                cu.wpm_on = !cu.wpm_on;
                settings_save();
                if (!cu.wpm_on) flash_start(HUE_RED, 255, 1, 200);
            }
            return false;
        case CU_POMO:
            if (record->event.pressed) {
                pomo_key_down = cu_timer_start();
                pomo_key_long = false;
            } else {
                if (pomo_key_down && !pomo_key_long) {
                    pomo_begin(pomo_phase == POMO_WORK ? POMO_BREAK : POMO_WORK);
                }
                pomo_key_down = 0;
            }
            return false;
        case CU_INFO:
            if (record->event.pressed) cu_host_report();
            return false;
        case CU_PSHW:
            if (record->event.pressed) {
                cu.pomo_show = !cu.pomo_show;
                settings_save();
                // While it runs the change is visible anyway; when idle, acknowledge it.
                if (pomo_phase == POMO_IDLE) flash_start(cu.pomo_show ? HUE_WORK : HUE_RED, 255, 1, 200);
            }
            return false;
        default:
            return true;
    }
}

void cu_draw_bar(void) {
    // Power save mode: only alerts and feedback (the flashes) and the computer's lights.
    if (cu_mode_bar_dark()) {
        bar_fill((RGB){0});
    } else {
        bool pomo_shown = pomo_phase != POMO_IDLE && cu.pomo_show;

        // The bar's own mode: an added effect replaces what the OEM mode drew.
        if (cu.bar_fx) cu_barfx_draw(cu.bar_fx);

        // With both on, the pomodoro shows while you're not typing.
        if (cu.wpm_on && (get_current_wpm() > 0 || !pomo_shown)) {
            wpm_draw();
        } else {
            pomo_draw();
        }
    }
    // The computer's lights (cu_overlay.c), bar and Q-T indicators, under the flashes.
    cu_overlay_draw_bar();
#if NUT65_KEY_LEDS
    cu_overlay_draw_keys(); // per-key models: under the Q-T host indicators
#endif
    cu_overlay_draw_indicators();
    flash_draw();
    cu_egg_draw(); // easter egg shows
    // The keyboard asking whether to enter the bootloader goes over everything.
    boot_request_draw();
}

uint8_t cu_bar_fx_get(void) {
    return cu.bar_fx;
}

void cu_bar_fx_set(uint8_t fx) {
    if (fx > CU_BARFX_COUNT || fx == cu.bar_fx) return;
    cu.bar_fx = fx;
    settings_save();
}

bool cu_prevent_sleep(void) {
    // The MCU clock stops while asleep, so sleeping would freeze the timer.
    return pomo_phase != POMO_IDLE;
}

bool cu_via_get_value(uint8_t value_id, uint8_t *value_data) {
    switch (value_id) {
        case id_cu_pomo_work:     value_data[0] = cu.pomo_work; break;
        case id_cu_pomo_break:    value_data[0] = cu.pomo_break; break;
        case id_cu_auto_usb:      value_data[0] = cu.auto_usb; break;
        case id_cu_low_bat_alert: value_data[0] = cu.low_bat_alert; break;
        case id_cu_wpm_full:      value_data[0] = cu.wpm_full; break;
        case id_cu_wpm_on:        value_data[0] = cu.wpm_on; break;
        case id_cu_pomo_show:     value_data[0] = cu.pomo_show; break;
        case id_cu_bar_mode:      value_data[0] = hs_rl_mode_get(); break;
        case id_cu_bar_level:     value_data[0] = hs_rl_level_get() + 1; break;
        case id_cu_profiles:      value_data[0] = cu_profile_enabled(); break;
        case id_cu_mode:          value_data[0] = cu_mode_get(); break;
        case id_cu_bar_speed:     value_data[0] = MIN(5, 1 + (MAX(rgb_matrix_get_speed(), BAR_SPEED_MIN) - BAR_SPEED_MIN + RGB_MATRIX_SPD_STEP / 2) / RGB_MATRIX_SPD_STEP); break;
        default: return false;
    }
    return true;
}

bool cu_via_set_value(uint8_t value_id, uint8_t *value_data) {
    uint8_t v = value_data[0];
    switch (value_id) {
        case id_cu_pomo_work:     cu.pomo_work = MAX(5, MIN(v, 60)); break;
        case id_cu_pomo_break:    cu.pomo_break = MAX(1, MIN(v, 30)); break;
        case id_cu_auto_usb:      cu.auto_usb = v != 0; break;
        case id_cu_low_bat_alert: cu.low_bat_alert = v != 0; break;
        case id_cu_wpm_full:      cu.wpm_full = MAX(40, MIN(v, 200)); break;
        case id_cu_wpm_on:        cu.wpm_on = v != 0; break;
        case id_cu_pomo_show:     cu.pomo_show = v != 0; break;
        // Kept by the board (confinfo) and the RGB matrix config, not in cu_settings_t.
        case id_cu_bar_mode:      hs_rl_mode_set(v); return true;
        case id_cu_bar_level:     hs_rl_level_set(MAX(1, MIN(v, 5)) - 1); return true;
        case id_cu_profiles:      cu_profile_set_enabled(v != 0); return true;
        case id_cu_mode:          cu_mode_set(v); return true;
        case id_cu_bar_speed:     rgb_matrix_set_speed(BAR_SPEED_MIN + (MAX(1, MIN(v, 5)) - 1) * RGB_MATRIX_SPD_STEP); return true;
        default: return false;
    }
    settings_save();
    return true;
}
