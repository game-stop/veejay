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
    ve->description = "Soft Luma Key (edge smoothing)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Opacity", "Min Threshold", "Max Threshold");
    return ve;
}

typedef struct {
	uint16_t *hblur;
    int n_threads;
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

static inline uint8_t mix8(uint8_t a, uint8_t b, int w)
{
    return (uint8_t)((w * a + (256 - w) * b + 128) >> 8);
}

void opacitythreshold_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    const int opacity   = args[0];
    const int tmin      = args[1];
    const int tmax      = args[2];

    const int w = frame->width;
    const int h = frame->height;

    uint8_t *Y   = frame->data[0];
    uint8_t *Cb  = frame->data[1];
    uint8_t *Cr  = frame->data[2];

    uint8_t *Y2  = frame2->data[0];
    uint8_t *Cb2 = frame2->data[1];
    uint8_t *Cr2 = frame2->data[2];

    op_thres_t *opt = (op_thres_t*) ptr;
    uint16_t *tmp = opt->hblur;

    const int t_diff = (tmax > tmin) ? (tmax - tmin) : 1;

    #pragma omp parallel num_threads(opt->n_threads)
    {
        #pragma omp for schedule(static)
        for (int y = 0; y < h; y++)
        {
            int row = y * w;
            tmp[row] = (Y[row] + (Y[row] << 1) + Y[row + 1]) >> 2;

            for (int x = 1; x < w - 1; x++)
            {
                tmp[row + x] = (Y[row + x - 1] + (Y[row + x] << 1) + Y[row + x + 1]) >> 2;
            }

            tmp[row + w - 1] = (Y[row + w - 2] + (Y[row + w - 1] << 1) + Y[row + w - 1]) >> 2;
        }

        #pragma omp for schedule(static)
        for (int y = 1; y < h - 1; y++)
        {
            int row = y * w;
            int up  = (y - 1) * w;
            int dn  = (y + 1) * w;

            for (int x = 1; x < w - 1; x++)
            {
                int blur = (tmp[up + x] + (tmp[row + x] << 1) + tmp[dn + x]) >> 2;
                int mask = 0;
                if (blur <= tmin)
                    mask = 0;
                else if (blur >= tmax)
                    mask = 256;
                else
                    mask = ((blur - tmin) * 256) / t_diff;

                mask = (mask * opacity) >> 8;
                int inv = 256 - mask;

                Y[row + x] = (inv * Y[row + x] + mask * Y2[row + x] + 128) >> 8;
                Cb[row + x] = (inv * Cb[row + x] + mask * Cb2[row + x] + 128) >> 8;
                Cr[row + x] = (inv * Cr[row + x] + mask * Cr2[row + x] + 128) >> 8;
            }
        }
    }
}