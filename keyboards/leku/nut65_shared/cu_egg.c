// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Easter eggs, see cu_egg.h. A typed sequence counts when its keys are pressed one after
// another, at most EGG_GAP_MS apart; modifiers in between (Shift for a capital) are
// ignored, any other key starts over. A new show replaces the one playing.
//
// Each show draws the bar (both models) and either the key LEDs (per-key models) or the
// NUT65 CU's eight indicators (under Q W E R T, Caps, LCtrl, Win):
//
// Konami code, 4.4 s, a nod to the NES games the code comes from:
//   0.0-1.0 s  replay: the ten inputs light up one by one, directions white, B and A red
//              (the controller's buttons). Bar: ten 8-LED blocks; CU: Q-T fill up like
//              Gradius' power meter; per-key: the keys themselves.
//   1.0-1.6 s  power-up: three white flashes.
//   1.6-3.0 s  Contra's spread gun: volleys from the middle of the bar, three speeds each
//              (the fan, in one dimension); CU: the meter's cursor runs; per-key: rings from A.
//   3.0-4.4 s  a blocky 8-colour rainbow scrolls right and fades out.
// Hearts (zhangtianyi: ice blue, qiuzi: bright red), 8 s:
//   0.0-1.0 s  the heart grows from K (per-key); the bar fills from its middle.
//   1.0-6.8 s  a heartbeat once a second (a strong beat, a weaker one 220 ms later): the
//              heart pulses, each beat sends a wave from the bar's middle to its ends, the
//              CU's indicators pulse. Other keys: falling snow (ice) or sparkles (red).
//   6.8-8.0 s  fade out. (The CU has no per-key LEDs: no heart shape there.)
// matrix, 6 s: digital rain. Bar and indicators: each LED is a column seen end-on, a drop
//   flashes white-green as it passes and leaves a fading green trail; per-key: drops fall
//   down the key columns.
// 666, 2.4 s: three gold flashes, each wider (bar: from the middle; per-key: from the 6
//   key), then gold glitter.
// sos, 6 s: ... --- ... in Morse code (200 ms units) in warm white over a dim red beacon;
//   per-key: the S / O key being sent shines brightest.
// Key count milestone, 6.2 s: five fireworks rise from alternating ends and burst in red,
//   gold, green, blue and purple.
// WPM record, 2 s: a gold sweep left to right, two flashes, fade.
// Four pomodoros, 4.6 s: four tomatoes (red, green calyx in the middle) slide in from the
//   ends and pile up in the middle, pulse twice and fade; CU: Q W E R turn red as they land,
//   T green; per-key: the rows fill red from the bottom, the top row green.
//
// The bar stays at the bar brightness (hs_strip_val(), at most 75: the boost converter
// whines above that), raised to BAR_MIN_VAL so a show is seen at the lowest level.

#include QMK_KEYBOARD_H
#include <string.h>
#include "lib/lib8tion/lib8tion.h"
#include "cu_egg.h"
#include "cu_timer.h"
#include "cu_keys.h"

extern uint8_t hs_strip_val(void);

#define BAR_N       RGBLED_NUM
#define BAR_MID     (BAR_N / 2) // LEDs BAR_MID - 1 and BAR_MID are the middle
#define EGG_HIST    11          // the longest sequence
#define EGG_GAP_MS  2000        // a longer pause between two keys starts over
#define BAR_MIN_VAL 40
#define KEY_MIN_VAL 64
#define KEY_MAX_VAL 160

static const uint16_t seq_konami[] = {KC_UP, KC_UP, KC_DOWN, KC_DOWN, KC_LEFT, KC_RIGHT, KC_LEFT, KC_RIGHT, KC_B, KC_A};
static const uint16_t seq_ice[]    = {KC_Z, KC_H, KC_A, KC_N, KC_G, KC_T, KC_I, KC_A, KC_N, KC_Y, KC_I};
static const uint16_t seq_red[]    = {KC_Q, KC_I, KC_U, KC_Z, KC_I};
static const uint16_t seq_matrix[] = {KC_M, KC_A, KC_T, KC_R, KC_I, KC_X};
static const uint16_t seq_666[]    = {KC_6, KC_6, KC_6};
static const uint16_t seq_sos[]    = {KC_S, KC_O, KC_S};

static const struct {
    const uint16_t *seq;
    uint8_t         len;
    cu_egg_t        egg;
} typed[] = {
    {seq_konami, ARRAY_SIZE(seq_konami), CU_EGG_KONAMI}, {seq_ice, ARRAY_SIZE(seq_ice), CU_EGG_ICE},
    {seq_red, ARRAY_SIZE(seq_red), CU_EGG_RED},          {seq_matrix, ARRAY_SIZE(seq_matrix), CU_EGG_MATRIX},
    {seq_666, ARRAY_SIZE(seq_666), CU_EGG_666},          {seq_sos, ARRAY_SIZE(seq_sos), CU_EGG_SOS},
};

#define KONAMI_LEN ARRAY_SIZE(seq_konami)

