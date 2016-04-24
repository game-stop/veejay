/* 
 * Linux VeeJay
 *
 * Copyright(C)2004-2015 Niels Elburg <nwelburg@gmail.com>
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

#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "rgbkey.h"
#include <stdlib.h>
#include <math.h>
#include "common.h"
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>

extern int yuv_sws_get_cpu_flags();

vj_effect *rgbkey_init(int w,int h)
{
    vj_effect *ve;
    ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->defaults[0] = 15;	/* tolerance near */
    ve->defaults[1] = 0;	/* r */
    ve->defaults[2] = 255;	/* g */
    ve->defaults[3] = 0;	/* b */
    ve->defaults[4] = 1;	/* tolerance far */
	ve->defaults[5] = 0;	/* level min */
	ve->defaults[6] = 0xff; /* level max */
	ve->defaults[7] = 0;    /* alpha-in operator */

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 255;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 255;

    ve->limits[0][4] = 0;
    ve->limits[1][4] = 255;

    ve->limits[0][5] = 0;
    ve->limits[1][5] = 255;

    ve->limits[0][6] = 0;
    ve->limits[1][6] = 255; 

    ve->limits[0][7] = 0;
    ve->limits[1][7] = 4; 

	ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_SRC_B | FLAG_ALPHA_OPTIONAL | FLAG_ALPHA_IN_OPERATOR;

	ve->param_description = vje_build_param_list(ve->num_params, 
			"Tolerance Near", "Red", "Green", "Blue", "Tolerance Far","Level Min", "Level Max", "Alpha-IN operator");

	ve->hints = vje_init_value_hint_list( ve->num_params );

	ve->has_user = 0;
    ve->description = "Chroma Key (RGB)";
    ve->extra_frame = 1;
    ve->sub_format = 1;
	ve->rgb_conv = 1;
    ve->parallel = 0;

	vje_build_value_hint_list( ve->hints, ve->limits[1][7],7, 
			
			"Ignore Alpha-IN", "Alpha-IN A", "Alpha-IN B", "Alpha-IN A or B", "Alpha-In A and B" );

	return ve;
}

typedef struct {
    float              radius;
    float              strength;
    int                threshold;
    float              quality;
    struct SwsContext *filter_context;
} FilterParam;

static int alloc_sws_context(FilterParam *f, int width, int height, unsigned int flags)
{
    SwsVector *vec;
    SwsFilter sws_filter;

    vec = sws_getGaussianVec(f->radius, f->quality);

    if (!vec)
        return 0;

    sws_scaleVec(vec, f->strength);
    vec->coeff[vec->length / 2] += 1.0 - f->strength;
    sws_filter.lumH = sws_filter.lumV = vec;
    sws_filter.chrH = sws_filter.chrV = NULL;
    f->filter_context = sws_getCachedContext(NULL,
                                             width, height, AV_PIX_FMT_GRAY8,
                                             width, height, AV_PIX_FMT_GRAY8,
                                             flags, &sws_filter, NULL, NULL);

    sws_freeVec(vec);

    if (!f->filter_context)
        return 0;

    return 1;
}

static uint8_t *temp[2];
static FilterParam *gaussfilter = NULL;
static uint8_t __lookup_table[256];

int	rgbkey_malloc(int w, int h)
{
	gaussfilter = (FilterParam*) vj_calloc(sizeof(FilterParam));
	gaussfilter->radius = 1.3f;
	gaussfilter->strength = 1.0f;
	gaussfilter->quality = 3.0f;

	if(!alloc_sws_context( gaussfilter,w,h,yuv_sws_get_cpu_flags() ) )
		return 0;
	
	temp[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(w*h*2));
	if(temp[0] == NULL)
		return 0;
	temp[1] = temp[0] + RUP8(w*h);	
	return 1;
}

void	rgbkey_free()
{
	if(temp[0]) {
		free(temp[0]);
		temp[0] = NULL;
		temp[1] = NULL;
	}

	if( gaussfilter->filter_context ) {
		sws_freeContext( gaussfilter->filter_context );
		gaussfilter->filter_context = NULL;
	}

	if( gaussfilter ) {
		free(gaussfilter);
		gaussfilter = NULL;
	}
}

