// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Light bar effects 13-24, see cu_barfx.h. Everything runs on elapsed time, not frames:
// the RGB matrix task draws at whatever rate it gets.

#include QMK_KEYBOARD_H
#include <string.h>
#include "lib/lib8tion/lib8tion.h"
#include "cu_barfx.h"
#include "cu_timer.h"

extern uint8_t hs_strip_val(void);

#define BAR_N RGBLED_NUM

enum {
    FX_STARLIGHT = 1,
    FX_RIPPLE,
    FX_AFTERGLOW,
    FX_HEAT,
    FX_STARS,
    FX_METEOR,
    FX_AURORA,
    FX_FIRE,
    FX_OCEAN,
    FX_RAIN,
    FX_SCANNER,
    FX_GAP,
};

_Static_assert(FX_GAP == CU_BARFX_COUNT, "effect list and CU_BARFX_COUNT disagree");

#define SPARKS        24  // key presses / drops alive at once
#define STAR_LIFE     1800
#define RIPPLE_LIFE   900
#define RIPPLE_SPEED  60 // LEDs per second
#define GLOW_LIFE     1400
#define GLOW_HALF     5 // LEDs on each side of the key
#define RAIN_LIFE     900
#define RAIN_EVERY    90 // ms between drops at the middle speed
#define STEP_MS       30 // heat / fire update

typedef struct {
    uint32_t t0; // 0: unused
    uint8_t  pos, hue;
} spark_t;

static spark_t  sparks[SPARKS];
static uint8_t  spark_next;
static uint8_t  heat[BAR_N]; // heat (typing heat) or flame level (fire)
static uint8_t  current;     // the effect the state above belongs to
static uint32_t start, last_step, last_drop;
static uint8_t  press_hue;

static void spark_add(uint8_t pos, uint8_t hue) {
    sparks[spark_next] = (spark_t){.t0 = cu_timer_start(), .pos = pos, .hue = hue};
    spark_next         = (spark_next + 1) % SPARKS;
}

// Where a key is across the keyboard, as a bar LED.
static uint8_t key_pos(keypos_t k) {
#if NUT65_KEY_LEDS
    uint8_t led = g_led_config.matrix_co[k.row][k.col];
    if (led < NUT65_KEY_LEDS) return (uint16_t)g_led_config.point[led].x * (BAR_N - 1) / 224;
#endif
    return ((uint16_t)k.col * 2 + 1) * BAR_N / (2 * MATRIX_COLS);
}

static uint8_t clamp_pos(int16_t p) {
    return p < 0 ? 0 : p >= BAR_N ? BAR_N - 1 : p;
}

void cu_barfx_key(uint8_t fx, keyrecord_t *record) {
    if (!fx || fx != current || !IS_KEYEVENT(record->event) || !record->event.pressed) return;
    uint8_t pos = key_pos(record->event.key);
    switch (fx) {
        case FX_STARLIGHT: { // mostly warm white, sometimes pale blue or gold
            uint8_t r = random8();
            spark_add(clamp_pos(pos + (int16_t)random8_max(5) - 2), r < 170 ? 0 : r < 215 ? 1 : 2);
            break;
        }
        case FX_RIPPLE:
            press_hue += 23;
            spark_add(pos, press_hue);
            break;
        case FX_AFTERGLOW:
            spark_add(pos, pos * 255 / BAR_N);
            break;
        case FX_HEAT:
            for (int8_t d = -2; d <= 2; d++) {
                int16_t i = pos + d;
                if (i >= 0 && i < BAR_N) heat[i] = qadd8(heat[i], d == 0 ? 150 : d == 1 || d == -1 ? 90 : 40);
            }
            break;
        default: break;
    }
}

static RGB hsv(uint8_t h, uint8_t s, uint8_t v) {
    return hsv_to_rgb((HSV){.h = h, .s = s, .v = v});
}

static void set(uint8_t i, RGB c) {
    rgb_matrix_set_color(HS_RGB_STRIP_START + i, c.r, c.g, c.b);
}

static uint8_t hash8(uint8_t x) {
    return (uint8_t)(x * 157u + 89u) ^ (uint8_t)(x * 29u);
}

static uint8_t dist(uint8_t a, uint8_t b) {
    return a > b ? a - b : b - a;
}

// 0..255 rising quickly, then falling to 0 at `life` (squared: a slow tail).
static uint8_t envelope(uint32_t age, uint32_t life, uint32_t rise) {
    if (age >= life) return 0;
    if (age < rise) return age * 255 / rise;
    uint8_t e = (life - age) * 255 / (life - rise);
    return scale8(e, e);
}

/* ---------- key driven ---------- */