static uint16_t hist[EGG_HIST]; // the last key presses, right-aligned: the newest is hist[EGG_HIST - 1]
static uint8_t  hist_n;
static uint32_t last_press;
static cu_egg_t playing;
static uint32_t start; // when the show playing started
#if NUT65_KEY_LEDS
static uint8_t hist_led[EGG_HIST];    // LED under each of those keys (NO_LED if none)
static uint8_t seq_led[EGG_HIST];     // the keys of the sequence that started the show, oldest first
static uint8_t heart[NUT65_KEY_LEDS]; // hearts: 0 outside, 1 edge, 2 inside
static uint8_t from_k[NUT65_KEY_LEDS]; // distance from K, LED units
static void    heart_map(void);
#endif

static uint32_t show_len(cu_egg_t e) {
    switch (e) {
        case CU_EGG_KONAMI: return 4400;
        case CU_EGG_ICE:
        case CU_EGG_RED: return 8000;
        case CU_EGG_MATRIX: return 6000;
        case CU_EGG_666: return 2400;
        case CU_EGG_SOS: return 6000;
        case CU_EGG_MILESTONE: return 6200;
        case CU_EGG_WPM: return 2000;
        case CU_EGG_POMO: return 4600;
        default: return 0;
    }
}

void cu_egg_play(cu_egg_t egg) {
    playing = egg;
    start   = cu_timer_start();
    hist_n  = 0;
#if NUT65_KEY_LEDS
    if (egg == CU_EGG_ICE || egg == CU_EGG_RED) heart_map();
#endif
}

void cu_egg_record(uint16_t keycode, keyrecord_t *record) {
    if (!IS_KEYEVENT(record->event) || !record->event.pressed || IS_MODIFIER_KEYCODE(keycode)) return;
    if (hist_n && timer_elapsed32(last_press) > EGG_GAP_MS) hist_n = 0;
    last_press = timer_read32();
    memmove(hist, hist + 1, sizeof(hist[0]) * (EGG_HIST - 1));
    hist[EGG_HIST - 1] = keycode;
#if NUT65_KEY_LEDS
    memmove(hist_led, hist_led + 1, EGG_HIST - 1);
    hist_led[EGG_HIST - 1] = g_led_config.matrix_co[record->event.key.row][record->event.key.col];
#endif
    if (hist_n < EGG_HIST) hist_n++;
#ifdef CU_EGG_TEST
    // Test builds (keymap eggtest): e g g and a digit plays show 1-9, in cu_egg_t order.
    if (hist_n >= 4 && hist[EGG_HIST - 4] == KC_E && hist[EGG_HIST - 3] == KC_G && hist[EGG_HIST - 2] == KC_G &&
        keycode >= KC_1 && keycode <= KC_9) {
#    if NUT65_KEY_LEDS
        memcpy(seq_led, hist_led + EGG_HIST - KONAMI_LEN, KONAMI_LEN);
#    endif
        cu_egg_play((cu_egg_t)(keycode - KC_1 + 1));
        return;
    }
#endif
    for (uint8_t e = 0; e < ARRAY_SIZE(typed); e++) {
        uint8_t n = typed[e].len;
        if (hist_n >= n && !memcmp(hist + EGG_HIST - n, typed[e].seq, n * sizeof(uint16_t))) {
#if NUT65_KEY_LEDS
            memcpy(seq_led, hist_led + EGG_HIST - n, n);
#endif
            cu_egg_play(typed[e].egg);
            return;
        }
    }
}

// Lifetime key count: 500,000, then every million (counted from what it was at start, so
// nothing plays at power-on; clearing the counts starts over).
static uint32_t next_milestone(uint32_t n) {
    return n < 500000 ? 500000 : (n / 1000000 + 1) * 1000000;
}

void cu_egg_task(void) {
    static uint32_t last_check, seen;
    static bool     started;
    if (timer_read32() < 3000 || timer_elapsed32(last_check) < 200) return; // the counts are loaded by then
    last_check     = timer_read32();
    uint32_t total = cu_keys_total();
    if (started && total >= seen && total >= next_milestone(seen)) cu_egg_play(CU_EGG_MILESTONE);
    started = true;
    seen    = total;
}

/* ---------- shared ---------- */

static RGB hsv(uint8_t h, uint8_t s, uint8_t v) {
    return hsv_to_rgb((HSV){.h = h, .s = s, .v = v});
}

static void bar_set(uint8_t i, RGB c) {
    rgb_matrix_set_color(HS_RGB_STRIP_START + i, c.r, c.g, c.b);
}

static uint8_t from_mid(uint8_t i) { // 0 for the two middle LEDs
    return i < BAR_MID ? BAR_MID - 1 - i : i - BAR_MID;
}

static uint8_t hash8(uint8_t x) { // a fixed pseudo-random phase per LED / column
    return (uint8_t)(x * 157u + 89u) ^ (uint8_t)(x * 29u);
}

