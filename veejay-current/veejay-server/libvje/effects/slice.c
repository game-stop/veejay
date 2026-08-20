/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "slice.h"
#include "motionmap.h"

#define SLICE_PARAMS 6

#define P_SLICES      0
#define P_PERIOD      1
#define P_MIX         2
#define P_CHROMA      3
#define P_SLICE_DRIVE 4
#define P_RECUT_DRIVE 5

typedef struct {
    uint8_t *block;
    uint8_t *slice_frame[3];

    int *slice_xshift;
    int *slice_yshift;
    int *prev_slice_xshift;
    int *prev_slice_yshift;

    int frame_periods;
    int current_period;
    int current_slices;
    int have_shift;
    int recut_bucket;

    int n__;
    int N__;
    int n_threads;

    uint32_t seed;

    float sm_slices;
    float sm_period;
    float sm_mix;
    float sm_chroma;
    float sm_slice_drive;
    float sm_recut_drive;
    int smooth_init;

    void *motionmap;
} slice_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t slice_mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline int slice_roundf_i(float v)
{
    return (int)(v + 0.5f);
}

static inline uint32_t slice_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;

    return x ? x : 0x6d2b79f5U;
}

static inline int slice_rand_range(uint32_t *state, int lo, int hi)
{
    *state = slice_hash_u32(*state + 0x9e3779b9U);

    return lo + (int)(*state % (uint32_t)(hi - lo + 1));
}



static inline float slice_smooth_value(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

static void slice_recalc(slice_t *s, int width, int height, int val, uint32_t seed, int smoothness)
{
    val = clampi(val, 2, 128);
    smoothness = clampi(smoothness, 0, 90);

    int *restrict slice_xshift = s->slice_xshift;
    int *restrict slice_yshift = s->slice_yshift;
    int *restrict prev_xshift = s->prev_slice_xshift;
    int *restrict prev_yshift = s->prev_slice_yshift;

    if(s->have_shift) {
        veejay_memcpy(prev_xshift, slice_xshift, sizeof(int) * (size_t)height);
        veejay_memcpy(prev_yshift, slice_yshift, sizeof(int) * (size_t)width);
    }
    else {
        veejay_memset(prev_xshift, 0, sizeof(int) * (size_t)height);
        veejay_memset(prev_yshift, 0, sizeof(int) * (size_t)width);
    }

    const int half = val >> 1;
    const int min_shift = -half;
    const int max_shift = half;
    const int min_run = 4;
    const int max_run = 4 + (half > 2 ? half : 2);
    const int keep = s->have_shift ? smoothness : 0;
    const int take = 100 - keep;

    uint32_t state = seed ? seed : 0x1234abcdU;
    int run = 0;
    int shift = 0;

    for(int x = 0; x < width; x++) {
        if(run <= 0) {
            shift = slice_rand_range(&state, min_shift, max_shift);
            run = slice_rand_range(&state, min_run, max_run);
        }
        else {
            run--;
        }

        slice_yshift[x] = (shift * take + prev_yshift[x] * keep + 50) / 100;
    }

    run = 0;
    shift = 0;

    for(int y = 0; y < height; y++) {
        if(run <= 0) {
            shift = slice_rand_range(&state, min_shift, max_shift);
            run = slice_rand_range(&state, min_run, max_run);
        }
        else {
            run--;
        }

        slice_xshift[y] = (shift * take + prev_xshift[y] * keep + 50) / 100;
    }

    s->current_slices = val;
    s->have_shift = 1;
}

vj_effect *slice_init(int width, int height)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = SLICE_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][P_SLICES] = 2;       ve->limits[1][P_SLICES] = 128;       ve->defaults[P_SLICES] = 63;
    ve->limits[0][P_PERIOD] = 0;       ve->limits[1][P_PERIOD] = 8 * 30;    ve->defaults[P_PERIOD] = 0;
    ve->limits[0][P_MIX] = 0;          ve->limits[1][P_MIX] = 1000;         ve->defaults[P_MIX] = 1000;
    ve->limits[0][P_CHROMA] = 0;       ve->limits[1][P_CHROMA] = 1000;      ve->defaults[P_CHROMA] = 1000;
    ve->limits[0][P_SLICE_DRIVE] = 0;  ve->limits[1][P_SLICE_DRIVE] = 1000; ve->defaults[P_SLICE_DRIVE] = 0;
    ve->limits[0][P_RECUT_DRIVE] = 0;  ve->limits[1][P_RECUT_DRIVE] = 1000; ve->defaults[P_RECUT_DRIVE] = 0;

    ve->description = "Slice Window";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->motion = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Slices",
        "Slice Period",
        "Mix",
        "Chroma Amount",
        "Slice Drive",
        "Recut Drive"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 4, 112, 82, 100, 16, 620, 0, 1, 180, VJ_BEAT_COST_EXPENSIVE, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 160, 76, 100, 8, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 64, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 360, 1000, 86, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 420, 1000, 70, 96, 120, 900, 0, 5, 0, VJ_BEAT_COST_CHEAP, 64, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 90, 100, 8, 420, 0, 5, 140, VJ_BEAT_COST_EXPENSIVE, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_IMPULSE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 88, 100, 4, 520, 20, 5, 120, VJ_BEAT_COST_EXPENSIVE, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *slice_malloc(int width, int height)
{
    slice_t *s = (slice_t*)vj_calloc(sizeof(slice_t));

    if(!s)
        return NULL;

    const int len = width * height;
    const size_t frame_bytes = (size_t)len * 3u;
    const size_t x_bytes = sizeof(int) * (size_t)height;
    const size_t y_bytes = sizeof(int) * (size_t)width;
    const size_t total = frame_bytes + (x_bytes * 2u) + (y_bytes * 2u) + 64u;

    s->block = (uint8_t*)vj_malloc(total);

    if(!s->block) {
        free(s);
        return NULL;
    }

    uint8_t *p = s->block;

    s->slice_frame[0] = p;
    s->slice_frame[1] = s->slice_frame[0] + len;
    s->slice_frame[2] = s->slice_frame[1] + len;
    p += frame_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->slice_xshift = (int*)p;
    p += x_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->prev_slice_xshift = (int*)p;
    p += x_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->slice_yshift = (int*)p;
    p += y_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->prev_slice_yshift = (int*)p;

    veejay_memset(s->slice_xshift, 0, x_bytes);
    veejay_memset(s->slice_yshift, 0, y_bytes);
    veejay_memset(s->prev_slice_xshift, 0, x_bytes);
    veejay_memset(s->prev_slice_yshift, 0, y_bytes);

    s->frame_periods = 0;
    s->current_period = 0;
    s->current_slices = -1;
    s->have_shift = 0;
    s->recut_bucket = -1;
    s->n__ = 0;
    s->N__ = 0;
    s->motionmap = NULL;
    s->seed = 0x6d2b79f5U ^ (uint32_t)(width * 73856093u) ^ (uint32_t)(height * 19349663u);
    s->sm_slices = 63.0f;
    s->sm_period = 0.0f;
    s->sm_mix = 1000.0f;
    s->sm_chroma = 1000.0f;
    s->sm_slice_drive = 0.0f;
    s->sm_recut_drive = 0.0f;
    s->smooth_init = 0;
    s->n_threads = vje_advise_num_threads(len);

    slice_recalc(s, width, height, 63, s->seed, 0);

    return (void*)s;
}

