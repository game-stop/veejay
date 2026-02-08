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
#include "coloradjust.h"

vj_effect *coloradjust_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 360;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 256;

	ve->limits[0][2] = 0;
	ve->limits[1][2] = 1024;

    ve->defaults[0] = 50;
    ve->defaults[1] = 50;
	ve->defaults[2] = 256;

    ve->param_description = vje_build_param_list( ve->num_params, "Degrees", "Intensity", "Exposure" );
    ve->description = "Exposure, Hue and Saturation";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;
    ve->parallel = 0;
    return ve;
}

void coloradjust_apply(void *ptr, VJFrame *frame, int *args) {
    int val = args[0];
    int _degrees = args[1];
    int exposureValue = args[2];

    const int len = frame->len;
    uint8_t *Y = frame->data[0];
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    float hue = ((float)val / 180.0f) * (float)M_PI;
    float sat = ((float)_degrees * 0.01f);

    const int s = (int)rintf(a_sin(hue) * (1 << 16) * sat);
    const int c = (int)rintf(a_cos(hue) * (1 << 16) * sat);

    if (exposureValue > 0) {
        float powValue = (float)exposureValue / 256.0f;
#pragma omp simd
        for (int i = 0; i < len; i++) {
            int y = (int)(Y[i] * powValue);
            y &= ~(y >> 31);
            int diff = y - 255;
            y = 255 + (diff & (diff >> 31));
            Y[i] = (uint8_t)y;
        }
    }

#pragma omp simd
    for (int i = 0; i < len; i++) {
        int u = (int)Cb[i] - 128;
        int v = (int)Cr[i] - 128;

        int new_u = (c * u - s * v + (1 << 15) + (128 << 16)) >> 16;
        int new_v = (s * u + c * v + (1 << 15) + (128 << 16)) >> 16;

        new_u &= ~(new_u >> 31);
        new_v &= ~(new_v >> 31);
        int diff_u = new_u - 255;
        int diff_v = new_v - 255;
        new_u = 255 + (diff_u & (diff_u >> 31));
        new_v = 255 + (diff_v & (diff_v >> 31));

        Cb[i] = (uint8_t)new_u;
        Cr[i] = (uint8_t)new_v;
    }
}
