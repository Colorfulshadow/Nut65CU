// Copyright 2026
// SPDX-License-Identifier: GPL-2.0-or-later
//
// NUT65 CU light bar features:
//   Fn+;  (CU_POMO)   pomodoro: tap = start work / skip to the next phase, hold 1 s = stop
//   Fn+'  (CU_PSHW)  pomodoro progress on the light bar on/off
//   Fn+A  (CU_WPM)    WPM meter on the light bar on/off
//   plus a low-battery alert on the bar and switching to USB while a cable is plugged in.
//
// Everything on the bar is drawn at the bar brightness (hs_strip_val(), at most 75):
// 80 LEDs at full brightness draw enough current to make the LED boost converter whine.

#include QMK_KEYBOARD_H
#include "wireless.h"
#include "usb_main.h"
#include "lib/lib8tion/lib8tion.h"
#include "cu_features.h"

extern bool    charging_state; // nut65cu.c: USB cable present (HS_BAT_CABLE_PIN)
extern uint8_t hs_strip_val(void);
extern uint8_t hs_rl_mode_get(void);
extern void    hs_rl_mode_set(uint8_t mode);
extern uint8_t hs_rl_level_get(void);
extern void    hs_rl_level_set(uint8_t level);

#define BAR_N RGBLED_NUM

#define HUE_RED    0
#define HUE_WORK   6  // tomato
#define HUE_YELLOW 36
#define HUE_GREEN  85

#define POMO_HOLD_MS          1000 // hold Fn+; this long to stop the timer
#define WPM_PEAK_HOLD_MS      1500 // peak marker stays this long, then falls
#define WPM_PEAK_FALL_MS      40   // ... one LED per this many ms
#define AUTO_USB_DEBOUNCE_MS  200
#define AUTO_USB_HOST_WAIT_MS 5000 // no USB host by then (charger only): go back to wireless
#define LOW_BAT_GRACE_MS      10000 // battery level isn't known right after boot
#define BAR_SPEED_MIN         55    // the OEM speed keys step 55, 95, ... 215

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

/* ---------- pomodoro ---------- */

typedef enum { POMO_IDLE, POMO_WORK, POMO_BREAK } pomo_phase_t;

static pomo_phase_t pomo_phase = POMO_IDLE;
static uint32_t     pomo_start, pomo_len;
static uint32_t     pomo_key_down; // when Fn+; went down, 0 when up
static bool         pomo_key_long;

static void pomo_begin(pomo_phase_t phase) {
    pomo_phase = phase;
    pomo_start = timer_read32();
    pomo_len   = (uint32_t)(phase == POMO_WORK ? cu.pomo_work : cu.pomo_break) * 60000;
    flash_start(phase == POMO_WORK ? HUE_WORK : HUE_GREEN, 255, 1, 200);
}

static void pomo_task(void) {
    if (pomo_key_down && !pomo_key_long && timer_elapsed32(pomo_key_down) >= POMO_HOLD_MS) {
        pomo_key_long = true;
        if (pomo_phase != POMO_IDLE) {
            pomo_phase = POMO_IDLE;
            flash_start(0, 0, 2, 150); // white: stopped
        }
    }

    if (pomo_phase == POMO_IDLE || timer_elapsed32(pomo_start) < pomo_len) return;

    if (pomo_phase == POMO_WORK) {
        pomo_begin(POMO_BREAK);
        flash_start(HUE_GREEN, 255, 6, 300); // time for a break
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

    last_alert = timer_read32() | 1;
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
    static uint32_t host_wait;

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
        host_wait = 0;
    }

    if (host_wait && timer_elapsed32(host_wait) >= AUTO_USB_HOST_WAIT_MS) {
        host_wait = 0;
        if (cu.auto_prev_devs != DEVS_USB && USB_DRIVER.state != USB_ACTIVE && USB_DRIVER.state != USB_SUSPENDED) {
            wireless_devs_change(DEVS_USB, cu.auto_prev_devs, false);
            set_auto_prev(DEVS_USB);
        }
    }

    if (charging_state == cable) {
        cable_change = 0;
        return;
    }
    if (!cable_change) cable_change = timer_read32() | 1;
    if (timer_elapsed32(cable_change) < AUTO_USB_DEBOUNCE_MS) return;
    cable_change = 0;
    cable        = charging_state;

    if (cable) {
        if (cu.auto_usb && devs != DEVS_USB) {
            set_auto_prev(devs);
            host_wait = timer_read32() | 1;
            wireless_devs_change(devs, DEVS_USB, false);
        }
    } else if (cu.auto_prev_devs != DEVS_USB) {
        wireless_devs_change(DEVS_USB, cu.auto_prev_devs, false);
        set_auto_prev(DEVS_USB);
        host_wait = 0;
    }
}

/* ---------- hooks ---------- */

void cu_features_init(void) {
    settings_load();
}

void cu_features_task(void) {
    // Let the board restore its saved device first (wireless_post_task, 100 ms after boot).
    if (timer_read32() > 1000) auto_usb_task();
    pomo_task();
    low_bat_task();
}

bool cu_process_record(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case CU_WPM:
            if (record->event.pressed) {
                cu.wpm_on = !cu.wpm_on;
                settings_save();
                if (!cu.wpm_on) flash_start(HUE_RED, 255, 1, 200);
            }
            return false;
        case CU_POMO:
            if (record->event.pressed) {
                pomo_key_down = timer_read32() | 1;
                pomo_key_long = false;
            } else {
                if (pomo_key_down && !pomo_key_long) {
                    pomo_begin(pomo_phase == POMO_WORK ? POMO_BREAK : POMO_WORK);
                }
                pomo_key_down = 0;
            }
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
    bool pomo_shown = pomo_phase != POMO_IDLE && cu.pomo_show;

    // With both on, the pomodoro shows while you're not typing.
    if (cu.wpm_on && (get_current_wpm() > 0 || !pomo_shown)) {
        wpm_draw();
    } else {
        pomo_draw();
    }
    flash_draw();
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
        case id_cu_bar_speed:     rgb_matrix_set_speed(BAR_SPEED_MIN + (MAX(1, MIN(v, 5)) - 1) * RGB_MATRIX_SPD_STEP); return true;
        default: return false;
    }
    settings_save();
    return true;
}
