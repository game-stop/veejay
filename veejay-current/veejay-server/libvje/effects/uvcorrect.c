/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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

/*
 * yuvcorrect_functions.c
 * Common functions between yuvcorrect and yuvcorrect_tune
 * Copyright (C) 2002 Xavier Biquard <xbiquard@free.fr>
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "uvcorrect.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define UVCORRECT_PARAMS 9

#define P_ANGLE        0
#define P_CENTER_U     1
#define P_CENTER_V     2
#define P_INTENSITY_U  3
#define P_INTENSITY_V  4
#define P_MIN_UV       5
#define P_MAX_UV       6
#define P_CHROMA_DRIVE 7
#define P_ROTATE_DRIVE 8

typedef struct {
    uint8_t *chrominance;
    int n_threads;

    int valid;
    int last_angle;
    int last_uc;
    int last_vc;
    int last_iu;
    int last_iv;
    int last_min;
    int last_max;

    int smooth_valid;
    float angle_f;
    float center_u_f;
    float center_v_f;
    float iu_f;
    float iv_f;
    float chroma_drive_f;
    float rotate_drive_f;
} uvcorrect_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t uvcorrect_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}



static inline float uvcorrect_smoothf(float oldv, float target, float coeff)
{
    return oldv + (target - oldv) * coeff;
}



vj_effect *uvcorrect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = UVCORRECT_PARAMS;

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

    ve->limits[0][P_ANGLE] = 1;        ve->limits[1][P_ANGLE] = 360;       ve->defaults[P_ANGLE] = 1;
    ve->limits[0][P_CENTER_U] = 0;     ve->limits[1][P_CENTER_U] = 255;    ve->defaults[P_CENTER_U] = 128;
    ve->limits[0][P_CENTER_V] = 0;     ve->limits[1][P_CENTER_V] = 255;    ve->defaults[P_CENTER_V] = 128;
    ve->limits[0][P_INTENSITY_U] = 0;  ve->limits[1][P_INTENSITY_U] = 100; ve->defaults[P_INTENSITY_U] = 10;
    ve->limits[0][P_INTENSITY_V] = 0;  ve->limits[1][P_INTENSITY_V] = 100; ve->defaults[P_INTENSITY_V] = 10;
    ve->limits[0][P_MIN_UV] = 0;       ve->limits[1][P_MIN_UV] = 255;      ve->defaults[P_MIN_UV] = pixel_U_lo_;
    ve->limits[0][P_MAX_UV] = 0;       ve->limits[1][P_MAX_UV] = 255;      ve->defaults[P_MAX_UV] = pixel_U_hi_;
    ve->limits[0][P_CHROMA_DRIVE] = 0;ve->limits[1][P_CHROMA_DRIVE] = 1000;ve->defaults[P_CHROMA_DRIVE] = 320;
    ve->limits[0][P_ROTATE_DRIVE] = 0;ve->limits[1][P_ROTATE_DRIVE] = 1000;ve->defaults[P_ROTATE_DRIVE] = 180;

    ve->description = "U/V Correction";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_help = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Angle",
        "U Rotate Center",
        "V Rotate Center",
        "Intensity U",
        "Intensity V",
        "Minimum UV",
        "Maximum UV",
        "Chroma Drive",
        "Rotate Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, 1,                  360,                10, 46, 220, 1500, 0,    66,
        VJ_BEAT_DRIFT,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE,                         64,                 192,                8,  30, 500, 2200, 0,    32,
        VJ_BEAT_DRIFT,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE,                         64,                 192,                8,  30, 500, 2200, 0,    32,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, 4,                  100,                10, 42, 260, 1500, 0,    64,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, 4,                  100,                10, 42, 260, 1500, 0,    64,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,   0,    0,    -1000,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,   0,    0,    -1000,
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                           120,                1000,               18, 72, 90,  650, 0,    96,
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                           120,                1000,               18, 72, 90,  650, 0,    92
    );

    return ve;
}

void *uvcorrect_malloc(int w, int h)
{
    uvcorrect_t *uv = (uvcorrect_t*) vj_calloc(sizeof(uvcorrect_t));
    if(!uv)
        return NULL;

    uv->chrominance = (uint8_t*) vj_malloc((size_t)2u * 256u * 256u);
    if(!uv->chrominance) {
        free(uv);
        return NULL;
    }

    uv->n_threads = vje_advise_num_threads(w * h);

    uv->valid = 0;
    uv->smooth_valid = 0;
    uv->angle_f = 1.0f;
    uv->center_u_f = 128.0f;
    uv->center_v_f = 128.0f;
    uv->iu_f = 10.0f;
    uv->iv_f = 10.0f;
    uv->chroma_drive_f = 0.0f;
    uv->rotate_drive_f = 0.0f;

    return uv;
}

void uvcorrect_free(void *ptr)
{
    uvcorrect_t *uv = (uvcorrect_t*) ptr;

    free(uv->chrominance);
    free(uv);
}

static void uvcorrect_rebuild_table(uvcorrect_t *uv,
                                    int angle,
                                    int center_u,
                                    int center_v,
                                    int iu_factor,
                                    int iv_factor,
                                    int uv_min,
                                    int uv_max)
{
    uint8_t *restrict table = uv->chrominance;

    const float uf = (float)iu_factor * 0.1f;
    const float vf = (float)iv_factor * 0.1f;
    const float a = (float)angle * ((float)M_PI / 180.0f);

    const float si = sinf(a);
    const float co = cosf(a);

#pragma omp parallel for schedule(static) num_threads(uv->n_threads)
    for(int u = 0; u < 256; u++) {
        const float uterm = ((float)u - (float)center_u) * uf;

        for(int v = 0; v < 256; v++) {
            const float vterm = ((float)v - (float)center_v) * vf;

            int out_u = (int)(0.5f + ((uterm * co) - (vterm * si)) + 128.0f);
            int out_v = (int)(0.5f + ((vterm * co) + (uterm * si)) + 128.0f);

            out_u = clampi(out_u, uv_min, uv_max);
            out_v = clampi(out_v, uv_min, uv_max);

            const uint32_t base = ((((uint32_t)u) << 8) | (uint32_t)v) << 1;

            table[base]     = uvcorrect_u8(out_u);
            table[base + 1] = uvcorrect_u8(out_v);
        }
    }

    uv->last_angle = angle;
    uv->last_uc = center_u;
    uv->last_vc = center_v;
    uv->last_iu = iu_factor;
    uv->last_iv = iv_factor;
    uv->last_min = uv_min;
    uv->last_max = uv_max;
    uv->valid = 1;
}

static inline int uvcorrect_table_dirty(uvcorrect_t *uv,
                                        int angle,
                                        int center_u,
                                        int center_v,
                                        int iu_factor,
                                        int iv_factor,
                                        int uv_min,
                                        int uv_max)
{
    return !uv->valid ||
           uv->last_angle != angle ||
           uv->last_uc    != center_u ||
           uv->last_vc    != center_v ||
           uv->last_iu    != iu_factor ||
           uv->last_iv    != iv_factor ||
           uv->last_min   != uv_min ||
           uv->last_max   != uv_max;
}

static void uvcorrect_chrominance_treatment(uvcorrect_t *uv,
                                            uint8_t *restrict u,
                                            uint8_t *restrict v,
                                            int len,
                                            int chroma_drive,
                                            int rotate_drive,
                                            int uv_min,
                                            int uv_max)
{
    const uint8_t *restrict chroma = uv->chrominance;

    chroma_drive = clampi(chroma_drive, 0, 1000);
    rotate_drive = clampi(rotate_drive, 0, 1000);

    if(chroma_drive <= 0 && rotate_drive <= 0) {
#pragma omp parallel for schedule(static) num_threads(uv->n_threads)
        for(int i = 0; i < len; i++) {
            const uint32_t base = ((((uint32_t)u[i]) << 8) | (uint32_t)v[i]) << 1;

            u[i] = chroma[base];
            v[i] = chroma[base + 1];
        }
        return;
    }

    const float chroma_boost = (float)chroma_drive * 0.001f;
    const float rotate_deg = (float)rotate_drive * 0.054f;
    const float rot = rotate_deg * ((float)M_PI / 180.0f);

    const int co_q10 = (int)(cosf(rot) * 1024.0f + (rot >= 0.0f ? 0.5f : -0.5f));
    const int si_q10 = (int)(sinf(rot) * 1024.0f + (rot >= 0.0f ? 0.5f : -0.5f));
    int sat_q8 = 256 + (int)(chroma_boost * 260.0f + 0.5f);

    if(sat_q8 < 256)
        sat_q8 = 256;
    else if(sat_q8 > 560)
        sat_q8 = 560;

#pragma omp parallel for schedule(static) num_threads(uv->n_threads)
    for(int i = 0; i < len; i++) {
        const uint32_t base = ((((uint32_t)u[i]) << 8) | (uint32_t)v[i]) << 1;

        int du = (int)chroma[base] - 128;
        int dv = (int)chroma[base + 1] - 128;

        const int ru = ((du * co_q10) - (dv * si_q10) + 512) >> 10;
        const int rv = ((dv * co_q10) + (du * si_q10) + 512) >> 10;

        du = (ru * sat_q8 + 128) >> 8;
        dv = (rv * sat_q8 + 128) >> 8;

        u[i] = uvcorrect_u8(clampi(du + 128, uv_min, uv_max));
        v[i] = uvcorrect_u8(clampi(dv + 128, uv_min, uv_max));
    }
}

void uvcorrect_apply(void *ptr, VJFrame *frame, int *args)
{
    uvcorrect_t *uv = (uvcorrect_t*) ptr;

    const int uv_len = frame->ssm ? frame->len : frame->uv_len;

    int angle         = args[P_ANGLE];
    int center_u      = args[P_CENTER_U];
    int center_v      = args[P_CENTER_V];
    int iu_factor     = args[P_INTENSITY_U];
    int iv_factor     = args[P_INTENSITY_V];
    int uv_min        = args[P_MIN_UV];
    int uv_max        = args[P_MAX_UV];
    int chroma_drive  = args[P_CHROMA_DRIVE];
    int rotate_drive  = args[P_ROTATE_DRIVE];

    const float slow = 0.118f;
    const float fast = 0.176f;

    if(!uv->smooth_valid) {
        uv->angle_f = (float)angle;
        uv->center_u_f = (float)center_u;
        uv->center_v_f = (float)center_v;
        uv->iu_f = (float)iu_factor;
        uv->iv_f = (float)iv_factor;
        uv->chroma_drive_f = (float)chroma_drive;
        uv->rotate_drive_f = (float)rotate_drive;
        uv->smooth_valid = 1;
    } else {
        uv->angle_f = uvcorrect_smoothf(uv->angle_f, (float)angle, fast);
        uv->center_u_f = uvcorrect_smoothf(uv->center_u_f, (float)center_u, slow);
        uv->center_v_f = uvcorrect_smoothf(uv->center_v_f, (float)center_v, slow);
        uv->iu_f = uvcorrect_smoothf(uv->iu_f, (float)iu_factor, fast * 0.92f);
        uv->iv_f = uvcorrect_smoothf(uv->iv_f, (float)iv_factor, fast * 0.92f);
        uv->chroma_drive_f = uvcorrect_smoothf(uv->chroma_drive_f, (float)chroma_drive, fast * 1.08f);
        uv->rotate_drive_f = uvcorrect_smoothf(uv->rotate_drive_f, (float)rotate_drive, fast * 1.08f);
    }

    angle = clampi((int)(uv->angle_f + 0.5f), 1, 360);
    center_u = clampi((int)(uv->center_u_f + 0.5f), 0, 255);
    center_v = clampi((int)(uv->center_v_f + 0.5f), 0, 255);
    iu_factor = clampi((int)(uv->iu_f + 0.5f), 0, 100);
    iv_factor = clampi((int)(uv->iv_f + 0.5f), 0, 100);
    chroma_drive = clampi((int)(uv->chroma_drive_f + 0.5f), 0, 1000);
    rotate_drive = clampi((int)(uv->rotate_drive_f + 0.5f), 0, 1000);

    if(uv_min > uv_max) {
        const int tmp = uv_min;
        uv_min = uv_max;
        uv_max = tmp;
    }

    if(uvcorrect_table_dirty(uv, angle, center_u, center_v, iu_factor, iv_factor, uv_min, uv_max)) {
        uvcorrect_rebuild_table(
            uv,
            angle,
            center_u,
            center_v,
            iu_factor,
            iv_factor,
            uv_min,
            uv_max
        );
    }

    uvcorrect_chrominance_treatment(
        uv,
        frame->data[1],
        frame->data[2],
        uv_len,
        chroma_drive,
        rotate_drive,
        uv_min,
        uv_max
    );
}
