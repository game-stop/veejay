/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <elburg@hio.hen.nl>
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

#include "coloradjust.h"
#include <stdlib.h>
#include <math.h>
#include <stdlib.h>
#include "common.h"
vj_effect *coloradjust_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = -235;
    ve->limits[1][0] = 235;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 36000;
    ve->defaults[0] = 116;
    ve->defaults[1] = 4500;
    ve->description = "Saturation";
    ve->extra_frame = 0;
    ve->sub_format = 0;
    ve->has_internal_data =0;
    return ve;
}

/* these methods were derived from yuv-subtitler */
inline uint8_t ccolor_adjust_u(double dcolor, double dsaturation)
{
    return (sin(dcolor) * dsaturation) + 128;
}
inline uint8_t ccolor_adjust_v(double dcolor, double dsaturation)
{
    return (cos(dcolor) * dsaturation) + 128;
}


inline double ccolor_sqrt(double u, double v)
{
//    return sqrt((u * u) + (v * v));
     double r;
     fast_sqrt( r,(u*u)+(v*v));
     return r;
}
inline double ccolor_sine(int u, double dsaturation)
{
    return asin((u / dsaturation));
}


void coloradjust_apply(uint8_t * yuv[3], int width, int height, int val,
		       int _degrees)
{
    unsigned int i;
    const unsigned int len = (width * height)/4;
    int cb, cr;
    double dsaturation, dcolor;
    const double degrees = (_degrees / 100.0);
    double co, si;
    const double dsat = val / 100.0;

    for (i = 0; i < len; i++) {
	cb = yuv[1][i] - 128;
	cr = yuv[2][i] - 128;
	if (cb != 0 && cr != 0) {
	    dsaturation = ccolor_sqrt((double) cb, (double) cr);
	    dcolor = ccolor_sine(cb, dsaturation);
	    if (cr < 0)
		dcolor = M_PI - dcolor;
	    dcolor += (degrees * M_PI) / 180.0;
	    dsaturation *= dsat;
	    sin_cos(co,si, dcolor );
	    yuv[1][i] = si * dsaturation + 128;
	    yuv[2][i] = co * dsaturation + 128;
	    //yuv[1][i] = ccolor_adjust_u(si, dsaturation);
	    //yuv[2][i] = ccolor_adjust_v(co, dsaturation);
	}
    }
}
void coloradjust_free(){}
