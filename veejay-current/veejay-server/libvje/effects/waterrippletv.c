/* EffecTV - Realtime Digital Video Effektor
 * Copyright (C) 2001-2003 FUKUCHI Kentaro
 *
 * RippleTV - Water ripple effect
 * Copyright (C) 2001 - 2002 FUKUCHI Kentaro
 * 
 * ported to Linux VeeJay by:
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License ,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include "waterrippletv.h"
#include <stdint.h>

#define WATERRIPPLETV_PARAMS 7

#define P_REFRESH_FREQ 0
#define P_WAVESPEED    1
#define P_DECAY        2
#define P_BEAT_DROPS   3
#define P_BEAT_POWER   4
#define P_BEAT_PUSH    5
#define P_BEAT_SMOOTH  6

typedef struct {
    uint8_t *ripple_data[3];

    int8_t *vtable;

    int *map;
    int *map1;
    int *map2;
    int *map3;

    int map_h;
    int map_w;

    int sqrtable[256];

    int point;
    int tick;
    int last_fresh_rate;
    int n_threads;

    unsigned int wfastrand_val;

    int rain_period;
    int rain_stat;
    int drop_prob;
    int drop_prob_increment;
    int drops_per_frame_max;
    int drops_per_frame;
    int drop_power;

    float beat_env;
    float beat_kick;
    float beat_prev;
} ripple_tv;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline unsigned int wfastrand(ripple_tv *r)
{
    r->wfastrand_val = r->wfastrand_val * 1103515245u + 12345u;
    return r->wfastrand_val;
}

static inline int waterripple_beat_shape(int beat_push)
{
    beat_push = clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return clampi((beat_push * 35 + sq * 65 + 50) / 100, 0, 1000);
}

static void setTable(ripple_tv *r)
{
    for(int i = 0; i < 128; i++) {
        const int sq = i * i;

        r->sqrtable[i] = sq;
        r->sqrtable[255 - i] = -sq;
    }
}

vj_effect *waterrippletv_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = WATERRIPPLETV_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_REFRESH_FREQ] = 1;
    ve->limits[1][P_REFRESH_FREQ] = 3600;
    ve->defaults[P_REFRESH_FREQ] = 25 * 60;

    ve->limits[0][P_WAVESPEED] = 1;
    ve->limits[1][P_WAVESPEED] = 16;
    ve->defaults[P_WAVESPEED] = 1;

    ve->limits[0][P_DECAY] = 1;
    ve->limits[1][P_DECAY] = 31;
    ve->defaults[P_DECAY] = 8;

    ve->limits[0][P_BEAT_DROPS] = 0;
    ve->limits[1][P_BEAT_DROPS] = 1000;
    ve->defaults[P_BEAT_DROPS] = 420;

    ve->limits[0][P_BEAT_POWER] = 0;
    ve->limits[1][P_BEAT_POWER] = 1000;
    ve->defaults[P_BEAT_POWER] = 620;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_BEAT_SMOOTH] = 0;
    ve->limits[1][P_BEAT_SMOOTH] = 1000;
    ve->defaults[P_BEAT_SMOOTH] = 520;

    ve->description = "RippleTV Beat Drops";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Refresh Frequency",
        "Wavespeed",
        "Decay",
        "Beat Drops",
        "Beat Power",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SPEED,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE,
        1, 240, 6, 22, 1800, 4200, 900, 24, /* Refresh Frequency */

        VJ_BEAT_SPEED,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,
        1, 8, 6, 22, 1800, 4200, 900, 26, /* Wavespeed */

        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,
        3, 22, 6, 22, 1800, 4200, 900, 26, /* Decay */

        VJ_BEAT_DETAIL,    VJ_BEAT_F_REJECT,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* Beat Drops */

        VJ_BEAT_INTENSITY, VJ_BEAT_F_REJECT,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* Beat Power */

        VJ_BEAT_KICK,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,
        0, 760, 18, 68, 80, 760, 0, 100, /* Beat Push */

        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY,
        260, 840, 5, 18, 2200, 5200, 1200, 18 /* Beat Smooth */
    );

    (void) width;
    (void) height;

    return ve;
}

