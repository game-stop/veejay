/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "slicer.h"

typedef struct {
    int *slice_xshift;
    int *slice_yshift;
    int *prev_slice_xshift;
    int *prev_slice_yshift;
    uint8_t *tmp_Y;
    uint8_t *tmp_Cb;
    uint8_t *tmp_Cr;
    uint8_t *tmp_A;
    int last_period;
    int current_period;
    uint32_t seed;
    int n_threads;
} slicer_t;


vj_effect *slicer_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1; ve->limits[1][0] = w;
    ve->limits[0][1] = 1; ve->limits[1][1] = h;
    ve->limits[0][2] = 0; ve->limits[1][2] = 128;
    ve->limits[0][3] = 0; ve->limits[1][3] = 500;
    ve->limits[0][4] = 0; ve->limits[1][4] = 1;
    ve->limits[0][5] = 0; ve->limits[1][5] = 100;
    ve->limits[0][6] = 0; ve->limits[1][6] = 100;

    int min_dim = (w < h) ? w : h;
    int max_block_size = min_dim / 2;

    if (max_block_size < 4)
        max_block_size = 4;

    if (max_block_size > 512)
        max_block_size = 512;

    int max_shift = 0;
    int bs = 1;
    while ((bs << 1) <= max_block_size && max_shift < 9) {
        bs <<= 1;
        max_shift++;
    }

    if (max_shift < 2)
        max_shift = 2;
    if (max_shift > 9)
        max_shift = 9;

    ve->limits[0][7] = 2; ve->limits[1][7] = max_shift;


    ve->defaults[0] = 16;
    ve->defaults[1] = 16;
    ve->defaults[2] = 8;
    ve->defaults[3] = 0;
    ve->defaults[4] = 0;
    ve->defaults[5] = 0;
    ve->defaults[6] = 50;
    ve->defaults[7] = 5;
    ve->description = "Slicer";
    ve->sub_format = 1;
    ve->extra_frame = 1;

    ve->param_description = vje_build_param_list(ve->num_params, "Width", "Height", "Shatter", "Period", "Mode", "Smoothness", "Dominance", "Block Size"); 
    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][4], 4, "Clip", "Wrap");

    return ve;
}

static inline uint32_t fast_rand(uint32_t *state) {
    *state = (*state * 1103515245 + 12345) & 0x7fffffff;
    return *state;
}

static void recalc(slicer_t *s, int w, int h, uint8_t *Yinp, int v1, int v2, const int shatter, uint32_t seed) 
{
    int x, y, dx, dy, r;
    uint32_t state = seed;

    int half_v1 = (v1 >> 1) + 1;
    int mask_v1 = (v1 >> 1) - 1;
    if (mask_v1 < 0) mask_v1 = 0;
    int half_v2 = (v2 >> 1) + 1;
    int mask_v2 = (v2 >> 1) - 1;
    if (mask_v2 < 0) mask_v2 = 0;

    for (x = dx = 0; x < w; x++) {
        if (dx <= 0) {
            int base = (fast_rand(&state) % v1) - half_v1;
            float scale = 1.0f + (shatter * 0.02f);
            r = (int)(base * scale);
            dx = 1 + (Yinp[x] & mask_v1);
        } else {
            dx--;
        }
        s->slice_yshift[x] = r;
    }

    for (y = dy = 0; y < h; y++) {
        if (dy <= 0) {
            uint8_t sample = Yinp[y * w];
            int base = (fast_rand(&state) % v2) - half_v2;
            float scale = 1.0f + (shatter * 0.02f);
            r = (int)(base * scale);
            dy = 1 + (sample & mask_v2);
        } else {
            dy--;
        }
        s->slice_xshift[y] = r;
    }
}

void *slicer_malloc(int width, int height)
{
    slicer_t *s = (slicer_t*) vj_calloc(sizeof(slicer_t));
    if (!s) return NULL;

    s->slice_xshift      = (int*) vj_malloc(sizeof(int) * height);
    s->slice_yshift      = (int*) vj_malloc(sizeof(int) * width);
    s->prev_slice_xshift = (int*) vj_malloc(sizeof(int) * height);
    s->prev_slice_yshift = (int*) vj_malloc(sizeof(int) * width);

    size_t frame_sz = width * height * sizeof(uint8_t);
    s->tmp_Y  = (uint8_t*) vj_malloc(frame_sz);
    s->tmp_Cb = (uint8_t*) vj_malloc(frame_sz);
    s->tmp_Cr = (uint8_t*) vj_malloc(frame_sz);
    s->tmp_A  = (uint8_t*) vj_malloc(frame_sz);

    if (!s->slice_xshift || !s->tmp_Y) {
        slicer_free(s);
        return NULL;
    }
    s->n_threads = vje_advise_num_threads(width * height);
    s->last_period = -1;
    s->current_period = 1;
    return s;
}

void slicer_free(void *ptr)
{
    slicer_t *s = (slicer_t*) ptr;
    if(s) {
        if(s->slice_xshift)      free(s->slice_xshift);
        if(s->slice_yshift)      free(s->slice_yshift);
        if(s->prev_slice_xshift) free(s->prev_slice_xshift);
        if(s->prev_slice_yshift) free(s->prev_slice_yshift);
        if(s->tmp_Y)  free(s->tmp_Y);
        if(s->tmp_Cb) free(s->tmp_Cb);
        if(s->tmp_Cr) free(s->tmp_Cr);
        if(s->tmp_A)  free(s->tmp_A);
        free(s);
    }
}

