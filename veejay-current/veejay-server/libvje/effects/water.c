/* EffecTV - Realtime Digital Video Effektor
 * Copyright (C) 2001-2003 FUKUCHI Kentaro
 *
 * RippleTV - Water ripple effect
 * Copyright (C) 2001 - 2002 FUKUCHI Kentaro
 * 
 * ported to Linux VeeJay by:
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include "water.h"

typedef struct {
    uint8_t *src_img;
    uint8_t *diff_img;
    uint8_t *bg_img;
    uint8_t *blur_img;

    int8_t *vtable;

    int *map;
    int *map1;
    int *map2;
    int *map3;

    int map_h;
    int map_w;

    int have_img;
    int sqrtable[256];

    int point;
    int impact;
    int last_fresh_rate;
    int lastmode;
    int loopnum;
    int n_threads;
    int frame_counter;

    float beat_env;
    float beat_kick;

    unsigned int wfastrand_val;

    int rain_period;
    int rain_stat;
    int drop_prob;
    int drop_prob_increment;
    int drops_per_frame_max;
    int drops_per_frame;
    int drop_power;
} water_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int water_wrap_s8(int v)
{
    return (int)(int8_t)((uint8_t)v);
}

static inline unsigned int wfastrand(water_t *w)
{
    w->wfastrand_val = w->wfastrand_val * 1103515245u + 12345u;
    return w->wfastrand_val;
}

static inline int water_scaled_period(int base, int fresh_rate)
{
    int64_t v = ((int64_t)base * (int64_t)fresh_rate + 5) / 10;

    if(v < 1)
        v = 1;

    if(v > 0x3fffffff)
        v = 0x3fffffff;

    return (int)v;
}

static void water_set_table(water_t *w)
{
    for(int i = 0; i < 128; i++) {
        const int sq = i * i;

        w->sqrtable[i] = sq;
        w->sqrtable[255 - i] = -sq;
    }
}

#define WATER_PARAMS 9
#define P_REFRESH_FREQ 0
#define P_WAVESPEED    1
#define P_DECAY        2
#define P_MODE         3
#define P_THRESHOLD    4
#define P_BEAT_DROPS   5
#define P_BEAT_POWER   6
#define P_BEAT_PUSH    7
#define P_BEAT_SMOOTH  8

static inline int water_beat_shape(int beat_push)
{
    beat_push = clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return clampi((beat_push * 32 + sq * 68 + 50) / 100, 0, 1000);
}

static inline void water_update_beat(water_t *w, int beat_push, int smooth, int *beat_q, int *kick_q)
{
    const int shaped = water_beat_shape(beat_push);
    const float target = (float)shaped * 0.001f;
    const float smooth_t = (float)clampi(smooth, 0, 1000) * 0.001f;
    const float attack = 0.20f + (1.0f - smooth_t) * 0.34f;
    const float release = 0.030f + (1.0f - smooth_t) * 0.090f;
    const float prev = w->beat_env;

    if(target > w->beat_env) {
        const float rise = target - w->beat_env;
        w->beat_env += rise * attack;
        w->beat_kick += rise * 0.72f;
    }
    else {
        w->beat_env += (target - w->beat_env) * release;
    }

    w->beat_kick *= 0.58f;

    if(w->beat_env < 0.0001f)
        w->beat_env = 0.0f;
    else if(w->beat_env > 1.0f)
        w->beat_env = 1.0f;

    if(w->beat_kick < 0.0001f)
        w->beat_kick = 0.0f;
    else if(w->beat_kick > 1.0f)
        w->beat_kick = 1.0f;

    (void)prev;

    *beat_q = clampi((int)(w->beat_env * 1000.0f + 0.5f), 0, 1000);
    *kick_q = clampi((int)(w->beat_kick * 1000.0f + 0.5f), 0, 1000);
}

vj_effect *water_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = WATER_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_REFRESH_FREQ] = 1;    ve->limits[1][P_REFRESH_FREQ] = 3600; ve->defaults[P_REFRESH_FREQ] = 10;
    ve->limits[0][P_WAVESPEED]    = 1;    ve->limits[1][P_WAVESPEED]    = 16;   ve->defaults[P_WAVESPEED]    = 1;
    ve->limits[0][P_DECAY]        = 1;    ve->limits[1][P_DECAY]        = 31;   ve->defaults[P_DECAY]        = 10;
    ve->limits[0][P_MODE]         = 0;    ve->limits[1][P_MODE]         = 6;    ve->defaults[P_MODE]         = 0;
    ve->limits[0][P_THRESHOLD]    = 0;    ve->limits[1][P_THRESHOLD]    = 255;  ve->defaults[P_THRESHOLD]    = 45;
    ve->limits[0][P_BEAT_DROPS]   = 0;    ve->limits[1][P_BEAT_DROPS]   = 12;   ve->defaults[P_BEAT_DROPS]   = 3;
    ve->limits[0][P_BEAT_POWER]   = 0;    ve->limits[1][P_BEAT_POWER]   = 1000; ve->defaults[P_BEAT_POWER]   = 650;
    ve->limits[0][P_BEAT_PUSH]    = 0;    ve->limits[1][P_BEAT_PUSH]    = 1000; ve->defaults[P_BEAT_PUSH]    = 0;
    ve->limits[0][P_BEAT_SMOOTH]  = 0;    ve->limits[1][P_BEAT_SMOOTH]  = 1000; ve->defaults[P_BEAT_SMOOTH]  = 520;

    ve->description = "Water ripples";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 1;
    ve->motion = 1;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Refresh Frequency",
        "Wavespeed",
        "Decay",
        "Mode",
        "Threshold (motion)",
        "Beat Drops",
        "Beat Power",
        "Beat Push",
        "Beat Smooth"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][3],
        3,
        "Raindrops",
        "Motion detect I (preview)",
        "Water ripples",
        "Motion detect II (preview)",
        "Drops and Ripples",
        "Motion detect III (preview)",
        "Decaying Motion"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SPEED,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE, 1,                  240,                6,  22, 1800, 4200, 900, 24,    /* Refresh Frequency */
        VJ_BEAT_SPEED,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                             1,                  8,                  6,  22, 1800, 4200, 900, 28,    /* Wavespeed */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                             3,                  24,                 6,  22, 1800, 4200, 900, 24,    /* Decay */

        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                 VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Mode */

        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                              10,                 150,                6,  22, 1600, 3600, 900, 26,    /* Threshold */

        VJ_BEAT_INTENSITY,    VJ_BEAT_F_REJECT,                                                        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Beat Drops */
        VJ_BEAT_INTENSITY,    VJ_BEAT_F_REJECT,                                                        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Beat Power */

        VJ_BEAT_KICK,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,                                0,                  820,                18, 72, 80,   760,  0,   100,   /* Beat Push */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY,                                                   280,                820,                5,  18, 2200, 5200, 1200,18     /* Beat Smooth */
    );

    (void) width;
    (void) height;

    return ve;
}

