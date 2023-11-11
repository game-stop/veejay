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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "greyselect.h"

vj_effect *greyselect_init(int w, int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 4500; /* angle */
    ve->defaults[1] = 255;  /* r */
    ve->defaults[2] = 0;    /* g */
    ve->defaults[3] = 0;    /* b */
    ve->defaults[4] = 0;    /* swap */
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

    ve->has_user = 0;
    ve->parallel = 1;
    ve->description = "Grayscale by Color Key (RGB)";
    ve->extra_frame = 0;
    ve->sub_format = 1;
    ve->rgb_conv = 1;
    ve->param_description = vje_build_param_list(ve->num_params,"Angle","Red","Green","Blue", "Swap");

    return ve;
}

void greyselect_apply( void *ptr, VJFrame *frame, int *args ) {
    int i_angle = args[0];
    int r = args[1];
    int g = args[2];
    int b = args[3];
    int swap = args[4];

    const int len = frame->len;
    uint8_t *fg_cb, *fg_cr;
    int accept_angle_tg;
    int cb, cr;
    float tmp, aa = 255.0f, bb = 255.0f;
    float angle = (float) i_angle / 100.0f;
    unsigned int pos;
    uint8_t val;
    uint8_t *Cb = frame->data[1];
    uint8_t *Cr = frame->data[2];
    int iy=0,iu=128,iv=128;
    _rgb2yuv(r,g,b,iy,iu,iv);
    aa = (float) iu;
    bb = (float) iv;

    tmp = sqrt(((aa * aa) + (bb * bb)));
    cb = 255 * (aa / tmp);
    cr = 255 * (bb / tmp);
   
    /* obtain coordinate system for cb / cr */
    accept_angle_tg = (int)( 15.0f * tanf(M_PI * angle / 180.0f));

    /* intialize pointers */
    fg_cb = Cb;
    fg_cr = Cr;

    if( swap == 0 ) {
        for (pos = len; pos != 0; pos--) {
            short xx, yy;
            xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
            yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;
            val = (xx * accept_angle_tg) >> 4;

            if (abs(yy) > val) {
                Cb[pos]=128;
                Cr[pos]=128;
            }
        }
    }
    else {
        for (pos = len; pos != 0; pos--) {
            short xx, yy;
            xx = (((fg_cb[pos]) * cb) + ((fg_cr[pos]) * cr)) >> 7;
            yy = (((fg_cr[pos]) * cb) - ((fg_cb[pos]) * cr)) >> 7;
            val = (xx * accept_angle_tg) >> 4;

            if (abs(yy) <= val) {
                Cb[pos]=128;
                Cr[pos]=128;
            }
        }
    }
}