// v, fading to 0 over the last `len` ms before `end`.
static uint8_t tail(uint32_t t, uint32_t end, uint32_t len, uint8_t v) {
    return t + len <= end ? v : t >= end ? 0 : v * (end - t) / len;
}

// Glitter: on now and then for each `n`, 0..255.
static uint8_t glitter(uint8_t n, uint32_t t) {
    uint8_t w = (uint8_t)(t / 5 + hash8(n));
    return w > 215 ? (uint8_t)(255 - w) * 255 / 40 : 0;
}

static const uint8_t rainbow[8] = {0, 16, 40, 85, 128, 160, 192, 224};

#define HUE_GOLD 32

#if NUT65_KEY_LEDS
static void key_set(uint8_t i, RGB c) {
    rgb_matrix_set_color(i, c.r, c.g, c.b);
}

static uint8_t key_dist(uint8_t i, int16_t x, int16_t y) {
    int16_t dx = g_led_config.point[i].x - x, dy = g_led_config.point[i].y - y;
    return sqrt16(dx * dx + dy * dy);
}

// Position of an LED of the sequence (the board's middle if it has none).
static int16_t seq_x(uint8_t k) {
    return seq_led[k] < NUT65_KEY_LEDS ? g_led_config.point[seq_led[k]].x : 112;
}
static int16_t seq_y(uint8_t k) {
    return seq_led[k] < NUT65_KEY_LEDS ? g_led_config.point[seq_led[k]].y : 32;
}
#else
static void ind_set(uint8_t i, RGB c) { // 0-4: T R E W Q, 5-7: Caps LCtrl Win
    rgb_matrix_set_color(i, c.r, c.g, c.b);
}

static void ind_fill(RGB c) {
    for (uint8_t i = 0; i < HS_RGB_INDICATOR_COUNT; i++) ind_set(i, c);
}
#endif

/* ---------- Konami code ---------- */

#define K_STEP_MS   100 // replay: one input per step; flashes: on / off this long
#define K_FLASH     1000
#define K_SPREAD    1600
#define K_RAINBOW   3000
#define K_END       4400
#define K_VOLLEY_MS 350
#define K_VOLLEYS   4

static RGB input_rgb(uint8_t k, uint8_t v) { // directions white, B and A red
    return k >= 8 ? hsv(0, 255, v) : hsv(0, 0, v);
}

static bool k_flash_on(uint32_t t) {
    return (t - K_FLASH) / K_STEP_MS % 2 == 0;
}

// Spread gun on the bar: 2 = bullet, 1 = its trail (toward the middle), 0 = dark.
static uint8_t spread_level(uint8_t i, uint32_t ts) {
    static const uint8_t speed[3] = {4, 3, 2}; // LEDs per 40 ms
    uint8_t              lv       = 0;
    for (uint8_t j = 0; j < K_VOLLEYS && ts >= j * K_VOLLEY_MS; j++) {
        uint32_t dt = ts - j * K_VOLLEY_MS;
        for (uint8_t s = 0; s < 3; s++) {
            uint32_t d = dt * speed[s] / 40;
            if (d >= BAR_MID) continue;
            uint8_t l = BAR_MID - 1 - d, r = BAR_MID + d;
            if (i == l || i == r) return 2;
            if (d && (i == l + 1 || i == r - 1)) lv = 1;
        }
    }
    return lv;
}

static void konami_bar(uint32_t t, uint8_t v) {
    for (uint8_t i = 0; i < BAR_N; i++) {
        RGB c = {0};
        if (t < K_FLASH) {
            uint8_t k = i * KONAMI_LEN / BAR_N;
            if (t >= k * K_STEP_MS && i % (BAR_N / KONAMI_LEN) != BAR_N / KONAMI_LEN - 1) c = input_rgb(k, v); // a gap after each block
        } else if (t < K_SPREAD) {
            if (k_flash_on(t)) c = hsv(0, 0, v);
        } else if (t < K_RAINBOW) {
            uint8_t lv = spread_level(i, t - K_SPREAD);
            if (lv == 2) c = hsv(16, 150, v);
            else if (lv == 1) c = hsv(4, 255, v / 3);
        } else {
            int16_t p = i - (int16_t)((t - K_RAINBOW) / 40) % BAR_N; // 25 LEDs/s to the right
            if (p < 0) p += BAR_N;
            c = hsv(rainbow[p / (BAR_N / 8) % 8], 255, tail(t, K_END, 400, v));
        }
        bar_set(i, c);
    }
}