void *water_malloc(int width, int height)
{
    if(width <= 0 || height <= 0)
        return NULL;

    water_t *w = (water_t*) vj_calloc(sizeof(water_t));
    if(!w)
        return NULL;

    const int len = width * height;

    w->src_img = (uint8_t*) vj_calloc((size_t)len * 4u);
    if(!w->src_img) {
        free(w);
        return NULL;
    }

    w->diff_img = w->src_img + len;
    w->bg_img   = w->diff_img + len;
    w->blur_img = w->bg_img + len;

    w->map_h = height / 2 + 1;
    w->map_w = width / 2 + 1;

    const int map_len = w->map_h * w->map_w;

    w->map = (int*) vj_calloc(sizeof(int) * (size_t)map_len * 3u);
    if(!w->map) {
        free(w->src_img);
        free(w);
        return NULL;
    }

    w->map1 = w->map;
    w->map2 = w->map1 + map_len;
    w->map3 = w->map2 + map_len;

    w->vtable = (int8_t*) vj_calloc(sizeof(int8_t) * (size_t)map_len * 2u);
    if(!w->vtable) {
        free(w->map);
        free(w->src_img);
        free(w);
        return NULL;
    }

    water_set_table(w);

    w->point = 16;
    w->impact = 2;
    w->loopnum = 2;
    w->lastmode = -1;
    w->last_fresh_rate = -1;
    w->wfastrand_val = 0x12345678u;
    w->beat_env = 0.0f;
    w->beat_kick = 0.0f;

    w->n_threads = vje_advise_num_threads(len);

    return (void*) w;
}

