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
#include "rotozoom.h"

typedef struct {
    uint8_t *rotobuffer[4];
	float sin_lut[360];
	float cos_lut[360];
	double zoom;
	double rotate;
	int frameCount;
	int direction;
} rotozoom_t;

vj_effect *rotozoom_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 30;
    ve->defaults[1] = 2;
    ve->defaults[2] = 1;
    ve->defaults[3] = 100;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 360;
    ve->limits[0][1] = -1000;
    ve->limits[1][1] = 1000;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
    ve->limits[0][3] = 1;
    ve->limits[1][3] = 1500;
    ve->description = "Rotozoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Rotate", "Zoom" , "Automatic", "Duration");
    ve->has_user = 0;

    return ve;
}

void *rotozoom_malloc(int width, int height)
{
    int i;
    rotozoom_t *r = (rotozoom_t*) vj_calloc( sizeof(rotozoom_t) );
    if(!r) {
        return NULL;
    }

    r->rotobuffer[0] = (uint8_t *) vj_calloc(sizeof(uint8_t) * (width * height * 3));
    if(!r->rotobuffer[0]) {
        free(r);
        return NULL;
    }

    r->rotobuffer[1] = r->rotobuffer[0] + (width * height);
    r->rotobuffer[2] = r->rotobuffer[1] + (width * height);

	r->direction = 1;

	for( i = 0; i < 360; i ++ ) {
		r->sin_lut[i] = a_sin( i * M_PI / 180.0 );
		r->cos_lut[i] = a_cos( i * M_PI / 180.0 );
	}

    return (void*) r;
}

void rotozoom_free(void *ptr) {

    rotozoom_t *r = (rotozoom_t*) ptr;

    if(r->rotobuffer[0])
        free(r->rotobuffer[0]);

    free(r);
}


void rotozoom_apply( void *ptr, VJFrame *frame, int *args )
{
    rotozoom_t *r = (rotozoom_t*) ptr;

    const unsigned int width = frame->width;
    const unsigned int height = frame->height;
    const int len = frame->len;
    
    double rotate = args[0];
    double zoom1 = args[1];
    int autom = args[2];
	int maxFrames = args[3];

	double zoom;
	if( zoom1 > 0) {
		zoom = 1.0 / (1.0 + zoom1 / 100.0);
	}
	else if(zoom1 < 0) {
		zoom = pow( 2.0, -zoom1 / 200.0);
	}
	else {
		zoom = 1.0;
	}
	
	if( autom ) {
		zoom1 = r->zoom;
		rotate = r->rotate;

		r->zoom += (r->direction * (2000.0 / maxFrames));
		r->rotate += (r->direction * (360.0 / maxFrames));

		r->frameCount ++;

		if( r->frameCount % maxFrames == 0 || (r->rotate <= 0 || r->rotate >= 360)) {
			r->direction *= -1;
			r->frameCount = 0;
		}
		r->zoom = fmin(1000, fmax(-1000, r->zoom));
	}

    uint8_t *dstY = frame->data[0];
    uint8_t *dstU = frame->data[1];
    uint8_t *dstV = frame->data[2];

    uint8_t *srcY = r->rotobuffer[0];
    uint8_t *srcU = r->rotobuffer[1];
    uint8_t *srcV = r->rotobuffer[2];

    veejay_memcpy( r->rotobuffer[0], frame->data[0], frame->len );
    veejay_memcpy( r->rotobuffer[1], frame->data[1], frame->len );
    veejay_memcpy( r->rotobuffer[2], frame->data[2], frame->len );

    const int centerX = width / 2;
    const int centerY = height / 2;


	float *cos_lut = r->cos_lut;
	float *sin_lut = r->sin_lut;


    int rotate_angle = (int)rotate % 360;
    float cos_val = cos_lut[rotate_angle];
    float sin_val = sin_lut[rotate_angle];


    for (int y = 0; y < height; ++y) {
#pragma omp simd
		for (int x = 0; x < width; ++x) {

		 	int rotatedX = (int)((x - centerX) * cos_val - (y - centerY) * sin_val + centerX);
            int rotatedY = (int)((x - centerX) * sin_val + (y - centerY) * cos_val + centerY);
         
            int newX = (int)((rotatedX - centerX) * zoom + centerX);
            int newY = (int)((rotatedY - centerY) * zoom + centerY);

            newX = (newX < 0) ? 0 : ((newX > width - 1) ? width - 1 : newX);
            newY = (newY < 0) ? 0 : ((newY > height - 1) ? height - 1 : newY);

            int srcIndex = newY * width + newX;
            int dstIndex = y * width + x;


            dstY[dstIndex] = srcY[srcIndex];
            dstU[dstIndex] = srcU[srcIndex];
            dstV[dstIndex] = srcV[srcIndex];
        }
    }

}
