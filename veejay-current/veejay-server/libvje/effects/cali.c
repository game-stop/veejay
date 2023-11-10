/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2010 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/yuvconv.h>
#include <veejaycore/vj-msg.h>
#include "softblur.h"
#include "cali.h"

// This is not an Effect

typedef struct
{
	uint8_t	*b[3];
	uint8_t *l[3];
	uint8_t *m[3];
	double mean[3];
    int flood;
} calidata_t;

vj_effect *cali_init(int width, int height)
{
    //int i,j;
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = 0;
    ve->defaults[1] = 1;

    ve->description = "Image calibration";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 1;

    ve->param_description = vje_build_param_list( ve->num_params, "Mode", "Subtract Dark Current Only" );

	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][0],0, "Dark Frame", "Light Frame", "Calibrated Frame", "Normal" );

    return ve;
}

// this method is not part of FX api, instead it is directly called by libstream and libsample
int cali_prepare( void *ed, double meanY, double meanU, double meanV, uint8_t *data, int len, int uv_len )
{
	int	fl  = len + (2*uv_len);
	calidata_t *c = (calidata_t*) ed;
	c->b[0] = data;
	c->b[1] = c->b[0] + len;
	c->b[2] = c->b[1] + uv_len;
	c->l[0] = data + fl;
	c->l[1] = c->l[0] + len;
	c->l[2] = c->l[1] + uv_len;
	c->m[0] = c->l[0] + fl;
	c->m[1] = c->m[0] + len;
	c->m[2] = c->m[1] + uv_len;
	c->mean[0] = meanY;
	c->mean[1] = meanU;
	c->mean[2] = meanV;
	return 1;
}


void *cali_malloc(int width, int height)
{
    calidata_t *d = (calidata_t*) vj_calloc(sizeof(calidata_t));
	if(!d) 
		return NULL;
	return d;
}

void cali_free(void *ptr)
{
    calidata_t *d = (calidata_t*) ptr;
    free(d);
}

void cali_apply(void *ptr, VJFrame *frame, int *args ) {
    int mode = args[0];
    int full = args[1];

	calidata_t *c = (calidata_t*) ptr;

	if( c->b[0] == NULL || c->l[0] == NULL ||
		c->mean[0] <= 0.0 || c->mean[1] <= 0.0 ||
		c->mean[2] <= 0.0 ) {
		if( c->flood == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR,
			"Please select a calibration source (use source/channel list)");
		}
		c->flood = (c->flood + 1) % 25;
		return;
	}

	uint8_t *Y = frame->data[0];
	uint8_t *U = frame->data[1];
	uint8_t *V = frame->data[2];
	const int chroma = 127;
	const int uv_len = frame->uv_len;
	const int len = frame->len;
	int p,i;

	if( mode == 1 ) {
		//@ just show dark frame
		veejay_memcpy(Y, c->b[0], (len));
		veejay_memcpy(U, c->b[1], uv_len);
		veejay_memcpy(V, c->b[2], uv_len);
		return;
	} else if ( mode == 2 ) {
		//@ just show light frame
		veejay_memcpy(Y, c->l[0], (len));
		veejay_memcpy(U, c->l[1], uv_len);
		veejay_memcpy(V, c->l[2], uv_len);
		return;
	} else if ( mode == 3 ) {
		veejay_memcpy(Y, c->m[0], (len));
		veejay_memcpy(U, c->m[1], uv_len);
		veejay_memcpy(V, c->m[2], uv_len);
		return;
	}

	uint8_t *by = c->b[0];
	uint8_t *bu = c->b[1];
	uint8_t *bv = c->b[2];

	uint8_t *wy = c->m[0];
	uint8_t *wu = c->m[1];
	uint8_t *wv = c->m[2];

	//@ process master flat image
	if( full ) {

		for( i = 0; i < len; i ++ ) {
			p = ( Y[i] - by[i] ); 
			if( p < 0 )
				p = 0;
			if( wy[i] != 0 )
				Y[i] = c->mean[0] * p / wy[i];
			else
				Y[i] = 0;
		}

		for( i = 0; i < uv_len; i ++ ) {
			p = chroma + ((U[i]-chroma)-(bu[i]-chroma));
			if( p<0)
				p = 0;
			
			if( wu[i]==0 )
				U[i] = chroma;
			else
				U[i] = (uint8_t) ( c->mean[1] * p / wu[i]);
		
			p 	= chroma + ((V[i]-chroma)-(bv[i]-chroma));
			if( p < 0 )
				p = 0;

			if( wv[i] == 0 )
				V[i] = chroma;
			else
				V[i] = (uint8_t) ( c->mean[2] * p / wv[i] );
		}

	} else {
		//@ just show result of frame - dark current
#pragma omp simd
		for( i = 0; i <(len); i ++ ) {
			p = ( Y[i] - by[i] );
			if( p < 0 )
				Y[i] = pixel_Y_lo_;
			else
				Y[i] = p;
		}
#pragma omp simd
		for( i = 0; i < uv_len; i ++ ) {
			p = chroma + ( (U[i]-chroma) - (bu[i]-chroma));
			if( p < 0 )
				U[i] = chroma;
			else
				U[i] = p;

			p = chroma + ( (V[i]-chroma) - (bv[i]-chroma));
			if( p < 0 )
				V[i] = chroma;
			else
				V[i] = p;
		}
	}

}
