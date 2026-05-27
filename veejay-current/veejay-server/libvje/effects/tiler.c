/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
#include "tiler.h"

typedef struct {
    uint8_t *buf[3];
    int n_threads;
} tiler_t;

static inline int tiler_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t tiler_mix_u8(uint8_t src, uint8_t tile, int tile_q8)
{
    return (uint8_t)((((int)src * (256 - tile_q8)) + ((int)tile * tile_q8) + 128) >> 8);
}

vj_effect *tiler_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_tiles = (w < h ? w : h) / 8;
    if(max_tiles < 2)
        max_tiles = 2;

    ve->limits[0][0] = 2;
    ve->limits[1][0] = max_tiles;
    ve->defaults[0] = 2;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 0;

    ve->description = "Tiler";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Tiles",
        "Opacity"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2,   max_tiles > 32 ? 32 : max_tiles, 6, 22, 2200, 5200, 1800, 25, /* Tiles */
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS,                         0,   220,                            8, 30, 1200, 3000, 0,    45  /* Opacity */
    );

    return ve;
}

void *tiler_malloc(int w, int h)
{
    tiler_t *s = (tiler_t*) vj_calloc(sizeof(tiler_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void tiler_free(void *ptr)
{
    tiler_t *s = (tiler_t*) ptr;

    if(!s)
        return;

    if(s->buf[0])
        free(s->buf[0]);

    free(s);
}

void tiler_apply(void *ptr, VJFrame *frame, int *args)
{
    tiler_t *s = (tiler_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int max_tiles = (width < height ? width : height) / 8;
    if(max_tiles < 2)
        max_tiles = 2;

    const int tiles = tiler_clampi(args[0], 2, max_tiles);
    const int opacity = tiler_clampi(args[1], 0, 255);

    const int small_w = (width  + tiles - 1) / tiles;
    const int small_h = (height + tiles - 1) / tiles;
    const int small_len = small_w * small_h;

    if(small_w <= 0 || small_h <= 0 || small_len <= 0 || small_len > len)
        return;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < small_h; y++) {
        const int sy = tiler_clampi(y * tiles, 0, height - 1);
        const int src_row = sy * width;
        const int dst_row = y * small_w;

        for(int x = 0; x < small_w; x++) {
            const int sx = tiler_clampi(x * tiles, 0, width - 1);
            const int src = src_row + sx;
            const int dst = dst_row + x;

            bufY[dst] = srcY[src];
            bufU[dst] = srcU[src];
            bufV[dst] = srcV[src];
        }
    }

    if(opacity >= 255)
        return;

    const int tile_q8 = 256 - ((opacity * 256 + 127) / 255);

    if(tile_q8 >= 256) {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int src_row = y * width;
            const int tile_y = y % small_h;
            const int tile_row = tile_y * small_w;

            for(int x = 0; x < width; x++) {
                const int dst = src_row + x;
                const int tile = tile_row + (x % small_w);

                srcY[dst] = bufY[tile];
                srcU[dst] = bufU[tile];
                srcV[dst] = bufV[tile];
            }
        }
    } else {
#pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for(int y = 0; y < height; y++) {
            const int src_row = y * width;
            const int tile_y = y % small_h;
            const int tile_row = tile_y * small_w;

            for(int x = 0; x < width; x++) {
                const int dst = src_row + x;
                const int tile = tile_row + (x % small_w);

                srcY[dst] = tiler_mix_u8(srcY[dst], bufY[tile], tile_q8);
                srcU[dst] = tiler_mix_u8(srcU[dst], bufU[tile], tile_q8);
                srcV[dst] = tiler_mix_u8(srcV[dst], bufV[tile], tile_q8);
            }
        }
    }
}