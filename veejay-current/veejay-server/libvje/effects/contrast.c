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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "contrast.h"
#include "common.h"
vj_effect *contrast_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 2;	/* type */
    ve->defaults[1] = 125;
    ve->defaults[2] = 200;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->parallel = 1;
	ve->description = "Contrast";
	ve->has_user = 0;
    ve->extra_frame = 0;
    ve->sub_format = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Intensity 1", "Intensity 2" );
    return ve;
}

/* also from yuvdenoise */
void contrast_cb_apply(VJFrame *frame, int width,int height, int *s) {
	unsigned int r;
	register int cb;
	register int cr;
	const int uv_len = frame->uv_len;
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];

	for(r=0; r < uv_len; r++) {
		cb = Cb[r];
		cb -= 128;
		cb *= s[2];
		cb = (cb + 50)/100;
		cb += 128;

		cr = Cr[r];
		cr -= 128;
		cr *= s[2];
		cr = (cr + 50)/100;
		cr += 128;

		Cb[r] = CLAMP_UV(cb);
		Cr[r] = CLAMP_UV(cr);
	}
}

void contrast_y_apply(VJFrame *frame, int width, int height, int *s) {
   unsigned int r;
   register int m;
	const int len = frame->len;
 	uint8_t *Y = frame->data[0];

   for(r=0; r < len; r++) {
	m = Y[r];
	m -= 128;
	m *= s[1];
	m = (m + 50)/100;
	m += 128;
	Y[r] = CLAMP_Y(m);
    }

}

void contrast_apply(VJFrame *frame, int width, int height, int *s ) {

      switch(s[0]) {
		  case 0:
      	contrast_y_apply(frame, width,height, s);
	  break;
        case 1:
	contrast_cb_apply(frame,width,height,s);
		break;
		case 2:
		contrast_y_apply(frame,width,height,s);
		contrast_cb_apply(frame,width,height,s);
		break;
	}
}

void contrast_free(){}
