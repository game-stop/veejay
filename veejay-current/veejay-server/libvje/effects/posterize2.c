/* 
 * Linux VeeJay
 *
 * Copyright(C)2018 Niels Elburg <nwelburg@gmail.com>
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
#include "posterize2.h"

vj_effect *posterize2_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4;
    ve->defaults[1] = 16;
    ve->defaults[2] = 235;
	ve->defaults[3] = 0;

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 256;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 256;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 5;

	ve->parallel = 1;
    ve->description = "Posterize II (Threshold Range)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->param_description = vje_build_param_list(ve->num_params, "Factor", "Min Threshold", "Max Threshold", "Mode");
	
	return ve;	
}

void posterize2_apply(void *ptr, VJFrame *frame, int *args)
{
    const int vfactor = args[0];
    const int t1 = args[1];
    const int t2 = args[2];
    const int mode = args[3];

    const int len = frame->len;
    const int factor = 256 / vfactor;

    uint8_t * restrict Y  = frame->data[0];
    uint8_t * restrict Cb = frame->data[1];
    uint8_t * restrict Cr = frame->data[2];
    uint8_t * restrict A  = frame->data[3];

    const uint8_t lo = pixel_Y_lo_;
    const uint8_t hi = pixel_Y_hi_;
    const uint8_t neutral = 128;

    uint8_t lutY[256];
    uint8_t lutA[256];
    uint8_t mask[256];


    for (int i = 0; i < 256; ++i)
    {
        uint8_t v = (i / factor) * factor;

        lutY[i] = v;
        lutA[i] = v;
        mask[i] = 0;

        switch (mode)
        {
            case 0:
                if (v < t1 || v > t2)
                {
                    lutY[i] = lo;
                    mask[i] = 1;
                }
                break;

            case 1:
                if (v >= t1 && v <= t2)
                {
                    mask[i] = 1;
                }
                break;

            case 2:
                if (v < t1)
                {
                    lutY[i] = lo;
                    mask[i] = 1;
                }
                else if (v > t2)
                {
                    lutY[i] = hi;
                    mask[i] = 1;
                }
                break;

            case 3:
                if (v < t1 || v > t2)
                    lutA[i] = lo;
                break;

            case 4:
                if (v < t1 || v > t2)
                    lutA[i] = A[0];
                break;

            case 5:
                if (v < t1)
                    lutA[i] = lo;
                else if (v > t2)
                    lutA[i] = hi;
                break;
        }
    }

    switch (mode)
    {
        case 0:
        case 1:
        case 2:
        {
            for (int i = 0; i < len; ++i)
            {
                uint8_t y = Y[i];
                uint8_t newY = lutY[y];

                Y[i] = newY;

                if (mask[y])
                {
                    Cb[i] = neutral;
                    Cr[i] = neutral;
                }
            }
            break;
        }

        case 3:
        case 4:
        case 5:
        {
            for (int i = 0; i < len; ++i)
            {
                uint8_t y = Y[i];
                A[i] = lutA[y];
            }
            break;
        }

        default:
            break;
    }
}