#if NUT65_KEY_LEDS
static void konami_keys(uint32_t t, uint8_t v) {
    int16_t cx = seq_x(KONAMI_LEN - 1), cy = seq_y(KONAMI_LEN - 1); // A
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        RGB c = {0};
        if (t < K_FLASH) {
            for (uint8_t k = 0; k < KONAMI_LEN; k++) { // each input glows and fades over 300 ms; a later one wins
                if (seq_led[k] == i && t >= k * K_STEP_MS && t < k * K_STEP_MS + 300)
                    c = input_rgb(k, v * (300 - (t - k * K_STEP_MS)) / 300);
            }
        } else if (t < K_SPREAD) {
            if (k_flash_on(t)) c = hsv(0, 0, v);
        } else if (t < K_RAINBOW) {
            uint16_t dist = key_dist(i, cx, cy);
            for (uint8_t j = 0; j < K_VOLLEYS && t - K_SPREAD >= j * K_VOLLEY_MS; j++) {
                uint16_t r = (t - K_SPREAD - j * K_VOLLEY_MS) * 224 / 700; // across the board in 0.7 s
                if (dist + 10 > r && dist < r + 10) c = hsv(dist + 4 > r ? 16 : 4, dist + 4 > r ? 150 : 255, v);
            }
        } else {
            int16_t p = g_led_config.point[i].x - (int16_t)((t - K_RAINBOW) / 8) % 224;
            if (p < 0) p += 224;
            c = hsv(rainbow[p / 28 % 8], 255, tail(t, K_END, 400, v));
        }
        key_set(i, c);
    }
}
#else
static void konami_ind(uint32_t t, uint8_t v) {
    for (uint8_t n = 0; n < 5; n++) { // n = 0 is Q
        RGB c = {0};
        if (t < K_FLASH) {
            if (t >= (2u * n + 1) * K_STEP_MS) c = hsv(20, 255, v); // one slot per two inputs
        } else if (t < K_SPREAD) {
            if (k_flash_on(t)) c = hsv(0, 0, v);
        } else if (t < K_RAINBOW) {
            c = (t - K_SPREAD) / 140 % 5 == n ? hsv(40, 120, v) : hsv(20, 255, v / 3);
        } else {
            c = hsv(rainbow[(n + (t - K_RAINBOW) / 175) % 8], 255, tail(t, K_END, 400, v));
        }
        ind_set(4 - n, c);
    }
    RGB w = t >= K_FLASH && t < K_SPREAD && k_flash_on(t) ? hsv(0, 0, v) : (RGB){0};
    for (uint8_t i = 5; i < HS_RGB_INDICATOR_COUNT; i++) ind_set(i, w);
}
#endif

/* ---------- hearts ---------- */

#define H_GROW    1000
#define H_FADE    6800
#define H_END     8000
#define H_BEAT_MS 1000
#define H_DUB_MS  220 // the weaker second beat
#define H_WAVE_MS 500 // a beat's wave reaches the bar's ends in this long
#define H_WAVE_W  6   // ... and is this many LEDs wide on each side

typedef struct {
    uint8_t core_h, core_s; // inside the heart, the bar, the indicators
    uint8_t edge_h, edge_s; // the heart's outline: frost / pink highlight
    uint8_t bg_h, bg_s;     // snow / sparkles on the other keys
} theme_t;

static const theme_t ice = {140, 210, 135, 80, 150, 40};
static const theme_t red = {0, 255, 250, 140, 245, 170};

// Strength of a beat this long ago, 0..255: on at once, gone after 400 ms.
static uint8_t beat_env(int32_t age) {
    if (age < 0 || age >= 400) return 0;
    return age < 80 ? 255 : 255 * (400 - age) / 320;
}

// The heartbeat now, 0..255 (strong beat, then a weaker one).
static uint8_t heartbeat(uint32_t t) {
    if (t < H_GROW || t >= H_FADE) return 0;
    int32_t a   = (t - H_GROW) % H_BEAT_MS;
    uint8_t lub = beat_env(a), dub = scale8(beat_env(a - H_DUB_MS), 150);
    return MAX(lub, dub);
}

static void heart_bar(uint32_t t, uint8_t v, const theme_t *th) {
    uint8_t base = v / 4;
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t d   = from_mid(i);
        uint8_t val = 0;
        if (t < H_GROW) {
            if (d <= t * BAR_MID / H_GROW) val = d + 3 > t * BAR_MID / H_GROW ? v : base; // a bright front
        } else {
            uint16_t w = 0;
            uint32_t a = (t - H_GROW) % H_BEAT_MS;
            for (uint8_t b = 0; b < 2 && t < H_FADE; b++) { // the two beats' waves
                if (b && a < H_DUB_MS) continue;
                uint32_t ab = b ? a - H_DUB_MS : a;
                if (ab >= H_WAVE_MS) continue;
                uint8_t front = ab * BAR_MID / H_WAVE_MS;
                uint8_t off   = d > front ? d - front : front - d;
                if (off < H_WAVE_W) {
                    uint16_t s = (uint16_t)(H_WAVE_W - off) * 255 / H_WAVE_W * (H_WAVE_MS - ab) / H_WAVE_MS;
                    w          = MAX(w, b ? s * 150 / 255 : s);
                }
            }
            val = base + (v - base) * w / 255;
        }
        bar_set(i, hsv(th->core_h, th->core_s, tail(t, H_END, H_END - H_FADE, val)));
    }
}