static void gaussblur(uint8_t *dst, const int dst_linesize,const uint8_t *src, const int src_linesize,
                 const int w, const int h,struct SwsContext *filter_context)
{
    const uint8_t* const src_array[4] = {src};
    uint8_t *dst_array[4]             = {dst};
    int src_linesize_array[4] = {src_linesize};
    int dst_linesize_array[4] = {dst_linesize};

    sws_scale(filter_context, src_array, src_linesize_array,
              0, h, dst_array, dst_linesize_array);
}

/*
 * originally from http://gc-films.com/chromakey.html
 */
static inline double color_distance( uint8_t Cb, uint8_t Cr, int Cbk, int Crk, double dA, double dB )
{
		double tmp = 0.0; 
		fast_sqrt( tmp, (Cbk - Cb) * (Cbk-Cb) + (Crk - Cr) * (Crk - Cr) );

		if( tmp < dA ) { /* near color key == bg */
			return 0.0; /* near */
		}
		if( tmp < dB ) { /* middle region */
			return (tmp - dA)/(dB - dA); /* distance to key color */
		}
		return 1.0; /* far from color key == fg */
}

void rgbkey_apply(VJFrame *frame, VJFrame *frame2, int tola, int r, int g,int b,
                  int tolb, int min, int max, int operator)
{
	unsigned int pos;
	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2= frame2->data[1];
	uint8_t *Cr2= frame2->data[2];
	uint8_t *A = frame->data[3];
	uint8_t *B = frame2->data[3];
	const unsigned int len = frame->len;
	int iy=0,iu=128,iv=128;
	uint8_t *T = temp[0];
	uint8_t op0,op1;

	double dtola = (double) tola + 0.5f;
	double dtolb = (double) tolb + 0.5f;

	/* get key color */
	_rgb2yuv(r,g,b,iy,iu,iv);

	/* euclidean distance between key color and chroma */
	// introduces spill 
	switch( operator ) {
		case ALPHA_IGNORE:	
			//ignore alpha-in
			for (pos = len; pos != 0; pos--) {
				T[pos] = (uint8_t)( 255.0 * color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb ) );
			}
			break;
		case ALPHA_IN_A:
			for (pos = len; pos != 0; pos--) {
				if(A[pos] == 0)
					continue;
				T[pos] = (uint8_t)( 255.0 * color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb ) );
			}
			break;
		case ALPHA_IN_A_OR_B:
			for (pos = len; pos != 0; pos--) {
				if(A[pos] == 0 || B[pos] == 0)
					continue;
				T[pos] = (uint8_t)( 255.0 * color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb ) );
			}
			break;
		case ALPHA_IN_A_AND_B:
			for (pos = len; pos != 0; pos--) {
				if(A[pos] == 0 && B[pos] == 0)
					continue;
				T[pos] = (uint8_t)( 255.0 * color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb ) );
			}
			break;
		case ALPHA_IN_B:
			for (pos = len; pos != 0; pos--) {
				if(B[pos] == 0)
					continue;
				T[pos] = (uint8_t)( 255.0 * color_distance( Cb[pos],Cr[pos],iu,iv,dtola,dtolb ) );
			}
			break;
	}

	/* choke matte */
	// reduces detail
	gaussblur( A, frame->width, temp[0], frame->width, frame->width,frame->height,gaussfilter->filter_context );

	/* level correction table */
	__init_lookup_table( __lookup_table, 256, (float)min, (float)max, 0, 0xff ); 

	/* composite bg onto fg */
	for( pos = 0; pos < len; pos ++ ) {
		op0     = __lookup_table[A[pos]];
		op1     = 0xff - op0;
		Y[pos]  = ((op0 * Y[pos]) + (op1 * Y2[pos]))>> 8;
	    Cb[pos] = ((op0 * Cb[pos]) + (op1 * Cb2[pos]))>> 8;
		Cr[pos] = ((op0 * Cr[pos]) + (op1 * Cr2[pos]))>>8;
	}

}
