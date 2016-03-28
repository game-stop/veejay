/* 
 * Linux VeeJay
 *
 * Copyright(C)2016 Niels Elburg <nwelburg@gmail.com>
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
#include <float.h>
#include <libvjmem/vjmem.h>
#include <math.h>
#include <libvje/vje.h>
#include <libyuv/yuvconv.h>
#include <libvjmsg/vj-msg.h>
#include <libsubsample/subsample.h>
#include "bgsubtractgauss.h"
#include "common.h"

static uint8_t *static_bg__ = NULL;
static uint8_t *static_bg_frame__[4] = { NULL,NULL,NULL,NULL };
static double *pMu = NULL;
static double *pVar = NULL;
static uint32_t bg_n = 0;
static double pNoise = 0.0;
static uint8_t *morph_frame__ = NULL;
static uint8_t *fg_frame__ = NULL;

vj_effect *bgsubtractgauss_init(int width, int height)
{
	vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
	ve->num_params = 6;
	ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
	ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);/* min */
	ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);/* max */
	ve->limits[0][0] = 0;	/* alpha max */
	ve->limits[1][0] = 25500;
	ve->limits[0][1] = 0;	/* threshold */
	ve->limits[1][1] = 25500;
	ve->limits[0][2] = 0;   /* noise */
	ve->limits[1][2] = 10000;
	ve->limits[0][3] = 0;
	ve->limits[1][3] = 4;   /* bg, fg, fg to alpha, fg and update bg, fg to alpha and update bg */
	ve->limits[0][4] = 1;
	ve->limits[1][4] = 500; /* maximum frame period, aprox. 20 seconds */
	ve->limits[0][5] = 0;
	ve->limits[1][5] = 1;   /* apply erosion and dilation on FG image (reduces framerate) */

	ve->defaults[0] = 2;  
	ve->defaults[1] = 122;
	ve->defaults[2] = 500;
	ve->defaults[3] = 0;   
	ve->defaults[4] = 25;   /* update bg every +/- second */
	ve->defaults[5] = 0;

	ve->description = "Subtract Background Gaussian";
	ve->extra_frame = 0;
	ve->sub_format = -1;
	ve->has_user = 1;
	ve->user_data = NULL;
	ve->sub_format = 0;
	ve->parallel = 1;

	ve->param_description = vje_build_param_list( ve->num_params, "Alpha Max", "Threshold", "Noise Level", "Mode", "Frame Period", "Morphology Level");
	ve->hints = vje_init_value_hint_list( ve->num_params );

	vje_build_value_hint_list( ve->hints, ve->limits[1][3], 3, 
					"Initialize Background",
					"Render FG, no update BG",
					"FG to Alpha, no update BG",
					"Render FG, periodically update BG",
					"FG to Alpha, periodically update BG" );

	vje_build_value_hint_list( ve->hints, ve->limits[1][5], 5,
					"No Erosion/Dilation of FG",
					"Erode/Dilate FG" );

	return ve;
}

static void bg_init( double noise, const int len )
{
	int i;
	for( i = 0; i < len; i ++ ) {
		pVar[i] = noise;
		pMu[i] = 0.0;
	}
	pNoise = noise;
}

int bgsubtractgauss_malloc(int width, int height)
{
	if(static_bg__ == NULL){
		static_bg__ = (uint8_t*) vj_malloc( RUP8(width*height*4));
		static_bg_frame__[0] = static_bg__;
		static_bg_frame__[1] = static_bg_frame__[0] + RUP8(width*height);
		static_bg_frame__[2] = static_bg_frame__[1] + RUP8(width*height);
		static_bg_frame__[3] = static_bg_frame__[2] + RUP8(width*height);

		veejay_memset( static_bg_frame__[1], 128, RUP8(width*height));
		veejay_memset( static_bg_frame__[2], 128, RUP8(width*height));
	}

	if( fg_frame__ == NULL ) {
		fg_frame__ = (uint8_t*) vj_calloc( RUP8(width*height) );
	}

	if( morph_frame__ == NULL ) {
		morph_frame__ = (uint8_t*) vj_calloc( RUP8(width*height));
	}

	if( pMu == NULL ) {
		pMu = (double*) vj_malloc( RUP8(sizeof(double) * width * height ));
	}

	if( pVar == NULL ) {
		pVar = (double*) vj_malloc( RUP8(sizeof(double) * width * height ));
		bg_init( 50.0, width * height );
	}

	bg_n = 0;

	return 1;
}

