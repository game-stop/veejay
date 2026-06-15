/*
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "mirrordistortion.h"

#include <stdint.h>

#define MIRRORDISTORTION_PARAMS 3

#define P_DISTORTION 0
#define P_OFFSET_X   1
#define P_OFFSET_Y   2

#define MD_TRIG_SHIFT 14
#define MD_TRIG_SCALE (1 << MD_TRIG_SHIFT)

typedef struct {
    int16_t *sin_y;
    int16_t *cos_x;
    int *dx_y;
    int *dy_x;
    uint8_t *buf[3];
    int distortion_key;
    int offset_x_key;
    int offset_y_key;
    int n_threads;
} mirror_distortion_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int mirrordistortion_mul_q14(int a, int b)
{
    const int p = a * b;
    return (p + (p >= 0 ? (MD_TRIG_SCALE >> 1) : -(MD_TRIG_SCALE >> 1))) >> MD_TRIG_SHIFT;
}

vj_effect *mirrordistortion_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MIRRORDISTORTION_PARAMS;
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

    ve->limits[0][P_DISTORTION] = 0; ve->limits[1][P_DISTORTION] = 100;   ve->defaults[P_DISTORTION] = 10;
    ve->limits[0][P_OFFSET_X] = 0;   ve->limits[1][P_OFFSET_X] = w * 2;   ve->defaults[P_OFFSET_X] = w;
    ve->limits[0][P_OFFSET_Y] = 0;   ve->limits[1][P_OFFSET_Y] = h * 2;   ve->defaults[P_OFFSET_Y] = h;

    ve->description = "Mirror Distortion";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Distortion", "Offset X", "Offset Y");

    int x_lo = w - (w >> 2);
    int x_hi = w + (w >> 2);
    int y_lo = h - (h >> 2);
    int y_hi = h + (h >> 2);

    if(x_lo < 0)
        x_lo = 0;
    if(x_hi > w * 2)
        x_hi = w * 2;
    if(y_lo < 0)
        y_lo = 0;
    if(y_hi > h * 2)
        y_hi = h * 2;

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, 4,    78,   14, 54,  800, 3000, 0,    78,
        VJ_BEAT_DRIFT,              VJ_BEAT_F_CONTINUOUS,                                                          w / 2, w + (w / 2), 12, 46,  900, 3400, 0,    64,
        VJ_BEAT_DRIFT,              VJ_BEAT_F_CONTINUOUS,                                                          h / 2, h + (h / 2), 12, 46,  900, 3400, 0,    64
    );

    return ve;
}

void mirrordistortion_free(void *ptr)
{
    mirror_distortion_t *m = (mirror_distortion_t*) ptr;

    if(m) {
        free(m->sin_y);
        free(m->dx_y);
        free(m->buf[0]);
        free(m);
    }
}

void *mirrordistortion_malloc(int w, int h)
{
    mirror_distortion_t *m = (mirror_distortion_t*) vj_calloc(sizeof(mirror_distortion_t));

    if(!m)
        return NULL;

    const int len = w * h;

    m->sin_y = (int16_t*) vj_malloc(sizeof(int16_t) * (size_t)(w + h));
    m->dx_y = (int*) vj_malloc(sizeof(int) * (size_t)(w + h));
    m->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);

    if(!m->sin_y || !m->dx_y || !m->buf[0]) {
        mirrordistortion_free(m);
        return NULL;
    }

    m->cos_x = m->sin_y + h;
    m->dy_x = m->dx_y + h;
    m->buf[1] = m->buf[0] + len;
    m->buf[2] = m->buf[1] + len;
    m->distortion_key = -1;
    m->offset_x_key = 0x7fffffff;
    m->offset_y_key = 0x7fffffff;
    m->n_threads = vje_advise_num_threads(len);

    return (void*) m;
}

static void mirrordistortion_update_trig(mirror_distortion_t *m, int w, int h, int distortion)
{
    const float dist = (float)distortion * 0.01f;

    for(int y = 0; y < h; y++)
        m->sin_y[y] = (int16_t)(a_sin((float)y * dist) * (float)MD_TRIG_SCALE);

    for(int x = 0; x < w; x++)
        m->cos_x[x] = (int16_t)(a_cos((float)x * dist) * (float)MD_TRIG_SCALE);

    m->distortion_key = distortion;
    m->offset_x_key = 0x7fffffff;
    m->offset_y_key = 0x7fffffff;
}

static void mirrordistortion_update_offsets(mirror_distortion_t *m, int w, int h, int offset_x, int offset_y)
{
    if(offset_x != m->offset_x_key) {
        for(int y = 0; y < h; y++)
            m->dx_y[y] = mirrordistortion_mul_q14(offset_x, m->sin_y[y]);

        m->offset_x_key = offset_x;
    }

    if(offset_y != m->offset_y_key) {
        for(int x = 0; x < w; x++)
            m->dy_x[x] = mirrordistortion_mul_q14(offset_y, m->cos_x[x]);

        m->offset_y_key = offset_y;
    }
}

void mirrordistortion_apply(void *ptr, VJFrame *frame, int *args)
{
    mirror_distortion_t *m = (mirror_distortion_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;
    const int distortion = args[P_DISTORTION];
    const int offset_x = clampi(args[P_OFFSET_X], 0, w * 2) - w;
    const int offset_y = clampi(args[P_OFFSET_Y], 0, h * 2) - h;
    const int w1 = w - 1;
    const int h1 = h - 1;

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict srcY = m->buf[0];
    uint8_t *restrict srcU = m->buf[1];
    uint8_t *restrict srcV = m->buf[2];

    if(distortion != m->distortion_key)
        mirrordistortion_update_trig(m, w, h, distortion);

    mirrordistortion_update_offsets(m, w, h, offset_x, offset_y);

    veejay_memcpy(srcY, dstY, len);
    veejay_memcpy(srcU, dstU, len);
    veejay_memcpy(srcV, dstV, len);

#pragma omp parallel for num_threads(m->n_threads) schedule(static)
    for(int y = 0; y < h; y++) {
        const int row = y * w;
        const int dx = m->dx_y[y];
        uint8_t *restrict outY = dstY + row;
        uint8_t *restrict outU = dstU + row;
        uint8_t *restrict outV = dstV + row;

        for(int x = 0; x < w; x++) {
            int sx = x + dx;
            int sy = y + m->dy_x[x];

            sx = sx < 0 ? 0 : (sx > w1 ? w1 : sx);
            sy = sy < 0 ? 0 : (sy > h1 ? h1 : sy);

            const int src_idx = sy * w + sx;

            outY[x] = srcY[src_idx];
            outU[x] = srcU[src_idx];
            outV[x] = srcV[src_idx];
        }
    }
}