void slicer_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    slicer_t *s = (slicer_t*) ptr;

    const int width  = frame->width;
    const int height = frame->height;

    size_t frame_sz = width * height;

    int val1       = args[0];
    int val2       = args[1];
    int shatter    = args[2];
    int period     = args[3];
    int mode       = args[4];
    int smoothness = args[5];
    int dominance  = args[6];
    int block_shift = args[7];

    if (s->current_period <= 0) {

        veejay_memcpy(s->prev_slice_xshift, s->slice_xshift, sizeof(int) * height);
        veejay_memcpy(s->prev_slice_yshift, s->slice_yshift, sizeof(int) * width);

        uint32_t frame_seed = (uint32_t)(frame->timecode * 1000.0) ^  (val1 * 0x12345678);

        recalc(s, width, height, frame->data[0], val1, val2, shatter, frame_seed);

        for (int y = 0; y < height; y++)
            s->slice_xshift[y] = (s->slice_xshift[y] * (100 - smoothness) +
                 s->prev_slice_xshift[y] * smoothness) / 100;

        for (int x = 0; x < width; x++)
            s->slice_yshift[x] =
                (s->slice_yshift[x] * (100 - smoothness) + s->prev_slice_yshift[x] * smoothness) / 100;

        s->current_period = (period > 0) ? period : 1;
    }

    s->current_period--;

    veejay_memcpy(s->tmp_Y, frame->data[0], frame_sz);
    veejay_memcpy(s->tmp_Cb, frame->data[1], frame_sz);
    veejay_memcpy(s->tmp_Cr, frame->data[2], frame_sz);

    const uint8_t *s1Y  = s->tmp_Y;
    const uint8_t *s1Cb = s->tmp_Cb;
    const uint8_t *s1Cr = s->tmp_Cr;

    const uint8_t *s2Y  = frame2->data[0];
    const uint8_t *s2Cb = frame2->data[1];
    const uint8_t *s2Cr = frame2->data[2];

    uint8_t *dY  = frame->data[0];
    uint8_t *dCb = frame->data[1];
    uint8_t *dCr = frame->data[2];

    #pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for (int y = 0; y < height; y++) {

        int row_offset = y * width;
        float shift_x = (float)s->slice_xshift[y];

        if (mode == 0)
        {
            for (int x = 0; x < width; x++) {

                float shift_y = (float)s->slice_yshift[x];

                float fx = (float)x + shift_x;
                float fy = (float)y + shift_y;

                if (fx < 0 || fx >= width - 1 ||
                    fy < 0 || fy >= height - 1)
                {
                    int idx = row_offset + x;
                    dY[idx]  = s1Y[idx];
                    dCb[idx] = s1Cb[idx];
                    dCr[idx] = s1Cr[idx];
                    continue;
                }

                int ix = (int)fx;
                int iy = (int)fy;

                int p00 = iy * width + ix;

                int chunk_x = ix >> block_shift;
                int chunk_y = iy >> block_shift;

                uint32_t hash = (chunk_x * 104729u) ^ (chunk_y * 131071u);
                int use_s2 = (hash % 100) < dominance;

                const uint8_t *srcY  = use_s2 ? s2Y  : s1Y;
                const uint8_t *srcCb = use_s2 ? s2Cb : s1Cb;
                const uint8_t *srcCr = use_s2 ? s2Cr : s1Cr;

                int idx = row_offset + x;

                dY[idx]  = srcY[ p00 ];
                dCb[idx] = srcCb[ p00 ];
                dCr[idx] =  srcCr[ p00 ];
            }
        }
        else
        {
            for (int x = 0; x < width; x++) {

                float shift_y = (float)s->slice_yshift[x];

                float fx = (float)x + shift_x;
                float fy = (float)y + shift_y;

                if (fx < 0) fx += width * ((-fx / width) + 1);
                if (fy < 0) fy += height * ((-fy / height) + 1);
                if (fx >= width) fx -= width * ((int)(fx / width));
                if (fy >= height) fy -= height * ((int)(fy / height));

                if (fx >= width) fx = width - 1;
                if (fy >= height) fy = height - 1;

                int ix = (int)fx;
                int iy = (int)fy;

                int p00 = iy * width + ix;

                int chunk_x = ix >> block_shift;
                int chunk_y = iy >> block_shift;

                uint32_t hash = (chunk_x * 104729u) ^ (chunk_y * 131071u);
                int use_s2 = (hash % 100) < dominance;

                const uint8_t *srcY  = use_s2 ? s2Y  : s1Y;
                const uint8_t *srcCb = use_s2 ? s2Cb : s1Cb;
                const uint8_t *srcCr = use_s2 ? s2Cr : s1Cr;

                int idx = row_offset + x;

                dY[idx]  = srcY[ p00 ];
                dCb[idx] = srcCb[ p00 ];
                dCr[idx] = srcCr[ p00 ];
            }
        }
    }
}