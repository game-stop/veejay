/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "motionblur.h"

vj_effect *motionblur_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 5;
    ve->limits[0][0] = 1;
    ve->limits[1][0] = fmin(width,height)/3; 
	ve->description = "Motion Blur";
    ve->sub_format = 1;
    ve->extra_frame = 0;
	ve->has_user = 0;
	ve->parallel = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Strength" );
	return ve;
}

typedef struct {
    uint8_t *buf[3];
} motionblur_t;

void	*motionblur_malloc(int w, int h) {
	motionblur_t *m = (motionblur_t*) vj_malloc(sizeof(motionblur_t));
	if(!m) {
		return NULL;
	}
	m->buf[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h * 3 );
    if(!m->buf[0]) {
		free(m);
		return NULL;
	}

	m->buf[1] = m->buf[0] + (w*h);
	m->buf[2] = m->buf[1] + (w*h);
		
	return (void*) m;
}

void	motionblur_free( void *ptr ) {
	motionblur_t *m = (motionblur_t*) ptr;
	if(m->buf[0]) {
		free(m->buf[0]);
	}
	free(m);
}

void motionblur_apply(void *ptr, VJFrame *frame, int *args) {
    motionblur_t *m = (motionblur_t *)ptr;
    const int width = frame->width;
    const int height = frame->height;
    const int blurStrength = args[0];

    uint8_t *tempY = m->buf[0];
    uint8_t *tempU = m->buf[1];
    uint8_t *tempV = m->buf[2];

	for (int i = 0; i < height; ++i) {
    	for (int j = 0; j < width; ++j) {
        	int totalY = 0;
       		int totalU = 0;
        	int totalV = 0;
        	int count = 0;

        	const int startK = (i - blurStrength < 0) ? -i : -blurStrength;
        	const int endK = (i + blurStrength >= height) ? height - 1 - i : blurStrength;

#pragma omp simd
        	for (int k = startK; k <= endK; ++k) {
            	int row = i + k;
            	int pixelIndex = row * width + j;
            	totalY += frame->data[0][pixelIndex];
            	totalU += frame->data[1][pixelIndex];
            	totalV += frame->data[2][pixelIndex];
            	count++;
        	}

        	const int currentIndex = i * width + j;
        	tempY[currentIndex] = totalY / count;
        	tempU[currentIndex] = totalU / count;
        	tempV[currentIndex] = totalV / count;
    	}
	}

	for (int i = 0; i < height; ++i) {
    		for (int j = 0; j < width; ++j) {
        		int totalY = 0;
        		int totalU = 0;
        		int totalV = 0;
        		int count = 0;

        		const int startK = (j - blurStrength < 0) ? -j : -blurStrength;
        		const int endK = (j + blurStrength >= width) ? width - 1 - j : blurStrength;

#pragma omp simd
        		for (int k = startK; k <= endK; ++k) {
            			int col = j + k;
            			int pixelIndex = i * width + col;
            			totalY += tempY[pixelIndex];
            			totalU += tempU[pixelIndex];
            	   		totalV += tempV[pixelIndex];
        	    		count++;
	        	}
	
        		const int currentIndex = i * width + j;
        		frame->data[0][currentIndex] = (uint8_t)(totalY / count);
       			frame->data[1][currentIndex] = (uint8_t)(totalU / count);
       			frame->data[2][currentIndex] = (uint8_t)(totalV / count);
    		}
	}


}

