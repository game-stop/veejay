/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "sinoids.h"
#include "motionmap.h"

#define DEFAULT_SINOIDS 70
#define SINOIDS_PI 3.14159265358979323846

#define SINOIDS_PARAMS 8

#define P_MODE        0
#define P_SINOIDS     1
#define P_MIX         2
#define P_CHROMA      3
#define P_PHASE       4
#define P_DRIFT       5
#define P_WARP_DRIVE   6
#define P_PHASE_DRIVE  7

typedef struct {
    uint8_t *block;
    int *sinoids_X;
    uint8_t *sinoid_frame[3];

    int current_sinoids;
    int current_phase_q16;

    float sm_sinoids;
    float sm_mix;
    float sm_chroma;
    float sm_phase;
    float sm_drift;
    float sm_warp_drive;
    float sm_phase_drive;

    float phase_accum;

    int smooth_init;
    int n__;
    int N__;
    int n_threads;
    void *motionmap;
} sinoids_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t sinoids_mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline float sinoids_smoothf(float oldv, float target, float amount)
{
    return oldv + (target - oldv) * amount;
}

static inline int sinoids_signed_speed_from_center(int v)
{
    const int d = clampi(v, 0, 1000) - 500;
    const int a = d < 0 ? -d : d;

    if(a < 5)
        return 0;

    int s = (a * a * 64 + 125000) / 250000;

    if(s < 1)
        s = 1;

    return d < 0 ? -s : s;
}

static inline int sinoids_reflect_x(int x, int width)
{
    const int max = width - 1;
    const int period = max << 1;

    x %= period;

    if(x < 0)
        x += period;

    return x <= max ? x : period - x;
}

vj_effect *sinoids_init(int width, int height)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = SINOIDS_PARAMS;
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

    ve->defaults[P_MODE] = 1;
    ve->defaults[P_SINOIDS] = DEFAULT_SINOIDS;
    ve->defaults[P_MIX] = 1000;
    ve->defaults[P_CHROMA] = 1000;
    ve->defaults[P_PHASE] = 0;
    ve->defaults[P_DRIFT] = 500;
    ve->defaults[P_WARP_DRIVE] = 0;
    ve->defaults[P_PHASE_DRIVE] = 0;

    ve->limits[0][P_MODE] = 0;        ve->limits[1][P_MODE] = 1;
    ve->limits[0][P_SINOIDS] = 0;     ve->limits[1][P_SINOIDS] = 1000;
    ve->limits[0][P_MIX] = 0;         ve->limits[1][P_MIX] = 1000;
    ve->limits[0][P_CHROMA] = 0;      ve->limits[1][P_CHROMA] = 1000;
    ve->limits[0][P_PHASE] = 0;       ve->limits[1][P_PHASE] = 1000;
    ve->limits[0][P_DRIFT] = 0;       ve->limits[1][P_DRIFT] = 1000;
    ve->limits[0][P_WARP_DRIVE] = 0;  ve->limits[1][P_WARP_DRIVE] = 1000;
    ve->limits[0][P_PHASE_DRIVE] = 0; ve->limits[1][P_PHASE_DRIVE] = 1000;

    ve->description = "Sinoids";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->motion = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Sinoids",
        "Mix",
        "Chroma Amount",
        "Phase",
        "Drift Speed",
        "Warp Drive",
        "Phase Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Inplace", "On Copy");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,        VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WARP,            VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, 24,                 760,                14, 54,  800, 3000, 0,    82,
        VJ_BEAT_ALPHA_OR_OPACITY,VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                            420,                1000,               12, 46, 1000, 3600, 0,    72,
        VJ_BEAT_COLOR_AMOUNT,    VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                            480,                1000,               12, 46, 1000, 3600, 0,    68,
        VJ_BEAT_GEOMETRY_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE,                           0,                  1000,               12, 46, 1000, 3600, 0,    64,
        VJ_BEAT_SIGNED_CURVE,    VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS,       80,                 920,                10, 38, 1200, 4200, 0,    56,
        VJ_BEAT_WARP,            VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, 160,                1000,               16, 62,  700, 2800, 0,    92,
        VJ_BEAT_GEOMETRY_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE,                           0,                  1000,               16, 62,  700, 2800, 0,    86
    );

    (void)width;
    (void)height;

    return ve;
}