#if NUT65_KEY_LEDS
// Which keys are in the heart, and how far each key is from K (once per show).
// The curve (x^2 + y^2 - 1)^3 - x^2 y^3 <= 0, in hundredths; one key is 16 units across,
// a row 12.8 down (keyboard.json): 52 x 24 units per curve unit puts it over rows 0-3,
// from G to the quote key, centred on K (worked out on the real LED map).
#    define K_ROW 2
#    define K_COL 8
static bool in_heart(int32_t x, int32_t y) {
    int64_t a = x * x + y * y - 10000;
    return a * a * a - (int64_t)x * x * y * y * y * 100 <= 0;
}

static void heart_map(void) {
    uint8_t k  = g_led_config.matrix_co[K_ROW][K_COL];
    int16_t kx = k < NUT65_KEY_LEDS ? g_led_config.point[k].x : 128;
    int16_t ky = k < NUT65_KEY_LEDS ? g_led_config.point[k].y : 26;
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        int16_t dx = g_led_config.point[i].x - kx, dy = g_led_config.point[i].y - ky;
        int32_t x = dx * 100 / 52, y = -dy * 100 / 24 + 5;
        heart[i]  = !in_heart(x, y) ? 0 : in_heart(x * 100 / 72, y * 100 / 72) ? 2 : 1;
        from_k[i] = key_dist(i, kx, ky);
    }
}

static void heart_keys(uint32_t t, uint8_t v, const theme_t *th, bool snow) {
    uint8_t grow = t < H_GROW ? t * 80 / H_GROW : 255;        // the heart's radius from K while it grows
    uint8_t lv   = 165 + scale8(90, heartbeat(t));             // the heart pulses between 65 % and 100 %
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        RGB c = {0};
        if (heart[i] && from_k[i] <= grow) {
            uint8_t val = tail(t, H_END, H_END - H_FADE, scale8(v, lv));
            c           = heart[i] == 2 ? hsv(th->core_h, th->core_s, val) : hsv(th->edge_h, th->edge_s, val);
        } else if (!heart[i] && t >= H_GROW) {
            uint8_t bg = 0, x = g_led_config.point[i].x, y = g_led_config.point[i].y;
            if (snow) { // a flake falls down each 16-unit column (about 1.3 s top to bottom)
                int16_t flake = (int16_t)(((t - H_GROW) / 20 + hash8(x / 16)) % 90) - 13;
                int16_t off   = y - flake;
                if (off < 0) off = -off;
                if (off < 7) bg = (7 - off) * 255 / 7;
            } else {
                bg = glitter(i, t);
            }
            c = hsv(th->bg_h, th->bg_s, tail(t, H_END, H_END - H_FADE, scale8(v / 3, bg)));
        }
        key_set(i, c);
    }
}
#else
static void heart_ind(uint32_t t, uint8_t v, const theme_t *th) {
    uint8_t val = t < H_GROW ? v * t / H_GROW / 2 : v / 2 + scale8(v - v / 2, heartbeat(t));
    ind_fill(hsv(th->core_h, th->core_s, tail(t, H_END, H_END - H_FADE, val)));
}
#endif

/* ---------- matrix ---------- */

#define M_END 6000

static uint8_t m_level(uint32_t t, uint8_t v) {
    return t < 400 ? v * t / 400 : tail(t, M_END, 1000, v);
}

// One raindrop passing LED / column `n` seen end-on: 0..255, *head while it is the bright head.
static uint8_t rain(uint8_t n, uint32_t t, bool *head) {
    uint16_t period = 600 + hash8(n) * 6;
    uint16_t age    = (t + hash8(n ^ 0x5A) * 8u) % period;
    *head           = age < 60;
    if (age < 60) return 255;
    return age < 510 ? (510 - age) * 255 / 450 : 0;
}

static void matrix_bar(uint32_t t, uint8_t v) {
    uint8_t lv = m_level(t, v);
    for (uint8_t i = 0; i < BAR_N; i++) {
        bool    head;
        uint8_t r = rain(i, t, &head);
        bar_set(i, head ? hsv(85, 110, lv) : hsv(85, 255, scale8(lv, r)));
    }
}

#if NUT65_KEY_LEDS
static void matrix_keys(uint32_t t, uint8_t v) {
    uint8_t lv = m_level(t, v);
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        uint8_t  col  = g_led_config.point[i].x / 16;
        uint16_t per  = 900 + hash8(col) * 4;
        uint16_t age  = (t + hash8(col ^ 0x33) * 7u) % per;
        int16_t  d    = (int16_t)(age * 90u / per) - 13 - g_led_config.point[i].y; // how far the drop has passed this key
        RGB      c    = {0};
        if (d >= 0 && d < 5) c = hsv(85, 110, lv);                       // the head
        else if (d >= 5 && d < 45) c = hsv(85, 255, lv * (45 - d) / 40); // its trail, about three rows
        key_set(i, c);
    }
}
#else
static void matrix_ind(uint32_t t, uint8_t v) {
    uint8_t lv = m_level(t, v);
    for (uint8_t i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        bool    head;
        uint8_t r = rain(100 + i, t, &head);
        ind_set(i, head ? hsv(85, 110, lv) : hsv(85, 255, scale8(lv, r)));
    }
}
#endif

