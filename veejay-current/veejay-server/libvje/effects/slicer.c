/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "slicer.h"

#define SLICER_PARAMS 11

#define P_WIDTH        0
#define P_HEIGHT       1
#define P_SHATTER      2
#define P_PERIOD       3
#define P_MODE         4
#define P_SMOOTHNESS   5
#define P_DOMINANCE    6
#define P_BLOCK_SIZE   7
#define P_SLICE_DRIVE   8
#define P_SHATTER_DRIVE 9
#define P_MIX_DRIVE     10

typedef struct {
    uint8_t *block;
    int *slice_xshift;
    int *slice_yshift;
    int *prev_slice_xshift;
    int *prev_slice_yshift;

    uint8_t *tmp[3];

    int last_period;
    int current_period;
    int have_shift;

    uint32_t seed;
    int n_threads;

    int smooth_ready;
    float sm_width;
    float sm_height;
    float sm_shatter;
    float sm_period;
    float sm_smoothness;
    float sm_dominance;
    float sm_block;
    float sm_slice_drive;
    float sm_shatter_drive;
    float sm_mix_drive;
} slicer_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t slicer_blend_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint32_t slicer_rand(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    *state = x ? x : 0x6d2b79f5u;

    return *state;
}

static inline int slicer_rand_range(uint32_t *state, int lo, int hi)
{
    return lo + (int)(slicer_rand(state) % (uint32_t)(hi - lo + 1));
}


static inline float slicer_smooth_value(float current, float target, float speed)
{
    return current + ((target - current) * speed);
}

vj_effect *slicer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = SLICER_PARAMS;
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

    int min_dim = w < h ? w : h;
    int max_block_size = min_dim / 2;

    if(max_block_size < 4)
        max_block_size = 4;

    if(max_block_size > 512)
        max_block_size = 512;

    int max_shift = 0;
    int bs = 1;

    while((bs << 1) <= max_block_size && max_shift < 9) {
        bs <<= 1;
        max_shift++;
    }

    if(max_shift < 2)
        max_shift = 2;

    ve->limits[0][P_WIDTH] = 1;        ve->limits[1][P_WIDTH] = w;              ve->defaults[P_WIDTH] = w >= 16 ? 16 : w;
    ve->limits[0][P_HEIGHT] = 1;       ve->limits[1][P_HEIGHT] = h;             ve->defaults[P_HEIGHT] = h >= 16 ? 16 : h;
    ve->limits[0][P_SHATTER] = 0;      ve->limits[1][P_SHATTER] = 128;          ve->defaults[P_SHATTER] = 8;
    ve->limits[0][P_PERIOD] = 0;       ve->limits[1][P_PERIOD] = 500;           ve->defaults[P_PERIOD] = 0;
    ve->limits[0][P_MODE] = 0;         ve->limits[1][P_MODE] = 1;               ve->defaults[P_MODE] = 0;
    ve->limits[0][P_SMOOTHNESS] = 0;   ve->limits[1][P_SMOOTHNESS] = 100;       ve->defaults[P_SMOOTHNESS] = 0;
    ve->limits[0][P_DOMINANCE] = 0;    ve->limits[1][P_DOMINANCE] = 100;        ve->defaults[P_DOMINANCE] = 50;
    ve->limits[0][P_BLOCK_SIZE] = 2;   ve->limits[1][P_BLOCK_SIZE] = max_shift; ve->defaults[P_BLOCK_SIZE] = clampi(5, 2, max_shift);
    ve->limits[0][P_SLICE_DRIVE] = 0;   ve->limits[1][P_SLICE_DRIVE] = 1000;     ve->defaults[P_SLICE_DRIVE] = 0;
    ve->limits[0][P_SHATTER_DRIVE] = 0; ve->limits[1][P_SHATTER_DRIVE] = 1000;   ve->defaults[P_SHATTER_DRIVE] = 0;
    ve->limits[0][P_MIX_DRIVE] = 0;     ve->limits[1][P_MIX_DRIVE] = 1000;       ve->defaults[P_MIX_DRIVE] = 0;

    ve->description = "Slicer";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Width",
        "Height",
        "Shatter",
        "Period",
        "Mode",
        "Smoothness",
        "Dominance",
        "Block Size",
        "Slice Drive",
        "Shatter Drive",
        "Mix Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Clip", "Wrap");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 4, w > 320 ? 320 : w, 78, 100, 16, 620, 0, 2, 180, VJ_BEAT_COST_EXPENSIVE, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 4, h > 240 ? 240 : h, 78, 100, 16, 620, 0, 2, 180, VJ_BEAT_COST_EXPENSIVE, 72, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 8, 128, 88, 100, 6, 420, 20, 1, 120, VJ_BEAT_COST_EXPENSIVE, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 360, 76, 100, 8, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 60, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 8, 88, 58, 90, 220, 1400, 0, 1, 0, VJ_BEAT_COST_CHEAP, 54, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, 10, 90, 60, 92, 120, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 62, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 2, max_shift, 76, 100, 16, 620, 0, 1, 240, VJ_BEAT_COST_EXPENSIVE, 52, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 90, 100, 8, 420, 0, 5, 140, VJ_BEAT_COST_EXPENSIVE, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 88, 100, 6, 420, 20, 5, 120, VJ_BEAT_COST_EXPENSIVE, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 86, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

