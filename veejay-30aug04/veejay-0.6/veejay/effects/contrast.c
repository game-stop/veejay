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

#include <stdlib.h>
#include "../vj-effect.h"
#include <stdint.h>

vj_effect *contrast_init(int w, int h)
{
    int i;
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 3;
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 2;	/* type */
    ve->defaults[1] = 125;
    ve->defaults[2] = 200;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->description = "Contrast";
    ve->has_internal_data = 0;
    ve->extra_frame = 0;
    ve->sub_format = 0;

    return ve;
}

/* also from yuvdenoise */
void contrast_cb_apply(uint8_t *src[3], int width,int height, int *s) {
	unsigned int r;
	const unsigned int len = (width*height)/4;
	register int cb;
	register int cr;
	for(r=0; r < len; r++) {
		cb = src[1][r];
		cb -= 128;
		cb *= s[2];
		cb = (cb + 50)/100;
		cb += 128;
		if(cb > 235) cb = 235;
		if(cb < 16) cb = 16;

		cr = src[2][r];
		cr -= 128;
		cr *= s[2];
		cr = (cr + 50)/100;
		cr += 128;
		if(cr > 235) cr = 235;
		if(cr < 16) cr = 16;

		src[1][r] = cb;
		src[2][r] = cr;
	}
}

void contrast_y_apply(uint8_t *src[3], int width, int height, int *s) {
   unsigned int r;
   const unsigned int elen = (width*height);
   register int m;

   for(r=0; r < elen; r++) {
	m = src[0][r];
	m -= 128;
	m *= s[1];
	m = (m + 50)/100;
	m += 128;
	if ( m > 240) m = 240;
	if ( m < 16) m = 16;
	src[0][r] = m;
    }

}

void contrast_apply(uint8_t *src[3], int width, int height, int *s ) {

      switch(s[0]) {
		  case 0:
      	contrast_y_apply(src, width,height, s);
	  break;
        case 1:
	contrast_cb_apply(src,width,height,s);
		break;
		case 2:
		contrast_y_apply(src,width,height,s);
		contrast_cb_apply(src,width,height,s);
		break;
	}
}

void contrast_free(){}
