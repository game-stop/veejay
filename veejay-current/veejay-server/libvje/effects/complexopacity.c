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

#include <math.h>
#include "common.h"
#include <veejaycore/vjmem.h>
#include "complexopacity.h"

vj_effect *complexopacity_init(int w, int h)
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
    ve->defaults[4] = 150;	/* opacity */
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
	ve->description = "Complex Overlay (RGB)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Angle", "Red", "Green", "Blue", "Opacity" );
    return ve;
}

/* this method decides whether or not a pixel from the fg will be accepted for keying */
int accept_ipixel(uint8_t fg_cb, uint8_t fg_cr, int cb, int cr,
		 int accept_angle_tg)
{
    short xx, yy;
    /* convert foreground to xz coordinates where x direction is
       defined by key color */
    uint8_t val;

    xx = ((fg_cb * cb) + (fg_cr * cr)) >> 7;
    yy = ((fg_cr * cb) - (fg_cb * cr)) >> 7;

    /* accept angle should not be > 90 degrees 
       reasonable results between 10 and 80 degrees.
     */

    val = (xx * accept_angle_tg) >> 4;
    if (abs(yy) < val) {
		return 1;
    }
    return 0;
}

void complexopacity_apply(void *ptr, VJFrame *frame, VJFrame *bg, int *args) {
	int i_angle = args[0];
    int r = args[1];
    int g = args[2];
    int b = args[3];
	int level = args[4];


    uint8_t *fg_y, *fg_cb, *fg_cr;
    uint8_t *bg_y;
    int accept_angle_tg;

    uint8_t cb, cr;
    float kg1, tmp, aa = 255.0f, bb = 255.0f;
    float angle = (float) i_angle / 100.0f;
    unsigned int pos;
    int matrix[5];
    uint8_t val;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
    int width = frame->width;
	
	int	iy,iu=128,iv=128;
	_rgb2yuv(r,g,b,iy,iu,iv);
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
    fg_y = frame->data[0];
    fg_cb = frame->data[1];
    fg_cr = frame->data[2];

    bg_y = bg->data[0];

    for (pos = width + 1; pos < (len) - width - 1; pos++) {
	int i = 0;
	int smooth = 0;
	/* setup matrix 
	   [ - 0 - ] = do not accept. [ - 1 - ] = level 5 , accept only when all n = 1
	   [ 0 0 0 ]                  [ 1 1 1 ]
	   [ - 0 - ]                  [ - 1 - ]

	   [ - 0 - ] sum of all n is acceptance value for level
	   [ 1 0 1 ]                    
	   [ 0 1 0 ]
	 */
	matrix[0] = accept_ipixel(fg_cb[pos], fg_cr[pos], cb, cr, accept_angle_tg);	/* center pixel */
	matrix[1] = accept_ipixel(fg_cb[pos - 1], fg_cr[pos - 1], cb, cr, accept_angle_tg);	/* left pixel */
	matrix[2] = accept_ipixel(fg_cb[pos + 1], fg_cr[pos + 1], cb, cr, accept_angle_tg);	/* right pixel */
	matrix[3] = accept_ipixel(fg_cb[pos + width], fg_cr[pos + width], cb, cr, accept_angle_tg);	/* top pixel */
	matrix[4] = accept_ipixel(fg_cb[pos - width], fg_cr[pos - width], cb, cr, accept_angle_tg);	/* bottom pixel */
	for (i = 0; i < 5; i++) {
	    if (matrix[i] == 1)
		smooth++;
	}
	if (smooth >= level) {
	    short xx, yy;
	    /* get bg/fg pixels */
	    uint8_t p1 = (matrix[0] == 0 ? fg_y[pos] : bg_y[pos]);
	    uint8_t p2 = (matrix[1] == 0 ? fg_y[pos - 1] : bg_y[pos - 1]);
	    uint8_t p3 = (matrix[2] == 0 ? fg_y[pos + 1] : bg_y[pos + 1]);
	    uint8_t p4 =
		(matrix[3] == 0 ? fg_y[pos + width] : bg_y[pos + width]);
	    uint8_t p5 =
		(matrix[4] == 0 ? fg_y[pos - width] : bg_y[pos - width]);
	    /* and blur the pixel */
	    fg_y[pos] = (p1 + p2 + p3 + p4 + p5) / 5;

	    /* convert foreground to xz coordinates where x direction is
	       defined by key color */
	    xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
	    yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;

	    val = (xx * accept_angle_tg) >> 4;
	    if (val > 127)
			val = 127;
	    /* see if pixel is within range of color and opacity it */
	    if (abs(yy) < val ) {
			Y[pos] = 255 - Y[pos];
			Cb[pos] = 255 - Cb[pos];
			Cr[pos] = 255 - Cr[pos];
	    }
	}
    }
}
