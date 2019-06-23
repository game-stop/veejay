/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2019 Niels Elburg <nwelburg@gmail.com>
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
#include "bloom.h"

vj_effect *bloom_init(int width,int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 4;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
	ve->limits[0][0] = 0;
	ve->limits[1][0] = 64;
	ve->limits[0][1] = 0;
	ve->limits[1][1] = 64;
	ve->limits[0][2] = 0;
	ve->limits[1][2] = 64;
    ve->limits[0][3] = 0;
	ve->limits[1][3] = 255;

	ve->defaults[0] = 16; 
	ve->defaults[1] = 22;
	ve->defaults[2] = 41;
	ve->defaults[3] = 180;
	ve->description = "Bloom (Glow)";
	ve->sub_format = -1;

	ve->param_description = vje_build_param_list( ve->num_params, "Blur Radius 1", "Blur Radius 2","Blur Radius 3", "Luminance Threshold" );

	return ve;
}

uint8_t *bloom_buf = NULL;
int bloom_malloc(int width, int height)
{
    if(bloom_buf == NULL) {
        bloom_buf = (uint8_t*) vj_calloc(sizeof(uint8_t) * RUP8(width * height * 8));
    }

    return 1;
}

void bloom_free() {
    if(bloom_buf) {
        free(bloom_buf);
        bloom_buf = NULL;
    }
}

static void rhblur_apply( uint8_t *dst , uint8_t *src, int w, int h, int r)
{
	int y;
	for(y = 0; y < h ; y ++ )
	{
		blur( dst + y * w, src + y *w , w, r,1, 1);
	}	

}
static void rvblur_apply( uint8_t *dst, uint8_t *src, int w, int h, int r)
{
	int x;
	for(x=0; x < w; x++)
	{
		blur( dst + x, src + x , h, r, w, w );
	}
}


void bloom_apply(VJFrame *frame, int a0, int b0, int c0, int threshold) {

    const unsigned int len = frame->len;
    int width = frame->width;
    int height = frame->height;
    uint8_t *L = frame->data[0];
    uint8_t *B = bloom_buf;
    uint8_t *B1h = B + RUP8(len);
    uint8_t *B1v = B1h + RUP8(len);
    uint8_t *B2h = B1v + RUP8(len);
    uint8_t *B2v = B2h + RUP8(len);
    uint8_t *B3h = B2v + RUP8(len);
    uint8_t *B3v = B3h + RUP8(len);

    for( int i = 0; i < len; i ++ ) {
        if( L[i] > threshold )
            B[i] = L[i];
        else
            B[i] = 0;

    }
    // just view threshold mask
    if( a0 == 0 && b0 == 0 && c0 == 0 ) {
        veejay_memcpy( frame->data[0], B, len );
        veejay_memset( frame->data[1], 128, frame->uv_len );
        veejay_memset( frame->data[2], 128, frame->uv_len );
        return;
    }
 
#pragma omp parallel
    {
        #pragma omp single
        {
            #pragma omp task
            {
                if( a0 > 0 ) { 
                    rhblur_apply( B1h, B, width, height, a0 );   
                    rvblur_apply( B1v, B1h, width, height, a0 );
                }
            }

            #pragma omp task
            {
                if( b0 > 0 ) { 
                    rhblur_apply( B2h, B, width, height, b0 );
                    rvblur_apply( B2v, B2h, width, height, b0 );
                }
            }

            #pragma omp task
            {
                if( c0 > 0 ) {
                    rhblur_apply( B3h, B, width, height, c0 );
                    rvblur_apply( B3v, B3h, width, height, c0 );
                }
            }

            #pragma omp taskwait

       }
   }  // end parallel region

    for( int i = 0; i < len; i ++ ) {
        uint16_t v = B1v[i] + B2v[i] + B3v[i] + L[i];

        if( v > 0xff )
            v = 0xff; // white

        L[i] = (uint8_t) v; 
    }

}
