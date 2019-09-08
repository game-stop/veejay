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
	ve->num_params = 2;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
   /* ve->limits[0][0] = -235;
	ve->limits[1][0] = 235;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 36000;
	ve->defaults[0] = 116;
	ve->defaults[1] = 4500;*/
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 360;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 256;
	ve->defaults[0] = 50;
	ve->defaults[1] = 50;
	ve->param_description = vje_build_param_list( ve->num_params, "Degrees", "Intensity" );
	ve->description = "Hue and Saturation";
	ve->extra_frame = 0;
	ve->sub_format = -1;
	ve->has_user = 0;
	ve->parallel = 1;
	return ve;
}

/* these methods were derived from yuv-subtitler */
static inline uint8_t ccolor_adjust_u(double dcolor, double dsaturation)
{
	return (sin(dcolor) * dsaturation) + 128;
}
static inline uint8_t ccolor_adjust_v(double dcolor, double dsaturation)
{
	return (cos(dcolor) * dsaturation) + 128;
}
static inline double ccolor_sqrt(double u, double v)
{
//	  return sqrt((u * u) + (v * v));
	 double r;
	 fast_sqrt( r,(u*u)+(v*v));
	 return r;
}
static inline double ccolor_sine(int u, double dsaturation)
{
	return asin((u / dsaturation));
}


void coloradjust_apply(void *ptr, VJFrame *frame, int *args) {
    int val = args[0];
    int _degrees = args[1];

	unsigned int i;
	const int len = (frame->ssm ? frame->len : frame->uv_len);
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
//@ Hue, Saturation, copied from AVIDEMUX!

	float hue = (float) ( (val/180.0) * M_PI);
	float sat = (float) ( _degrees * 0.01 );
	
	const int s = (int) rint( sin(hue) * (1<<16) * sat );
	const int c = (int) rint( cos(hue) * (1<<16) * sat );
	
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
