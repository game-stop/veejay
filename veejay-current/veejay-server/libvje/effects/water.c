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
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "water.h"

typedef struct {
    uint8_t *region;
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

    float loop_env;
    float decay_env;
    float threshold_env;
    float drops_env;
    float power_env;

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

#define WATER_PARAMS 7
#define P_REFRESH_FREQ 0
#define P_WAVESPEED    1
#define P_DECAY        2
#define P_MODE         3
#define P_THRESHOLD    4
#define P_DROP_DRIVE   5
#define P_RIPPLE_POWER 6

static inline float water_env(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

static inline int water_env_i(float *state, int target, int lo, int hi, float attack, float release)
{
    if(*state < (float)lo - 0.5f || *state > (float)hi + 0.5f)
        *state = (float)target;

    *state = water_env(*state, (float)target, attack, release);

    return clampi((int)(*state + 0.5f), lo, hi);
}

vj_effect *water_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = WATER_PARAMS;

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

    ve->limits[0][P_REFRESH_FREQ] = 1;    ve->limits[1][P_REFRESH_FREQ] = 3600; ve->defaults[P_REFRESH_FREQ] = 10;
    ve->limits[0][P_WAVESPEED]    = 1;    ve->limits[1][P_WAVESPEED]    = 16;   ve->defaults[P_WAVESPEED]    = 1;
    ve->limits[0][P_DECAY]        = 1;    ve->limits[1][P_DECAY]        = 31;   ve->defaults[P_DECAY]        = 10;
    ve->limits[0][P_MODE]         = 0;    ve->limits[1][P_MODE]         = 6;    ve->defaults[P_MODE]         = 0;
    ve->limits[0][P_THRESHOLD]    = 0;    ve->limits[1][P_THRESHOLD]    = 255;  ve->defaults[P_THRESHOLD]    = 45;
    ve->limits[0][P_DROP_DRIVE]   = 0;    ve->limits[1][P_DROP_DRIVE]   = 1000; ve->defaults[P_DROP_DRIVE]   = 360;
    ve->limits[0][P_RIPPLE_POWER] = 0;    ve->limits[1][P_RIPPLE_POWER] = 1000; ve->defaults[P_RIPPLE_POWER] = 650;

    ve->description = "Water ripples";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 1;
    ve->motion = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Refresh Frequency",
        "Wavespeed",
        "Decay",
        "Mode",
        "Threshold (motion)",
        "Drop Drive",
        "Ripple Power"
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

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 16, 92, 100, 8, 520, 0, 1, 80, VJ_BEAT_COST_CHEAP, 90, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 4, 24, 68, 96, 180, 1200, 0, 1, 120, VJ_BEAT_COST_CHEAP, 66, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 10, 120, 90, 100, 8, 520, 0, 2, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 98, 100, 4, 420, 24, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 94, 100, 4, 520, 24, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *water_malloc(int width, int height)
{
    water_t *w = (water_t*) vj_calloc(sizeof(water_t));
    if(!w)
        return NULL;

    const int len = width * height;

    w->map_h = height / 2 + 1;
    w->map_w = width / 2 + 1;

    const int map_len = w->map_h * w->map_w;
    const size_t img_bytes = (size_t)len * 4u;
    const size_t map_bytes = sizeof(int) * (size_t)map_len * 3u;
    const size_t vtable_bytes = sizeof(int8_t) * (size_t)map_len * 2u;
    const size_t total = img_bytes + map_bytes + vtable_bytes + 32u;

    w->region = (uint8_t*) vj_calloc(total);
    if(!w->region) {
        free(w);
        return NULL;
    }

    uint8_t *p = w->region;

    w->src_img = p;
    w->diff_img = w->src_img + len;
    w->bg_img   = w->diff_img + len;
    w->blur_img = w->bg_img + len;

    p += img_bytes;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);

    w->map = (int*)p;
    w->map1 = w->map;
    w->map2 = w->map1 + map_len;
    w->map3 = w->map2 + map_len;

    p += map_bytes;

    w->vtable = (int8_t*)p;

    water_set_table(w);

    w->point = 16;
    w->impact = 2;
    w->loopnum = 2;
    w->lastmode = -1;
    w->last_fresh_rate = -1;
    w->wfastrand_val = 0x12345678u;
    w->loop_env = -1.0f;
    w->decay_env = -1.0f;
    w->threshold_env = -1.0f;
    w->drops_env = -1.0f;
    w->power_env = -1.0f;

    w->n_threads = vje_advise_num_threads(len);

    return (void*) w;
}

void water_free(void *ptr)
{
    water_t *w = (water_t*) ptr;

    free(w->region);
    free(w);
}

static void water_clear_maps(water_t *w)
{
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

static void water_inject_drive_drops(water_t *w, int drop_drive, int ripple_power)
{
    drop_drive = clampi(drop_drive, 0, 1000);
    ripple_power = clampi(ripple_power, 0, 1000);

    if(drop_drive <= 0 || ripple_power <= 0)
        return;

    const int max_drops = 1 + ((drop_drive * 15 + 500) / 1000);
    int count = (drop_drive * max_drops + 900) / 1800;

    const int chance = drop_drive - 100;
    if(chance > 0 && (int)((wfastrand(w) >> 8) % 1000u) < chance)
        count++;

    if(drop_drive > 760)
        count += (drop_drive - 700) / 150;

    if(count > 22)
        count = 22;

    if(count <= 0)
        return;

    const int magnitude = 2 + ((ripple_power * 24 + 500) / 1000);
    const int power = -(magnitude << w->point);

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
    const int uv_len = f->uv_len;

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        f->data[0][i] = w->diff_img[i];

#pragma omp for schedule(static)
    for(int i = 0; i < uv_len; i++) {
        f->data[1][i] = 128;
        f->data[2][i] = 128;
    }
}

static void water_blur_luma(water_t *w, const uint8_t *restrict src, int width, int height)
{
    uint8_t *restrict dst = w->blur_img;

#pragma omp for schedule(static)
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

static void water_build_motion_diff(water_t *w, const uint8_t *restrict in, int threshold, int len, int mode)
{
#pragma omp for schedule(static)
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
}

static void water_inject_motion_map(water_t *w, const uint8_t *restrict diff, int width, int height)
{
    if(w->map_w <= 2 || w->map_h <= 2)
        return;

    const int shift = w->point + w->impact - 8;

#pragma omp for schedule(static)
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

static void water_simulate(water_t *w, int loopnum, int decay)
{
    const int wi = w->map_w;
    const int hi = w->map_h;

    if(wi <= 2 || hi <= 2)
        return;

    loopnum = clampi(loopnum, 1, 16);
    decay = clampi(decay, 1, 31);

    for(int n = 0; n < loopnum; n++) {
#pragma omp for schedule(static)
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

#pragma omp for schedule(static)
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

#pragma omp single
        {
            int *tmp = w->map1;
            w->map1 = w->map2;
            w->map2 = tmp;
        }
    }
}

static void water_calc_vtable(water_t *w)
{
    const int wi = w->map_w;
    const int hi = w->map_h;

    if(wi <= 1 || hi <= 1)
        return;

#pragma omp for schedule(static)
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

void water_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    water_t *w = (water_t*) ptr;

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    const int fresh_rate = args[P_REFRESH_FREQ];
    const int loopnum_arg = args[P_WAVESPEED];
    const int decay_arg = args[P_DECAY];
    const int mode = args[P_MODE];
    const int threshold_arg = args[P_THRESHOLD];
    const int drop_drive_arg = args[P_DROP_DRIVE];
    const int ripple_power_arg = args[P_RIPPLE_POWER];

    const int drive_q = clampi(drop_drive_arg, 0, 1000);
    const int power_q = clampi(ripple_power_arg, 0, 1000);

    const int loop_target = clampi(loopnum_arg + ((drive_q * 5 + 500) / 1000), 1, 16);
    const int decay_target = clampi(decay_arg + ((drive_q * 8 + 500) / 1000) - ((drive_q * power_q + 500000) / 1000000), 1, 31);

    const int threshold_drop =
        ((12 + ((power_q * 72 + 500) / 1000)) * drive_q + 500) / 1000;
    const int threshold_target = clampi(threshold_arg - threshold_drop, 0, 255);

    const int loopnum_eff = water_env_i(&w->loop_env, loop_target, 1, 16, 0.34f, 0.105f);
    const int decay_eff = water_env_i(&w->decay_env, decay_target, 1, 31, 0.26f, 0.070f);
    const int threshold_eff = water_env_i(&w->threshold_env, threshold_target, 0, 255, 0.42f, 0.115f);
    const int drop_drive = water_env_i(&w->drops_env, drop_drive_arg, 0, 1000, 0.40f, 0.130f);
    const int ripple_power = water_env_i(&w->power_env, ripple_power_arg, 0, 1000, 0.36f, 0.120f);

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

    const int motion_mode = (mode == 3 || mode == 4) ? 1 : ((mode == 5 || mode == 6) ? 2 : 0);
    const int use_motion = mode != 0;
    const int preview = (mode == 1 || mode == 3 || mode == 5);
    int motion_seeded = 0;

#pragma omp parallel num_threads(w->n_threads)
    {
        if(use_motion) {
            const uint8_t *restrict in = frame2->data[0];

            if(motion_mode != 0) {
                water_blur_luma(w, frame2->data[0], width, height);
                in = w->blur_img;
            }

#pragma omp single
            {
                if(!w->have_img) {
                    veejay_memcpy(w->bg_img, in, len);
                    w->have_img = 1;
                    veejay_memset(w->diff_img, 0, len);
                    motion_seeded = 1;
                }
            }

            if(!motion_seeded) {
                water_build_motion_diff(w, in, threshold_eff, len, motion_mode);
                water_inject_motion_map(w, w->diff_img, width, height);
            }
        }

        if(preview) {
            water_draw_motion_frame(frame, w);
        }
        else {
#pragma omp single
            {
                if(mode == 0) {
                    water_raindrop(w, fresh_rate);
                    veejay_memcpy(w->bg_img, frame->data[0], len);
                }
                else if(mode == 4) {
                    water_raindrop(w, fresh_rate);
                }

                water_inject_drive_drops(w, drop_drive, ripple_power);
                w->loopnum = loopnum_eff;
            }

            water_simulate(w, loopnum_eff, decay_eff);
            water_calc_vtable(w);
            water_render(frame, w);
        }
    }

    if(!preview) {
        w->frame_counter++;
    }
}
