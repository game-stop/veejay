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
    ve->parallel = 1;
    return ve;
}

void coloradjust_apply(void *ptr, VJFrame *frame, int *args) {
	int val = args[0];
    int _degrees = args[1];
    int exposureValue = args[2];

    unsigned int i;
    const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];

    float hue = (float) ( (val/180.0) * M_PI);
    float sat = (float) ( _degrees * 0.01 );
    
    const int s = (int) rint( a_sin(hue) * (1<<16) * sat );
    const int c = (int) rint( a_cos(hue) * (1<<16) * sat );

	if( exposureValue > 0.0f ) {
    	float powValue = exposureValue / 256.0f; 
#pragma opm simd
		for( i = 0; i < len ; i ++ ) 
		{
			Y[i] = (uint8_t)(Y[i] * powValue > 255 ? 255 : (Y[i] * powValue));
		}	
	}

#pragma omp simd
    for( i = 0 ; i < len ;i ++ )
    {
        const int u = Cb[i] - 128;
        const int v = Cr[i] - 128;
        int new_u = (c * u - s * v + (1<<15) + (128<<16)) >> 16;
        int new_v = (s * u + c * v + (1<<15) + (128<<16)) >> 16;
        if( new_u & 768 ) new_u = (-new_u) >> 31;
        if( new_v & 768 ) new_v = (-new_v) >> 31;

        Cb[i] = new_u;
        Cr[i] = new_v;
    }

}
