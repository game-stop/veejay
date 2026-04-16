/* * Linux VeeJay
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
#include <config.h>
#include <time.h>
#include <string.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "boxfit.h"
#include <omp.h>

#define CLAMP(x, min, max) ((x < (min)) ? (min) : ((x > (max)) ? (max) : (x)))

typedef struct
{
    uint8_t *buf[3];
    uint32_t *integralY;
    uint32_t *integralU;
    uint32_t *integralV;
    int n_threads;
} boxfit_t;

vj_effect *boxfit_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 2;
    ve->limits[1][0] = w/8;
    ve->limits[0][1] = 4;
    ve->limits[1][1] = w/4;
    ve->limits[0][2] = 1;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;

    ve->defaults[0] = 8;
    ve->defaults[1] = 40;
    ve->defaults[2] = 128;
    ve->defaults[3] = 1;

    ve->description = "Box Accumulator";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Min Size", "Max Size", "Sensitivity", "Borders" );
    return ve;
}

void *boxfit_malloc(int w, int h)
{
    boxfit_t *s = (boxfit_t *)vj_calloc(sizeof(boxfit_t));
    if (!s) return NULL;

    s->buf[0] = (uint8_t *)vj_malloc(w * h * 3);
    s->integralY = (uint32_t *)vj_malloc(sizeof(uint32_t) * (w + 1) * (h + 1) * 3);
    s->integralU = s->integralY + ((w + 1) * (h + 1));
    s->integralV = s->integralU + ((w + 1) * (h + 1));
    s->n_threads = vje_advise_num_threads(w * h);

    if (!s->buf[0] || !s->integralY) {
        if(s->buf[0]) free(s->buf[0]);
        if(s->integralY) free(s->integralY);
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + (w * h);
    s->buf[2] = s->buf[1] + (w * h);
    return (void *)s;
}

void boxfit_free(void *ptr)
{
    boxfit_t *s = (boxfit_t *)ptr;
    if(s) {
        free(s->buf[0]);
        free(s->integralY);
        free(s);
    }
}

static inline uint32_t get_rect_sum(uint32_t *intC, int stride, int x, int y, int rw, int rh)
{
    int x2 = x + rw;
    int y2 = y + rh;
    return intC[y2 * stride + x2] - intC[y * stride + x2] - intC[y2 * stride + x] + intC[y * stride + x];
}

void boxfit_apply(void *ptr, VJFrame *frame, int *args)
{
    boxfit_t *s = (boxfit_t *)ptr;
    const int min_s = args[0];
    const int max_s = args[1];
    const int sensitivity = CLAMP(args[2], 1, 255);
    const int show_borders = args[3];

    const int width = frame->width;
    const int height = frame->height;
    const int stride = width + 1;

    int size_lut[256];
    for (int i = 0; i < 256; i++) {
        float avg = (float)i + 0.001f;
        float s_f = (float)sensitivity;
        float detail = (avg > s_f) ? (s_f / avg) : (avg / s_f);
        int sz = (((int)(max_s * detail) + 2) >> 2) << 2;
        size_lut[i] = CLAMP(sz, min_s, max_s);
    }

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for (int c = 0; c < 3; c++) {
        uint8_t *src = frame->data[c];
        uint32_t *intC = (c == 0) ? s->integralY : (c == 1 ? s->integralU : s->integralV);

        for (int x = 0; x <= width; x++) intC[x] = 0;

        for (int y = 0; y < height; y++) {
            uint32_t row_sum = 0;
            uint32_t *curr = &intC[(y+1) * stride];
            uint32_t *prev = &intC[y * stride];
            curr[0] = 0;
            for (int x = 0; x < width; x++) {
                row_sum += src[y * width + x];
                curr[x+1] = prev[x+1] + row_sum;
            }
        }
    }

    int i = 0;
    while (i < height) {
        int rem_h = height - i;
        int sh = (min_s < rem_h ? min_s : rem_h);
        int r_avg = get_rect_sum(s->integralY, stride, 0, i, (min_s < width ? min_s : width), sh) / 
                    ((min_s < width ? min_s : width) * sh);
        int row_h = size_lut[r_avg]; 
        if (row_h > rem_h) row_h = rem_h;

        int j = 0;
        while (j < width) {
            int rem_w = width - j;
            int sw = (min_s < rem_w ? min_s : rem_w);
            int b_avg = get_rect_sum(s->integralY, stride, j, i, sw, row_h) / (sw * row_h);
            int box_w = size_lut[b_avg];
            if (box_w > rem_w) box_w = rem_w;

            uint32_t area = box_w * row_h;
            uint8_t valY = (uint8_t)(get_rect_sum(s->integralY, stride, j, i, box_w, row_h) / area);
            uint8_t valU = (uint8_t)(get_rect_sum(s->integralU, stride, j, i, box_w, row_h) / area);
            uint8_t valV = (uint8_t)(get_rect_sum(s->integralV, stride, j, i, box_w, row_h) / area);
            uint8_t borderY = valY >> 1;

            for (int bi = 0; bi < row_h; bi++) {
                int row_off = (i + bi) * width + j;
                uint8_t *pY = frame->data[0] + row_off;
                uint8_t *pU = frame->data[1] + row_off;
                uint8_t *pV = frame->data[2] + row_off;

                if (show_borders) {
                    if (bi == 0 || bi == row_h - 1) {
                        for (int bk = 0; bk < box_w; bk++) pY[bk] = borderY;
                    } else {
                        pY[0] = borderY;
                        for (int bk = 1; bk < box_w - 1; bk++) pY[bk] = valY;
                        pY[box_w - 1] = borderY;
                    }
                } else {
                    for (int bk = 0; bk < box_w; bk++) pY[bk] = valY;
                }

                for (int bk = 0; bk < box_w; bk++) {
                    pU[bk] = valU;
                    pV[bk] = valV;
                }
            }
            j += box_w;
        }
        i += row_h;
    }
}