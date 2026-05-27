/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "swirl.h"
#include <math.h>

typedef struct {
    double *polar_map;
    double *fish_angle;
    int *cached_coords;
    uint8_t *buf[3];
    int v;
    int mode;
    int n_threads;
} swirl_t;

static inline int swirl_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *swirl_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 360;
    ve->defaults[0] = 250;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[1] = 0;

    ve->description = "Swirl";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Degrees",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][1],
        1,
        "Normal",
        "Mirrored"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,     VJ_BEAT_F_CONTINUOUS,                         12,                 360,                8, 30, 1200, 3000, 0,   55,    /* Degrees */
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000  /* Mode */
    );

    (void) w;
    (void) h;

    return ve;
}

void *swirl_malloc(int w, int h)
{
    swirl_t *s = (swirl_t*) vj_calloc(sizeof(swirl_t));
    if(!s)
        return NULL;

    const int len = w * h;
    const int w2 = w >> 1;
    const int h2 = h >> 1;

    s->polar_map = (double*) vj_malloc(sizeof(double) * (size_t)len);
    if(!s->polar_map) {
        free(s);
        return NULL;
    }

    s->fish_angle = (double*) vj_malloc(sizeof(double) * (size_t)len);
    if(!s->fish_angle) {
        free(s->polar_map);
        free(s);
        return NULL;
    }

    s->cached_coords = (int*) vj_malloc(sizeof(int) * (size_t)len);
    if(!s->cached_coords) {
        free(s->fish_angle);
        free(s->polar_map);
        free(s);
        return NULL;
    }

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s->cached_coords);
        free(s->fish_angle);
        free(s->polar_map);
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    for(int y = 0; y < h; y++) {
        const int dy = y - h2;
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const int dx = x - w2;
            const int i = row + x;

            s->polar_map[i] = sqrt((double)(dy * dy + dx * dx));
            s->fish_angle[i] = atan2((double)dy, (double)dx);
            s->cached_coords[i] = i;
        }
    }

    s->v = -1;
    s->mode = -1;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void swirl_free(void *ptr)
{
    swirl_t *s = (swirl_t*) ptr;

    if(!s)
        return;

    if(s->polar_map)
        free(s->polar_map);
    if(s->fish_angle)
        free(s->fish_angle);
    if(s->cached_coords)
        free(s->cached_coords);
    if(s->buf[0])
        free(s->buf[0]);

    free(s);
}

static void swirl_rebuild_cache(swirl_t *s, int width, int height, int degrees, int mode)
{
    const int len = width * height;
    const int w2 = width >> 1;
    const int h2 = height >> 1;
    const double coeff = (double)degrees;

    double *restrict polar_map = s->polar_map;
    double *restrict fish_angle = s->fish_angle;
    int *restrict cached_coords = s->cached_coords;

    if(mode == 0) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int i = 0; i < len; i++) {
            double co;
            double si;

            const double r = polar_map[i];
            const double a = fish_angle[i];

            sin_cos(co, si, a + (r / coeff));

            int px = (int)(r * co) + w2;
            int py = (int)(r * si) + h2;

            px = swirl_clampi(px, 0, width - 1);
            py = swirl_clampi(py, 0, height - 1);

            cached_coords[i] = py * width + px;
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const int my = (y <= h2) ? y : (height - 1 - y);

            for(int x = 0; x < width; x++) {
                double co;
                double si;

                const int mx = (x <= w2) ? x : (width - 1 - x);
                const int qidx = my * width + mx;
                const int idx = row + x;

                const double r = polar_map[qidx];
                const double a = fish_angle[qidx];

                sin_cos(co, si, a + (r / coeff));

                int px = (int)(r * co) + w2;
                int py = (int)(r * si) + h2;

                px = swirl_clampi(px, 0, width - 1);
                py = swirl_clampi(py, 0, height - 1);

                cached_coords[idx] = py * width + px;
            }
        }
    }

    s->v = degrees;
    s->mode = mode;
}

void swirl_apply(void *ptr, VJFrame *frame, int *args)
{
    swirl_t *s = (swirl_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    const int degrees = swirl_clampi(args[0], 1, 360);
    const int mode = args[1] ? 1 : 0;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY  = s->buf[0];
    uint8_t *restrict srcCb = s->buf[1];
    uint8_t *restrict srcCr = s->buf[2];

    if(s->v != degrees || s->mode != mode)
        swirl_rebuild_cache(s, width, height, degrees, mode);

    veejay_memcpy(srcY,  Y,  len);
    veejay_memcpy(srcCb, Cb, len);
    veejay_memcpy(srcCr, Cr, len);

    int *restrict cached_coords = s->cached_coords;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < len; i++) {
        const int idx = cached_coords[i];

        Y[i]  = srcY[idx];
        Cb[i] = srcCb[idx];
        Cr[i] = srcCr[idx];
    }
}