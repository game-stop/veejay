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

#define UVCORRECT_PARAMS 11

#define P_ANGLE        0
#define P_CENTER_U     1
#define P_CENTER_V     2
#define P_INTENSITY_U  3
#define P_INTENSITY_V  4
#define P_MIN_UV       5
#define P_MAX_UV       6
#define P_BEAT_CHROMA  7
#define P_BEAT_ROTATE  8
#define P_BEAT_PUSH    9
#define P_BEAT_SMOOTH 10

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

    float beat_env;
    float beat_kick;
} uvcorrect_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t uvcorrect_u8(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline int uvcorrect_beat_shape_q8(int beat_push)
{
    beat_push = clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push * 255 + 500000) / 1000000;
    return clampi(sq, 0, 255);
}

static inline void uvcorrect_update_beat(uvcorrect_t *uv, int beat_push, int beat_smooth)
{
    const float target = (float)uvcorrect_beat_shape_q8(beat_push) * (1.0f / 255.0f);
    const float prev = uv->beat_env;

    beat_smooth = clampi(beat_smooth, 0, 1000);

    const float inv = 1.0f - ((float)beat_smooth * 0.001f);
    const float attack = 0.14f + inv * 0.34f;
    const float release = 0.025f + inv * 0.110f;

    if(target > uv->beat_env)
        uv->beat_env += (target - uv->beat_env) * attack;
    else
        uv->beat_env += (target - uv->beat_env) * release;

    if(uv->beat_env < 0.0001f)
        uv->beat_env = 0.0f;
    else if(uv->beat_env > 1.0f)
        uv->beat_env = 1.0f;

    {
        float rise = uv->beat_env - prev;
        if(rise < 0.0f)
            rise = 0.0f;

        uv->beat_kick = uv->beat_kick * 0.62f + rise * 1.80f;

        if(uv->beat_kick > 1.0f)
            uv->beat_kick = 1.0f;
        else if(uv->beat_kick < 0.0001f)
            uv->beat_kick = 0.0f;
    }
}

vj_effect *uvcorrect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = UVCORRECT_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_ANGLE] = 1;        ve->limits[1][P_ANGLE] = 360;       ve->defaults[P_ANGLE] = 1;
    ve->limits[0][P_CENTER_U] = 0;     ve->limits[1][P_CENTER_U] = 255;    ve->defaults[P_CENTER_U] = 128;
    ve->limits[0][P_CENTER_V] = 0;     ve->limits[1][P_CENTER_V] = 255;    ve->defaults[P_CENTER_V] = 128;
    ve->limits[0][P_INTENSITY_U] = 0;  ve->limits[1][P_INTENSITY_U] = 100; ve->defaults[P_INTENSITY_U] = 10;
    ve->limits[0][P_INTENSITY_V] = 0;  ve->limits[1][P_INTENSITY_V] = 100; ve->defaults[P_INTENSITY_V] = 10;
    ve->limits[0][P_MIN_UV] = 0;       ve->limits[1][P_MIN_UV] = 255;      ve->defaults[P_MIN_UV] = pixel_U_lo_;
    ve->limits[0][P_MAX_UV] = 0;       ve->limits[1][P_MAX_UV] = 255;      ve->defaults[P_MAX_UV] = pixel_U_hi_;
    ve->limits[0][P_BEAT_CHROMA] = 0;  ve->limits[1][P_BEAT_CHROMA] = 1000;ve->defaults[P_BEAT_CHROMA] = 320;
    ve->limits[0][P_BEAT_ROTATE] = 0;  ve->limits[1][P_BEAT_ROTATE] = 1000;ve->defaults[P_BEAT_ROTATE] = 180;
    ve->limits[0][P_BEAT_PUSH] = 0;    ve->limits[1][P_BEAT_PUSH] = 1000;  ve->defaults[P_BEAT_PUSH] = 0;
    ve->limits[0][P_BEAT_SMOOTH] = 0;  ve->limits[1][P_BEAT_SMOOTH] = 1000;ve->defaults[P_BEAT_SMOOTH] = 560;

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
        "Beat Chroma",
        "Beat Rotate",
        "Beat Push",
        "Beat Smooth"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_WRAP | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE, 1,                  360,                6,  20, 1800, 4200, 900,  26,    /* Angle */
        VJ_BEAT_DRIFT,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE,                      96,                 160,                5,  18, 2200, 5200, 1200, 16,    /* U Rotate Center */
        VJ_BEAT_DRIFT,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE,                      96,                 160,                5,  18, 2200, 5200, 1200, 16,    /* V Rotate Center */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE,                      0,                  64,                 6,  22, 1800, 4200, 900,  30,    /* Intensity U */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE,                      0,                  64,                 6,  22, 1800, 4200, 900,  30,    /* Intensity V */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Minimum UV */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Maximum UV */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_REJECT,                                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Chroma */
        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_REJECT,                                                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Beat Rotate */
        VJ_BEAT_KICK,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,                                             0,                  760,                18, 76, 80,   760,  0,    100,   /* Beat Push */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY,                                                                280,                820,                5,  18, 2200, 5200, 1200, 18     /* Beat Smooth */
    );

    (void) w;
    (void) h;

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
    if(uv->n_threads < 1)
        uv->n_threads = 1;

    uv->valid = 0;
    uv->beat_env = 0.0f;
    uv->beat_kick = 0.0f;

    return uv;
}

