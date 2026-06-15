/* 
 * Linux VeeJay
 *
 * Copyright(C)2010 Niels Elburg <nwelburg@gmail.com>
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

// Uses: ctmf
/*
 * ctmf.c - Constant-time median filtering
 * Copyright (C) 2006  Simon Perreault
 *
 * Reference: S. Perreault and P. Hébert, "Median Filtering in Constant Time",
 * IEEE Transactions on Image Processing, September 2007.
 *
 * This program has been obtained from http://nomis80.org/ctmf.html. No patent
 * covers this program, although it is subject to the following license:
 */

#include <unistd.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include <ctmf/ctmf.h>
#include "median.h"

#define MEDIANFILTER_PARAMS 1

#define P_RADIUS 0

static long l2_cache_size_;

typedef struct {
    uint8_t *buffer[3];
} medianfilter_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *medianfilter_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MEDIANFILTER_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

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

    ve->limits[0][P_RADIUS] = 0;
    ve->limits[1][P_RADIUS] = 127;
    ve->defaults[P_RADIUS] = 3;

    ve->description = "Constant-time median filtering";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Radius");

#ifdef _SC_LEVEL2_CACHE_SIZE
    l2_cache_size_ = sysconf(_SC_LEVEL2_CACHE_SIZE);
    if(l2_cache_size_ < 0)
        l2_cache_size_ = 0;
#else
    l2_cache_size_ = 0;
#endif
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 1, 24, 4, 14, 3000, 8200, 2200, 22
    );

    return ve;
}

void *medianfilter_malloc(int w, int h)
{
    medianfilter_t *m = (medianfilter_t*) vj_calloc(sizeof(medianfilter_t));

    if(!m)
        return NULL;

    const int len = w * h;

    m->buffer[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 3);

    if(!m->buffer[0]) {
        free(m);
        return NULL;
    }

    m->buffer[1] = m->buffer[0] + len;
    m->buffer[2] = m->buffer[1] + len;

    return (void*) m;
}

void medianfilter_free(void *ptr)
{
    medianfilter_t *m = (medianfilter_t*) ptr;

    free(m->buffer[0]);
    free(m);
}

void medianfilter_apply(void *ptr, VJFrame *frame, int *args)
{
    medianfilter_t *m = (medianfilter_t*) ptr;

    const int radius = args[P_RADIUS];

    if(radius == 0)
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int uv_len = frame->uv_len;
    const int uv_width = frame->uv_width;
    const int uv_height = frame->uv_height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t **buffer = m->buffer;

    ctmf(Y,  buffer[0], width,    height,    width,    width,    radius, 1, l2_cache_size_);
    ctmf(Cb, buffer[1], uv_width, uv_height, uv_width, uv_width, radius, 1, l2_cache_size_);
    ctmf(Cr, buffer[2], uv_width, uv_height, uv_width, uv_width, radius, 1, l2_cache_size_);

    veejay_memcpy(Y,  buffer[0], len);
    veejay_memcpy(Cb, buffer[1], uv_len);
    veejay_memcpy(Cr, buffer[2], uv_len);
}
