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

vj_effect *enhancemask_init(int width, int height)
{
    int i;
    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);
    ve->defaults[0] = 125;	/* type */
   // ve->defaults[1] = 255;      /* yuvdenoise's default sharpen parameter */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2048;
  //  ve->limits[0][1] = 0;
   // ve->limits[1][1] = 255;
    ve->description = "Sharpen";

    ve->extra_frame = 0;
    ve->sub_format = 0;
    ve->has_internal_data = 0;
    return ve;
}

void enhancemask_apply(uint8_t *src[3], int width, int height, int *s ) {

   //int s[9]= { 1, 0, -1, 2, 0, -2, 1 , 0 , -1};
   unsigned int r;
   const unsigned int len = (width*height)-width-1;
	/*
   int sum=0;
   for(r=0; r < 9; r++) sum+=s[r];
	
   for(r=width; r < len; r+=width) {
	for(c=1; c < width-1; c++) {
	  int p1 = (
			 (src[0][r - width + c - 1] * s[0]) +
			 (src[0][r - width + c ] * s[1]) +
			 (src[0][r - width + c + 1] * s[2]) +
			 (src[0][r + c - 1] * s[3]) +
			 (src[0][r + c] * s[4]) +
			 (src[0][r + c + 1] * s[5]) +
			 (src[0][r + width + c - 1] * s[6]) +
			 (src[0][r + width + c ] * s[7]) + 
			 (src[0][r + width + c + 1] * s[8]) );
	  src[0][r+c] = (p1/sum);
	}
    } 
	*/

   /* The sharpen comes from yuvdenoiser, we like grainy video so 512 is allowed.  */ 
      

   register int d,c,e,m;
   register uint8_t a;
//   unsigned int op0,op1;
  // op0 = (s[1] > 255) ? 255 : s[1];
  // op1 = 255 - op1;
   for(r=0; r < len; r++) {
	m = ( src[0][r] + src[0][r+1] + src[0][r+width] + src[0][r+width+1] + 2) >> 2;
	d = src[0][r] - m;
	d *= s[0];
	d /= 100;
	m = m + d;
//	a = src[0][r];
	if( m > 240) m = 240;
	if( m < 16) m = 16;
//	src[0][r] = (m * op0 + a * op1) / 255;
	src[0][r] = m;
	}
   for(r=len; r < (width*height); r++) {
        m = (src[0][r] + src[0][r+1] + src[0][r-width] + src[0][r-width+1] + 2) >> 2;
	d = src[0][r]-m;
	d *= s[0];
	d /= 100;
	m = m + d;
	if( m > 240) m = 240;
	if( m < 16) m = 16;
	src[0][r] = m;
   }

}

void enhancemask_free(){}
