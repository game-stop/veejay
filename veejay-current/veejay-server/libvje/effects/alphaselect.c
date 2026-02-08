/* 
 * Linux VeeJay
 *
 * Copyright(C)2015 Niels Elburg <nelburg@gmail.com>
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

/*
 * derived from greyselect.c
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "alphaselect.h"

vj_effect *alphaselect_init(int w, int h)
{
	vj_effect *ve;
	ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 6;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->defaults[0] = 4500;	/* angle */
	ve->defaults[1] = 255;	/* r */
	ve->defaults[2] = 0;	/* g */
	ve->defaults[3] = 0;	/* b */
	ve->defaults[4] = 0;	/* swap */
	ve->defaults[5] = 0;	/* to alpha */

	ve->limits[0][0] = 1;
	ve->limits[1][0] = 9000;

	ve->limits[0][1] = 0;
	ve->limits[1][1] = 255;

	ve->limits[0][2] = 0;
	ve->limits[1][2] = 255;

	ve->limits[0][3] = 0;
	ve->limits[1][3] = 255;

	ve->limits[0][4] = 0;
	ve->limits[1][4] = 1;

	ve->limits[0][5] = 0;
	ve->limits[1][5] = 1;

	ve->has_user = 0;
	ve->parallel = 1;
	ve->description = "Alpha: Set by chroma key";
	ve->extra_frame = 0;
	ve->sub_format = 1;
	ve->rgb_conv = 1;

	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL | FLAG_ALPHA_SRC_A;

	ve->param_description = vje_build_param_list(ve->num_params,"Angle","Red","Green","Blue", "Invert", "To Alpha");

	return ve;
}

void alphaselect_apply(void *ptr, VJFrame *frame, int *args)
{
    const int i_angle = args[0];
    const int r = args[1];
    const int g = args[2];
    const int b = args[3];
    const int swap = args[4];

    const int len = frame->len;

    uint8_t *Y  = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    uint8_t *A  = frame->data[3];

    int iy = 0, iu = 128, iv = 128;
    _rgb2yuv(r, g, b, iy, iu, iv);

    const float aa = (float)iu;
    const float bb = (float)iv;

    float tmp = sqrtf((aa * aa) + (bb * bb));
    if (tmp < 1e-6f)
        return;

    const int cb = (int)(255.0f * (aa / tmp));
    const int cr = (int)(255.0f * (bb / tmp));

    const float angle = (float)i_angle * (1.0f / 100.0f);
    const float tanv  = tanf((float)M_PI * angle / 180.0f);
    if (!isfinite(tanv))
        return;

    const int accept_angle_tg = (int)(15.0f * tanv);

    if (swap == 0) {

        #pragma omp simd
        for (int pos = 0; pos < len; pos++) {

            const int a0 = A[pos];
            const int a_mask = -(a0 != 0);

            const int xx =
                ((Cb[pos] * cb) + (Cr[pos] * cr)) >> 7;
            const int yy =
                ((Cr[pos] * cb) - (Cb[pos] * cr)) >> 7;

            int val = (xx * accept_angle_tg) >> 4;

			val &= ~(val >> 31);
			val = 255 + ((val - 255) & ((val - 255) >> 31));

            const int s = yy >> 31;
            const int abs_yy = (yy ^ s) - s;

            const int reject = (abs_yy > val);

            const int kill = reject & (a_mask & 1);

            A[pos]  = kill ? 0 : (uint8_t)(val & a_mask);
            Y[pos]  = kill ? pixel_Y_lo_ : Y[pos];
            Cb[pos] = kill ? 128 : Cb[pos];
            Cr[pos] = kill ? 128 : Cr[pos];
        }
    }
    else {

        #pragma omp simd
        for (int pos = 0; pos < len; pos++) {

            const int a0 = A[pos];
            const int a_mask = -(a0 != 0);

            const int xx =
                ((Cb[pos] * cb) + (Cr[pos] * cr)) >> 7;
            const int yy =
                ((Cr[pos] * cb) - (Cb[pos] * cr)) >> 7;

            int val = (xx * accept_angle_tg) >> 4;

			val &= ~(val >> 31);
			val = 255 + ((val - 255) & ((val - 255) >> 31));

            const int s = yy >> 31;
            const int abs_yy = (yy ^ s) - s;

            const int reject = (abs_yy <= val);
            const int kill   = reject & (a_mask & 1);

            A[pos]  = kill ? 0 : (uint8_t)(val & a_mask);
            Y[pos]  = kill ? pixel_Y_lo_ : Y[pos];
            Cb[pos] = kill ? 128 : Cb[pos];
            Cr[pos] = kill ? 128 : Cr[pos];
        }
    }
}
