/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <elburg@hio.hen.nl>
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

#include "complexsaturate.h"
#include <stdlib.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "common.h"

vj_effect *complexsaturation_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 6;
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 80;	/* angle */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 255;	/* b */
    ve->defaults[4] = 20;	/* v_adjust */
    ve->defaults[5] = 1500;	/* degrees */

    ve->limits[0][0] = 5;
    ve->limits[1][0] = 900;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = -255;
    ve->limits[1][4] = 255;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 36000;
    ve->has_internal_data = 0;
    ve->description = "Complex Saturation";
    ve->extra_frame = 0;
    ve->sub_format = 0;
    return ve;
}

extern uint8_t ccolor_adjust_u(double dcolor, double dsaturation);
extern uint8_t ccolor_adjust_v(double dcolor, double dsaturation);
extern double ccolor_sqrt(double u, double v);
extern double ccolor_sine(int u, double dsaturation);


void complexsaturation_apply(uint8_t * src1[3], int width,
		   int height, int i_angle, int r, int g,
		   int b, int adjust_v, int adjust_degrees)
{

    uint8_t *fg_y, *fg_cb, *fg_cr;
    uint8_t *bg_y, *bg_cb, *bg_cr;
    int accept_angle_tg, accept_angle_ctg, one_over_kc;
    int kfgy_scale, kg;
    int cb, cr;
    float kg1, tmp, aa = 128, bb = 128, _y = 0;
    float angle = (float) i_angle / 10.0;
    unsigned int pos;
    uint8_t val;
    double dsaturation, dcolor;
    double degrees = ( adjust_degrees / 100.0);
    double dsat = adjust_v / 100.0;
    const unsigned int len = width * height;
 
   _y = ((Y_Redco * r) + (Y_Greenco * g) + (Y_Blueco * b) + 16);
    aa = ((U_Redco * r) - (U_Greenco * g) - (U_Blueco * b) + 128);
    bb = (-(V_Redco  * r) - (V_Greenco * g) + (V_Blueco * b) + 128);
    tmp = sqrt(((aa * aa) + (bb * bb)));
    cb = 127 * (aa / tmp);
    cr = 127 * (bb / tmp);
    kg1 = tmp;

    /* obtain coordinate system for cb / cr */
    accept_angle_tg = 0xf * tan(M_PI * angle / 180.0);
    accept_angle_ctg = 0xf / tan(M_PI * angle / 180.0);
    
    tmp = 1 / kg1;
    one_over_kc = 0xff * 2 * tmp - 0xff;
    kfgy_scale = 0xf * (float) (_y) / kg1;
    kg = kg1;

    /* intialize pointers */
    fg_y = src1[0];
    fg_cb = src1[1];
    fg_cr = src1[2];

    bg_y = src1[0];
    bg_cb = src1[1];
    bg_cr = src1[2];

    for (pos = len; pos != 0; pos--) {
	short xx, yy;
	xx = (((fg_cb[pos>>2]) * cb) + ((fg_cr[pos>>2]) * cr)) >> 7;
	if (xx < -128) {
	    xx = -128;
	}
	if (xx > 127) {
	    xx = 127;
	}
	yy = (((fg_cr[pos>>2]) * cb) - ((fg_cb[pos>>2]) * cr)) >> 7;
	if (yy < -128) {
	    yy = -128;
	}
	if (yy > 127) {
	    yy = 127;
	}
	val = (xx * accept_angle_tg) >> 4;
	if (val > 127)
	    val = 127;

	if (abs(yy) > val) { /* pixel is within selected color range,  saturate */	
		int _cb = src1[1][pos>>2] - 128;
		int _cr = src1[2][pos>>2] - 128;
		if( _cb != 0 && _cr != 0) {
			double co=0.0,si=0.0;
			//fast_sqrt( dsaturation, (double) (_cb * cr + _cr * _cr) );
			dsaturation = ccolor_sqrt( (double) _cb, (double) _cr);
			dcolor = ccolor_sine( _cb, dsaturation);
			if( _cr < 0) {
				dcolor = M_PI - dcolor;
			}
			dcolor += (degrees * M_PI) / 180.0;
			dsaturation *= dsat;
			sin_cos( co, si , dcolor );
			src1[1][pos>>2] = si * dsaturation + 128;
			src1[2][pos>>2] = co * dsaturation + 128;
		}
	}
    }
}

void complexsaturate_free(){}