void *waterrippletv_malloc(int width, int height)
{
    if(width <= 0 || height <= 0)
        return NULL;

    ripple_tv *r = (ripple_tv*) vj_calloc(sizeof(ripple_tv));
    if(!r)
        return NULL;

    const int len = width * height;

    r->ripple_data[0] = (uint8_t*) vj_malloc((size_t)len);
    if(!r->ripple_data[0]) {
        free(r);
        return NULL;
    }

    veejay_memset(r->ripple_data[0], pixel_Y_lo_, len);

    r->map_h = height / 2 + 1;
    r->map_w = width / 2 + 1;

    const int map_len = r->map_h * r->map_w;

    r->map = (int*) vj_calloc(sizeof(int) * (size_t)map_len * 3u);
    if(!r->map) {
        free(r->ripple_data[0]);
        free(r);
        return NULL;
    }

    r->map1 = r->map;
    r->map2 = r->map1 + map_len;
    r->map3 = r->map2 + map_len;

    r->vtable = (int8_t*) vj_calloc(sizeof(int8_t) * (size_t)map_len * 2u);
    if(!r->vtable) {
        free(r->map);
        free(r->ripple_data[0]);
        free(r);
        return NULL;
    }

    setTable(r);

    r->point = 16;
    r->tick = 0;
    r->last_fresh_rate = -1;
    r->wfastrand_val = 0x12345678u;
    r->beat_env = 0.0f;
    r->beat_kick = 0.0f;
    r->beat_prev = 0.0f;

    r->n_threads = vje_advise_num_threads(len);
    if(r->n_threads < 1)
        r->n_threads = 1;

    return (void*) r;
}

void waterrippletv_free(void *ptr)
{
    ripple_tv *r = (ripple_tv*) ptr;

    if(!r)
        return;

    if(r->ripple_data[0])
        free(r->ripple_data[0]);

    if(r->map)
        free(r->map);

    if(r->vtable)
        free(r->vtable);

    free(r);
}

static void waterripple_clear_maps(ripple_tv *r)
{
    if(!r || !r->map || !r->vtable)
        return;

    const int map_len = r->map_w * r->map_h;

    veejay_memset(r->map, 0, sizeof(int) * (size_t)map_len * 3u);
    veejay_memset(r->vtable, 0, sizeof(int8_t) * (size_t)map_len * 2u);
}

static inline void drop(ripple_tv *r, int power)
{
    if(r->map_w <= 4 || r->map_h <= 4)
        return;

    const int x = (int)((wfastrand(r) >> 8) % (unsigned int)(r->map_w - 4)) + 2;
    const int y = (int)((wfastrand(r) >> 8) % (unsigned int)(r->map_h - 4)) + 2;

    int *p = r->map1 + y * r->map_w + x;
    int *q = r->map2 + y * r->map_w + x;

    *p = power;
    *q = power;

    *(p - r->map_w) = power >> 1;
    *(p - 1)        = power >> 1;
    *(p + 1)        = power >> 1;
    *(p + r->map_w) = power >> 1;

    *(p - r->map_w - 1) = power >> 2;
    *(p - r->map_w + 1) = power >> 2;
    *(p + r->map_w - 1) = power >> 2;
    *(p + r->map_w + 1) = power >> 2;

    *(q - r->map_w) = power >> 1;
    *(q - 1)        = power >> 1;
    *(q + 1)        = power >> 1;
    *(q + r->map_w) = power >> 1;

    *(q - r->map_w - 1) = power >> 2;
    *(q - r->map_w + 1) = power >> 2;
    *(q + r->map_w - 1) = power >> 2;
    *(q + r->map_w + 1) = power >> 2;
}

static void waterripple_update_beat(ripple_tv *r, int beat_push, int smooth)
{
    const int shaped = waterripple_beat_shape(beat_push);
    const float target = (float)shaped * 0.001f;
    const float smooth_t = (float)clampi(smooth, 0, 1000) * 0.001f;

    const float attack = 0.18f + (1.0f - smooth_t) * 0.36f;
    const float release = 0.030f + (1.0f - smooth_t) * 0.095f;

    if(target > r->beat_env)
        r->beat_env += (target - r->beat_env) * attack;
    else
        r->beat_env += (target - r->beat_env) * release;

    if(r->beat_env < 0.0001f)
        r->beat_env = 0.0f;
    else if(r->beat_env > 1.0f)
        r->beat_env = 1.0f;

    const float rise = target - r->beat_prev;
    r->beat_prev = target;

    r->beat_kick *= 0.58f + smooth_t * 0.18f;

    if(rise > 0.015f)
        r->beat_kick += rise * (0.90f + (1.0f - smooth_t) * 0.35f);

    if(r->beat_kick > 1.0f)
        r->beat_kick = 1.0f;
    else if(r->beat_kick < 0.0001f)
        r->beat_kick = 0.0f;
}

