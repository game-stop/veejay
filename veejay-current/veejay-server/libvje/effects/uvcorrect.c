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
} uvcorrect_t;

static inline int uvcorrect_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t uvcorrect_u8(int v)
{
    return (uint8_t)uvcorrect_clampi(v, 0, 255);
}

vj_effect *uvcorrect_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 7;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;   ve->limits[1][0] = 360;
    ve->limits[0][1] = 0;   ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;   ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;   ve->limits[1][3] = 100;
    ve->limits[0][4] = 0;   ve->limits[1][4] = 100;
    ve->limits[0][5] = 0;   ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;   ve->limits[1][6] = 255;

    ve->defaults[0] = 1;
    ve->defaults[1] = 128;
    ve->defaults[2] = 128;
    ve->defaults[3] = 10;
    ve->defaults[4] = 10;
    ve->defaults[5] = pixel_U_lo_;
    ve->defaults[6] = pixel_U_hi_;

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
        "Maximum UV"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_COLOR_PHASE,  VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, 1,   360, 8,  30, 1200, 3000, 0, 45, /* Angle */
        VJ_BEAT_DRIFT,        VJ_BEAT_F_CONTINUOUS,                       64,  192, 8,  30, 1200, 3000, 0, 35, /* U Rotate Center */
        VJ_BEAT_DRIFT,        VJ_BEAT_F_CONTINUOUS,                       64,  192, 8,  30, 1200, 3000, 0, 35, /* V Rotate Center */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS,                       0,   72,  8,  30, 1200, 3000, 0, 50, /* Intensity U */
        VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS,                       0,   72,  8,  30, 1200, 3000, 0, 50, /* Intensity V */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_CONTINUOUS,                       0,   128, 8,  30, 1200, 3000, 0, 35, /* Minimum UV */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_CONTINUOUS,                       128, 255, 8,  30, 1200, 3000, 0, 35  /* Maximum UV */
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

            int out_u = (int)floorf(0.5f + ((uterm * co) - (vterm * si)) + 128.0f);
            int out_v = (int)floorf(0.5f + ((vterm * co) + (uterm * si)) + 128.0f);

            out_u = uvcorrect_clampi(out_u, uv_min, uv_max);
            out_v = uvcorrect_clampi(out_v, uv_min, uv_max);

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
                                            int len)
{
    const uint8_t *restrict chroma = uv->chrominance;

#pragma omp parallel for schedule(static) num_threads(uv->n_threads)
    for(int i = 0; i < len; i++) {
        const uint32_t base = ((((uint32_t)u[i]) << 8) | (uint32_t)v[i]) << 1;

        u[i] = chroma[base];
        v[i] = chroma[base + 1];
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

    int angle     = uvcorrect_clampi(args[0], 1, 360);
    int center_u  = uvcorrect_clampi(args[1], 0, 255);
    int center_v  = uvcorrect_clampi(args[2], 0, 255);
    int iu_factor = uvcorrect_clampi(args[3], 0, 100);
    int iv_factor = uvcorrect_clampi(args[4], 0, 100);
    int uv_min    = uvcorrect_clampi(args[5], 0, 255);
    int uv_max    = uvcorrect_clampi(args[6], 0, 255);

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
        uv_len
    );
}