static void draw_starlight(uint8_t v) {
    static const uint8_t hue[3] = {28, 150, 36}, sat[3] = {60, 100, 170};
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t best = 0, kind = 0;
        for (uint8_t s = 0; s < SPARKS; s++) {
            if (!sparks[s].t0) continue;
            uint32_t a = timer_elapsed32(sparks[s].t0);
            uint8_t  d = dist(i, sparks[s].pos);
            if (a >= STAR_LIFE || d > 1) continue;
            uint8_t tw  = 170 + scale8(85, sin8(a / 5 + s * 40)); // the twinkle
            uint8_t val = scale8(scale8(v, envelope(a, STAR_LIFE, 80)), tw);
            if (d) val /= 4; // a little glow beside it
            if (val > best) best = val, kind = sparks[s].hue;
        }
        set(i, hsv(hue[kind], sat[kind], best));
    }
}

static void draw_ripple(uint8_t v) {
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t best = 0, hue = 0;
        for (uint8_t s = 0; s < SPARKS; s++) {
            if (!sparks[s].t0) continue;
            uint32_t a = timer_elapsed32(sparks[s].t0);
            if (a >= RIPPLE_LIFE) continue;
            uint8_t r   = a * RIPPLE_SPEED / 1000;
            uint8_t off = dist(dist(i, sparks[s].pos), r);
            if (off >= 3) continue;
            uint8_t val = scale8(v, (3 - off) * 85) * (RIPPLE_LIFE - a) / RIPPLE_LIFE;
            if (val > best) best = val, hue = sparks[s].hue;
        }
        set(i, hsv(hue, 255, best));
    }
}

static void draw_afterglow(uint8_t v) {
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t hue = i * 255 / BAR_N, best = v / 10; // a faint rainbow underneath
        for (uint8_t s = 0; s < SPARKS; s++) {
            if (!sparks[s].t0) continue;
            uint32_t a = timer_elapsed32(sparks[s].t0);
            uint8_t  d = dist(i, sparks[s].pos);
            if (a >= GLOW_LIFE || d > GLOW_HALF) continue;
            uint8_t val = scale8(scale8(v, envelope(a, GLOW_LIFE, 40)), 255 - d * 30);
            if (val > best) best = val;
        }
        set(i, hsv(hue, 255, best));
    }
}

static RGB heat_rgb(uint8_t h, uint8_t v) { // black, red, orange, yellow, white
    if (h < 85) return hsv(0, 255, scale8(v, h * 3));
    if (h < 170) return hsv((h - 85) * 40 / 85, 255, v);
    return hsv(40, 255 - (h - 170) * 2, v);
}

static void draw_heat(uint8_t v) {
    if (timer_elapsed32(last_step) >= STEP_MS) { // spread and cool
        last_step = timer_read32();
        uint8_t prev = heat[0];
        for (uint8_t i = 0; i < BAR_N; i++) {
            uint8_t  next = i + 1 < BAR_N ? heat[i + 1] : heat[i];
            uint16_t sum  = prev + 2 * heat[i] + next;
            prev          = heat[i];
            heat[i]       = qsub8(sum / 4, 2);
        }
    }
    for (uint8_t i = 0; i < BAR_N; i++) set(i, heat_rgb(heat[i], v));
}

/* ---------- ambient (ts: time scaled by the bar speed) ---------- */

static void draw_stars(uint8_t v, uint32_t ts) {
    for (uint8_t i = 0; i < BAR_N; i++) {
        RGB c = hsv(165, 255, v / 6); // night
        if (hash8(i * 7) & 3) {       // three in four LEDs are stars
            uint16_t period = 2000 + hash8(i) * 12;
            uint8_t  w      = (ts + hash8(i ^ 0x3C) * 16u) % period * 256 / period;
            if (w < 40) {
                uint8_t lv = w < 20 ? w * 12 : (40 - w) * 12;
                if (scale8(v, lv) > v / 6) c = hsv(40, 30, scale8(v, lv));
            }
        }
        set(i, c);
    }
}

static void draw_meteor(uint8_t v, uint32_t ts) {
    const uint16_t period = 1800, tail = 22;
    int16_t        head   = (int16_t)(ts % period * (BAR_N + tail) / period) - 2;
    for (uint8_t i = 0; i < BAR_N; i++) {
        int16_t d = head - i; // behind the head
        RGB     c = {0};
        if (d == 0) c = hsv(150, 60, v);
        else if (d > 0 && d < tail) {
            uint8_t k   = (tail - d) * 255 / tail;
            uint8_t val = scale8(v, scale8(k, k));
            if ((hash8(i + ts / 50) & 7) == 0) val /= 2; // it sparkles
            c = hsv(150 + d * 40 / tail, 255, val);
        }
        set(i, c);
    }
}

