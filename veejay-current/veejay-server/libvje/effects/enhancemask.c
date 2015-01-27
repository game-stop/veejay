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

#include "enhancemask.h"
#include "common.h"
vj_effect *enhancemask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 125;	/* type */
   // ve->defaults[1] = 255;      /* yuvdenoise's default sharpen parameter */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2048;
  //  ve->limits[0][1] = 0;
   // ve->limits[1][1] = 255;
    ve->description = "Sharpen";

    ve->extra_frame = 0;
    ve->sub_format = 0;
	ve->has_user = 0;
	ve->parallel = 1;
	ve->param_description = vje_build_param_list( ve->num_params, "Value" );
    return ve;
}

void enhancemask_apply(VJFrame *frame, int width, int height, int *s ) {

   //int s[9]= { 1, 0, -1, 2, 0, -2, 1 , 0 , -1};
   unsigned int r;
   const unsigned int len = (width*height)-width-1;
   uint8_t *Y = frame->data[0];
	/*
   int sum=0;
   for(r=0; r < 9; r++) sum+=s[r];
	
   for(r=width; r < len; r+=width) {
	for(c=1; c < width-1; c++) {
	  int p1 = (
			 (Y[r - width + c - 1] * s[0]) +
			 (Y[r - width + c ] * s[1]) +
			 (Y[r - width + c + 1] * s[2]) +
			 (Y[r + c - 1] * s[3]) +
			 (Y[r + c] * s[4]) +
			 (Y[r + c + 1] * s[5]) +
			 (Y[r + width + c - 1] * s[6]) +
			 (Y[r + width + c ] * s[7]) + 
			 (Y[r + width + c + 1] * s[8]) );
	  Y[r+c] = (p1/sum);
	}
    } 
	*/

   /* The sharpen comes from yuvdenoiser, we like grainy video so 512 is allowed.  */ 
      

   register int d,m;
//   unsigned int op0,op1;
  // op0 = (s[1] > 255) ? 255 : s[1];
  // op1 = 255 - op1;
   for(r=0; r < len; r++) {
	m = ( Y[r] + Y[r+1] + Y[r+width] + Y[r+width+1] + 2) >> 2;
	d = Y[r] - m;
	d *= s[0];
	d /= 100;
	m = m + d;
//	a = Y[r];
//	Y[r] = (m * op0 + a * op1) / 255;
	Y[r] = m;
	}
   for(r=len; r < (width*height); r++) {
        m = (Y[r] + Y[r+1] + Y[r-width] + Y[r-width+1] + 2) >> 2;
	d = Y[r]-m;
	d *= s[0];
	d /= 100;
	m = m + d;
	Y[r] = m;
   }

}

void enhancemask_free(){}
