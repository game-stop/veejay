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
#include "transform.h"

typedef struct {
    uint8_t *buf[3];
    int *xmap;
    int *ymap;
    int n_threads;
    int max_size;
} transform_t;

static inline int transform_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *transform_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_size = height / 16;
    if(max_size < 1)
        max_size = 1;

    ve->defaults[0] = transform_clampi(5, 1, max_size);
    ve->limits[0][0] = 1;
    ve->limits[1][0] = max_size;

    ve->description = "Transform Cubics";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Cubics"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE,
        1, max_size, 6, 22, 2200, 5200, 1800, 25 /* Cubics */
    );

    (void) width;

    return ve;
}

void *transform_malloc(int w, int h)
{
    transform_t *t = (transform_t*) vj_calloc(sizeof(transform_t));
    if(!t)
        return NULL;

    const int len = w * h;

    t->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!t->buf[0]) {
        free(t);
        return NULL;
    }

    t->buf[1] = t->buf[0] + len;
    t->buf[2] = t->buf[1] + len;

    t->xmap = (int*) vj_malloc(sizeof(int) * (size_t)(w + h));
    if(!t->xmap) {
        free(t->buf[0]);
        free(t);
        return NULL;
    }

    t->ymap = t->xmap + w;

    t->max_size = h / 16;
    if(t->max_size < 1)
        t->max_size = 1;

    t->n_threads = vje_advise_num_threads(len);

    return (void*) t;
}

void transform_free(void *ptr)
{
    transform_t *t = (transform_t*) ptr;

    if(!t)
        return;

    if(t->buf[0])
        free(t->buf[0]);

    if(t->xmap)
        free(t->xmap);

    free(t);
}

static void transform_build_map(int *restrict map, int n, int size)
{
    const int hsize = size >> 1;

    for(int i = 0; i < n; i++) {
        const int off = i % size;
        int v;

        if(((i / size) & 1) != 0)
            v = i - off + hsize;
        else
            v = i + off - hsize;

        map[i] = transform_clampi(v, 0, n - 1);
    }
}

void transform_apply(void *ptr, VJFrame *frame, int *args)
{
    transform_t *t = (transform_t*) ptr;

    if(!t || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    const int size = transform_clampi(args[0], 1, t->max_size);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY  = t->buf[0];
    uint8_t *restrict srcCb = t->buf[1];
    uint8_t *restrict srcCr = t->buf[2];

    veejay_memcpy(srcY,  Y,  len);
    veejay_memcpy(srcCb, Cb, len);
    veejay_memcpy(srcCr, Cr, len);

    transform_build_map(t->xmap, width, size);
    transform_build_map(t->ymap, height, size);

#pragma omp parallel for schedule(static) num_threads(t->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int sy = t->ymap[y] * width;

        for(int x = 0; x < width; x++) {
            const int dst = row + x;
            const int src = sy + t->xmap[x];

            Y[dst]  = srcY[src];
            Cb[dst] = srcCb[src];
            Cr[dst] = srcCr[src];
        }
    }
}