void slice_free(void *ptr)
{
    slice_t *s = (slice_t*)ptr;

    free(s->block);
    free(s);
}

static void slice_snapshot(slice_t *s, VJFrame *frame)
{
    const int len = frame->len;

    veejay_memcpy(s->slice_frame[0], frame->data[0], len);
    veejay_memcpy(s->slice_frame[1], frame->data[1], len);
    veejay_memcpy(s->slice_frame[2], frame->data[2], len);
}

static void slice_update_smoothing(slice_t *s,
                                   int val,
                                   int period,
                                   int mix,
                                   int chroma,
                                   int slice_drive,
                                   int recut_drive)
{
    const float param_attack = 0.28f;
    const float param_release = 0.105f;

    if(!s->smooth_init) {
        s->sm_slices = (float)val;
        s->sm_period = (float)period;
        s->sm_mix = (float)mix;
        s->sm_chroma = (float)chroma;
        s->sm_slice_drive = (float)slice_drive;
        s->sm_recut_drive = (float)recut_drive;
        s->smooth_init = 1;
    }
    else {
        s->sm_slices = slice_smooth_value(s->sm_slices, (float)val, param_attack, param_release);
        s->sm_period = slice_smooth_value(s->sm_period, (float)period, param_attack, param_release);
        s->sm_mix = slice_smooth_value(s->sm_mix, (float)mix, param_attack, param_release);
        s->sm_chroma = slice_smooth_value(s->sm_chroma, (float)chroma, param_attack, param_release);
        s->sm_slice_drive = slice_smooth_value(s->sm_slice_drive, (float)slice_drive, param_attack, param_release);
        s->sm_recut_drive = slice_smooth_value(s->sm_recut_drive, (float)recut_drive, param_attack, param_release);
    }
}