void water_free(void *ptr)
{
    water_t *w = (water_t*) ptr;

    if(!w)
        return;

    if(w->vtable)
        free(w->vtable);

    if(w->map)
        free(w->map);

    if(w->src_img)
        free(w->src_img);

    free(w);
}

static void water_clear_maps(water_t *w)
{
    if(!w || !w->map)
        return;

    const int map_len = w->map_w * w->map_h;

    veejay_memset(w->map, 0, sizeof(int) * (size_t)map_len * 3u);
    veejay_memset(w->vtable, 0, sizeof(int8_t) * (size_t)map_len * 2u);
}

static void water_drop(water_t *w, int power)
{
    if(w->map_w <= 4 || w->map_h <= 4)
        return;

    const int x = (int)((wfastrand(w) >> 8) % (unsigned int)(w->map_w - 4)) + 2;
    const int y = (int)((wfastrand(w) >> 8) % (unsigned int)(w->map_h - 4)) + 2;

    int *maps[2] = { w->map1, w->map2 };

    for(int m = 0; m < 2; m++) {
        int *p = maps[m] + y * w->map_w + x;

        *p = power;

        *(p - w->map_w) = power >> 1;
        *(p - 1)        = power >> 1;
        *(p + 1)        = power >> 1;
        *(p + w->map_w) = power >> 1;

        *(p - w->map_w - 1) = power >> 2;
        *(p - w->map_w + 1) = power >> 2;
        *(p + w->map_w - 1) = power >> 2;
        *(p + w->map_w + 1) = power >> 2;
    }
}

static void water_inject_beat_drops(water_t *w, int beat_drops, int beat_power, int beat_q, int kick_q)
{
    if(!w || beat_drops <= 0 || (beat_q <= 0 && kick_q <= 0))
        return;

    beat_drops = clampi(beat_drops, 0, 12);
    beat_power = clampi(beat_power, 0, 1000);
    beat_q = clampi(beat_q, 0, 1000);
    kick_q = clampi(kick_q, 0, 1000);

    int count = ((beat_drops * beat_q) + 700) / 1000;

    if(kick_q > 180)
        count += 1 + ((beat_drops * kick_q) / 1800);

    if(count > beat_drops + 3)
        count = beat_drops + 3;

    if(count <= 0)
        return;

    const int magnitude = 2 + ((beat_power * 14 + 500) / 1000);
    const int kick_boost = (kick_q * 3 + 500) / 1000;
    const int power = -((magnitude + kick_boost) << w->point);

    for(int i = 0; i < count; i++)
        water_drop(w, power);
}

static void water_raindrop(water_t *w, int fresh_rate)
{
    fresh_rate = clampi(fresh_rate, 1, 3600);

    if(w->rain_period == 0) {
        switch(w->rain_stat) {
            case 0:
                w->rain_period = water_scaled_period((int)(wfastrand(w) >> 23) + 100, fresh_rate);
                w->drop_prob = 0;
                w->drop_prob_increment = 0x00ffffff / w->rain_period;
                w->drop_power = (-((int)(wfastrand(w) >> 28)) - 2) << w->point;
                w->drops_per_frame_max = 2 << (wfastrand(w) >> 30);
                w->rain_stat = 1;
                break;

            case 1:
                w->drop_prob = 0x00ffffff;
                w->drops_per_frame = 1;
                w->drop_prob_increment = 1;
                w->rain_period = water_scaled_period((w->drops_per_frame_max - 1) * 16, fresh_rate);
                w->rain_stat = 2;
                break;

            case 2:
                w->rain_period = water_scaled_period((int)(wfastrand(w) >> 22) + 1000, fresh_rate);
                w->drop_prob_increment = 0;
                w->rain_stat = 3;
                break;

            case 3:
                w->rain_period = water_scaled_period((w->drops_per_frame_max - 1) * 16, fresh_rate);
                w->drop_prob_increment = -1;
                w->rain_stat = 4;
                break;

            case 4:
                w->rain_period = water_scaled_period((int)(wfastrand(w) >> 24) + 60, fresh_rate);
                w->drop_prob_increment = -(w->drop_prob / w->rain_period);
                w->rain_stat = 5;
                break;

            case 5:
            default:
                w->rain_period = water_scaled_period((int)(wfastrand(w) >> 23) + 500, fresh_rate);
                w->drop_prob = 0;
                w->drop_prob_increment = 0;
                w->rain_stat = 0;
                break;
        }
    }

    switch(w->rain_stat) {
        case 1:
        case 5:
            if((int)(wfastrand(w) >> 8) < w->drop_prob)
                water_drop(w, w->drop_power);

            w->drop_prob += w->drop_prob_increment;
            w->drop_prob = clampi(w->drop_prob, 0, 0x00ffffff);
            break;

        case 2:
        case 3:
        case 4:
        {
            const int drops = w->drops_per_frame >> 4;

            for(int i = drops; i > 0; i--)
                water_drop(w, w->drop_power);

            w->drops_per_frame += w->drop_prob_increment;
            if(w->drops_per_frame < 0)
                w->drops_per_frame = 0;
            break;
        }

        default:
            break;
    }

    w->rain_period--;
}