/* ---------- 666 ---------- */

#define S6_FLASH_MS 400 // a flash every this long, lit for half of it
#define S6_GLITTER  1200
#define S6_END      2400

// Which flash is lit now (0-2), or -1.
static int8_t s6_flash(uint32_t t) {
    if (t >= 3 * S6_FLASH_MS || t % S6_FLASH_MS >= S6_FLASH_MS / 2) return -1;
    return t / S6_FLASH_MS;
}

static void s666_bar(uint32_t t, uint8_t v) {
    int8_t f = s6_flash(t);
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t val = 0;
        if (f >= 0 && from_mid(i) < (f + 1) * BAR_MID / 3) val = v;
        else if (t >= S6_GLITTER) val = tail(t, S6_END, 600, scale8(v, glitter(i, t)));
        bar_set(i, hsv(HUE_GOLD, 210, val));
    }
}

#if NUT65_KEY_LEDS
static void s666_keys(uint32_t t, uint8_t v) {
    static const uint8_t reach[3] = {8, 50, 255}; // the 6 key, around it, everything
    int8_t               f        = s6_flash(t);
    int16_t              x = seq_x(2), y = seq_y(2);
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        uint8_t val = 0;
        if (f >= 0 && key_dist(i, x, y) < reach[f]) val = v;
        else if (t >= S6_GLITTER) val = tail(t, S6_END, 600, scale8(v, glitter(i, t)));
        key_set(i, hsv(HUE_GOLD, 210, val));
    }
}
#else
static void s666_ind(uint32_t t, uint8_t v) {
    int8_t f = s6_flash(t);
    for (uint8_t i = 0; i < HS_RGB_INDICATOR_COUNT; i++) {
        uint8_t val = f >= 0 ? v : t >= S6_GLITTER ? tail(t, S6_END, 600, scale8(v, glitter(100 + i, t))) : 0;
        ind_set(i, hsv(HUE_GOLD, 210, val));
    }
}
#endif

/* ---------- sos ---------- */

#define SOS_UNIT_MS 200
#define SOS_UNITS   27         // ... --- ...: dot 1, dash 3, gaps 1, between letters 3
#define SOS_ON      0x05477715 // bit u: lit in unit u
#define SOS_END     6000

static bool sos_on(uint32_t t) {
    uint32_t u = t / SOS_UNIT_MS;
    return u < SOS_UNITS && (SOS_ON >> u & 1);
}

static uint8_t sos_beacon(uint32_t t, uint8_t v) { // the dim red under it, fading after the last S
    return tail(t, SOS_END, SOS_END - SOS_UNITS * SOS_UNIT_MS, v / 8);
}

static void sos_bar(uint32_t t, uint8_t v) {
    RGB c = sos_on(t) ? hsv(20, 40, v) : hsv(0, 255, sos_beacon(t, v));
    for (uint8_t i = 0; i < BAR_N; i++) bar_set(i, c);
}

#if NUT65_KEY_LEDS
static void sos_keys(uint32_t t, uint8_t v) {
    uint32_t u      = t / SOS_UNIT_MS;
    uint8_t  letter = seq_led[u < 8 ? 0 : u < 22 ? 1 : 2]; // the S or O being sent
    bool     on     = sos_on(t);
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        bool mine = i == letter || i == seq_led[0] || i == seq_led[1];
        RGB  c    = on ? hsv(20, 40, i == letter ? v : v / 4) : hsv(0, 255, mine ? sos_beacon(t, v) * 3 : 0);
        key_set(i, c);
    }
}
#else
static void sos_ind(uint32_t t, uint8_t v) {
    ind_fill(sos_on(t) ? hsv(20, 40, v) : hsv(0, 255, sos_beacon(t, v)));
}
#endif

/* ---------- key count milestone: fireworks ---------- */

#define F_ROCKETS   5
#define F_EVERY_MS  1100
#define F_RISE_MS   500
#define F_BURST_MS  1000
#define F_RADIUS    14 // LEDs on the bar
#define F_END       6200

static const uint8_t f_pos[F_ROCKETS] = {20, 58, 38, 28, 50};
static const uint8_t f_hue[F_ROCKETS] = {0, HUE_GOLD, 85, 160, 200};

