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
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "waterrippletv.h"

#define WATERRIPPLETV_PARAMS 5

#define P_REFRESH_FREQ 0
#define P_WAVESPEED    1
#define P_DECAY        2
#define P_DROP_DRIVE   3
#define P_RIPPLE_POWER 4

typedef struct {
    uint8_t *region;
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

    float wavespeed_env;
    float decay_env;
    float drops_env;
    float power_env;
    int smooth_ready;
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
        free(ve->defaults);
        free(ve->limits[0]);
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

    ve->limits[0][P_DROP_DRIVE] = 0;
    ve->limits[1][P_DROP_DRIVE] = 1000;
    ve->defaults[P_DROP_DRIVE] = 420;

    ve->limits[0][P_RIPPLE_POWER] = 0;
    ve->limits[1][P_RIPPLE_POWER] = 1000;
    ve->defaults[P_RIPPLE_POWER] = 620;

    ve->description = "RippleTV Drop Drive";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Refresh Frequency",
        "Wavespeed",
        "Decay",
        "Drop Drive",
        "Ripple Power"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 16, 92, 100, 8, 520, 0, 1, 80, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 4, 24, 68, 96, 180, 1200, 0, 1, 120, VJ_BEAT_COST_CHEAP, 66, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 98, 100, 4, 420, 24, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 94, 100, 4, 520, 24, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *waterrippletv_malloc(int width, int height)
{
    ripple_tv *r = (ripple_tv*) vj_calloc(sizeof(ripple_tv));
    if(!r)
        return NULL;

    const int len = width * height;

    r->map_h = height / 2 + 1;
    r->map_w = width / 2 + 1;

    const int map_len = r->map_h * r->map_w;
    const size_t ripple_bytes = (size_t)len;
    const size_t map_bytes = sizeof(int) * (size_t)map_len * 3u;
    const size_t vtable_bytes = sizeof(int8_t) * (size_t)map_len * 2u;
    const size_t total = ripple_bytes + map_bytes + vtable_bytes + 32u;

    r->region = (uint8_t*) vj_calloc(total);
    if(!r->region) {
        free(r);
        return NULL;
    }

    uint8_t *p = r->region;

    r->ripple_data[0] = p;
    veejay_memset(r->ripple_data[0], pixel_Y_lo_, len);

    p += ripple_bytes;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    r->map = (int*)p;
    r->map1 = r->map;
    r->map2 = r->map1 + map_len;
    r->map3 = r->map2 + map_len;

    p += map_bytes;

    r->vtable = (int8_t*)p;

    setTable(r);

    r->point = 16;
    r->tick = 0;
    r->last_fresh_rate = -1;
    r->wfastrand_val = 0x12345678u;
    r->wavespeed_env = 1.0f;
    r->decay_env = 8.0f;
    r->drops_env = 420.0f;
    r->power_env = 620.0f;
    r->smooth_ready = 0;

    r->n_threads = vje_advise_num_threads(len);

    return (void*) r;
}

void waterrippletv_free(void *ptr)
{
    ripple_tv *r = (ripple_tv*) ptr;

    free(r->region);
    free(r);
}

static void waterripple_clear_maps(ripple_tv *r)
{
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

static inline int waterripple_smooth_i(float *env, int target, float attack, float release)
{
    const float t = (float)target;

    if(t > *env)
        *env += (t - *env) * attack;
    else
        *env += (t - *env) * release;

    return (int)(*env + 0.5f);
}

static void waterripple_inject_drive_drops(ripple_tv *r,
                                            int drop_drive,
                                            int ripple_power)
{
    drop_drive = clampi(drop_drive, 0, 1000);
    ripple_power = clampi(ripple_power, 0, 1000);

    if(drop_drive <= 0 || ripple_power <= 0)
        return;

    const int max_drops = 1 + ((drop_drive * 17 + 500) / 1000);
    int drops = (drop_drive * max_drops + 850) / 1700;

    const int chance = drop_drive - 80;
    if(chance > 0 && (int)((wfastrand(r) >> 8) % 1000u) < chance)
        drops++;

    if(drop_drive > 760)
        drops += (drop_drive - 700) / 150;

    if(drops > 24)
        drops = 24;

    if(drops <= 0)
        return;

    const int power_units = 2 + ((ripple_power * 30 + 500) / 1000);
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
#pragma omp for schedule(static)
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

#pragma omp for schedule(static)
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

#pragma omp single
        {
            int *tmp = rip->map1;
            rip->map1 = rip->map2;
            rip->map2 = tmp;
        }
    }
}

static void waterripple_calc_vtable(ripple_tv *rip)
{
    const int wi = rip->map_w;
    const int hi = rip->map_h;

    if(wi <= 1 || hi <= 1)
        return;

#pragma omp for schedule(static)
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

    const uint8_t *restrict src = rip->ripple_data[0];
    uint8_t *restrict dst = frame->data[0];
    const int8_t *restrict vt = rip->vtable;

    const int map_w = rip->map_w;
    const int map_h = rip->map_h;

#pragma omp for schedule(static)
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

    const int len = frame->len;

    const int fresh_rate = args[P_REFRESH_FREQ];
    const int loopnum_arg = args[P_WAVESPEED];
    const int decay_arg = args[P_DECAY];
    const int drop_drive_arg = args[P_DROP_DRIVE];
    const int ripple_power_arg = args[P_RIPPLE_POWER];

    const float fast = 0.235f;
    const float slow = 0.118f;

    if(!rip->smooth_ready) {
        rip->wavespeed_env = (float)loopnum_arg;
        rip->decay_env = (float)decay_arg;
        rip->drops_env = (float)drop_drive_arg;
        rip->power_env = (float)ripple_power_arg;
        rip->smooth_ready = 1;
    }

    const int loopnum = clampi(waterripple_smooth_i(&rip->wavespeed_env, loopnum_arg, fast * 0.76f, slow), 1, 16);
    const int decay = clampi(waterripple_smooth_i(&rip->decay_env, decay_arg, fast * 0.58f, slow), 1, 32);
    const int drop_drive = clampi(waterripple_smooth_i(&rip->drops_env, drop_drive_arg, fast * 1.08f, slow), 0, 1000);
    const int ripple_power = clampi(waterripple_smooth_i(&rip->power_env, ripple_power_arg, fast, slow), 0, 1000);

    if(rip->last_fresh_rate != fresh_rate || rip->tick > fresh_rate) {
        rip->last_fresh_rate = fresh_rate;
        rip->tick = 0;
        rip->rain_period = 0;
        waterripple_clear_maps(rip);
    }

    rip->tick++;

    veejay_memcpy(rip->ripple_data[0], frame->data[0], len);

    raindrop(rip);
    waterripple_inject_drive_drops(rip, drop_drive, ripple_power);

    int effective_loopnum = loopnum + ((drop_drive * 3 + 500) / 1000);
    if(drop_drive > 760)
        effective_loopnum++;

    int effective_decay = decay + ((drop_drive * 3 + 500) / 1000) - ((drop_drive * ripple_power + 500000) / 1000000);
    effective_decay = clampi(effective_decay, 1, 32);

#pragma omp parallel num_threads(rip->n_threads)
    {
        waterripple_simulate(rip, effective_loopnum, effective_decay);
        waterripple_calc_vtable(rip);
        waterripple_render(frame, rip);
    }
}