static void water_draw_motion_frame(VJFrame *f, water_t *w)
{
    const int len = f->len;
    const int uv_len = f->ssm ? len : f->uv_len;

#pragma omp parallel for schedule(static) num_threads(w->n_threads)
    for(int i = 0; i < len; i++)
        f->data[0][i] = w->diff_img[i];

#pragma omp parallel for schedule(static) num_threads(w->n_threads)
    for(int i = 0; i < uv_len; i++) {
        f->data[1][i] = 128;
        f->data[2][i] = 128;
    }
}

static void water_blur_luma(water_t *w, const uint8_t *restrict src, int width, int height)
{
    uint8_t *restrict dst = w->blur_img;

#pragma omp parallel for schedule(static) num_threads(w->n_threads)
    for(int y = 0; y < height; y++) {
        const int ym = y > 0 ? y - 1 : y;
        const int yp = y < height - 1 ? y + 1 : y;

        const uint8_t *restrict r0 = src + ym * width;
        const uint8_t *restrict r1 = src + y  * width;
        const uint8_t *restrict r2 = src + yp * width;

        uint8_t *restrict out = dst + y * width;

        for(int x = 0; x < width; x++) {
            const int xm = x > 0 ? x - 1 : x;
            const int xp = x < width - 1 ? x + 1 : x;

            const int sum =
                (int)r0[xm] + (int)r0[x] + (int)r0[xp] +
                (int)r1[xm] + (int)r1[x] + (int)r1[xp] +
                (int)r2[xm] + (int)r2[x] + (int)r2[xp];

            out[x] = (uint8_t)((sum + 4) / 9);
        }
    }
}

static void water_inject_motion_map(water_t *w, const uint8_t *restrict diff, int width, int height)
{
    if(w->map_w <= 2 || w->map_h <= 2)
        return;

    const int shift = w->point + w->impact - 8;

#pragma omp parallel for schedule(static) num_threads(w->n_threads)
    for(int my = 1; my < w->map_h - 1; my++) {
        int *restrict p = w->map1 + my * w->map_w;
        int *restrict q = w->map2 + my * w->map_w;

        const int py = clampi(my << 1, 0, height - 1);
        const int py1 = clampi(py + 1, 0, height - 1);

        for(int mx = 1; mx < w->map_w - 1; mx++) {
            const int px = clampi(mx << 1, 0, width - 1);
            const int px1 = clampi(px + 1, 0, width - 1);

            const int h =
                (int)diff[py  * width + px] +
                (int)diff[py  * width + px1] +
                (int)diff[py1 * width + px] +
                (int)diff[py1 * width + px1];

            const int val = (h > 0) ? (h << shift) : 0;

            p[mx] = val;
            q[mx] = val;
        }
    }
}

static void water_motiondetect(VJFrame *f, VJFrame *f2, int threshold, water_t *w, int mode)
{
    const int len = f->len;
    const int width = f->width;
    const int height = f->height;

    const uint8_t *in = f2->data[0];

    if(mode != 0) {
        water_blur_luma(w, f2->data[0], width, height);
        in = w->blur_img;
    }

    if(!w->have_img) {
        veejay_memcpy(w->bg_img, in, len);
        w->have_img = 1;
        veejay_memset(w->diff_img, 0, len);
        return;
    }

#pragma omp parallel for schedule(static) num_threads(w->n_threads)
    for(int i = 0; i < len; i++) {
        int d = (int)w->bg_img[i] - (int)in[i];

        if(d < 0)
            d = -d;

        if(mode == 0)
            w->diff_img[i] = (d > threshold) ? (uint8_t)d : 0;
        else if(mode == 1)
            w->diff_img[i] = (d > threshold) ? (uint8_t)(255 - d) : 0;
        else
            w->diff_img[i] = (d < threshold) ? (uint8_t)d : 0;
    }

    water_inject_motion_map(w, w->diff_img, width, height);
}

