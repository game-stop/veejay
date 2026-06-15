/*
 * Copyright (C) 2026 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#include "common.h"
#include "tessaract.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <veejaycore/vjmem.h>
#include <libvje/vje.h>

#ifndef ATS_PI
#define ATS_PI 3.14159265358979323846f
#endif


#define ATS_FP 14
#define ATS_ONE (1 << ATS_FP)
#define ATS_MFP 10
#define ATS_MONE (1 << ATS_MFP)

#define ATS_MAX_BANDS 512
#define ATS_BAND_MASK (ATS_MAX_BANDS - 1)
#define ATS_PHASE_BANDS ATS_MAX_BANDS
#define ATS_WAVE_LUT_SIZE 1024
#define ATS_WAVE_LUT_MASK (ATS_WAVE_LUT_SIZE - 1)

#define ATS_MIN_SLICE 8
#define ATS_MAX_SLICE 180

enum {
    ATS_SLICE_WIDTH = 0,
    ATS_IMPACT,
    ATS_AXIS_ANGLE,
    ATS_DEPTH_PUSH,
    ATS_SLAB_SCALE,
    ATS_SLIDE_SPEED,
    ATS_EDGE_FLASH,
    ATS_SNARE_FLASH,
    ATS_HAT_FLICKER,
    ATS_LAYERS,
    ATS_HINGE_FOLD,
    ATS_SETTLE,
    ATS_NUM_PARAMS
};

typedef struct {
    int base_slide;
    int base_depth;
    int base_scale;
    int base_shear;
    int base_rot;
    int base_hinge;
    int base_sub;
    int base_energy;
    int base_phase;
    int polarity;

    int m00;
    int m01;
    int m10;
    int m11;

    int tx;
    int ty;
    int hinge;
    int subshift;
    int glow;
    int flicker;
    int energy;
} ats_band_t;

typedef struct {
    int w;
    int h;
    int len;
    int n_threads;

    uint8_t *src_y;
    uint8_t *src_u;
    uint8_t *src_v;

    int *x_proj;
    int *y_proj;
    int *x_proj2;
    int *y_proj2;

    ats_band_t bands[ATS_MAX_BANDS];
    int16_t wave_lut[ATS_WAVE_LUT_SIZE];

    float impact_env;
    float snare_env;
    float hat_env;

    float phase_vel;
    float phase2_vel;
    float axis_vel;
    float axis_spin;
    float axis_spin_vel;
    float slice_width_f;
    float axis_angle_f;
    float depth_push_f;
    float slab_scale_f;
    float slide_speed_f;
    float edge_flash_f;
    float hat_flicker_f;
    float hinge_fold_f;

    float last_impact;
    float last_snare;
    float last_hat;
    int impact_cooldown;
    int snare_cooldown;
    int hat_cooldown;
    int smooth_ready;

    float axis_phase;
    int phase_fp;
    int phase2_fp;

    int nx;
    int ny;
    int tx_axis;
    int ty_axis;
    int sx;
    int sy;
    int sx_axis;
    int sy_axis;

    int seed;
    uint32_t frame_count;
} ats_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float ats_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t ats_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline int ats_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline uint32_t ats_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float ats_env(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

static void ats_reseed_bands(ats_t *s, float impact, float snare)
{
    uint32_t base =
        ats_hash_u32((uint32_t)s->seed ^
                     (s->frame_count * 747796405U) ^
                     ((uint32_t)(impact * 1000.0f) << 8) ^
                     ((uint32_t)(snare * 1000.0f) << 18));

    s->seed = (int)ats_hash_u32(base + 0x9e3779b9U);

    const int boost = (int)(impact * 72.0f + snare * 38.0f);

    for(int i = 0; i < ATS_MAX_BANDS; i++) {
        ats_band_t *b = &s->bands[i];

        uint32_t hv = ats_hash_u32(base ^ ((uint32_t)i * 2246822519U));

        b->base_slide = ((int)(hv & 255U)) - 128;
        b->base_depth = ((int)((hv >> 8) & 255U)) - 128;
        b->base_scale = ((int)((hv >> 16) & 255U)) - 128;
        b->base_shear = ((int)((hv >> 24) & 255U)) - 128;

        hv = ats_hash_u32(hv + 0x632be59bU);

        b->base_rot = ((int)(hv & 255U)) - 128;
        b->base_hinge = ((int)((hv >> 8) & 255U)) - 128;
        b->base_sub = ((int)((hv >> 16) & 255U)) - 128;
        b->base_energy = 90 + (int)((hv >> 24) & 159U) + boost;

        hv = ats_hash_u32(hv + 0x85157af5U);

        b->base_phase = (int)(hv & 1023U);
        b->polarity = (hv & 0x80000000U) ? 1 : -1;

        b->m00 = ATS_MONE;
        b->m01 = 0;
        b->m10 = 0;
        b->m11 = ATS_MONE;
        b->tx = 0;
        b->ty = 0;
        b->hinge = 0;
        b->subshift = 0;
        b->glow = 0;
        b->flicker = 0;
        b->energy = b->base_energy;
    }
}

static void ats_update_projection(ats_t *s,
                                  int axis_arg,
                                  int layers_arg)
{
    const int w = s->w;
    const int h = s->h;

    const float drift =
        sinf(s->axis_phase) *
        ((float)layers_arg * (1.0f / 16.0f)) *
        12.0f;

    float deg = (float)axis_arg + s->axis_spin + drift;

    deg = deg < 0.0f ? deg + 360.0f : deg;
    deg = deg >= 360.0f ? deg - 360.0f : deg;
    deg = deg >= 360.0f ? deg - 360.0f : deg;

    const float angle = deg * (ATS_PI / 180.0f);
    const float angle2 = angle + 1.1071487f + sinf(s->axis_phase * 0.31f) * 0.135f;

    const int nx = (int)lrintf(cosf(angle) * (float)ATS_ONE);
    const int ny = (int)lrintf(sinf(angle) * (float)ATS_ONE);
    const int sx = (int)lrintf(cosf(angle2) * (float)ATS_ONE);
    const int sy = (int)lrintf(sinf(angle2) * (float)ATS_ONE);

    s->nx = nx;
    s->ny = ny;
    s->tx_axis = -ny;
    s->ty_axis = nx;

    s->sx = sx;
    s->sy = sy;
    s->sx_axis = -sy;
    s->sy_axis = sx;

    int * restrict x_proj = s->x_proj;
    int * restrict x_proj2 = s->x_proj2;
    int * restrict y_proj = s->y_proj;
    int * restrict y_proj2 = s->y_proj2;

    for(int x = 0; x < w; x++) {
        x_proj[x] = x * nx;
        x_proj2[x] = x * sx;
    }

    for(int y = 0; y < h; y++) {
        y_proj[y] = y * ny;
        y_proj2[y] = y * sy;
    }
}

static void ats_update_bands(ats_t *s,
                             int layers,
                             int depth_push,
                             int slab_scale,
                             int hinge_fold,
                             int edge_flash,
                             int hat_flicker,
                             int impact_i,
                             int snare_i,
                             int hat_i)
{
    const int layer_count = clampi(layers, 2, 16);
    const int motion = clampi(46 + impact_i + ((snare_i * 72) >> 8), 0, 370);

    const int nx = s->nx;
    const int ny = s->ny;
    const int tx_axis = s->tx_axis;
    const int ty_axis = s->ty_axis;

    for(int i = 0; i < ATS_MAX_BANDS; i++) {
        ats_band_t *b = &s->bands[i];

        const int layer_wave_i = (int)s->wave_lut[(i * layer_count) & ATS_WAVE_LUT_MASK];
        const int layer_abs_i = ats_absi(layer_wave_i);
        const float layer_wave = (float)layer_wave_i * (1.0f / 1024.0f);
        const int layer_weight = 52 + ((layer_abs_i * 64) >> 10);
        const int layer_depth = ((layer_wave_i * 96) >> 10) + (b->base_depth >> 3);
        const int energy = clampi((b->base_energy * motion) >> 8, 0, 560);

        const int slide_px = (int)(((int64_t)b->base_slide *
                                    (int64_t)depth_push *
                                    (int64_t)energy *
                                    (int64_t)layer_weight) >> 22);

        const int depth_px = (int)(((int64_t)layer_depth *
                                    (int64_t)depth_push *
                                    (int64_t)energy) >> 19);

        b->tx = ((tx_axis * slide_px) + (nx * depth_px)) >> ATS_FP;
        b->ty = ((ty_axis * slide_px) + (ny * depth_px)) >> ATS_FP;

        const float strength = (float)energy * (1.0f / 256.0f);

        float scale =
            1.0f +
            ((float)b->base_scale * (1.0f / 128.0f)) *
            ((float)slab_scale * (1.0f / 240.0f)) *
            strength *
            0.56f;

        scale = ats_clampf(scale, 0.46f, 2.12f);

        const float rot =
            ((float)b->base_rot * (1.0f / 128.0f)) *
            ((float)slab_scale * (1.0f / 240.0f)) *
            strength *
            0.32f +
            (layer_wave * (float)hinge_fold * 0.0035f);

        const float shear =
            ((float)b->base_shear * (1.0f / 128.0f)) *
            ((float)hinge_fold * (1.0f / 240.0f)) *
            strength *
            0.44f;

        const float cs = cosf(rot);
        const float sn = sinf(rot);

        const float a = cs * scale + shear * 0.12f;
        const float d = cs * scale - shear * 0.12f;
        const float c = sn - shear * 0.32f;
        const float e = -sn + shear;

        b->m00 = (int)lrintf(a * (float)ATS_MONE);
        b->m01 = (int)lrintf(e * (float)ATS_MONE);
        b->m10 = (int)lrintf(c * (float)ATS_MONE);
        b->m11 = (int)lrintf(d * (float)ATS_MONE);

        b->hinge =
            (int)(((int64_t)b->base_hinge *
                   (int64_t)hinge_fold *
                   (int64_t)energy) >> 16);

        b->subshift = clampi(
            (int)(((int64_t)b->base_sub *
                   (int64_t)depth_push *
                   (int64_t)energy) >> 20),
            -96,
            96);

        b->glow = clampi(edge_flash + ((snare_i * 155) >> 8), 0, 390);
        b->flicker = clampi(hat_flicker + ((hat_i * 95) >> 8), 0, 310);
        b->energy = energy;
    }
}

vj_effect *tessaractslide_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = ATS_NUM_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ATS_NUM_PARAMS);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ATS_NUM_PARAMS);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ATS_NUM_PARAMS);

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

    const int min_dim = w < h ? w : h;
    const int max_dim = w > h ? w : h;

    const int def_slice = clampi(min_dim / 12, 32, 72);
    const int def_depth = clampi((min_dim * 145) / 576, 95, 175);
    const int def_scale = clampi((min_dim * 86) / 576, 58, 118);
    const int def_edge = clampi((min_dim * 82) / 576, 48, 110);
    const int def_hat = clampi((min_dim * 54) / 576, 28, 74);
    const int def_hinge = clampi((min_dim * 82) / 576, 48, 118);
    const int def_layers = max_dim >= 1280 ? 9 : (min_dim <= 360 ? 5 : 7);

    ve->description = "Tesseract Slice";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->defaults[ATS_SLICE_WIDTH] = def_slice;
    ve->defaults[ATS_IMPACT] = 0;
    ve->defaults[ATS_AXIS_ANGLE] = 28;
    ve->defaults[ATS_DEPTH_PUSH] = def_depth;
    ve->defaults[ATS_SLAB_SCALE] = def_scale;
    ve->defaults[ATS_SLIDE_SPEED] = 100;
    ve->defaults[ATS_EDGE_FLASH] = def_edge;
    ve->defaults[ATS_SNARE_FLASH] = 0;
    ve->defaults[ATS_HAT_FLICKER] = def_hat;
    ve->defaults[ATS_LAYERS] = def_layers;
    ve->defaults[ATS_HINGE_FOLD] = def_hinge;
    ve->defaults[ATS_SETTLE] = 86;

    ve->limits[0][ATS_SLICE_WIDTH] = ATS_MIN_SLICE;
    ve->limits[1][ATS_SLICE_WIDTH] = ATS_MAX_SLICE;
    ve->limits[0][ATS_IMPACT] = 0;
    ve->limits[1][ATS_IMPACT] = 100;
    ve->limits[0][ATS_AXIS_ANGLE] = 0;
    ve->limits[1][ATS_AXIS_ANGLE] = 360;
    ve->limits[0][ATS_DEPTH_PUSH] = 0;
    ve->limits[1][ATS_DEPTH_PUSH] = 280;
    ve->limits[0][ATS_SLAB_SCALE] = 0;
    ve->limits[1][ATS_SLAB_SCALE] = 240;
    ve->limits[0][ATS_SLIDE_SPEED] = 0;
    ve->limits[1][ATS_SLIDE_SPEED] = 220;
    ve->limits[0][ATS_EDGE_FLASH] = 0;
    ve->limits[1][ATS_EDGE_FLASH] = 260;
    ve->limits[0][ATS_SNARE_FLASH] = 0;
    ve->limits[1][ATS_SNARE_FLASH] = 100;
    ve->limits[0][ATS_HAT_FLICKER] = 0;
    ve->limits[1][ATS_HAT_FLICKER] = 220;
    ve->limits[0][ATS_LAYERS] = 2;
    ve->limits[1][ATS_LAYERS] = 16;
    ve->limits[0][ATS_HINGE_FOLD] = 0;
    ve->limits[1][ATS_HINGE_FOLD] = 240;
    ve->limits[0][ATS_SETTLE] = 0;
    ve->limits[1][ATS_SETTLE] = 100;

    ve->param_description = vje_build_param_list(
        ATS_NUM_PARAMS,
        "Slice Width",
        "Impact Pulse",
        "Axis Angle",
        "Depth Push",
        "Slab Scale",
        "Slide Speed",
        "Edge Flash",
        "Snare Flash",
        "Hat Flicker",
        "Layers",
        "Hinge Fold",
        "Settle"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GRID_SIZE,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, ATS_MIN_SLICE, ATS_MAX_SLICE, 18, 72,  500, 2200, 0, 78,
        VJ_BEAT_TRIGGER,       VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,                         0,             100,           100,100,1,   170, 35,230,
        VJ_BEAT_MOTION_REACT,  VJ_BEAT_F_CONTINUOUS,                                             0,             360,           12, 58,  600, 3200, 0, 58,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                   36,            280,           18, 76,  500, 2200, 0, 84,
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                   42,            240,           18, 72,  500, 2400, 0, 76,
        VJ_BEAT_SIGNED_SPEED,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                   24,            220,           20, 82,  360, 1800, 0, 88,
        VJ_BEAT_INTENSITY,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                   32,            260,           24, 88,  300, 1600, 15,94,
        VJ_BEAT_SNARE,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,                         0,             100,           100,100,1,   170, 35,220,
        VJ_BEAT_HAT,           VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                   24,            220,           24, 92,  180, 1200, 0, 86,
        VJ_BEAT_GRID_SIZE,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000,
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                   36,            240,           18, 78,  500, 2400, 0, 82,
        VJ_BEAT_MEMORY,        VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, -1000
    );

    return ve;
}

void *tessaractslide_malloc(int w, int h)
{
    const int len = w * h;
    const int n_threads = vje_advise_num_threads(len);

    const size_t plane = (size_t)len;
    const size_t x_bytes = sizeof(int) * (size_t)w;
    const size_t y_bytes = sizeof(int) * (size_t)h;

    const size_t total =
        sizeof(ats_t) +
        plane * 3 +
        x_bytes * 2 +
        y_bytes * 2 +
        128;

    ats_t *s = (ats_t *)vj_calloc(total);

    if(!s)
        return NULL;

    const int min_dim = w < h ? w : h;
    const int init_slice = clampi(min_dim / 12, 32, 72);

    s->w = w;
    s->h = h;
    s->len = len;
    s->n_threads = n_threads;
    s->seed = 0x51ed270bU ^ (uint32_t)(w * 73856093U) ^ (uint32_t)(h * 19349663U);

    uint8_t *p = (uint8_t *)(s + 1);

    s->src_y = p;
    p += plane;
    s->src_u = p;
    p += plane;
    s->src_v = p;
    p += plane;

    p = (uint8_t *)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);
    s->x_proj = (int *)p;
    p += x_bytes;
    s->y_proj = (int *)p;
    p += y_bytes;
    s->x_proj2 = (int *)p;
    p += x_bytes;
    s->y_proj2 = (int *)p;

    s->impact_env = 0.0f;
    s->snare_env = 0.0f;
    s->hat_env = 0.0f;
    s->phase_vel = 0.0f;
    s->phase2_vel = 0.0f;
    s->axis_vel = 0.0f;
    s->axis_spin = 0.0f;
    s->axis_spin_vel = 0.0f;
    s->slice_width_f = (float)init_slice;
    s->axis_angle_f = 28.0f;
    s->depth_push_f = (float)clampi((min_dim * 145) / 576, 95, 175);
    s->slab_scale_f = (float)clampi((min_dim * 86) / 576, 58, 118);
    s->slide_speed_f = 100.0f;
    s->edge_flash_f = (float)clampi((min_dim * 82) / 576, 48, 110);
    s->hat_flicker_f = (float)clampi((min_dim * 54) / 576, 28, 74);
    s->hinge_fold_f = (float)clampi((min_dim * 82) / 576, 48, 118);

    s->last_impact = 0.0f;
    s->last_snare = 0.0f;
    s->last_hat = 0.0f;
    s->impact_cooldown = 0;
    s->snare_cooldown = 0;
    s->hat_cooldown = 0;
    s->smooth_ready = 0;

    s->axis_phase = 0.0f;
    s->phase_fp = 0;
    s->phase2_fp = 0;
    s->nx = ATS_ONE;
    s->ny = 0;
    s->tx_axis = 0;
    s->ty_axis = ATS_ONE;
    s->sx = 0;
    s->sy = ATS_ONE;
    s->sx_axis = -ATS_ONE;
    s->sy_axis = 0;
    s->frame_count = 0;

    for(int i = 0; i < ATS_WAVE_LUT_SIZE; i++)
        s->wave_lut[i] = (int16_t)lrintf(sinf(((float)i * (ATS_PI * 2.0f)) / (float)ATS_WAVE_LUT_SIZE) * 1024.0f);

    ats_reseed_bands(s, 0.0f, 0.0f);

    return s;
}

void tessaractslide_free(void *ptr)
{
    free(ptr);
}

void tessaractslide_apply(void *ptr, VJFrame *frame, int *args)
{
    ats_t *s = (ats_t *)ptr;

    uint8_t * restrict Y = frame->data[0];
    uint8_t * restrict U = frame->data[1];
    uint8_t * restrict V = frame->data[2];

    uint8_t * restrict src_y = s->src_y;
    uint8_t * restrict src_u = s->src_u;
    uint8_t * restrict src_v = s->src_v;

    const int w = s->w;
    const int h = s->h;
    const int len = s->len;
    const int threads = s->n_threads;

    const int slice_target = args[ATS_SLICE_WIDTH];
    const int impact_arg = args[ATS_IMPACT];
    const int axis_arg = args[ATS_AXIS_ANGLE];
    const int depth_push_arg = args[ATS_DEPTH_PUSH];
    const int slab_scale_arg = args[ATS_SLAB_SCALE];
    const int slide_speed_arg = args[ATS_SLIDE_SPEED];
    const int edge_flash_arg = args[ATS_EDGE_FLASH];
    const int snare_arg = args[ATS_SNARE_FLASH];
    const int hat_arg = args[ATS_HAT_FLICKER];
    const int layers_arg = args[ATS_LAYERS];
    const int hinge_arg = args[ATS_HINGE_FOLD];
    const int settle_arg = args[ATS_SETTLE];

    if(!s->smooth_ready) {
        s->slice_width_f = (float)slice_target;
        s->axis_angle_f = (float)axis_arg;
        s->depth_push_f = (float)depth_push_arg;
        s->slab_scale_f = (float)slab_scale_arg;
        s->slide_speed_f = (float)slide_speed_arg;
        s->edge_flash_f = (float)edge_flash_arg;
        s->hat_flicker_f = (float)hat_arg;
        s->hinge_fold_f = (float)hinge_arg;
        s->smooth_ready = 1;
    }

    const float impact_target = (float)impact_arg * 0.01f;
    const float snare_target = (float)snare_arg * 0.01f;
    const float hat_target = ats_clampf((float)hat_arg * (1.0f / 220.0f), 0.0f, 1.0f);

    const int impact_rise =
        s->impact_cooldown <= 0 &&
        ((impact_target > 0.30f && s->last_impact < 0.18f) ||
         ((impact_target - s->last_impact) > 0.12f));

    const int snare_rise =
        s->snare_cooldown <= 0 &&
        ((snare_target > 0.30f && s->last_snare < 0.18f) ||
         ((snare_target - s->last_snare) > 0.12f));

    const int hat_rise =
        s->hat_cooldown <= 0 &&
        ((hat_target > 0.24f && s->last_hat < 0.12f) ||
         ((hat_target - s->last_hat) > 0.10f));

    const float release = 0.010f + ((float)(100 - settle_arg) * 0.00105f);

    s->impact_env = ats_env(s->impact_env, impact_target, 0.82f, release);
    s->snare_env = ats_env(s->snare_env, snare_target, 0.86f, 0.130f);
    s->hat_env = ats_env(s->hat_env, hat_target, 0.74f, 0.280f);

    if(impact_rise || snare_rise) {
        const float reseed_impact = impact_target > 0.22f ? impact_target : s->impact_env;
        const float reseed_snare = snare_target > 0.22f ? snare_target : s->snare_env;
        ats_reseed_bands(s, reseed_impact, reseed_snare);
        s->impact_cooldown = impact_rise ? 4 : s->impact_cooldown;
        s->snare_cooldown = snare_rise ? 4 : s->snare_cooldown;
    }

    if(hat_rise)
        s->hat_cooldown = 2;

    if(s->impact_cooldown > 0)
        s->impact_cooldown--;
    if(s->snare_cooldown > 0)
        s->snare_cooldown--;
    if(s->hat_cooldown > 0)
        s->hat_cooldown--;

    const float slice_alpha = (float)slice_target > s->slice_width_f ? 0.160f : 0.070f;
    s->slice_width_f += ((float)slice_target - s->slice_width_f) * slice_alpha;
    s->axis_angle_f += ((float)axis_arg - s->axis_angle_f) * 0.055f;
    s->depth_push_f = ats_env(s->depth_push_f, (float)depth_push_arg, 0.260f, 0.085f);
    s->slab_scale_f = ats_env(s->slab_scale_f, (float)slab_scale_arg, 0.235f, 0.080f);
    s->slide_speed_f = ats_env(s->slide_speed_f, (float)slide_speed_arg, 0.280f, 0.095f);
    s->edge_flash_f = ats_env(s->edge_flash_f, (float)edge_flash_arg, 0.360f, 0.150f);
    s->hat_flicker_f = ats_env(s->hat_flicker_f, (float)hat_arg, 0.420f, 0.230f);
    s->hinge_fold_f = ats_env(s->hinge_fold_f, (float)hinge_arg, 0.245f, 0.085f);

    const int slice_width = clampi((int)lrintf(s->slice_width_f), ATS_MIN_SLICE, ATS_MAX_SLICE);
    const int axis_eff = clampi((int)lrintf(s->axis_angle_f), 0, 360);
    const int depth_push_eff = clampi((int)lrintf(s->depth_push_f), 0, 280);
    const int slab_scale_eff = clampi((int)lrintf(s->slab_scale_f), 0, 240);
    const int slide_speed_base = clampi((int)lrintf(s->slide_speed_f), 0, 220);
    const int edge_flash_eff = clampi((int)lrintf(s->edge_flash_f), 0, 260);
    const int hat_flicker_eff = clampi((int)lrintf(s->hat_flicker_f), 0, 220);
    const int hinge_eff = clampi((int)lrintf(s->hinge_fold_f), 0, 240);

    const int impact_i = (int)(s->impact_env * 256.0f);
    const int snare_i = (int)(s->snare_env * 256.0f);
    const int hat_i = (int)(s->hat_env * 256.0f);

    s->last_impact = impact_target;
    s->last_snare = snare_target;
    s->last_hat = hat_target;

    const int beat_activity = impact_i > snare_i ? impact_i : snare_i;
    const int speed_gate = clampi((beat_activity - 4) * 2, 0, 256);
    const int slide_speed_eff = clampi(
        1 + ((slide_speed_base * (64 + (speed_gate >> 1))) >> 8) + (impact_i >> 5),
        1,
        320);

    const int phase_target =
        ((7 + slide_speed_eff * 2 + (impact_i >> 3) + (snare_i >> 4)) << ATS_FP) >> 5;

    const int phase2_target =
        ((4 + slide_speed_eff + (impact_i >> 4) + (hat_i >> 4)) << ATS_FP) >> 5;

    s->phase_vel += ((float)phase_target - s->phase_vel) * 0.105f;
    s->phase2_vel += ((float)phase2_target - s->phase2_vel) * 0.085f;

    s->phase_fp += (int)s->phase_vel;
    s->phase2_fp -= (int)s->phase2_vel;

    const float axis_target =
        0.0018f +
        ((float)slide_speed_eff * 0.000045f) +
        s->impact_env * 0.0065f +
        s->snare_env * 0.0035f;

    s->axis_vel += (axis_target - s->axis_vel) * 0.055f;
    s->axis_phase += s->axis_vel;

    s->axis_phase = s->axis_phase > ATS_PI * 2.0f
        ? s->axis_phase - ATS_PI * 2.0f
        : s->axis_phase;

    const float spin_target =
        ((float)slide_speed_eff * 0.010f) +
        s->impact_env * 0.22f +
        s->snare_env * 0.10f;

    s->axis_spin_vel += (spin_target - s->axis_spin_vel) * 0.045f;
    s->axis_spin += s->axis_spin_vel;

    s->axis_spin = s->axis_spin >= 360.0f
        ? s->axis_spin - 360.0f
        : (s->axis_spin < 0.0f ? s->axis_spin + 360.0f : s->axis_spin);

    ats_update_projection(s, axis_eff, layers_arg);

    ats_update_bands(
        s,
        layers_arg,
        clampi(depth_push_eff + ((impact_i * 118) >> 8) + ((snare_i * 32) >> 8), 0, 360),
        clampi(slab_scale_eff + ((snare_i * 86) >> 8) + ((impact_i * 42) >> 8), 0, 310),
        clampi(hinge_eff + ((impact_i * 96) >> 8) + ((snare_i * 62) >> 8), 0, 340),
        clampi(edge_flash_eff + ((snare_i * 175) >> 8) + ((impact_i * 55) >> 8), 0, 420),
        clampi(hat_flicker_eff + ((hat_i * 140) >> 8), 0, 360),
        impact_i,
        snare_i,
        hat_i
    );

    s->frame_count++;

    const int hw = w >> 1;
    const int hh = h >> 1;

    const int slice_fp = slice_width << ATS_FP;
    const int slice2_width = clampi(slice_width + (slice_width >> 1) + layers_arg * 2, 12, 200);
    const int slice2_fp = slice2_width << ATS_FP;

    const int phase_wrap = slice_fp * ATS_PHASE_BANDS;
    const int phase2_wrap = slice2_fp * ATS_PHASE_BANDS;

    if(s->phase_fp >= phase_wrap || s->phase_fp < 0)
        s->phase_fp %= phase_wrap;

    if(s->phase_fp < 0)
        s->phase_fp += phase_wrap;

    if(s->phase2_fp >= phase2_wrap || s->phase2_fp < 0)
        s->phase2_fp %= phase2_wrap;

    if(s->phase2_fp < 0)
        s->phase2_fp += phase2_wrap;

    const int edge_width = clampi(2 + (slice_width >> 5) + ((edge_flash_eff + snare_i) >> 7), 2, 8);

    const int q_offset = (w + h + slice_width * ATS_PHASE_BANDS + 512) << ATS_FP;
    const int q2_offset = (w + h + slice2_width * ATS_PHASE_BANDS + 512) << ATS_FP;

    const uint32_t frame_a = s->frame_count * 19U;

    const int * restrict x_proj = s->x_proj;
    const int * restrict y_proj = s->y_proj;
    const int * restrict x_proj2 = s->x_proj2;
    const int * restrict y_proj2 = s->y_proj2;
    const ats_band_t * restrict bands = s->bands;
    const int16_t * restrict wave_lut = s->wave_lut;
    const int edge_glow_active = edge_flash_eff > 0;

#pragma omp parallel num_threads(threads)
    {
#pragma omp for schedule(static) nowait
        for(int i = 0; i < len; i++)
            src_y[i] = Y[i];

#pragma omp for schedule(static) nowait
        for(int i = 0; i < len; i++)
            src_u[i] = U[i];

#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            src_v[i] = V[i];

#pragma omp for schedule(static)
        for(int y = 0; y < h; y++) {
            const int row = y * w;
            const int row_up = (y > 0 ? y - 1 : y) * w;
            const int row_dn = (y < h - 1 ? y + 1 : y) * w;
            const int yq = y_proj[y];
            const int yq2 = y_proj2[y];

            for(int x = 0; x < w; x++) {
                const int i = row + x;

                const int q = x_proj[x] + yq + s->phase_fp + q_offset;
                const int band_raw = q / slice_fp;
                const int band_idx = band_raw & ATS_BAND_MASK;
                const int local_fp = q - band_raw * slice_fp;
                const int local_pix = local_fp >> ATS_FP;

                const int q2 = x_proj2[x] + yq2 + s->phase2_fp + q2_offset;
                const int sub_raw = q2 / slice2_fp;
                const int sub_local_fp = q2 - sub_raw * slice2_fp;
                const int sub_local_pix = sub_local_fp >> ATS_FP;

                const ats_band_t *b = &bands[band_idx];

                const int dx = x - hw;
                const int dy = y - hh;

                const int local_center = local_pix - (slice_width >> 1);
                const int hinge_px = (local_center * b->hinge) >> 7;

                const int sub_idx = (sub_local_pix * ATS_WAVE_LUT_SIZE) / slice2_width;
                const int sub_wave = wave_lut[sub_idx & ATS_WAVE_LUT_MASK];
                const int sub_px = (b->subshift * sub_wave) >> 10;

                int px =
                    hw +
                    ((dx * b->m00 + dy * b->m01) >> ATS_MFP) -
                    b->tx -
                    ((s->nx * hinge_px) >> ATS_FP) -
                    ((s->sx_axis * sub_px) >> ATS_FP);

                int py =
                    hh +
                    ((dx * b->m10 + dy * b->m11) >> ATS_MFP) -
                    b->ty -
                    ((s->ny * hinge_px) >> ATS_FP) -
                    ((s->sy_axis * sub_px) >> ATS_FP);

                px = px < 0 ? 0 : (px >= w ? w - 1 : px);
                py = py < 0 ? 0 : (py >= h ? h - 1 : py);

                const int pi = py * w + px;

                int yy = src_y[pi];
                int uu = src_u[pi];
                int vv = src_v[pi];

                const int dist_a = local_pix;
                const int dist_b = slice_width - local_pix;
                const int edge_dist = dist_a < dist_b ? dist_a : dist_b;

                int edge = edge_width - edge_dist;
                edge = edge > 0 ? edge : 0;

                const int edge_total = edge;

                if(edge_total > 0 && (edge_glow_active || b->glow > 0)) {
                    const int xm = x > 0 ? x - 1 : x;
                    const int xp = x < w - 1 ? x + 1 : x;

                    const int source_edge =
                        ats_absi((int)src_y[row + xp] - (int)src_y[row + xm]) +
                        ats_absi((int)src_y[row_dn + x] - (int)src_y[row_up + x]);

                    const int edge_gate = source_edge < 255 ? source_edge : 255;
                    const int texture = edge_gate > 22 ? edge_gate - 22 : 0;
                    const int break_mask =
                        5 + (int)(((uint32_t)(x * 13 + y * 17 + b->base_phase) + frame_a) & 7U);

                    int glint =
                        (edge_total * edge_flash_eff * (14 + (b->energy >> 6))) >> 12;

                    glint += texture > 0
                        ? ((edge_total * texture * snare_i * b->glow) >> 17)
                        : 0;

                    yy += (glint * break_mask) >> 3;
                }

                if(b->flicker > 0 && hat_i > 10) {
                    const int pat =
                        (int)(((uint32_t)(x * 13 + y * 17 + b->base_phase) + frame_a) & 31U) - 15;
                    const int flicker_gate = 5 + edge_total * 12;
                    const int flicker_y = (pat * hat_i * b->flicker * flicker_gate) >> 18;
                    const int flicker_uv = (pat * hat_i * b->flicker) >> 18;

                    yy += flicker_y;
                    uu += flicker_uv;
                    vv -= flicker_uv >> 1;
                }

                Y[i] = ats_u8(yy);
                U[i] = ats_u8(uu);
                V[i] = ats_u8(vv);
            }
        }
    }
}
