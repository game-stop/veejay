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
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejaycore/vjmem.h>
#include "opacitythreshold.h"

vj_effect *opacitythreshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[0] = 180;
    ve->defaults[1] = 50;
    ve->defaults[2] = 255;
    ve->description = "Threshold blur with overlay";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Min Threshold", "Max Threshold");
    return ve;
}

typedef struct {
	uint16_t *hblur;
} op_thres_t;

void *opacitythreshold_malloc(int w, int h) {
	op_thres_t *opt = (op_thres_t*) vj_calloc(sizeof(op_thres_t));
	if(!opt)
		return NULL;
	opt->hblur = (uint16_t*) vj_calloc(sizeof(uint16_t) * w * h );
	if(!opt->hblur) {
		free(opt);
		return NULL;
	}
	return (void*) opt;
}

void opacitythreshold_free(void *ptr) {
	op_thres_t *opt = (op_thres_t*) ptr;
	if(opt) {
		if(opt->hblur)
			free(opt->hblur);
		free(opt);
	}
}

void opacitythreshold_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    const int opacity   = args[0];
    const int threshold = args[1];
    const int tmax      = args[2];

    const int width  = frame->width;
    const int height = frame->height;

    const unsigned int op1 = (opacity > 255) ? 255 : opacity;
    const unsigned int op0 = 255 - op1;

    uint8_t *restrict Y   = frame->data[0];
    uint8_t *restrict Cb  = frame->data[1];
    uint8_t *restrict Cr  = frame->data[2];

    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

    const int inv9 = 7282;

	op_thres_t *opt = (op_thres_t*) ptr;

    uint16_t *hblur = opt->hblur;

    for (int y = 0; y < height; y++)
    {
        int row = y * width;

        hblur[row + 0] = Y[row + 0] + Y[row + 1] + Y[row + 0];
#pragma omp simd
        for (int x = 1; x < width - 1; x++)
        {
            hblur[row + x] = Y[row + x - 1] + Y[row + x] + Y[row + x + 1];
        }

        hblur[row + width - 1] = Y[row + width - 2] + Y[row + width - 1] + Y[row + width - 1];
    }


    for (int y = 1; y < height - 1; y++)
    {
        int row = y * width;
        int row_up    = (y - 1) * width;
        int row_down  = (y + 1) * width;

#pragma omp simd
        for (int x = 1; x < width - 1; x++)
        {
            int sum = hblur[row_up + x] + hblur[row + x] + hblur[row_down + x];
            int blur = (sum * inv9) >> 16;

            if (blur < threshold || blur > tmax)
            {
                Y[row + x]  = (op0 * blur + op1 * Y2[row + x]) >> 8;
                Cb[row + x] = (op0 * Cb[row + x] + op1 * Cb2[row + x]) >> 8;
                Cr[row + x] = (op0 * Cr[row + x] + op1 * Cr2[row + x]) >> 8;
            }
        }
    }
}