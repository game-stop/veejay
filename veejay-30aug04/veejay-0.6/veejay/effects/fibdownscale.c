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

#include "fibdownscale.h"
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>

extern void *(* veejay_memcpy)(void *to, const void *from, size_t len) ;

vj_effect *fibdownscale_init(int w, int h)
{

    vj_effect *ve = (vj_effect *) malloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->description = "Fibonacci Downscaler";
    ve->defaults = (int *) malloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) malloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) malloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;
    ve->limits[0][0] = 0;
    ve->limits[0][1] = 1;
    ve->limits[1][0] = 1;
    ve->limits[1][1] = 8;
    
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_internal_data =0;
    return ve;
}

void fibdownscale_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			int height, int n)
{
    if (n == 0)
	_fibdownscale_apply(yuv1, yuv2, width, height);
    if (n == 1)
	_fibrectangle_apply(yuv1, yuv2, width, height);
}

void _fibdownscale_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			 int height)
{

    unsigned i, f1;
    unsigned int len;
    unsigned int uv_len = (width*height)/8;
    len = (width * height) / 2;
    /* do fib over half of image. (now we have 2 squares in upper half) */
    for (i = 2; i < len; i++) {
	f1 = (i + 1) + (i - 1);
	yuv1[0][i] = yuv2[0][f1];
    }

    /* copy over first half (we could use veejay_memcpy) */
//    for (i = len; i < (width * height); i++) {
//	yuv1[0][i] = yuv1[0][i - len];
  //  }
    veejay_memcpy( yuv1[0] + len, yuv1[0], len ); 

    /* do the same thing for UV to get correct image */
    len >>= 2;

    for (i = 2; i < len; i++) {
	f1 = (i + 1) + (i - 1);
	yuv1[1][i] = yuv2[1][f1];
	yuv1[2][i] = yuv2[2][f1];
    }

//    for (i = len; i < (width * height) / 4; i++) {
//	yuv1[1][i] = yuv1[1][i - len];
//	yuv1[2][i] = yuv1[2][i - len];
  //  }
	veejay_memcpy( yuv1[1] + uv_len, yuv1[1] , uv_len );
	veejay_memcpy( yuv1[2] + uv_len, yuv1[2] , uv_len );
}

void _fibrectangle_apply(uint8_t * yuv1[3], uint8_t * yuv2[3], int width,
			 int height)
{
    unsigned int i, f1;
    unsigned int len;
    len = (width * height);
    for (i = 2; i < len; i++) {
	f1 = (i - 1) + (i - 2);
	if (f1 < len)
	    yuv1[0][i] = yuv2[0][f1];
	else
	    yuv1[0][i] = yuv1[0][(f1 - len)];
    }
    len >>= 2;
    for (i = 2; i < len; i++) {
	f1 = (i - 1) + (i - 2);
	if (f1 < len) {
	    yuv1[1][i] = yuv2[1][f1];
	    yuv1[2][i] = yuv2[2][f1];
	} else {
	    yuv1[1][i] = yuv2[1][f1 - len];
	    yuv1[2][i] = yuv2[2][f1 - len];
	}
    }
}
void fibdownscale_free(){}
