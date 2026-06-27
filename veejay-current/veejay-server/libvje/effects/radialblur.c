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
/*
 * Copyright (C) 2000-2004 the xine project
 * 
 * This file is part of xine, a free video player.
 * 
 * xine is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * at your option) any later version.
 * 
 * xine is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA
 *
 * $Id: radialblur.c,v 1.1.1.1 2004/10/27 23:49:01 niels Exp $
 *
 * mplayer's boxblur
 * Copyright (C) 2002 Michael Niedermayer <michaelni@gmx.at>
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "radialblur.h"

#define RADIALBLUR_PARAMS 3

#define P_RADIUS    0
#define P_POWER     1
#define P_DIRECTION 2

typedef struct {
    uint8_t *radial_src[3];
    int n_threads;
} radialblur_t;

static inline int radialblur_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *radialblur_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RADIALBLUR_PARAMS;
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

    ve->defaults[P_RADIUS] = 15;
    ve->defaults[P_POWER] = 0;
    ve->defaults[P_DIRECTION] = 2;

    ve->limits[0][P_RADIUS] = 0;    ve->limits[1][P_RADIUS] = 90;
    ve->limits[0][P_POWER] = 0;     ve->limits[1][P_POWER] = 8;
    ve->limits[0][P_DIRECTION] = 0; ve->limits[1][P_DIRECTION] = 2;

    ve->description = "Radial Blur";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Radius",
        "Power",
        "Direction"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_DIRECTION], P_DIRECTION, "Horizontal", "Vertical", "Both");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                    4,                  68,                 16, 62,  700, 2800, 0,    86,
        VJ_BEAT_INTENSITY,     VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,                        0,                  7,                  4,  14, 3000, 8200, 2200, 22,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                           VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *radialblur_malloc(int w, int h)
{
    radialblur_t *r = (radialblur_t*)vj_calloc(sizeof(radialblur_t));

    if(!r)
        return NULL;

    const int len = w * h;

    r->radial_src[0] = (uint8_t*)vj_malloc((size_t)len * 3u);

    if(!r->radial_src[0]) {
        free(r);
        return NULL;
    }

    r->radial_src[1] = r->radial_src[0] + len;
    r->radial_src[2] = r->radial_src[1] + len;
    r->n_threads = vje_advise_num_threads(len);

    return (void*)r;
}

void radialblur_free(void *ptr)
{
    radialblur_t *r = (radialblur_t*)ptr;

    free(r->radial_src[0]);
    free(r);
}

static void radialblur_h(uint8_t *restrict dst, uint8_t *restrict src, int w, int h, int radius, int power, int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < h; y++)
        veejay_blur2(dst + y * w, src + y * w, w, radius, power, 1, 1);
}

static void radialblur_v(uint8_t *restrict dst, uint8_t *restrict src, int w, int h, int radius, int power, int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int x = 0; x < w; x++)
        veejay_blur2(dst + x, src + x, h, radius, power, w, w);
}

void radialblur_apply(void *ptr, VJFrame *frame, int *args)
{
    radialblur_t *r = (radialblur_t*)ptr;

    const int radius = args[P_RADIUS];
    const int power = args[P_POWER];
    const int direction = args[P_DIRECTION];

    if(radius == 0)
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int uv_width = frame->uv_width;
    const int uv_height = frame->uv_height;
    const int uv_len = frame->ssm ? len : frame->uv_len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY = r->radial_src[0];
    uint8_t *restrict srcU = r->radial_src[1];
    uint8_t *restrict srcV = r->radial_src[2];

    veejay_memcpy(srcY, Y, len);
    veejay_memcpy(srcU, Cb, uv_len);
    veejay_memcpy(srcV, Cr, uv_len);

    switch(direction) {
        case 0:
            radialblur_h(Y,  srcY, width, height, radius, power, r->n_threads);
            radialblur_h(Cb, srcU, uv_width, uv_height, radius, power, r->n_threads);
            radialblur_h(Cr, srcV, uv_width, uv_height, radius, power, r->n_threads);
            break;
        case 1:
            radialblur_v(Y,  srcY, width, height, radius, power, r->n_threads);
            radialblur_v(Cb, srcU, uv_width, uv_height, radius, power, r->n_threads);
            radialblur_v(Cr, srcV, uv_width, uv_height, radius, power, r->n_threads);
            break;
        case 2:
            radialblur_v(Y,  srcY, width, height, radius, power, r->n_threads);
            radialblur_v(Cb, srcU, uv_width, uv_height, radius, power, r->n_threads);
            radialblur_v(Cr, srcV, uv_width, uv_height, radius, power, r->n_threads);

            veejay_memcpy(srcY, Y, len);
            veejay_memcpy(srcU, Cb, uv_len);
            veejay_memcpy(srcV, Cr, uv_len);

            radialblur_h(Y,  srcY, width, height, radius, power, r->n_threads);
            radialblur_h(Cb, srcU, uv_width, uv_height, radius, power, r->n_threads);
            radialblur_h(Cr, srcV, uv_width, uv_height, radius, power, r->n_threads);
            break;
    }
}
