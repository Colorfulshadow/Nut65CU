// Copyright 2026 Colorfulshadow (@Colorfulshadow)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Lights the computer controls, see cu_overlay.h. Bar layers are drawn at the bar
// brightness (hs_strip_val()), like everything else on the bar.

#include QMK_KEYBOARD_H
#include "lib/lib8tion/lib8tion.h"
#include "cu_overlay.h"

extern uint8_t hs_strip_val(void); // nut65cu.c

#define BAR_N RGBLED_NUM

typedef struct {
    cu_bar_layer_t cfg;
    uint32_t       start, expire; // the animation runs from start
    bool           active;
} layer_t;

typedef struct {
    cu_host_ind_t cfg;
    uint32_t      start, expire;
    bool          active;
} ind_t;

static layer_t layers[CU_BAR_LAYERS];
static ind_t   inds[CU_HOST_INDICATORS];
#if NUT65_KEY_LEDS
static ind_t keys[NUT65_KEY_LEDS];
#endif

static uint8_t phase8(uint32_t start, uint8_t period) {
    uint32_t ms = period ? period * 20u : 1000;
    return timer_elapsed32(start) % ms * 256 / ms;
}

static uint8_t breathe(uint8_t ph) {
    return 24 + scale8(231, cubicwave8(ph)); // never quite dark
}

bool cu_overlay_bar_set(const cu_bar_layer_t *in) {
    if (in->layer >= CU_BAR_LAYERS || in->fx >= CU_FX_COUNT) return false;
    bool off = in->fx == CU_FX_OFF || in->ttl == 0;
    if (!off && (in->start >= in->end || in->end > BAR_N || in->dir > 2)) return false;

    layer_t *l = &layers[in->layer];
    if (off) {
        l->active = false;
        return true;
    }
    // The host renews a layer before it runs out: keep the animation going smoothly.
    if (!l->active || l->cfg.fx != in->fx || l->cfg.period != in->period) l->start = timer_read32();
    l->cfg    = *in;
    l->expire = timer_read32() + in->ttl * 100u;
    l->active = true;
    return true;
}

bool cu_overlay_ind_set(const cu_host_ind_t *in) {
    if (in->index >= CU_HOST_INDICATORS || in->fx > CU_FX_BLINK) return false;

    ind_t *d = &inds[in->index];
    if (in->fx == CU_FX_OFF || in->ttl == 0) {
        d->active = false;
        return true;
    }
    if (!d->active || d->cfg.fx != in->fx || d->cfg.period != in->period) d->start = timer_read32();
    d->cfg    = *in;
    d->expire = timer_read32() + in->ttl * 100u;
    d->active = true;
    return true;
}

void cu_overlay_clear(void) {
    for (uint8_t i = 0; i < CU_BAR_LAYERS; i++) layers[i].active = false;
    for (uint8_t i = 0; i < CU_HOST_INDICATORS; i++) inds[i].active = false;
#if NUT65_KEY_LEDS
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) keys[i].active = false;
#endif
}

#if NUT65_KEY_LEDS
bool cu_overlay_key_set(const cu_host_ind_t *in) {
    if (in->index >= NUT65_KEY_LEDS || in->fx > CU_FX_BLINK) return false;

    ind_t *d = &keys[in->index];
    if (in->fx == CU_FX_OFF || in->ttl == 0) {
        d->active = false;
        return true;
    }
    if (!d->active || d->cfg.fx != in->fx || d->cfg.period != in->period) d->start = timer_read32();
    d->cfg    = *in;
    d->expire = timer_read32() + in->ttl * 100u;
    d->active = true;
    return true;
}
#endif