void uvcorrect_free(void *ptr)
{
    uvcorrect_t *uv = (uvcorrect_t*) ptr;

    if(!uv)
        return;

    if(uv->chrominance)
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
                                            int beat_chroma,
                                            int beat_rotate,
                                            int beat_push,
                                            int beat_smooth,
                                            int uv_min,
                                            int uv_max)
{
    const uint8_t *restrict chroma = uv->chrominance;

    beat_chroma = clampi(beat_chroma, 0, 1000);
    beat_rotate = clampi(beat_rotate, 0, 1000);

    uvcorrect_update_beat(uv, beat_push, beat_smooth);

    const float beat = uv->beat_env;
    const float kick = uv->beat_kick;
    const float drive = beat * beat + kick * 0.35f;

    if(drive <= 0.0001f || (beat_chroma <= 0 && beat_rotate <= 0)) {
#pragma omp parallel for schedule(static) num_threads(uv->n_threads)
        for(int i = 0; i < len; i++) {
            const uint32_t base = ((((uint32_t)u[i]) << 8) | (uint32_t)v[i]) << 1;

            u[i] = chroma[base];
            v[i] = chroma[base + 1];
        }
        return;
    }

    const float chroma_boost = ((float)beat_chroma * 0.001f) * drive;
    const float rotate_deg = ((float)beat_rotate * 0.001f) * drive * 28.0f;
    const float rot = rotate_deg * ((float)M_PI / 180.0f);

    const int co_q10 = (int)(cosf(rot) * 1024.0f + (rot >= 0.0f ? 0.5f : -0.5f));
    const int si_q10 = (int)(sinf(rot) * 1024.0f + (rot >= 0.0f ? 0.5f : -0.5f));
    int sat_q8 = 256 + (int)(chroma_boost * 178.0f + 0.5f);

    if(sat_q8 < 256)
        sat_q8 = 256;
    else if(sat_q8 > 448)
        sat_q8 = 448;

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

    if(!uv || !frame || !args || !frame->data[1] || !frame->data[2])
        return;

    const int uv_len = frame->ssm ? frame->len : frame->uv_len;
    if(uv_len <= 0)
        return;

    int angle        = clampi(args[P_ANGLE], 1, 360);
    int center_u     = clampi(args[P_CENTER_U], 0, 255);
    int center_v     = clampi(args[P_CENTER_V], 0, 255);
    int iu_factor    = clampi(args[P_INTENSITY_U], 0, 100);
    int iv_factor    = clampi(args[P_INTENSITY_V], 0, 100);
    int uv_min       = clampi(args[P_MIN_UV], 0, 255);
    int uv_max       = clampi(args[P_MAX_UV], 0, 255);
    int beat_chroma  = clampi(args[P_BEAT_CHROMA], 0, 1000);
    int beat_rotate  = clampi(args[P_BEAT_ROTATE], 0, 1000);
    int beat_push    = clampi(args[P_BEAT_PUSH], 0, 1000);
    int beat_smooth  = clampi(args[P_BEAT_SMOOTH], 0, 1000);

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
        beat_chroma,
        beat_rotate,
        beat_push,
        beat_smooth,
        uv_min,
        uv_max
    );
}
