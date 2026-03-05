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

/*
  this effect takes lumaninance information of frame B  (0=no displacement,255=max displacement)
  to extract distortion offsets for frame A.
  h_scale and v_scale can be used to limit the scaling factor.
  if the value is < 128, the pixels will be shifted to the left
  otherwise to the right.
           


*/

#include "common.h"
#include <veejaycore/vjmem.h>
#include "lumamask.h"
#include "motionmap.h"

typedef struct {
    uint8_t *buf[4];
    void *motionmap;
    int n__;
    int N__;
} lumamask_t;

vj_effect *lumamask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = -width;
    ve->limits[1][0] = width;
    ve->limits[0][1] = -height;
    ve->limits[1][1] = height;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;
	ve->limits[0][3] = 0;
    ve->limits[1][3] = 1;
    ve->defaults[0] = width/20; 
    ve->defaults[1] = height/10;
    ve->defaults[2] = 0; // border
    ve->description = "Displacement Map";
    ve->motion = 1;
	ve->sub_format = 1;
    ve->extra_frame = 1;
  	ve->has_user = 0; 
	ve->param_description = vje_build_param_list(ve->num_params, "X displacement", "Y displacement", "Mode", "Update Alpha" );
    return ve;
}

int lumamask_requests_fx(void) {
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void lumamask_set_motionmap(void *ptr, void *priv) {
    lumamask_t* l = (lumamask_t*) ptr;
    l->motionmap = priv;
}

void *lumamask_malloc(int width, int height)
{
    lumamask_t *l = (lumamask_t*) vj_calloc(sizeof(lumamask_t));
    if(!l) {
        return NULL;
    }

    l->buf[0] = (uint8_t*)vj_malloc( sizeof(uint8_t) * ( width * height * 4) );
    if(!l->buf[0]) {
        free(l);
        return NULL;
    }

    veejay_memset( l->buf[0], pixel_Y_lo_, width * height );
   
    l->buf[1] = l->buf[0] + (width *height);
    veejay_memset( l->buf[1], 128, width * height );
    
    l->buf[2] = l->buf[1] + (width *height);
    veejay_memset( l->buf[2], 128, width * height );
   
    l->buf[3] = l->buf[2] + (width *height);
    veejay_memset( l->buf[3], 0, width * height );

    return (void*) l;
}

static inline void lumamask_inner_noalpha_noborder(
    uint8_t *restrict Y, uint8_t *restrict Cb, uint8_t *restrict Cr,
    uint8_t *restrict Y2, uint8_t *restrict Cb2, uint8_t *restrict Cr2,
    int width, int height, int w_mul_q8, int h_mul_q8)
{
    for (int y = 0; y < height; y++) {
        uint8_t *Y_row  = Y  + y*width;
        uint8_t *Cb_row = Cb + y*width;
        uint8_t *Cr_row = Cr + y*width;

        for (int x = 0; x < width; x++) {
            int tmp = Y2[y*width + x] - 128;
            int dx = (w_mul_q8 * tmp) >> 8;
            int dy = (h_mul_q8 * tmp) >> 8;

            int nx = x + dx;
            int ny = y + dy;

            nx = nx < 0 ? 0 : (nx >= width ? width - 1 : nx);
            ny = ny < 0 ? 0 : (ny >= height ? height - 1 : ny);

            int idx = ny*width + nx;

            Y_row[x]  = Y2[idx];
            Cb_row[x] = Cb2[idx];
            Cr_row[x] = Cr2[idx];
        }
    }
}

static inline void lumamask_inner_noalpha_border(
    uint8_t *restrict Y, uint8_t *restrict Cb, uint8_t *restrict Cr,
    uint8_t *restrict Y2, uint8_t *restrict Cb2, uint8_t *restrict Cr2,
    int width, int height, int w_mul_q8, int h_mul_q8,
    int pixel_Y_lo_)
{
    for (int y = 0; y < height; y++) {
        uint8_t *Y_row  = Y  + y*width;
        uint8_t *Cb_row = Cb + y*width;
        uint8_t *Cr_row = Cr + y*width;

        for (int x = 0; x < width; x++) {
            int tmp = Y2[y*width + x] - 128;
            int dx = (w_mul_q8 * tmp) >> 8;
            int dy = (h_mul_q8 * tmp) >> 8;

            int nx = x + dx;
            int ny = y + dy;

            int mask = (nx < 0 || nx >= width || ny < 0 || ny >= height);

            int clamped_nx = nx < 0 ? 0 : (nx >= width ? width-1 : nx);
            int clamped_ny = ny < 0 ? 0 : (ny >= height ? height-1 : ny);

            int idx = clamped_ny * width + clamped_nx;

            Y_row[x]  = (uint8_t)(mask ? pixel_Y_lo_ : Y2[idx]);
            Cb_row[x] = (uint8_t)(mask ? 128         : Cb2[idx]);
            Cr_row[x] = (uint8_t)(mask ? 128         : Cr2[idx]);
        }
    }
}

static inline void lumamask_inner_alpha_noborder(
    uint8_t *restrict Y, uint8_t *restrict Cb, uint8_t *restrict Cr, uint8_t *restrict aA,
    uint8_t *restrict Y2, uint8_t *restrict Cb2, uint8_t *restrict Cr2, uint8_t *restrict aB,
    int width, int height, int w_mul_q8, int h_mul_q8)
{
    for (int y = 0; y < height; y++) {
        uint8_t *restrict Y_row = Y + y*width;
        uint8_t *restrict Cb_row = Cb + y*width;
        uint8_t *restrict Cr_row = Cr + y*width;
        uint8_t *restrict aA_row = aA + y*width;

        for (int x = 0; x < width; x++) {
            int tmp = Y2[y*width + x] - 128;
            int dx = (w_mul_q8 * tmp) >> 8;
            int dy = (h_mul_q8 * tmp) >> 8;

            int nx = x + dx;
            int ny = y + dy;

            nx = nx < 0 ? 0 : (nx >= width ? width-1 : nx);
            ny = ny < 0 ? 0 : (ny >= height ? height-1 : ny);

            int idx = ny*width + nx;
            Y_row[x]  = Y2[idx];
            Cb_row[x] = Cb2[idx];
            Cr_row[x] = Cr2[idx];
            aA_row[x] = aB[idx];
        }
    }
}

static inline void lumamask_inner_alpha_border(
    uint8_t *restrict Y, uint8_t *restrict Cb, uint8_t *restrict Cr, uint8_t *restrict aA,
    uint8_t *restrict Y2, uint8_t *restrict Cb2, uint8_t *restrict Cr2, uint8_t *restrict aB,
    int width, int height, int w_mul_q8, int h_mul_q8,
    int pixel_Y_lo_)
{
    for (int y = 0; y < height; y++) {
        uint8_t *restrict Y_row  = Y  + y*width;
        uint8_t *restrict Cb_row = Cb + y*width;
        uint8_t *restrict Cr_row = Cr + y*width;
        uint8_t *restrict aA_row = aA + y*width;
#pragma omp simd
        for (int x = 0; x < width; x++) {
            int tmp = Y2[y*width + x] - 128;
            int dx = (w_mul_q8 * tmp) >> 8;
            int dy = (h_mul_q8 * tmp) >> 8;

            int nx = x + dx;
            int ny = y + dy;

            int clamped_nx = nx < 0 ? 0 : (nx >= width ? width - 1 : nx);
            int clamped_ny = ny < 0 ? 0 : (ny >= height ? height - 1 : ny);

            int idx = clamped_ny * width + clamped_nx;

            int mask = (nx < 0 || nx >= width || ny < 0 || ny >= height);

            Y_row[x]  = (uint8_t)((mask ? pixel_Y_lo_ : Y2[idx]));
            Cb_row[x] = (uint8_t)((mask ? 128         : Cb2[idx]));
            Cr_row[x] = (uint8_t)((mask ? 128         : Cr2[idx]));
            aA_row[x] = (uint8_t)((mask ? 0           : aB[idx]));
        }
    }
}

void lumamask_apply( void *ptr, VJFrame *frame, VJFrame *frame2, int *args ) {
    int v_scale = args[0];
    int h_scale = args[1];
    int border = args[2];
    int alpha = args[3];

    lumamask_t *l = (lumamask_t*) ptr;

	unsigned int x,y;
	int dx,dy,nx,ny;
	int tmp;
	int interpolate = 1;
	int tmp1 = v_scale;
	int tmp2 = h_scale;
	int motion = 0;
	const unsigned int width = frame->width;
	const unsigned int height = frame->height;
	const int len = frame->len;

	if( motionmap_active(l->motionmap) )
	{
		motionmap_scale_to(l->motionmap, width,height,1,1,&tmp1,&tmp2,&(l->n__),&(l->N__) );
		motion = 1;
	}
	else
	{
		l->n__ = 0;
		l->N__ = 0;
	}	

	if( l->n__ == l->N__ || l->n__ == 0 )
		interpolate = 0;

	double w_ratio = (double) tmp1 / 128.0;
	double h_ratio = (double) tmp2 / 128.0;
  	uint8_t *restrict Y = frame->data[0];
	uint8_t *restrict Cb= frame->data[1];
	uint8_t *restrict Cr= frame->data[2];
	uint8_t *restrict Y2 = frame2->data[0];
	uint8_t *restrict Cb2 = frame2->data[1];
	uint8_t *restrict Cr2 = frame2->data[2];
	uint8_t *restrict aA = frame->data[3];
	uint8_t *restrict aB = frame2->data[3];

	int strides[4] = { len, len, len ,( alpha ? len : 0 )};
	vj_frame_copy( frame->data, l->buf, strides );

    int w_mul_q8 = (int)(-((double)tmp1 / 128.0) * 256.0);
    int h_mul_q8 = (int)(-((double)tmp2 / 128.0) * 256.0);
	const double w_mul = -w_ratio;
	const double h_mul = -h_ratio;
	
    if (!alpha) {
        if (!border) {
            lumamask_inner_noalpha_noborder(Y, Cb, Cr, Y2, Cb2, Cr2, width, height, w_mul_q8, h_mul_q8);
        } else {
            lumamask_inner_noalpha_border(Y, Cb, Cr, Y2, Cb2, Cr2, width, height, w_mul_q8, h_mul_q8, pixel_Y_lo_);
        }
    } else {
        if (!border) {
            lumamask_inner_alpha_noborder(Y, Cb, Cr, aA, Y2, Cb2, Cr2, aB, width, height, w_mul_q8, h_mul_q8);
        } else {
            lumamask_inner_alpha_border(Y, Cb, Cr, aA, Y2, Cb2, Cr2, aB, width, height, w_mul_q8, h_mul_q8, pixel_Y_lo_);
        }
    }

	if( interpolate ) {
		motionmap_interpolate_frame( l->motionmap, frame, l->N__, l->n__ );
	}

	if( motion ) {
		motionmap_store_frame( l->motionmap, frame );
	}

}

void lumamask_free(void *ptr)
{
    lumamask_t *l = (lumamask_t*) ptr;
    free(l->buf[0]);
    free(l);
}