// How much of the foreground the LED at pos (0..len-1, along the layer's direction) shows.
static uint8_t fx_level(const cu_bar_layer_t *c, uint8_t ph, uint8_t pos, uint8_t len) {
    switch (c->fx) {
        case CU_FX_SOLID:
            return 255;
        case CU_FX_BREATHE:
            return breathe(ph);
        case CU_FX_BLINK:
            return ph < 128 ? 255 : 0;
        case CU_FX_FLOW: {
            // The head runs to the end and comes round again; the tail fades over a quarter.
            uint8_t tail = MAX(3, len / 4);
            uint8_t head = (uint16_t)ph * len / 256;
            uint8_t d    = head >= pos ? head - pos : head + len - pos;
            return d < tail ? 255 - (uint16_t)d * 255 / tail : 0;
        }
        case CU_FX_SCAN: {
            uint8_t head = (uint16_t)triwave8(ph) * (len - 1) / 254;
            uint8_t d    = head > pos ? head - pos : pos - head;
            return d < 3 ? 255 - d * 100 : 0;
        }
        case CU_FX_PROGRESS: {
            uint32_t fill = (uint32_t)c->value * len * 256 / 255;
            uint8_t  full = fill >> 8;
            return pos < full ? 255 : pos == full ? (fill & 0xFF) : 0;
        }
        default:
            return 0;
    }
}

void cu_overlay_draw_bar(void) {
    uint8_t v = hs_strip_val();

    for (uint8_t i = 0; i < CU_BAR_LAYERS; i++) {
        if (!layers[i].active) continue;
        if (timer_expired32(timer_read32(), layers[i].expire)) {
            layers[i].active = false;
            continue;
        }

        const cu_bar_layer_t *c    = &layers[i].cfg;
        uint8_t               fg   = scale8(v, c->val);
        RGB                   bg   = hsv_to_rgb((HSV){.h = c->bg_hue, .s = c->bg_sat, .v = scale8(v, c->bg_val)});
        uint8_t               n    = c->end - c->start;
        uint8_t               half = n / 2;
        uint8_t               len  = c->dir == 2 ? n - half : n;
        uint8_t               ph   = phase8(layers[i].start, c->period);

        for (uint8_t j = 0; j < n; j++) {
            uint8_t pos = c->dir == 1 ? n - 1 - j : c->dir == 2 ? (j < half ? half - 1 - j : j - half) : j;
            uint8_t k   = fx_level(c, ph, pos, len);
            RGB     rgb = k ? hsv_to_rgb((HSV){.h = c->hue, .s = c->sat, .v = scale8(fg, k)}) : bg;
            rgb_matrix_set_color(HS_RGB_STRIP_START + c->start + j, rgb.r, rgb.g, rgb.b);
        }
    }
}

void cu_overlay_draw_indicators(void) {
    for (uint8_t i = 0; i < CU_HOST_INDICATORS; i++) {
        if (!inds[i].active) continue;
        if (timer_expired32(timer_read32(), inds[i].expire)) {
            inds[i].active = false;
            continue;
        }

        const cu_host_ind_t *c  = &inds[i].cfg;
        uint8_t              ph = phase8(inds[i].start, c->period);
        uint8_t              k  = c->fx == CU_FX_BREATHE ? breathe(ph) : c->fx == CU_FX_BLINK && ph >= 128 ? 0 : 255;
        RGB                  rgb = hsv_to_rgb((HSV){.h = c->hue, .s = c->sat, .v = scale8(c->val, k)});
        // LEDs 0-4 are T R E W Q (config.h), indices here are Q W E R T.
        rgb_matrix_set_color(CU_HOST_INDICATORS - 1 - i, rgb.r, rgb.g, rgb.b);
    }
}

#if NUT65_KEY_LEDS
void cu_overlay_draw_keys(void) {
    for (uint8_t i = 0; i < NUT65_KEY_LEDS; i++) {
        if (!keys[i].active) continue;
        if (timer_expired32(timer_read32(), keys[i].expire)) {
            keys[i].active = false;
            continue;
        }
        const cu_host_ind_t *c  = &keys[i].cfg;
        uint8_t              ph = phase8(keys[i].start, c->period);
        uint8_t              k  = c->fx == CU_FX_BREATHE ? breathe(ph) : c->fx == CU_FX_BLINK && ph >= 128 ? 0 : 255;
        RGB                  rgb = hsv_to_rgb((HSV){.h = c->hue, .s = c->sat, .v = scale8(c->val, k)});
        rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
    }
}
#endif