static void waterripple_inject_beat_drops(ripple_tv *r,
                                           int beat_drops,
                                           int beat_power,
                                           int beat_q,
                                           int kick_q)
{
    if(!r || (beat_q <= 0 && kick_q <= 0))
        return;

    beat_drops = clampi(beat_drops, 0, 1000);
    beat_power = clampi(beat_power, 0, 1000);

    if(beat_drops <= 0 || beat_power <= 0)
        return;

    const int max_drops = 1 + ((beat_drops * 11 + 500) / 1000);
    int drops = 0;

    if(kick_q > 18)
        drops += 1 + ((kick_q * max_drops + 700) / 1000);

    if(beat_q > 440 && ((r->tick & 1) == 0))
        drops += (beat_q * (1 + (max_drops >> 2)) + 1500) / 3000;

    if(drops > 18)
        drops = 18;

    if(drops <= 0)
        return;

    const int power_units = 2 + ((beat_power * 20 + 500) / 1000) + ((kick_q * 8 + 500) / 1000);
    const int power = -(power_units << r->point);

    for(int i = 0; i < drops; i++)
        drop(r, power);
}

static void raindrop(ripple_tv *r)
{
    if(r->rain_period <= 0) {
        switch(r->rain_stat) {
            case 0:
                r->rain_period = (int)(wfastrand(r) >> 23) + 100;
                r->drop_prob = 0;
                r->drop_prob_increment = 0x00ffffff / r->rain_period;
                r->drop_power = (-((int)(wfastrand(r) >> 28)) - 2) << r->point;
                r->drops_per_frame_max = 2 << (wfastrand(r) >> 30);
                r->rain_stat = 1;
                break;

            case 1:
                r->drop_prob = 0x00ffffff;
                r->drops_per_frame = 1;
                r->drop_prob_increment = 1;
                r->rain_period = (r->drops_per_frame_max - 1) * 16;
                r->rain_stat = 2;
                break;

            case 2:
                r->rain_period = (int)(wfastrand(r) >> 22) + 1000;
                r->drop_prob_increment = 0;
                r->rain_stat = 3;
                break;

            case 3:
                r->rain_period = (r->drops_per_frame_max - 1) * 16;
                r->drop_prob_increment = -1;
                r->rain_stat = 4;
                break;

            case 4:
                r->rain_period = (int)(wfastrand(r) >> 24) + 60;
                r->drop_prob_increment = -(r->drop_prob / r->rain_period);
                r->rain_stat = 5;
                break;

            case 5:
            default:
                r->rain_period = (int)(wfastrand(r) >> 23) + 500;
                r->drop_prob = 0;
                r->drop_prob_increment = 0;
                r->rain_stat = 0;
                break;
        }
    }

    switch(r->rain_stat) {
        case 1:
        case 5:
            if((int)(wfastrand(r) >> 8) < r->drop_prob)
                drop(r, r->drop_power);

            r->drop_prob += r->drop_prob_increment;
            r->drop_prob = clampi(r->drop_prob, 0, 0x00ffffff);
            break;

        case 2:
        case 3:
        case 4:
        {
            const int drops = r->drops_per_frame >> 4;

            for(int i = drops; i > 0; i--)
                drop(r, r->drop_power);

            r->drops_per_frame += r->drop_prob_increment;

            if(r->drops_per_frame < 0)
                r->drops_per_frame = 0;
            break;
        }

        default:
            break;
    }

    r->rain_period--;
}

static void waterripple_simulate(ripple_tv *rip, int loopnum, int decay)
{
    const int wi = rip->map_w;
    const int hi = rip->map_h;

    if(wi <= 2 || hi <= 2)
        return;

    loopnum = clampi(loopnum, 1, 16);
    decay = clampi(decay, 1, 32);

    for(int n = 0; n < loopnum; n++) {
#pragma omp parallel for schedule(static) num_threads(rip->n_threads)
        for(int y = 1; y < hi - 1; y++) {
            const int row = y * wi;

            for(int x = 1; x < wi - 1; x++) {
                const int idx = row + x;

                int h =
                    rip->map1[idx - wi - 1] +
                    rip->map1[idx - wi + 1] +
                    rip->map1[idx + wi - 1] +
                    rip->map1[idx + wi + 1] +
                    rip->map1[idx - wi] +
                    rip->map1[idx - 1] +
                    rip->map1[idx + 1] +
                    rip->map1[idx + wi] -
                    rip->map1[idx] * 9;

                h >>= 3;

                int v = rip->map1[idx] - rip->map2[idx];

                v += h - (v >> decay);

                rip->map3[idx] = v + rip->map1[idx];
            }
        }

#pragma omp parallel for schedule(static) num_threads(rip->n_threads)
        for(int y = 1; y < hi - 1; y++) {
            const int row = y * wi;

            for(int x = 1; x < wi - 1; x++) {
                const int idx = row + x;

                const int h =
                    rip->map3[idx - wi] +
                    rip->map3[idx - 1] +
                    rip->map3[idx + 1] +
                    rip->map3[idx + wi] +
                    rip->map3[idx] * 60;

                rip->map2[idx] = h >> 6;
            }
        }

        int *tmp = rip->map1;
        rip->map1 = rip->map2;
        rip->map2 = tmp;
    }
}