void *sinoids_malloc(int width, int height)
{
    sinoids_t *s = (sinoids_t*)vj_calloc(sizeof(sinoids_t));

    if(!s)
        return NULL;

    const int len = width * height;
    const size_t x_bytes = sizeof(int) * (size_t)width;
    const size_t frame_bytes = (size_t)len * 3u;
    const size_t total = x_bytes + frame_bytes + 64u;

    s->block = (uint8_t*)vj_malloc(total);

    if(!s->block) {
        free(s);
        return NULL;
    }

    uint8_t *p = s->block;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->sinoids_X = (int*)p;
    p += x_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->sinoid_frame[0] = p;
    s->sinoid_frame[1] = s->sinoid_frame[0] + len;
    s->sinoid_frame[2] = s->sinoid_frame[1] + len;

    s->current_sinoids = -1;
    s->current_phase_q16 = -1;
    s->sm_sinoids = (float)DEFAULT_SINOIDS;
    s->sm_mix = 1000.0f;
    s->sm_chroma = 1000.0f;
    s->sm_phase = 0.0f;
    s->sm_drift = 500.0f;
    s->sm_warp_drive = 0.0f;
    s->sm_phase_drive = 0.0f;
    s->phase_accum = 0.0f;
    s->smooth_init = 0;
    s->n__ = 0;
    s->N__ = 0;
    s->motionmap = NULL;
    s->n_threads = vje_advise_num_threads(len);

    return (void*)s;
}

void sinoids_free(void *ptr)
{
    sinoids_t *s = (sinoids_t*)ptr;

    free(s->block);
    free(s);
}

static void sinoids_recalc(sinoids_t *s, int width, int z, int phase_q16)
{
    z = clampi(z, 0, 1000);
    phase_q16 &= 65535;

    const double zoom = (double)z * 0.1;
    const double phase_add = ((double)phase_q16 * (2.0 * SINOIDS_PI)) / 65536.0;
    int *restrict sinoids_X = s->sinoids_X;

#pragma omp for schedule(static)
    for(int i = 0; i < width; i++) {
        const double phase = (((double)i / (double)width) * 2.0 * SINOIDS_PI) + phase_add;
        sinoids_X[i] = (int)(a_sin(phase) * zoom * 4.0);
    }

#pragma omp single
    {
        s->current_sinoids = z;
        s->current_phase_q16 = phase_q16;
    }
}

static void sinoids_apply_inplace(sinoids_t *s, VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int *restrict offset = s->sinoids_X;

#pragma omp for schedule(static)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int sx = sinoids_reflect_x(x + offset[x], width);
            const int dst = row + x;
            const int src = row + sx;

            Y[dst] = Y[src];
            Cb[dst] = Cb[src];
            Cr[dst] = Cr[src];
        }
    }
}

static void sinoids_apply_copy_mix(sinoids_t *s, VJFrame *frame, int mix_q8, int chroma_q8)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY = s->sinoid_frame[0];
    uint8_t *restrict srcCb = s->sinoid_frame[1];
    uint8_t *restrict srcCr = s->sinoid_frame[2];

    int *restrict offset = s->sinoids_X;

#pragma omp for schedule(static)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int sx = sinoids_reflect_x(x + offset[x], width);
            const int dst = row + x;
            const int src = row + sx;

            Y[dst] = sinoids_mix_u8(srcY[dst], srcY[src], mix_q8);
            Cb[dst] = sinoids_mix_u8(srcCb[dst], srcCb[src], chroma_q8);
            Cr[dst] = sinoids_mix_u8(srcCr[dst], srcCr[src], chroma_q8);
        }
    }
}


static void sinoids_blend_with_snapshot(sinoids_t *s, VJFrame *frame, int mix_q8, int chroma_q8)
{
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY = s->sinoid_frame[0];
    uint8_t *restrict srcCb = s->sinoid_frame[1];
    uint8_t *restrict srcCr = s->sinoid_frame[2];

    if(mix_q8 >= 256 && chroma_q8 >= 256)
        return;

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        Y[i] = sinoids_mix_u8(srcY[i], Y[i], mix_q8);
        Cb[i] = sinoids_mix_u8(srcCb[i], Cb[i], chroma_q8);
        Cr[i] = sinoids_mix_u8(srcCr[i], Cr[i], chroma_q8);
    }
}