void slice_apply(void *ptr, VJFrame *frame, int *args)
{
    slice_t *s = (slice_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    int val = args[P_SLICES];
    int re_init = args[P_PERIOD];
    int mix = args[P_MIX];
    int chroma = args[P_CHROMA];
    int slice_drive = args[P_SLICE_DRIVE];
    int recut_drive = args[P_RECUT_DRIVE];

    int interpolate = 0;
    int motion = 0;
    int tmp1 = val;
    int tmp2 = re_init;

    if(s->motionmap && motionmap_active(s->motionmap)) {
        motionmap_scale_to(
            s->motionmap,
            128,
            1,
            2,
            0,
            &tmp1,
            &tmp2,
            &(s->n__),
            &(s->N__)
        );

        val = clampi(tmp1, 2, 128);
        re_init = clampi(tmp2, 0, 8 * 30);
        motion = 1;
        interpolate = !(s->n__ == s->N__ || s->n__ == 0);
    }
    else {
        s->n__ = 0;
        s->N__ = 0;
    }

    slice_update_smoothing(s, val, re_init, mix, chroma, slice_drive, recut_drive);

    int effective_val = slice_roundf_i(s->sm_slices);
    const int slice_q = clampi(slice_roundf_i(s->sm_slice_drive), 0, 1000);
    const int recut_q = clampi(slice_roundf_i(s->sm_recut_drive), 0, 1000);

    effective_val += (slice_q * 92 + 500) / 1000;
    effective_val = clampi(effective_val, 2, 128);

    int effective_period = clampi(slice_roundf_i(s->sm_period), 0, 8 * 30);
    const int forced_recut_period = recut_q > 0 ? (1 + (((1000 - recut_q) * 34 + 500) / 1000)) : 0;

    if(forced_recut_period > 0 && (effective_period == 0 || forced_recut_period < effective_period))
        effective_period = forced_recut_period;

    int force_recalc = 0;
    const int recut_bucket = recut_q / 80;

    if(recut_bucket != s->recut_bucket) {
        s->recut_bucket = recut_bucket;

        if(recut_q > 0)
            force_recalc = 1;
    }

    if(s->frame_periods != effective_period) {
        s->frame_periods = effective_period;
        s->current_period = effective_period;
    }

    if(motion) {
        force_recalc = 1;
    }
    else if(effective_period > 0) {
        s->current_period--;

        if(s->current_period <= 0) {
            force_recalc = 1;
            s->current_period = s->frame_periods;
        }
    }
    else if(effective_val != s->current_slices) {
        force_recalc = 1;
    }

    if(force_recalc || !s->have_shift) {
        const int table_smooth = 8 + ((1000 - recut_q) * 50 + 500) / 1000;

        s->seed = slice_hash_u32(
            s->seed ^
            (uint32_t)(effective_val * 0x45d9f3bu) ^
            (uint32_t)(recut_q * 0x27d4eb2du) ^
            (uint32_t)(slice_q * 0x9e3779b9u) ^
            (uint32_t)(frame->timecode * 1000003.0)
        );

        slice_recalc(s, width, height, effective_val, s->seed, table_smooth);
    }

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    slice_snapshot(s, frame);

    const uint8_t *restrict sY = s->slice_frame[0];
    const uint8_t *restrict sCb = s->slice_frame[1];
    const uint8_t *restrict sCr = s->slice_frame[2];

    int *restrict slice_xshift = s->slice_xshift;
    int *restrict slice_yshift = s->slice_yshift;

    int mix_q8 = (clampi(slice_roundf_i(s->sm_mix), 0, 1000) * 256 + 500) / 1000;
    int chroma_q8 = (clampi(slice_roundf_i(s->sm_chroma), 0, 1000) * 256 + 500) / 1000;

    mix_q8 = clampi(mix_q8 + ((slice_q * 34 + recut_q * 18 + 500) / 1000), 0, 256);
    chroma_q8 = clampi(chroma_q8 + ((slice_q * 24 + recut_q * 18 + 500) / 1000), 0, 256);

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int y_shift = slice_xshift[y];

        for(int x = 0; x < width; x++) {
            const int dst = row + x;
            const int dx = x + y_shift;
            const int dy = y + slice_yshift[x];

            if((unsigned)dx < (unsigned)width && (unsigned)dy < (unsigned)height) {
                const int src = dy * width + dx;
                const uint8_t sy = sY[src];
                const uint8_t su = slice_mix_u8(sCb[dst], sCb[src], chroma_q8);
                const uint8_t sv = slice_mix_u8(sCr[dst], sCr[src], chroma_q8);

                Y[dst] = slice_mix_u8(sY[dst], sy, mix_q8);
                Cb[dst] = slice_mix_u8(sCb[dst], su, mix_q8);
                Cr[dst] = slice_mix_u8(sCr[dst], sv, mix_q8);
            }
        }
    }

    if(interpolate)
        motionmap_interpolate_frame(s->motionmap, frame, s->N__, s->n__);

    if(motion)
        motionmap_store_frame(s->motionmap, frame);
}

int slice_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void slice_set_motionmap(void *ptr, void *priv)
{
    slice_t *s = (slice_t*)ptr;

    s->motionmap = priv;
}
