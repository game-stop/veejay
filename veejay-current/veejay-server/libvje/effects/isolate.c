/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include <libvjmem/vjmem.h>
#include <math.h>
#include "common.h"
#include "isolate.h"

vj_effect *isolate_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 4500;	/* angle */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 0;	/* g */
    ve->defaults[3] = 255;	/* b */
    ve->defaults[4] = 150;	/* white value */
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 9000;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->description = "Isolate by Color Key";
    ve->extra_frame = 0;
    ve->sub_format = 1;
	ve->rgb_conv = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Angle","Red", "Green","Blue", "White");
    return ve;
}

/* another key derivate */

void isolate_apply( VJFrame *frame, int width,
		   int height, int i_angle, int r, int g,
		   int b, int opacity)
{
	uint8_t *fg_cb, *fg_cr;
    int accept_angle_tg;
    int cb, cr;
    float kg1, tmp, aa = 255, bb = 255, _y = 0;
    float angle = (float) i_angle / 100.0f;
    unsigned int pos;
    uint8_t val;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	int iy=0,iu=0,iv=0;
	_rgb2yuv(r,g,b,iy,iu,iv);
	_y = (float) iy;
	aa = (float) iu;
	bb = (float) iv;
    tmp = sqrt(((aa * aa) + (bb * bb)));
    cb = 255 * (aa / tmp);
    cr = 255 * (bb / tmp);
    kg1 = tmp;
   
    /* obtain coordinate system for cb / cr */
    accept_angle_tg = (int)( 15.0f * tanf(M_PI * angle / 180.0f));
 
    tmp = 1 / kg1;

    /* intialize pointers */
    fg_cb = Cb;
    fg_cr = Cr;

    for (pos = (width * height); pos != 0; pos--) {
		short xx, yy;
		xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
		yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;
		val = (xx * accept_angle_tg) >> 4;

		if (abs(yy) >= val) {
			Y[pos]=opacity;
			Cb[pos]=128;
			Cr[pos]=128;
		}
		else {
			Y[pos] = pixel_Y_lo_;
			Cb[pos] = 128;
			Cr[pos] = 128;
		}
    }
}

void isolate_free(){}
