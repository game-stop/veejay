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
#include "sobel.h"

vj_effect *sobel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 0;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 2;
    ve->defaults[1] = 1;

    ve->description = "Sobel";
    ve->sub_format = -1;
    ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Mode" );
    return ve;
}

typedef struct 
{
    uint8_t *buf;
    int n_threads;
} sobel_t;

void *sobel_malloc(int w, int h) {
    sobel_t *s = (sobel_t*) vj_malloc(sizeof(sobel_t));
    if(!s) return NULL;
    s->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
    if(!s->buf) {
        free(s);
        return NULL;
    }
    s->n_threads = vje_advise_num_threads(w*h);
    return (void*) s;
}

void sobel_free(void *ptr) {
    sobel_t *s = (sobel_t*) ptr;
    free(s->buf);
    free(s);
}


void sobel_apply( void *ptr, VJFrame *frame, int *args ) {
    sobel_t *s = (sobel_t*) ptr;
    const int t = args[0];
    const int threshold = t * t;
    const int mode = args[1];

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict B = s->buf;

    veejay_memcpy(B, Y, len);

    if (mode == 0) {
        #pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for (int y = 1; y < height - 1; y++) {
            int rowOffset = y * width;
            #pragma omp simd
            for (int x = 1; x < width - 1; x++) {
                int index = rowOffset + x;

                int idx_nw = index - width - 1;
                int idx_n  = index - width;
                int idx_ne = index - width + 1;
                int idx_w  = index - 1;
                int idx_e  = index + 1;
                int idx_sw = index + width - 1;
                int idx_s  = index + width;
                int idx_se = index + width + 1;

                int gx = B[idx_nw] - B[idx_ne] + 2 * (B[idx_w] - B[idx_e]) + B[idx_sw] - B[idx_se];
                int gy = B[idx_nw] + 2 * B[idx_n] + B[idx_ne] - B[idx_sw] - 2 * B[idx_s] - B[idx_se];

                int gradSq = gx * gx + gy * gy;

                Y[index] = (gradSq > threshold) ? pixel_Y_hi_ : pixel_Y_lo_;
            }
        }
    } else if (mode == 1) {
        #pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for (int y = 1; y < height - 1; y++) {
            int rowOffset = y * width;
            #pragma omp simd
            for (int x = 1; x < width - 1; x++) {
                int index = rowOffset + x;

                int idx_nw = index - width - 1;
                int idx_n  = index - width;
                int idx_ne = index - width + 1;
                int idx_w  = index - 1;
                int idx_e  = index + 1;
                int idx_sw = index + width - 1;
                int idx_s  = index + width;
                int idx_se = index + width + 1;

                int gx = B[idx_nw] - B[idx_ne] + 2 * (B[idx_w] - B[idx_e]) + B[idx_sw] - B[idx_se];
                int gy = B[idx_nw] + 2 * B[idx_n] + B[idx_ne] - B[idx_sw] - 2 * B[idx_s] - B[idx_se];

                int abs_gx = (gx ^ (gx >> 31)) - (gx >> 31);
                int abs_gy = (gy ^ (gy >> 31)) - (gy >> 31);
                int grad = abs_gx + abs_gy;

                int norm = (grad * 255) / 1020;

                Y[index] = (norm > t) ? grad : 0;
            }
        }
    } else if (mode == 2) {
        #pragma omp parallel for schedule(static) num_threads(s->n_threads)
        for (int y = 2; y < height - 2; y++) {
            int rowOffset = y * width;
            #pragma omp simd
            for (int x = 2; x < width - 2; x++) {
                int index = rowOffset + x;

                int idx_nw = index - 2 * width - 2;
                int idx_n  = index - 2 * width;
                int idx_ne = index - 2 * width + 2;
                int idx_w  = index - width - 2;
                int idx_e  = index - width + 2;
                int idx_sw = index + 2 * width - 2;
                int idx_s  = index + 2 * width;
                int idx_se = index + 2 * width + 2;

                int gx = -B[idx_nw] - 2*B[idx_n] - B[idx_ne] + B[idx_sw] + 2*B[idx_s] + B[idx_se];
                int gy = -B[idx_nw] - 2*B[idx_w] - B[idx_ne] + B[idx_sw] + 2*B[idx_e] + B[idx_se];

                int abs_gx = (gx ^ (gx >> 31)) - (gx >> 31);
                int abs_gy = (gy ^ (gy >> 31)) - (gy >> 31);
                int grad = abs_gx + abs_gy;

                int norm = (grad * 255) / 2040;

                Y[index] = (norm > t) ? grad : 0;
            }
        }
    }

    veejay_memset(Cb, 128, frame->uv_len);
    veejay_memset(Cr, 128, frame->uv_len);


}