static void recalc(slicer_t *s,
                   int w,
                   int h,
                   const uint8_t *restrict Yinp,
                   int v1,
                   int v2,
                   int shatter,
                   uint32_t seed,
                   int smoothness)
{
    uint32_t state = seed ? seed : 0x1234abcdU;

    v1 = clampi(v1, 1, w);
    v2 = clampi(v2, 1, h);
    shatter = clampi(shatter, 0, 128);
    smoothness = clampi(smoothness, 0, 100);

    if(s->have_shift) {
        veejay_memcpy(s->prev_slice_xshift, s->slice_xshift, sizeof(int) * (size_t)h);
        veejay_memcpy(s->prev_slice_yshift, s->slice_yshift, sizeof(int) * (size_t)w);
    }
    else {
        veejay_memset(s->prev_slice_xshift, 0, sizeof(int) * (size_t)h);
        veejay_memset(s->prev_slice_yshift, 0, sizeof(int) * (size_t)w);
    }

    const int half_x = v1 >> 1;
    const int half_y = v2 >> 1;
    const int scale_num = 100 + (shatter * 2);

    int run = 0;
    int shift = 0;

    for(int x = 0; x < w; x++) {
        if(run <= 0) {
            const int base = slicer_rand_range(&state, -half_x, half_x);
            const int sample = Yinp[x];
            const int span = half_x > 0 ? half_x : 1;

            shift = (base * scale_num) / 100;
            run = 1 + (sample % span);
        }
        else {
            run--;
        }

        s->slice_yshift[x] = s->have_shift
            ? ((shift * (100 - smoothness)) + (s->prev_slice_yshift[x] * smoothness)) / 100
            : shift;
    }

    run = 0;
    shift = 0;

    for(int y = 0; y < h; y++) {
        if(run <= 0) {
            const int base = slicer_rand_range(&state, -half_y, half_y);
            const int sample = Yinp[y * w];
            const int span = half_y > 0 ? half_y : 1;

            shift = (base * scale_num) / 100;
            run = 1 + (sample % span);
        }
        else {
            run--;
        }

        s->slice_xshift[y] = s->have_shift
            ? ((shift * (100 - smoothness)) + (s->prev_slice_xshift[y] * smoothness)) / 100
            : shift;
    }

    s->have_shift = 1;
}

void *slicer_malloc(int width, int height)
{
    slicer_t *s = (slicer_t*)vj_calloc(sizeof(slicer_t));

    if(!s)
        return NULL;

    const size_t frame_sz = (size_t)width * (size_t)height;
    const size_t frame_bytes = frame_sz * 3u;
    const size_t x_bytes = sizeof(int) * (size_t)height;
    const size_t y_bytes = sizeof(int) * (size_t)width;
    const size_t total = frame_bytes + (x_bytes * 2u) + (y_bytes * 2u) + 64u;

    s->block = (uint8_t*)vj_malloc(total);

    if(!s->block) {
        free(s);
        return NULL;
    }

    uint8_t *p = s->block;

    s->tmp[0] = p;
    s->tmp[1] = s->tmp[0] + frame_sz;
    s->tmp[2] = s->tmp[1] + frame_sz;
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

    s->last_period = -1;
    s->current_period = 0;
    s->have_shift = 0;
    s->seed = 0x6d2b79f5u ^ (uint32_t)(width * 73856093u) ^ (uint32_t)(height * 19349663u);
    s->smooth_ready = 0;
    s->sm_width = 16.0f;
    s->sm_height = 16.0f;
    s->sm_shatter = 8.0f;
    s->sm_period = 0.0f;
    s->sm_smoothness = 0.0f;
    s->sm_dominance = 50.0f;
    s->sm_block = 5.0f;
    s->sm_slice_drive = 0.0f;
    s->sm_shatter_drive = 0.0f;
    s->sm_mix_drive = 0.0f;
    s->n_threads = vje_advise_num_threads((int)frame_sz);

    return s;
}

