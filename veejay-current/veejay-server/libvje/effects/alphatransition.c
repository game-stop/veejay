/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nwelburg@gmail.com>
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
#include "alphatransition.h"

/* almost the same as masktransition.c, but adding threshold and direction parameters */

vj_effect *alphatransition_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);		/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1255;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1000;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 1;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 0xff;

    ve->defaults[0] = 0; 
    ve->defaults[1] = 50;
	ve->defaults[2] = 0;
	ve->defaults[3] = 30;

    ve->description = "Alpha: Transition Mask";
	ve->sub_format = 1;
    ve->extra_frame = 1;
  	ve->has_user = 0; 
	ve->alpha = FLAG_ALPHA_SRC_A;
		 
	ve->param_description = vje_build_param_list(ve->num_params, "Time Index", "Smooth", "Direction", "Threshold" );

    ve->hints = vje_init_value_hint_list( ve->num_params );

    vje_build_value_hint_list( ve->hints, ve->limits[1][2], 2, "Channel A", "Channel B" );

    return ve;
}


void alphatransition_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    const int time_index = args[0];
    const int duration   = args[1] + 1;
    const int direction  = args[2];
    const int threshold  = args[3];

    const int len = frame->len;
    const int n_threads = vje_advise_num_threads(len);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A  = frame->data[3];

    uint8_t *restrict Y2  = frame2->data[0];
    uint8_t *restrict Cb2 = frame2->data[1];
    uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for (int i = 0; i < len; i++)
    {
        const int a = A[i];

        int alpha;
        if (time_index < a)
        {
            alpha = 0;
        }
        else if (time_index >= (a + duration))
        {
            alpha = 255;
        }
        else
        {
            alpha = (255 * (time_index - a)) / duration;
        }

        if (alpha < threshold)
            alpha = 0;

        const int ia = 255 - alpha;

        if (direction == 0)
        {
            Y[i]  = (uint8_t)((alpha * Y[i]  + ia * Y2[i])  >> 8);
            Cb[i] = (uint8_t)((alpha * Cb[i] + ia * Cb2[i]) >> 8);
            Cr[i] = (uint8_t)((alpha * Cr[i] + ia * Cr2[i]) >> 8);
        }
        else
        {
            Y[i]  = (uint8_t)((ia * Y[i]  + alpha * Y2[i])  >> 8);
            Cb[i] = (uint8_t)((ia * Cb[i] + alpha * Cb2[i]) >> 8);
            Cr[i] = (uint8_t)((ia * Cr[i] + alpha * Cr2[i]) >> 8);
        }
    }
}