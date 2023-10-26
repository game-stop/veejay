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
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Mode" );
    return ve;
}

typedef struct 
{
    uint8_t *buf;
} sobel_t;

void *sobel_malloc(int w, int h) {
    sobel_t *s = (sobel_t*) vj_malloc(sizeof(sobel_t));
    if(!s) return NULL;
    s->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h );
    if(!s->buf) {
        free(s);
        return NULL;
    }

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
    const int threshold = (args[0] * args[0]);
    const int mode = args[1];

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict B = s->buf;

    veejay_memcpy( B, Y, len );

    if( mode == 0 ) {
        for (int y = 1; y < height - 1; ++y) {
#pragma omp simd
            for (int x = 1; x < width - 1; ++x) {
                const int index = y * width + x;

                const int gx = B[index - width - 1] - B[index - width + 1] + 2 * (B[index - 1] - B[index + 1]) + B[index + width - 1] - B[index + width + 1];
                const int gy = B[index - width - 1] + 2 * B[index - width] + B[index - width + 1] - B[index + width - 1] - 2 * B[index + width] - B[index + width + 1];

                const int gradientMagnitude = gx * gx + gy * gy;

                Y[index] = (gradientMagnitude > threshold) ? pixel_Y_hi_ : pixel_Y_lo_;
            }
        }
    } else if (mode == 1) {
        for (int y = 1; y < height - 1; ++y) {
#pragma omp simd
            for (int x = 1; x < width - 1; ++x) {
                const int index = y * width + x;

                const int gx = B[index - width - 1] - B[index - width + 1] + 2 * (B[index - 1] - B[index + 1]) + B[index + width - 1] - B[index + width + 1];
                const int gy = B[index - width - 1] + 2 * B[index - width] + B[index - width + 1] - B[index + width - 1] - 2 * B[index + width] - B[index + width + 1];

                const int abs_gx = (gx ^ (gx >> 31)) - (gx >> 31);
                const int abs_gy = (gy ^ (gy >> 31)) - (gy >> 31);
                const int gradientMagnitude = abs_gx + abs_gy;

                const int normMagnitude = (int) (((float) gradientMagnitude / 1020) * 255.0);

                Y[index] = (normMagnitude > t) ? gradientMagnitude : 0;
            }
        }
    } else if (mode == 2) {
        for (int y = 2; y < height - 2; ++y) {
#pragma omp simd
            for (int x = 2; x < width - 2; ++x) {
                const int index = y * width + x;

                const int gx = -B[index - width * 2 - 2] - 2 * B[index - width * 2] - B[index - width * 2 + 2]
                    + B[index + width * 2 - 2] + 2 * B[index + width * 2] + B[index + width * 2 + 2];

                const int gy = -B[index - width * 2 - 2] - 2 * B[index - width] - B[index - width * 2 + 2]
                    + B[index + width * 2 - 2] + 2 * B[index + width] + B[index + width * 2 + 2];

                const int abs_gx = (gx ^ (gx >> 31)) - (gx >> 31);
                const int abs_gy = (gy ^ (gy >> 31)) - (gy >> 31);
                const int gradientMagnitude = abs_gx + abs_gy;

                const int normMagnitude = (int)(((float)gradientMagnitude / 2040.0) * 255.0);

                Y[index] = (normMagnitude > t) ? gradientMagnitude : 0;
            }
        }
    }

    veejay_memset( Cb, 128, frame->uv_len );
    veejay_memset( Cr, 128, frame->uv_len );

}