void sinoids_apply(void *ptr, VJFrame *frame, int *args)
{
    sinoids_t *s = (sinoids_t*)ptr;

    int mode = args[P_MODE];
    int ns = args[P_SINOIDS];
    int tmp1 = mode;
    int tmp2 = ns;
    int interpolate = 0;
    int motion = 0;

    if(s->motionmap && motionmap_active(s->motionmap)) {
        motionmap_scale_to(
            s->motionmap,
            1,
            1000,
            0,
            0,
            &tmp1,
            &tmp2,
            &(s->n__),
            &(s->N__)
        );

        mode = clampi(tmp1, 0, 1);
        ns = clampi(tmp2, 0, 1000);
        motion = 1;
        interpolate = !(s->n__ == s->N__ || s->n__ == 0);
    }
    else {
        s->n__ = 0;
        s->N__ = 0;
    }

    const int width = frame->width;
    const int len = frame->len;
    const int mix_arg = args[P_MIX];
    const int chroma_arg = args[P_CHROMA];
    const int phase_arg = args[P_PHASE];
    const int drift_arg = args[P_DRIFT];
    const int warp_drive_arg = args[P_WARP_DRIVE];
    const int phase_drive_arg = args[P_PHASE_DRIVE];

    if(!s->smooth_init) {
        s->sm_sinoids = (float)ns;
        s->sm_mix = (float)mix_arg;
        s->sm_chroma = (float)chroma_arg;
        s->sm_phase = (float)phase_arg;
        s->sm_drift = (float)drift_arg;
        s->sm_warp_drive = (float)warp_drive_arg;
        s->sm_phase_drive = (float)phase_drive_arg;
        s->smooth_init = 1;
    }

    const float lane_alpha = 0.30f;

    s->sm_sinoids = sinoids_smoothf(s->sm_sinoids, (float)ns, lane_alpha);
    s->sm_mix = sinoids_smoothf(s->sm_mix, (float)mix_arg, lane_alpha);
    s->sm_chroma = sinoids_smoothf(s->sm_chroma, (float)chroma_arg, lane_alpha);
    s->sm_phase = sinoids_smoothf(s->sm_phase, (float)phase_arg, lane_alpha);
    s->sm_drift = sinoids_smoothf(s->sm_drift, (float)drift_arg, lane_alpha);
    s->sm_warp_drive = sinoids_smoothf(s->sm_warp_drive, (float)warp_drive_arg, lane_alpha);
    s->sm_phase_drive = sinoids_smoothf(s->sm_phase_drive, (float)phase_drive_arg, lane_alpha);

    const int drift_speed = sinoids_signed_speed_from_center((int)(s->sm_drift + 0.5f));

    s->phase_accum += (float)drift_speed * (18.0f / 65536.0f);

    while(s->phase_accum >= 1.0f)
        s->phase_accum -= 1.0f;
    while(s->phase_accum < 0.0f)
        s->phase_accum += 1.0f;

    const int warp_drive_q = clampi((int)(s->sm_warp_drive + 0.5f), 0, 1000);
    const int phase_drive_q = clampi((int)(s->sm_phase_drive + 0.5f), 0, 1000);
    const int direct_warp = (warp_drive_q * 720 + 500) / 1000;
    const int eff_sinoids = clampi((int)(s->sm_sinoids + 0.5f) + direct_warp, 0, 1000);
    const int phase_base_q16 = ((int)(s->sm_phase + 0.5f) * 65535 + 500) / 1000;
    const int phase_direct_q16 = (phase_drive_q * 65535 + 500) / 1000;
    const int phase_drift_q16 = (int)(s->phase_accum * 65536.0f + 0.5f) & 65535;
    const int eff_phase_q16 = (phase_base_q16 + phase_direct_q16 + phase_drift_q16) & 65535;
    const int rebuild = eff_sinoids != s->current_sinoids || eff_phase_q16 != s->current_phase_q16;

    int mix_q8 = ((int)(s->sm_mix + 0.5f) * 256 + 500) / 1000;
    int chroma_q8 = ((int)(s->sm_chroma + 0.5f) * 256 + 500) / 1000;

    mix_q8 = clampi(mix_q8 + (((warp_drive_q + phase_drive_q) * 18 + 500) / 2000), 0, 256);
    chroma_q8 = clampi(chroma_q8 + (((warp_drive_q + phase_drive_q) * 24 + 500) / 2000), 0, 256);

    veejay_memcpy(s->sinoid_frame[0], frame->data[0], len);
    veejay_memcpy(s->sinoid_frame[1], frame->data[1], len);
    veejay_memcpy(s->sinoid_frame[2], frame->data[2], len);

#pragma omp parallel num_threads(s->n_threads)
    {
        if(rebuild)
            sinoids_recalc(s, width, eff_sinoids, eff_phase_q16);

        if(mode == 0) {
            sinoids_apply_inplace(s, frame);
            sinoids_blend_with_snapshot(s, frame, mix_q8, chroma_q8);
        }
        else {
            sinoids_apply_copy_mix(s, frame, mix_q8, chroma_q8);
        }
    }

    if(interpolate)
        motionmap_interpolate_frame(s->motionmap, frame, s->N__, s->n__);

    if(motion)
        motionmap_store_frame(s->motionmap, frame);
}


int sinoids_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void sinoids_set_motionmap(void *ptr, void *priv)
{
    sinoids_t *s = (sinoids_t*)ptr;

    s->motionmap = priv;
}
