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
#include "ghost.h"

typedef struct {
    uint8_t *ghost_buf[4];
    uint8_t *diff_map;
    int diff_period;
    int n_threads;
} ghost_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t ghost_blend255(uint8_t a, uint8_t b, int opacity)
{
    const int inv = 255 - opacity;
    const int x = (int)a * inv + (int)b * opacity;
    return (uint8_t)(((x + 1) + (x >> 8)) >> 8);
}

vj_effect *ghost_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 1;
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

    ve->limits[0][0] = 16; 
    ve->limits[1][0] = 255;
    ve->defaults[0] = 134;

    ve->description = "Motion Ghost";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Opacity");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_MEMORY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 28, 235, 18, 68, 650, 2600, 0, 90
    );

    return ve;
}

void *ghost_malloc(int w, int h)
{
    ghost_t *g = (ghost_t*) vj_calloc(sizeof(ghost_t));

    if(!g)
        return NULL;

    const int len = w * h;

    g->ghost_buf[0] = (uint8_t*) vj_malloc((size_t)len * 4u);

    if(!g->ghost_buf[0]) {
        free(g);
        return NULL;
    }

    g->ghost_buf[1] = g->ghost_buf[0] + len;
    g->ghost_buf[2] = g->ghost_buf[1] + len;
    g->diff_map = g->ghost_buf[2] + len;
    g->diff_period = 0;
    g->n_threads = vje_advise_num_threads(len);

    return (void*) g;
}

void ghost_free(void *ptr)
{
    ghost_t *g = (ghost_t*) ptr;

    free(g->ghost_buf[0]);
    free(g);
}

void ghost_apply(void *ptr, VJFrame *frame, int *args)
{
    ghost_t *g = (ghost_t*) ptr;

    const int opacity = args[0];
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict dY = g->ghost_buf[0];
    uint8_t *restrict dU = g->ghost_buf[1];
    uint8_t *restrict dV = g->ghost_buf[2];
    uint8_t *restrict bm = g->diff_map;

    if(g->diff_period == 0) {
        int strides[4] = { len, len, len, 0 };
        vj_frame_copy(frame->data, g->ghost_buf, strides);
        g->diff_period = 1;
        return;
    }

#pragma omp parallel num_threads(g->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            const int diff = (int)srcY[i] - (int)dY[i];
            const int abs_diff = (diff ^ (diff >> 31)) - (diff >> 31);

            bm[i] = (uint8_t)(-(abs_diff > 0));
        }

#pragma omp for schedule(static)
        for(int y = 1; y < height - 1; y++) {
            const int row = y * width;

#pragma omp simd
            for(int x = 1; x < width - 1; x++) {
                const int i = row + x;
                const int active =
                    bm[i - width - 1] | bm[i - width] | bm[i - width + 1] |
                    bm[i - 1]         | bm[i]         | bm[i + 1] |
                    bm[i + width - 1] | bm[i + width] | bm[i + width + 1];

                if(active) {
                    dY[i] = ghost_blend255(dY[i], srcY[i], opacity);
                    dU[i] = ghost_blend255(dU[i], srcU[i], opacity);
                    dV[i] = ghost_blend255(dV[i], srcV[i], opacity);

                    srcY[i] = dY[i];
                    srcU[i] = dU[i];
                    srcV[i] = dV[i];
                }
            }
        }
    }
}