void bgsubtractgauss_free()
{
	if( static_bg__ ) {
		free(static_bg__ );

		static_bg_frame__[0] = NULL;
		static_bg_frame__[1] = NULL;
		static_bg_frame__[2] = NULL;
		static_bg_frame__[3] = NULL;
		static_bg__ = NULL;
	}

	if( fg_frame__ ) {
		free(fg_frame__);
		fg_frame__ = NULL;
	} 

	if( morph_frame__ ) {
		free(morph_frame__);
		morph_frame__ = NULL;
	}

	if( pVar ) {
		free(pVar);
		pVar = NULL;
	}

	if( pMu ) {
		free(pMu);
		pMu = NULL;
	}
}

int bgsubtractgauss_prepare(VJFrame *frame)
{
	if(!static_bg__ )
	{
		return 0;
	}
	
	//@ copy chroma planes only, bg is updated dynamically
	if( frame->ssm ) {
		veejay_memcpy( static_bg_frame__[1], frame->data[1], frame->len );
		veejay_memcpy( static_bg_frame__[2], frame->data[2], frame->len );
	}
	else {
		// if data is not subsampled, upsample chroma planes now 
		veejay_memcpy( static_bg_frame__[1], frame->data[1], frame->uv_len );
		veejay_memcpy( static_bg_frame__[2], frame->data[2], frame->uv_len );
		chroma_supersample( SSM_422_444, frame, static_bg_frame__ );
	}
		
	veejay_msg(2, "Subtract Background Gaussian: Snapped background frame (4:4:4)");

	return 1;
}

/* always returns chroma planes in 4:4:4 */
uint8_t* bgsubtractgauss_get_bg_frame(unsigned int plane)
{
	if( static_bg__ == NULL )
		return NULL;
	return static_bg_frame__[ plane ];
}

static void bgsubtractgauss_show_bg( VJFrame *frame )
{
	veejay_memcpy( frame->data[0], static_bg_frame__[0], frame->len );
}

static void bgsubtractgauss_show_fg( VJFrame *frame )
{
	veejay_memcpy( frame->data[0], fg_frame__, frame->len );
	veejay_memset( frame->data[1], 128, frame->uv_len );
	veejay_memset( frame->data[2], 128, frame->uv_len );
}

static void bg_subtract( VJFrame *frame, double threshold, uint8_t *A )
{
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	int i;
	for( i = 0; i < len; i ++ )
	{
		double dY = ((double) Y[i]) - pMu[i];
		double d2 = (dY * dY) / pVar[i];

		A[i]      = (d2 < threshold ? 0: 0xff);
	}
}

static void fg_erode( uint8_t *I, const int w, const int h, uint8_t *O )
{
	unsigned int x,y;
	const int len = w * h;
	const int aw = w - 1;

	for( y = w; y < len; y += w )
	{
		for( x = 1; x < aw; x ++ )
		{
			if( I[x + y] == 0xff )
			{
				if( I[ x - 1 + y - w ] == 0||
				    I[ x + y - w ] == 0 ||
			    	    I[ x + 1 + y - w ] == 0 ||
				    I[ x - 1 + y ] == 0 ||
				    I[ x + 1 + y ] == 0 ||
			            I[ x - 1 + y + w ] == 0 ||
				    I[ x + y + w ] == 0 ||
				    I[ x + 1 + y + w ] == 0 )
			    	  O[ x + y ] = 0;
				else
				  O[ x + y ] = 0xff;		
			}
			else {
				O[x+y] = 0;
			}

		}
	}
}