static void fireworks_bar(uint32_t t, uint8_t v) {
    for (uint8_t i = 0; i < BAR_N; i++) {
        RGB     c    = {0};
        uint8_t best = 0;
        for (uint8_t k = 0; k < F_ROCKETS && t >= k * F_EVERY_MS; k++) {
            uint32_t a = t - k * F_EVERY_MS;
            if (a < F_RISE_MS) { // the rocket, from the left end for even k, the right end for odd
                int16_t from = k % 2 ? BAR_N - 1 : 0;
                int16_t at   = from + ((int16_t)f_pos[k] - from) * (int32_t)a / F_RISE_MS;
                int16_t back = k % 2 ? at + 1 : at - 1;
                uint8_t val  = i == at ? v : i == back ? v / 3 : 0;
                if (val > best) best = val, c = hsv(30, 120, val);
            } else if (a < F_RISE_MS + F_BURST_MS) { // the burst: sparks spread, twinkle and fade
                uint32_t b = a - F_RISE_MS;
                uint8_t  r = b < 400 ? b * F_RADIUS / 400 : F_RADIUS;
                uint8_t  d = i > f_pos[k] ? i - f_pos[k] : f_pos[k] - i;
                if (d > r || ((hash8(i + k * 31) + b / 40) & 3) == 0) continue;
                uint8_t fade = b < 400 ? 255 : (F_BURST_MS - b) * 255 / (F_BURST_MS - 400);
                uint8_t val  = scale8(scale8(v, fade), 255 - d * 120 / (F_RADIUS + 1));
                if (val > best) best = val, c = hsv(f_hue[k], 230, val);
            }
        }
        bar_set(i, c);
    }
}

#if NUT65_KEY_LEDS
static void fireworks_keys(uint32_t t, uint8_t v) {
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        RGB     c    = {0};
        uint8_t best = 0;
        for (uint8_t k = 0; k < F_ROCKETS && t >= k * F_EVERY_MS; k++) {
            uint32_t a  = t - k * F_EVERY_MS;
            int16_t  px = f_pos[k] * 224 / BAR_N, py = 13; // bursts over the number / top letter rows
            if (a < F_RISE_MS) {                           // rising from the bottom row
                int16_t y  = 51 - (51 - py) * (int32_t)a / F_RISE_MS;
                int16_t dx = g_led_config.point[i].x - px, dy = g_led_config.point[i].y - y;
                if (dx > -10 && dx < 10 && dy > -7 && dy < 7 && v > best) best = v, c = hsv(30, 120, v);
            } else if (a < F_RISE_MS + F_BURST_MS) {
                uint32_t b = a - F_RISE_MS;
                uint8_t  r = b < 400 ? b * 60 / 400 : 60;
                uint8_t  d = key_dist(i, px, py);
                if (d > r || ((hash8(i + k * 31) + b / 40) & 3) == 0) continue;
                uint8_t fade = b < 400 ? 255 : (F_BURST_MS - b) * 255 / (F_BURST_MS - 400);
                uint8_t val  = scale8(scale8(v, fade), 255 - d * 2);
                if (val > best) best = val, c = hsv(f_hue[k], 230, val);
            }
        }
        key_set(i, c);
    }
}
#else
static void fireworks_ind(uint32_t t, uint8_t v) {
    RGB c = {0};
    for (uint8_t k = 0; k < F_ROCKETS && t >= k * F_EVERY_MS; k++) {
        uint32_t a = t - k * F_EVERY_MS;
        if (a >= F_RISE_MS && a < F_RISE_MS + F_BURST_MS) // each burst lights them in its colour
            c = hsv(f_hue[k], 230, scale8(v, (F_RISE_MS + F_BURST_MS - a) * 255 / F_BURST_MS));
    }
    ind_fill(c);
}
#endif

/* ---------- WPM record ---------- */

#define W_SWEEP_MS 800
#define W_END      2000

// Gold: the sweep's head and what it has passed, then two flashes, then a glow fading out.
static uint8_t wpm_level(uint32_t t, uint16_t pos, uint16_t width, uint8_t v) { // pos, width: same units
    if (t < W_SWEEP_MS) {
        uint16_t head = t * width / W_SWEEP_MS;
        if (pos > head) return 0;
        return head - pos < width / 20 ? v : v / 2;
    }
    if ((t >= 900 && t < 1050) || (t >= 1200 && t < 1350)) return v;
    return t < 1500 ? v / 2 : tail(t, W_END, W_END - 1500, v / 2);
}

static void wpm_bar(uint32_t t, uint8_t v) {
    for (uint8_t i = 0; i < BAR_N; i++) bar_set(i, hsv(HUE_GOLD, 200, wpm_level(t, i, BAR_N, v)));
}

#if NUT65_KEY_LEDS
static void wpm_keys(uint32_t t, uint8_t v) {
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) key_set(i, hsv(HUE_GOLD, 200, wpm_level(t, g_led_config.point[i].x, 224, v)));
}
#else
static void wpm_ind(uint32_t t, uint8_t v) {
    for (uint8_t n = 0; n < 5; n++) ind_set(4 - n, hsv(HUE_GOLD, 200, wpm_level(t, n, 5, v))); // Q to T with the sweep
    for (uint8_t i = 5; i < HS_RGB_INDICATOR_COUNT; i++) ind_set(i, hsv(HUE_GOLD, 200, t < W_SWEEP_MS ? 0 : wpm_level(t, 0, 1, v)));
}
#endif

/* ---------- four pomodoros: tomato harvest ---------- */

