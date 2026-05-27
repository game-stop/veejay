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
#include "widthmirror.h"

typedef struct {
    uint8_t *buf[3];
    int n_threads;
    int w;
    int h;
} widthmirror_t;

static inline int widthmirror_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *widthmirror_init(int max_width, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    int max_freq = max_width > 0 ? max_width : 2;
    if(max_freq > 256)
        max_freq = 256;
    if(max_freq < 2)
        max_freq = 2;

    ve->defaults[0] = 4;
    ve->limits[0][0] = 2;
    ve->limits[1][0] = max_freq;

    ve->description = "Width Mirror";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Frequency"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2, max_freq, 6, 22, 2200, 5200, 1800, 25 /* Frequency */
    );

    (void) h;

    return ve;
}

void *widthmirror_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    widthmirror_t *wm = (widthmirror_t*) vj_calloc(sizeof(widthmirror_t));
    if(!wm)
        return NULL;

    const int len = w * h;

    wm->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!wm->buf[0]) {
        free(wm);
        return NULL;
    }

    wm->buf[1] = wm->buf[0] + len;
    wm->buf[2] = wm->buf[1] + len;

    wm->w = w;
    wm->h = h;

    wm->n_threads = vje_advise_num_threads(len);
    if(wm->n_threads < 1)
        wm->n_threads = 1;

    return (void*) wm;
}

void widthmirror_free(void *ptr)
{
    widthmirror_t *wm = (widthmirror_t*) ptr;

    if(!wm)
        return;

    if(wm->buf[0])
        free(wm->buf[0]);

    free(wm);
}

void widthmirror_apply(void *ptr, VJFrame *frame, int *args)
{
    widthmirror_t *wm = (widthmirror_t*) ptr;

    if(!wm || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    if(width != wm->w || height != wm->h)
        return;

    int frequency = widthmirror_clampi(args[0], 2, width);
    if(frequency > 256)
        frequency = 256;

    const int band_w = (width + frequency - 1) / frequency;
    if(band_w <= 1)
        return;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY  = wm->buf[0];
    uint8_t *restrict srcCb = wm->buf[1];
    uint8_t *restrict srcCr = wm->buf[2];

    veejay_memcpy(srcY,  Y,  len);
    veejay_memcpy(srcCb, Cb, len);
    veejay_memcpy(srcCr, Cr, len);

#pragma omp parallel for schedule(static) num_threads(wm->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int band = x / band_w;
            const int band_start = band * band_w;
            const int band_end = widthmirror_clampi(band_start + band_w, 0, width);
            const int local = x - band_start;
            const int src_x = (band & 1)
                ? (band_end - 1 - local)
                : (band_start + local);

            const int sx = widthmirror_clampi(src_x, band_start, band_end - 1);
            const int dst = row + x;
            const int src = row + sx;

            Y[dst]  = srcY[src];
            Cb[dst] = srcCb[src];
            Cr[dst] = srcCr[src];
        }
    }
}