static void water_simulate(water_t *w, int loopnum, int decay)
{
    const int wi = w->map_w;
    const int hi = w->map_h;

    if(wi <= 2 || hi <= 2)
        return;

    loopnum = clampi(loopnum, 1, 16);
    decay = clampi(decay, 1, 31);

    for(int n = 0; n < loopnum; n++) {
#pragma omp parallel for schedule(static) num_threads(w->n_threads)
        for(int y = 1; y < hi - 1; y++) {
            const int row = y * wi;

            for(int x = 1; x < wi - 1; x++) {
                const int idx = row + x;

                int h =
                    w->map1[idx - wi - 1] +
                    w->map1[idx - wi + 1] +
                    w->map1[idx + wi - 1] +
                    w->map1[idx + wi + 1] +
                    w->map1[idx - wi] +
                    w->map1[idx - 1] +
                    w->map1[idx + 1] +
                    w->map1[idx + wi] -
                    w->map1[idx] * 9;

                h >>= 3;

                int v = w->map1[idx] - w->map2[idx];

                v += h - (v >> decay);

                w->map3[idx] = v + w->map1[idx];
            }
        }

#pragma omp parallel for schedule(static) num_threads(w->n_threads)
        for(int y = 1; y < hi - 1; y++) {
            const int row = y * wi;

            for(int x = 1; x < wi - 1; x++) {
                const int idx = row + x;

                const int h =
                    w->map3[idx - wi] +
                    w->map3[idx - 1] +
                    w->map3[idx + 1] +
                    w->map3[idx + wi] +
                    w->map3[idx] * 60;

                w->map2[idx] = h >> 6;
            }
        }

        int *tmp = w->map1;
        w->map1 = w->map2;
        w->map2 = tmp;
    }
}

static void water_calc_vtable(water_t *w)
{
    const int wi = w->map_w;
    const int hi = w->map_h;

    if(wi <= 1 || hi <= 1)
        return;

#pragma omp parallel for schedule(static) num_threads(w->n_threads)
    for(int y = 0; y < hi - 1; y++) {
        const int row = y * wi;

        for(int x = 0; x < wi - 1; x++) {
            const int idx = row + x;
            const int vi = idx << 1;

            const int dh = ((w->map1[idx] - w->map1[idx + 1]) >> (w->point - 1)) & 0xff;
            const int dv = ((w->map1[idx] - w->map1[idx + wi]) >> (w->point - 1)) & 0xff;

            w->vtable[vi]     = (int8_t)((uint8_t)w->sqrtable[dh]);
            w->vtable[vi + 1] = (int8_t)((uint8_t)w->sqrtable[dv]);
        }
    }
}

static void water_render(VJFrame *frame, water_t *w)
{
    const int width = frame->width;
    const int height = frame->height;

    const uint8_t *restrict src = w->src_img;
    uint8_t *restrict dst = frame->data[0];
    const int8_t *restrict vt = w->vtable;

    const int map_w = w->map_w;
    const int map_h = w->map_h;

#pragma omp parallel for schedule(static) num_threads(w->n_threads)
    for(int y = 0; y < height; y += 2) {
        int my = y >> 1;

        if(my >= map_h - 1)
            my = map_h - 2;

        if(my < 0)
            my = 0;

        for(int x = 0; x < width; x += 2) {
            int mx = x >> 1;

            if(mx >= map_w - 1)
                mx = map_w - 2;

            if(mx < 0)
                mx = 0;

            const int vi = (my * map_w + mx) << 1;
            const int vi_r = (mx + 1 < map_w) ? (vi + 2) : vi;
            const int vi_d = (my + 1 < map_h) ? (((my + 1) * map_w + mx) << 1) : vi;

            const int h0 = (int)vt[vi];
            const int v0 = (int)vt[vi + 1];
            const int hr = (int)vt[vi_r];
            const int vd = (int)vt[vi_d + 1];

            const int dx0 = clampi(x + h0, 0, width - 1);
            const int dy0 = clampi(y + v0, 0, height - 1);

            const int dx1 = clampi(x + 1 + ((h0 + hr) >> 1), 0, width - 1);
            const int dy1 = clampi(y + 1 + ((v0 + vd) >> 1), 0, height - 1);

            const int dst0 = y * width + x;

            dst[dst0] = src[dy0 * width + dx0];

            if(x + 1 < width)
                dst[dst0 + 1] = src[dy0 * width + dx1];

            if(y + 1 < height) {
                const int dst1 = dst0 + width;

                dst[dst1] = src[dy1 * width + dx0];

                if(x + 1 < width)
                    dst[dst1 + 1] = src[dy1 * width + dx1];
            }
        }
    }
}

