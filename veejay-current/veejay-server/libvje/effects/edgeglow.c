/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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

/*
 * Edge flow, inspired by Salsaman's Edgeflow Frei0r plugin  (salsaman@gmail.com)
 *
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "edgeglow.h"

vj_effect *edgeglow_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 5;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->defaults[0] = 15;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 255;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;
    ve->defaults[3] = 0;

	ve->limits[0][4] = 1;
	ve->limits[1][4] = 100;
	ve->defaults[4] = 20;

    ve->description = "Edge Glow";
    ve->sub_format = 1;
    ve->rgb_conv = 1;
	ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Red", "Green" , "Blue", "Scaling Factor" );
    return ve;
}

typedef struct 
{
    uint8_t *buf;
	uint8_t *blurmask;
} edgeglow_t;

void *edgeglow_malloc(int w, int h) {
    edgeglow_t *s = (edgeglow_t*) vj_malloc(sizeof(edgeglow_t));
    if(!s) return NULL;
    s->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 2 );
    if(!s->buf) {
        free(s);
        return NULL;
    }
	s->blurmask = s->buf + (w*h);

    return (void*) s;
}

void edgeglow_free(void *ptr) {
    edgeglow_t *s = (edgeglow_t*) ptr;
    free(s->buf);
    free(s);
}


void edgeglow_apply( void *ptr, VJFrame *frame, int *args ) {
    edgeglow_t *s = (edgeglow_t*) ptr;
    const int t = args[0];
    const int threshold = (args[0] * args[0]);
	const int red = args[1];
	const int green = args[2];
	const int blue = args[3];
	const float scalingFactor = (args[4] * 0.1f);

    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict B = s->buf;
	uint8_t *restrict C = s->blurmask;

	int nY=0,nU=128,nV=128;
	
	_rgb2yuv( red,green,blue, nY, nU, nV );

	int L2 = (nY * 100) >> 8;
	int a2 = (((nU - 128) * 127) >> 8);
	int b2 = (((nV - 128) * 127) >> 8);


	for( int y = 0; y < 1; y ++ ) {
		for( int x = 0; x < width; x ++ )
			B[y*width+x] = 0;
	}

	for( int y = (height-1); y < height; y ++ ) {
		for( int x = 0; x < width; x ++ ) {
			B[y*width+x] = 0;
		}
	}

	// edge detect
    for (int y = 1; y < height - 1; ++y) {
		B[ y * width ] = 0;
#pragma omp simd
        for (int x = 1; x < width - 1; ++x) {
            const int index = y * width + x;

            const int gx = Y[index - width - 1] - Y[index - width + 1] + 2 * (Y[index - 1] - Y[index + 1]) + Y[index + width - 1] - Y[index + width + 1];
            const int gy = Y[index - width - 1] + 2 * Y[index - width] + Y[index - width + 1] - Y[index + width - 1] - 2 * Y[index + width] - Y[index + width + 1];

            const int abs_gx = (gx ^ (gx >> 31)) - (gx >> 31);
            const int abs_gy = (gy ^ (gy >> 31)) - (gy >> 31);
            const int gradientMagnitude = abs_gx + abs_gy;

            const int normMagnitude = (int) (((float) gradientMagnitude / 1020) * 255.0);

            B[index] = (normMagnitude > t) ? gradientMagnitude : 0;
        }
		B[ y * width + width ] = 0;
    }

	// blur edge mask
	for (int y = 1; y < height - 1; ++y) {
    	#pragma omp simd
    	for (int x = 1; x < width - 1; ++x) {
        	const int index = y * width + x;
       		const int blurredValue = (B[index - width - 1] + B[index - width] + B[index - width + 1] +
                                  B[index - 1] + B[index] + B[index + 1] +
                                  B[index + width - 1] + B[index + width] + B[index + width + 1]) / 9;

    	    C[index] = blurredValue;
    	}
	}


    for( int i = 0; i < len; i ++ ) {
		const int edgeIntensity = (int) ((float) C[i] * scalingFactor );
	
		if( edgeIntensity > 0 ) {
			int L1 = (Y[i] * 100) >> 8;
			int a1 = (((Cb[i] - 128) * 127) >> 8);
			int b1 = (((Cr[i] - 128) * 127) >> 8);

 
            L1 = L1 + ((L2 - L1) * edgeIntensity) / 255;
            a1 = a1 + ((a2 - a1) * edgeIntensity) / 255;
            b1 = b1 + ((b2 - b1) * edgeIntensity) / 255;

			Y[i]  = CLAMP_Y( (L1 * 255) / 100 );
			Cb[i] = CLAMP_UV( a1 + 128 );
			Cr[i] = CLAMP_UV( b1 + 128 );
		}
    }

}