static void fg_dilate( uint8_t *I, const int w, const int h, uint8_t *O )
{
	unsigned int x,y;
	const int len = w * h;
	const int aw = w - 1;

	for( y = w; y < len; y += w )
	{
		for( x = 1; x < aw; x ++ )
		{
			if( I[x + y] == 0 )
			{
				if( I[ x - 1 + y - w ] ||
				    I[ x + y - w ] ||
			    	    I[ x + 1 + y - w ] ||
				    I[ x - 1 + y ] ||
				    I[ x + 1 + y ] ||
			            I[ x - 1 + y + w ] ||
				    I[ x + y + w ] ||
				    I[ x + 1 + y + w ] )
			    	  O[ x + y ] = 0xff;
				else
				  O[ x + y ] = 0;		
			}
			else {
				O[x+y] = 0xff;
			}

		}
	}
}

/*  
 *  The method 'bg_update' is originally from Scene 1.0.8 -- Background subtraction and object tracking for complex environments  
    Copyright (C) 2011-2015 Laurence Bender <lbender@untref.edu.ar>
    @see void BGModelGauss::UpdateCPU() 
 */
static void bg_update( VJFrame *frame, double threshold, double alpha, double noise )
{
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *bg = static_bg_frame__[0];
	unsigned int i;

	for( i = 0; i < len; i ++ )
	{
		double src = (double) Y[i];
		double d   = (src - pMu[i]) * (src - pMu[i]) - pVar[i];

		pMu[i]  += (alpha * (src - pMu[i]));
		pVar[i] += (alpha * d);

		pVar[i] = MAX(pVar[i], noise);
		bg[i]   = pMu[i];
	}
}

/* apply erosion to remove salt noise,
 * apply dilation to fill holes */
static void bgsubtractgauss_morph( VJFrame *frame, uint8_t *I, uint8_t *O )
{
	fg_erode( I, frame->width, frame->height, morph_frame__ );
	fg_dilate( morph_frame__, frame->width,frame->height, O );
}

void bgsubtractgauss_apply(VJFrame *frame, int alpha_max, int threshold, int noise, int mode, int period, int morphology )
{
	double g_alphaMax = ( (double)alpha_max) / 100.0;
	double g_noise = ( (double) noise ) / 10.0;
	double g_threshold = ( (double) threshold) / 100.0;

	switch( mode )
	{
		case 0:
			/* show background */
			bg_update( frame, g_threshold, g_alphaMax, g_noise );
			bgsubtractgauss_show_bg( frame );
			break;
		case 1:
			/* show foreground, no update of bg */
			bg_subtract( frame, g_threshold, fg_frame__ ); 

			if( morphology ) {
				bgsubtractgauss_morph( frame, fg_frame__, fg_frame__ );
			}	

			bgsubtractgauss_show_fg( frame );
			break;
		case 2:
			/* fill alpha channel with foreground, no update of bg */
			bg_subtract( frame, g_threshold, frame->data[3] );
			if( morphology ) {
				bgsubtractgauss_morph( frame, frame->data[3], frame->data[3] );
			}

			break;
		case 3:
			/* show foreground, update bg every period frames*/
			if( (bg_n % period) == 0 ) {
				bg_update( frame, g_threshold, g_alphaMax, g_noise );
			}

			bg_subtract( frame, g_threshold, fg_frame__ );

			if( morphology ) {
				bgsubtractgauss_morph( frame, fg_frame__, fg_frame__ );
			}

			bgsubtractgauss_show_fg( frame );

			bg_n ++;
			break;
		case 4:
			/* fill alpha channel with foreground, update bg every period frames */
			if( (bg_n % period) == 0 ) {
				bg_update( frame, g_threshold, g_alphaMax, g_noise );
			}

			bg_subtract( frame, g_threshold, frame->data[3] );

			if( morphology ) {
				bgsubtractgauss_morph( frame, frame->data[3], frame->data[3] );
			}

			bg_n ++;
			break;
	}
}