void slicer_free(void *ptr)
{
    slicer_t *s = (slicer_t*)ptr;

    free(s->block);
    free(s);
}

static inline int slicer_wrapi(int v, int max)
{
    v %= max;

    if(v < 0)
        v += max;

    return v;
}

void slicer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    slicer_t *s = (slicer_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int base_w = args[P_WIDTH];
    const int base_h = args[P_HEIGHT];
    const int base_shatter = args[P_SHATTER];
    const int base_period = args[P_PERIOD];
    const int mode = args[P_MODE] ? 1 : 0;
    const int base_smooth = args[P_SMOOTHNESS];
    const int base_dom = args[P_DOMINANCE];
    const int base_block = args[P_BLOCK_SIZE];
    const int slice_drive_arg = args[P_SLICE_DRIVE];
    const int shatter_drive_arg = args[P_SHATTER_DRIVE];
    const int mix_drive_arg = args[P_MIX_DRIVE];

    const float param_step = 0.24f;

    if(!s->smooth_ready) {
        s->sm_width = (float)base_w;
        s->sm_height = (float)base_h;
        s->sm_shatter = (float)base_shatter;
        s->sm_period = (float)base_period;
        s->sm_smoothness = (float)base_smooth;
        s->sm_dominance = (float)base_dom;
        s->sm_block = (float)base_block;
        s->sm_slice_drive = (float)slice_drive_arg;
        s->sm_shatter_drive = (float)shatter_drive_arg;
        s->sm_mix_drive = (float)mix_drive_arg;
        s->smooth_ready = 1;
    }
    else {
        s->sm_width = slicer_smooth_value(s->sm_width, (float)base_w, param_step);
        s->sm_height = slicer_smooth_value(s->sm_height, (float)base_h, param_step);
        s->sm_shatter = slicer_smooth_value(s->sm_shatter, (float)base_shatter, param_step);
        s->sm_period = slicer_smooth_value(s->sm_period, (float)base_period, param_step);
        s->sm_smoothness = slicer_smooth_value(s->sm_smoothness, (float)base_smooth, param_step);
        s->sm_dominance = slicer_smooth_value(s->sm_dominance, (float)base_dom, param_step);
        s->sm_block = slicer_smooth_value(s->sm_block, (float)base_block, param_step);
        s->sm_slice_drive = slicer_smooth_value(s->sm_slice_drive, (float)slice_drive_arg, param_step);
        s->sm_shatter_drive = slicer_smooth_value(s->sm_shatter_drive, (float)shatter_drive_arg, param_step);
        s->sm_mix_drive = slicer_smooth_value(s->sm_mix_drive, (float)mix_drive_arg, param_step);
    }

    const int slice_drive = clampi((int)(s->sm_slice_drive + 0.5f), 0, 1000);
    const int shatter_drive = clampi((int)(s->sm_shatter_drive + 0.5f), 0, 1000);
    const int mix_drive = clampi((int)(s->sm_mix_drive + 0.5f), 0, 1000);

    int val1 = clampi((int)(s->sm_width + 0.5f), 1, width);
    int val2 = clampi((int)(s->sm_height + 0.5f), 1, height);
    int shatter = clampi((int)(s->sm_shatter + 0.5f), 0, 128);
    int period = clampi((int)(s->sm_period + 0.5f), 0, 500);
    int smoothness = clampi((int)(s->sm_smoothness + 0.5f), 0, 100);
    int dominance = clampi((int)(s->sm_dominance + 0.5f), 0, 100);
    int block_shift = clampi((int)(s->sm_block + 0.5f), 2, 9);

    val1 = clampi(val1 + (((width - val1) * slice_drive + 500) / 1000), 1, width);
    val2 = clampi(val2 + (((height - val2) * slice_drive + 500) / 1000), 1, height);
    shatter = clampi(shatter + (((128 - shatter) * shatter_drive + 500) / 1000), 0, 128);


    dominance = clampi(dominance + (((100 - dominance) * mix_drive + 500) / 1000), 0, 100);
    block_shift = clampi(block_shift - ((mix_drive + 333) / 500), 2, 9);

    const int extra_mix_q8 = clampi((mix_drive * 88 + 500) / 1000, 0, 96);

    if(s->last_period != period) {
        s->last_period = period;
        s->current_period = 0;
    }


    if(s->current_period <= 0 || !s->have_shift) {
        s->seed ^= (uint32_t)(frame->timecode * 1000003.0);
        s->seed ^= (uint32_t)(val1 * 0x45d9f3bu);
        s->seed ^= (uint32_t)(val2 * 0x119de1f3u);
        s->seed ^= (uint32_t)(shatter * 0x27d4eb2du);
        s->seed ^= (uint32_t)((slice_drive + (shatter_drive << 1) + (mix_drive << 2)) * 0x9e3779b9u);

        recalc(s, width, height, frame->data[0], val1, val2, shatter, s->seed, smoothness);

        s->current_period = period > 0 ? period : 1;
    }

    s->current_period--;

    uint8_t *restrict dY = frame->data[0];
    uint8_t *restrict dCb = frame->data[1];
    uint8_t *restrict dCr = frame->data[2];

    veejay_memcpy(s->tmp[0], dY, len);
    veejay_memcpy(s->tmp[1], dCb, len);
    veejay_memcpy(s->tmp[2], dCr, len);

    const uint8_t *restrict s1Y = s->tmp[0];
    const uint8_t *restrict s1Cb = s->tmp[1];
    const uint8_t *restrict s1Cr = s->tmp[2];

    const uint8_t *restrict s2Y = frame2->data[0];
    const uint8_t *restrict s2Cb = frame2->data[1];
    const uint8_t *restrict s2Cr = frame2->data[2];

    int *restrict sx_row = s->slice_xshift;
    int *restrict sy_col = s->slice_yshift;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int shift_x = sx_row[y];

        for(int x = 0; x < width; x++) {
            int ix = x + shift_x;
            int iy = y + sy_col[x];

            const int dst = row + x;
            uint8_t out_y = s1Y[dst];
            uint8_t out_cb = s1Cb[dst];
            uint8_t out_cr = s1Cr[dst];

            if(mode == 0) {
                if((unsigned)ix < (unsigned)width && (unsigned)iy < (unsigned)height) {
                    const int src = iy * width + ix;
                    const int chunk_x = ix >> block_shift;
                    const int chunk_y = iy >> block_shift;
                    const uint32_t hash = ((uint32_t)chunk_x * 104729u) ^ ((uint32_t)chunk_y * 131071u);
                    const int use_s2 = (int)(hash % 100u) < dominance;

                    const uint8_t *restrict srcY = use_s2 ? s2Y : s1Y;
                    const uint8_t *restrict srcCb = use_s2 ? s2Cb : s1Cb;
                    const uint8_t *restrict srcCr = use_s2 ? s2Cr : s1Cr;

                    out_y = srcY[src];
                    out_cb = srcCb[src];
                    out_cr = srcCr[src];
                }
            }
            else {
                ix = slicer_wrapi(ix, width);
                iy = slicer_wrapi(iy, height);

                const int src = iy * width + ix;
                const int chunk_x = ix >> block_shift;
                const int chunk_y = iy >> block_shift;
                const uint32_t hash = ((uint32_t)chunk_x * 104729u) ^ ((uint32_t)chunk_y * 131071u);
                const int use_s2 = (int)(hash % 100u) < dominance;

                const uint8_t *restrict srcY = use_s2 ? s2Y : s1Y;
                const uint8_t *restrict srcCb = use_s2 ? s2Cb : s1Cb;
                const uint8_t *restrict srcCr = use_s2 ? s2Cr : s1Cr;

                out_y = srcY[src];
                out_cb = srcCb[src];
                out_cr = srcCr[src];
            }

            if(extra_mix_q8 > 0) {
                out_y = slicer_blend_u8(out_y, s2Y[dst], extra_mix_q8);
                out_cb = slicer_blend_u8(out_cb, s2Cb[dst], extra_mix_q8);
                out_cr = slicer_blend_u8(out_cr, s2Cr[dst], extra_mix_q8);
            }

            dY[dst] = out_y;
            dCb[dst] = out_cb;
            dCr[dst] = out_cr;
        }
    }
}