static void draw_aurora(uint8_t v, uint32_t ts) {
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t a = sin8(i * 4 + ts / 12), b = sin8(i * 9 - ts / 17), c = sin8(i * 2 + ts / 29);
        uint8_t hue = 90 + scale8(a, 100); // green, teal, blue, purple
        uint8_t val = scale8(v, 60 + scale8(195, (b + c) / 2));
        set(i, hsv(hue, 220, val));
    }
}

static void draw_fire(uint8_t v, uint32_t ts) {
    if (timer_elapsed32(last_step) >= STEP_MS) { // each flame flickers toward a new random height
        last_step = timer_read32();
        for (uint8_t i = 0; i < BAR_N; i++) heat[i] = (heat[i] * 3 + random8()) / 4;
    }
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t f = qadd8(scale8(heat[i], 200), sin8(i * 6 + ts / 8) / 4); // plus a slow sway
        set(i, hsv(scale8(f, 32), f > 230 ? 200 : 255, scale8(v, 100 + scale8(155, f))));
    }
}

static void draw_ocean(uint8_t v, uint32_t ts) {
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t s = (sin8(i * 6 - ts / 10) + sin8(i * 11 + ts / 15)) / 2;
        set(i, hsv(140 + scale8(s, 25), s > 225 ? 80 : 255, scale8(v, 70 + scale8(185, s)))); // white crests
    }
}

static void draw_rain(uint8_t v, uint32_t speed) {
    if (timer_elapsed32(last_drop) >= RAIN_EVERY * 128 / speed) {
        last_drop = timer_read32();
        spark_add(random8_max(BAR_N), random8());
    }
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t best = 0, hue = 0;
        for (uint8_t s = 0; s < SPARKS; s++) {
            if (!sparks[s].t0) continue;
            uint32_t a = timer_elapsed32(sparks[s].t0);
            uint8_t  d = dist(i, sparks[s].pos);
            if (a >= RAIN_LIFE || d > 1) continue;
            uint8_t val = scale8(v, envelope(a, RAIN_LIFE, 60));
            if (d) val /= 3;
            if (val > best) best = val, hue = sparks[s].hue;
        }
        set(i, hsv(hue, 255, best));
    }
}

static void draw_scanner(uint8_t v, uint32_t ts) {
    const uint16_t period = 2000, tail = 14;
    uint16_t       ph     = ts % period;
    bool           right  = ph < period / 2;
    uint8_t        x      = (right ? ph : period - ph) * (BAR_N - 1) / (period / 2);
    for (uint8_t i = 0; i < BAR_N; i++) {
        int16_t d   = right ? x - i : i - x; // behind the light
        uint8_t val = 0;
        if (d >= -1 && d <= 0) val = v;
        else if (d > 0 && d < tail) val = scale8(v, (tail - d) * (tail - d) * 255 / (tail * tail));
        set(i, hsv(0, 255, val));
    }
}

static void draw_gap(uint8_t v, uint32_t ts) {
    const uint16_t period = 6000, half = 10;
    int16_t        c      = (int16_t)(ts % period * (BAR_N + 2 * half) / period) - half;
    for (uint8_t i = 0; i < BAR_N; i++) {
        uint8_t hue = 15 + i * 85 / (BAR_N - 1), sat = 150 - i * 60 / (BAR_N - 1); // copper to sea green
        uint8_t val = scale8(v, 140);
        int16_t d   = c > i ? c - i : i - c;
        if (d < half) { // the light passing through
            uint8_t k = (half - d) * 255 / half;
            val       = val + scale8(v - val, k);
            sat       = sat - scale8(sat - 40, k);
        }
        set(i, hsv(hue, sat, val));
    }
}

void cu_barfx_draw(uint8_t fx) {
    if (fx != current) { // a fresh start for each effect
        current = fx;
        memset(sparks, 0, sizeof(sparks));
        memset(heat, 0, sizeof(heat));
        start = cu_timer_start();
    }
    uint8_t  v     = hs_strip_val();
    uint8_t  speed = MAX(rgb_matrix_get_speed(), 55); // 55-215, 128 = normal
    uint32_t ts    = (uint64_t)timer_elapsed32(start) * speed / 128;
    switch (fx) {
        case FX_STARLIGHT: draw_starlight(v); break;
        case FX_RIPPLE: draw_ripple(v); break;
        case FX_AFTERGLOW: draw_afterglow(v); break;
        case FX_HEAT: draw_heat(v); break;
        case FX_STARS: draw_stars(v, ts); break;
        case FX_METEOR: draw_meteor(v, ts); break;
        case FX_AURORA: draw_aurora(v, ts); break;
        case FX_FIRE: draw_fire(v, ts); break;
        case FX_OCEAN: draw_ocean(v, ts); break;
        case FX_RAIN: draw_rain(v, speed); break;
        case FX_SCANNER: draw_scanner(v, ts); break;
        case FX_GAP: draw_gap(v, ts); break;
        default: break;
    }
}
