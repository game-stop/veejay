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
#include "softblur.h"
#ifdef HAVE_ARM
#include <arm_neon.h>
#endif
#ifdef HAVE_ASM_SSE2
#include <emmintrin.h>
#endif

vj_effect *softblur_init(int w,int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2; /* 3*/
    ve->description = "Soft Blur";
    ve->sub_format = -1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Kernel Size");

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][0], 0, "1x3", "3x3","5x5");

    return ve;
}

static void softblur1_apply(VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->len / width;
    uint8_t *restrict Y = frame->data[0];

    #pragma omp parallel for simd
    for (int r = 0; r < height; r++) {
        int row_offset = r * width;

        Y[row_offset] = (Y[row_offset] + Y[row_offset + 1]) / 2;

        for (int c = 1; c < width - 1; c++) {
            Y[row_offset + c] =
                (Y[row_offset + c - 1] +
                 Y[row_offset + c] +
                 Y[row_offset + c + 1]) / 3;
        }

        Y[row_offset + width - 1] =
            (Y[row_offset + width - 2] + Y[row_offset + width - 1]) / 2;
    }
}

static void softblur3_apply(VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->len / width;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *tmp = (uint8_t *) malloc(frame->len);
    if (!tmp) return;

    for (int i = 0; i < frame->len; i++)
        tmp[i] = Y[i];

    for (int r = 0; r < height; r++) {
        int row_offset = r * width;
        for (int c = 0; c < width; c++) {
            int sum = 0;
            int count = 0;

            for (int i = -1; i <= 1; i++) {
                int rr = r + i;
                if (rr < 0 || rr >= height) continue;

                for (int j = -1; j <= 1; j++) {
                    int cc = c + j;
                    if (cc < 0 || cc >= width) continue;

                    sum += tmp[rr * width + cc];
                    count++;
                }
            }
            Y[row_offset + c] = sum / count;
        }
    }

    free(tmp);
}

static void softblur5_apply(VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->len / width;
    uint8_t *restrict Y = frame->data[0];
    uint8_t *tmp = (uint8_t *) malloc(frame->len);
    if (!tmp) return;

    for (int i = 0; i < frame->len; i++)
        tmp[i] = Y[i];

    // apply 5x5 kernel safely
    #pragma omp parallel for
    for (int r = 0; r < height; r++) {
        for (int c = 0; c < width; c++) {
            int sum = 0;
            int count = 0;

            // iterate over kernel
            for (int i = -2; i <= 2; i++) {
                int rr = r + i;
                if (rr < 0 || rr >= height) continue;  // clip row

                for (int j = -2; j <= 2; j++) {
                    int cc = c + j;
                    if (cc < 0 || cc >= width) continue;  // clip col

                    sum += tmp[rr * width + cc];
                    count++;
                }
            }

            Y[r * width + c] = sum / count;
        }
    }

    free(tmp);
}


void softblur_apply(void *ptr, VJFrame *frame, int *args)
{
    int type = args[0];

    switch (type) {
        case 0: softblur1_apply(frame); break;
        case 1: softblur3_apply(frame); break;
        case 2: softblur5_apply(frame); break;
    }
}

void softblur_apply_internal(VJFrame *frame, int type)
{
    switch (type) {
        case 0: softblur1_apply(frame); break;
        case 1: softblur3_apply(frame); break;
        case 2: softblur5_apply(frame); break;
    }
}