void water_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    water_t *w = (water_t*) ptr;

    if(!w || !frame || !args || !frame->data[0])
        return;

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    if(len <= 0 || width <= 0 || height <= 0)
        return;

    const int fresh_rate = clampi(args[P_REFRESH_FREQ], 1, 3600);
    const int loopnum = clampi(args[P_WAVESPEED], 1, 16);
    const int decay = clampi(args[P_DECAY], 1, 31);
    const int mode = clampi(args[P_MODE], 0, 6);
    const int threshold = clampi(args[P_THRESHOLD], 0, 255);
    const int beat_drops = clampi(args[P_BEAT_DROPS], 0, 12);
    const int beat_power = clampi(args[P_BEAT_POWER], 0, 1000);
    const int beat_push = clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = clampi(args[P_BEAT_SMOOTH], 0, 1000);

    int beat_q = 0;
    int kick_q = 0;
    water_update_beat(w, beat_push, beat_smooth, &beat_q, &kick_q);

    const int threshold_drop = ((18 + ((beat_power * 54 + 500) / 1000)) * beat_q + 500) / 1000;
    const int threshold_eff = clampi(threshold - threshold_drop, 0, 255);

    int loopnum_eff = loopnum + ((beat_q * 2 + 500) / 1000);
    if(kick_q > 520)
        loopnum_eff++;
    loopnum_eff = clampi(loopnum_eff, 1, 16);

    const int decay_eff = clampi(decay + ((beat_q * 3 + 500) / 1000), 1, 31);

    if(w->last_fresh_rate != fresh_rate) {
        w->last_fresh_rate = fresh_rate;
        w->rain_period = 0;
    }

    if(w->lastmode != mode) {
        water_clear_maps(w);
        w->have_img = 0;
        w->lastmode = mode;
        w->rain_period = 0;
    }

    veejay_memcpy(w->src_img, frame->data[0], len);

    if(mode == 0 && !w->have_img) {
        veejay_memcpy(w->bg_img, frame->data[0], len);
        w->have_img = 1;
        return;
    }

    if(mode == 0) {
        water_raindrop(w, fresh_rate);
        veejay_memcpy(w->bg_img, frame->data[0], len);
    } else {
        if(!frame2 || !frame2->data[0])
            return;

        switch(mode) {
            case 1:
                water_motiondetect(frame, frame2, threshold_eff, w, 0);
                water_draw_motion_frame(frame, w);
                return;

            case 2:
                water_motiondetect(frame, frame2, threshold_eff, w, 0);
                break;

            case 3:
                water_motiondetect(frame, frame2, threshold_eff, w, 1);
                water_draw_motion_frame(frame, w);
                return;

            case 4:
                water_motiondetect(frame, frame2, threshold_eff, w, 1);
                water_raindrop(w, fresh_rate);
                break;

            case 5:
                water_motiondetect(frame, frame2, threshold_eff, w, 2);
                water_draw_motion_frame(frame, w);
                return;

            case 6:
                water_motiondetect(frame, frame2, threshold_eff, w, 2);
                break;

            default:
                break;
        }
    }

    water_inject_beat_drops(w, beat_drops, beat_power, beat_q, kick_q);

    w->loopnum = loopnum_eff;

    water_simulate(w, loopnum_eff, decay_eff);
    water_calc_vtable(w);
    water_render(frame, w);

    w->frame_counter++;
}