static void waterripple_calc_vtable(ripple_tv *rip)
{
    const int wi = rip->map_w;
    const int hi = rip->map_h;

    if(wi <= 1 || hi <= 1)
        return;

#pragma omp parallel for schedule(static) num_threads(rip->n_threads)
    for(int y = 0; y < hi - 1; y++) {
        const int row = y * wi;

        for(int x = 0; x < wi - 1; x++) {
            const int idx = row + x;
            const int vi = idx << 1;

            const int dh = ((rip->map1[idx] - rip->map1[idx + 1]) >> (rip->point - 1)) & 0xff;
            const int dv = ((rip->map1[idx] - rip->map1[idx + wi]) >> (rip->point - 1)) & 0xff;

            rip->vtable[vi]     = (int8_t)((uint8_t)rip->sqrtable[dh]);
            rip->vtable[vi + 1] = (int8_t)((uint8_t)rip->sqrtable[dv]);
        }
    }
}

static void waterripple_render(VJFrame *frame, ripple_tv *rip)
{
    const int width = frame->width;
    const int height = frame->height;

    if(width <= 0 || height <= 0 || rip->map_w <= 1 || rip->map_h <= 1)
        return;

    const uint8_t *restrict src = rip->ripple_data[0];
    uint8_t *restrict dst = frame->data[0];
    const int8_t *restrict vt = rip->vtable;

    const int map_w = rip->map_w;
    const int map_h = rip->map_h;

#pragma omp parallel for schedule(static) num_threads(rip->n_threads)
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

void waterrippletv_apply(void *ptr, VJFrame *frame, int *args)
{
    ripple_tv *rip = (ripple_tv*) ptr;

    if(!rip || !frame || !args || !frame->data[0])
        return;

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    if(len <= 0 || width <= 0 || height <= 0)
        return;

    const int fresh_rate = clampi(args[P_REFRESH_FREQ], 1, 3600);
    const int loopnum = clampi(args[P_WAVESPEED], 1, 16);
    const int decay = clampi(args[P_DECAY], 1, 32);
    const int beat_drops = clampi(args[P_BEAT_DROPS], 0, 1000);
    const int beat_power = clampi(args[P_BEAT_POWER], 0, 1000);
    const int beat_push = clampi(args[P_BEAT_PUSH], 0, 1000);
    const int beat_smooth = clampi(args[P_BEAT_SMOOTH], 0, 1000);

    waterripple_update_beat(rip, beat_push, beat_smooth);

    const int beat_q = clampi((int)(rip->beat_env * 1000.0f + 0.5f), 0, 1000);
    const int kick_q = clampi((int)(rip->beat_kick * 1000.0f + 0.5f), 0, 1000);

    if(rip->last_fresh_rate != fresh_rate || rip->tick > fresh_rate) {
        rip->last_fresh_rate = fresh_rate;
        rip->tick = 0;
        rip->rain_period = 0;
        waterripple_clear_maps(rip);
        rip->beat_kick = 0.0f;
    }

    rip->tick++;

    veejay_memcpy(rip->ripple_data[0], frame->data[0], len);

    raindrop(rip);
    waterripple_inject_beat_drops(rip, beat_drops, beat_power, beat_q, kick_q);

    int effective_loopnum = loopnum + ((beat_q * 2 + 500) / 1000);
    if(kick_q > 220)
        effective_loopnum++;

    int effective_decay = decay + ((beat_q * 4 + 500) / 1000);

    waterripple_simulate(rip, effective_loopnum, effective_decay);
    waterripple_calc_vtable(rip);
    waterripple_render(frame, rip);
}