#define P_EVERY_MS 600
#define P_SLIDE_MS 500
#define P_LANDED   (3 * P_EVERY_MS + P_SLIDE_MS) // all four are in
#define P_PULSE_MS 500
#define P_FADE     (P_LANDED + 2 * P_PULSE_MS)
#define P_END      4600
#define P_TOMATO   5 // LEDs: red, red, green calyx, red, red

static const int8_t p_slot[4] = {BAR_MID - 5, BAR_MID, BAR_MID - 11, BAR_MID + 6}; // first LED of each, in landing order

// Tomatoes that have landed by now.
static uint8_t p_landed(uint32_t t) {
    uint8_t n = 0;
    while (n < 4 && t >= n * P_EVERY_MS + P_SLIDE_MS) n++;
    return n;
}

static uint8_t p_level(uint32_t t, uint8_t v) { // two gentle pulses once all are in, then a fade
    if (t < P_LANDED) return v;
    if (t < P_FADE) return scale8(v, 165 + scale8(90, cubicwave8((t - P_LANDED) * 256 / P_PULSE_MS)));
    return tail(t, P_END, P_END - P_FADE, v);
}

static void pomo_bar(uint32_t t, uint8_t v) {
    uint8_t lv = p_level(t, v);
    RGB     c[BAR_N];
    memset(c, 0, sizeof(c));
    for (uint8_t j = 0; j < 4 && t >= j * P_EVERY_MS; j++) {
        uint32_t a    = MIN(t - j * P_EVERY_MS, P_SLIDE_MS);
        int16_t  from = j % 2 ? BAR_N : -P_TOMATO; // even ones come from the left
        int16_t  at   = from + (p_slot[j] - from) * (int32_t)a / P_SLIDE_MS;
        for (uint8_t k = 0; k < P_TOMATO; k++) {
            int16_t i = at + k;
            if (i >= 0 && i < BAR_N) c[i] = k == P_TOMATO / 2 ? hsv(85, 255, lv) : hsv(0, 255, lv);
        }
    }
    for (uint8_t i = 0; i < BAR_N; i++) bar_set(i, c[i]);
}

#if NUT65_KEY_LEDS
static void pomo_keys(uint32_t t, uint8_t v) {
    static const uint8_t row_y[5] = {51, 38, 26, 13, 0}; // bottom row first (keyboard.json)
    uint8_t              n        = p_landed(t), lv = p_level(t, v);
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        uint8_t y   = g_led_config.point[i].y;
        RGB     c   = {0};
        for (uint8_t r = 0; r < 5; r++) {
            if (y + 6 < row_y[r] || y > row_y[r] + 6) continue;
            if (r < n) c = hsv(0, 255, lv);                 // a row per tomato
            else if (r == 4 && n == 4) c = hsv(85, 255, lv); // the leaves on top
        }
        key_set(i, c);
    }
}
#else
static void pomo_ind(uint32_t t, uint8_t v) {
    uint8_t n = p_landed(t), lv = p_level(t, v);
    for (uint8_t q = 0; q < 4; q++) ind_set(4 - q, q < n ? hsv(0, 255, lv) : (RGB){0}); // Q W E R
    ind_set(0, n == 4 ? hsv(85, 255, lv) : (RGB){0});                                    // T: the stem
    for (uint8_t i = 5; i < HS_RGB_INDICATOR_COUNT; i++) ind_set(i, (RGB){0});
}
#endif

/* ---------- drawing ---------- */

void cu_egg_draw(void) {
    if (playing == CU_EGG_NONE) return;
    uint32_t t = timer_elapsed32(start);
    if (t >= show_len(playing)) {
        playing = CU_EGG_NONE;
        return;
    }
    uint8_t v = MAX(hs_strip_val(), BAR_MIN_VAL);
#if NUT65_KEY_LEDS
    uint8_t kv = MIN(MAX(rgb_matrix_get_val(), KEY_MIN_VAL), KEY_MAX_VAL);
#    define DRAW(name) (name##_bar(t, v), name##_keys(t, kv))
#else
    uint8_t kv = MIN(2 * v, 255); // the indicators
#    define DRAW(name) (name##_bar(t, v), name##_ind(t, kv))
#endif
    switch (playing) {
        case CU_EGG_KONAMI: DRAW(konami); break;
        case CU_EGG_ICE: heart_bar(t, v, &ice);
#if NUT65_KEY_LEDS
            heart_keys(t, kv, &ice, true);
#else
            heart_ind(t, kv, &ice);
#endif
            break;
        case CU_EGG_RED: heart_bar(t, v, &red);
#if NUT65_KEY_LEDS
            heart_keys(t, kv, &red, false);
#else
            heart_ind(t, kv, &red);
#endif
            break;
        case CU_EGG_MATRIX: DRAW(matrix); break;
        case CU_EGG_666: DRAW(s666); break;
        case CU_EGG_SOS: DRAW(sos); break;
        case CU_EGG_MILESTONE: DRAW(fireworks); break;
        case CU_EGG_WPM: DRAW(wpm); break;
        case CU_EGG_POMO: DRAW(pomo); break;
        default: break;
    }
#undef DRAW
}
