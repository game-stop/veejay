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

#include "common.h"
#include <veejaycore/vjmem.h>
#include "tiler.h"

vj_effect *tiler_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 2;
    ve->limits[1][0] = ( w < h ? w / 8 : h / 8);
    ve->defaults[0] = 2;

	ve->limits[0][1] = 0;
	ve->limits[1][1] = 255;
	ve->defaults[1] = 0;	

    ve->description = "Tiler";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Tiles", "Opacity" );
    return ve;
}

typedef struct 
{
    uint8_t *buf[3];
} tiler_t;

void *tiler_malloc(int w, int h) {
    tiler_t *s = (tiler_t*) vj_malloc(sizeof(tiler_t));
    if(!s) return NULL;
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 3 );
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }
    s->buf[1] = s->buf[0] + ( w * h );
    s->buf[2] = s->buf[1] + ( w * h );

    return (void*) s;
}

void tiler_free(void *ptr) {
    tiler_t *s = (tiler_t*) ptr;
    free(s->buf[0]);
    free(s);
}


void tiler_apply( void *ptr, VJFrame *frame, int *args ) {
    tiler_t *s = (tiler_t*) ptr;
    const int t = args[0];
    const uint8_t op0 = args[1];
	const uint8_t op1 = 0xff - op0;
    const int len = frame->len;
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    for (int i = 0; i < height / t; ++i) {
#pragma omp simd
        for (int j = 0; j < width / t; ++j) {
            int src_idx = (i * t) * width + (j * t);
            int buf_idx = i * (width / t) + j;

            bufY[buf_idx] = srcY[src_idx];
            bufU[buf_idx] = srcU[src_idx];
            bufV[buf_idx] = srcV[src_idx];
        }
    }

	if( op1 == 0 ) {
    	for (int i = 0; i < height; i += height / t) {
        	for (int j = 0; j < width; j += width / t) {
            	for (int ii = 0; ii < height / t; ++ii) {
#pragma omp simd
					for (int jj = 0; jj < width / t; ++jj) {
                    	int src_idx = (i + ii) * width + (j + jj);
                    	int buf_idx = ii * (width / t) + jj;

                    	srcY[src_idx] = bufY[buf_idx];
                    	srcU[src_idx] = bufU[buf_idx];
                    	srcV[src_idx] = bufV[buf_idx];
                	}
            	}
        	}
    	}
	}
	else {
    	for (int i = 0; i < height; i += height / t) {
        	for (int j = 0; j < width; j += width / t) {
            	for (int ii = 0; ii < height / t; ++ii) {
#pragma omp simd
					for (int jj = 0; jj < width / t; ++jj) {
                    	int src_idx = (i + ii) * width + (j + jj);
                    	int buf_idx = ii * (width / t) + jj;
        				srcY[src_idx] = (((op0 * srcY[src_idx]) + (op1 * bufY[buf_idx])) >> 8);
        				srcU[src_idx] = (((op0 * srcU[src_idx]) + (op1 * bufU[buf_idx])) >> 8);
        				srcV[src_idx] = (((op0 * srcV[src_idx]) + (op1 * bufV[buf_idx])) >> 8);
                	}
            	}
        	}
    	}
	}

}
