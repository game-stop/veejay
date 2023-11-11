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
#include <veejaycore/vjmem.h>
#include <math.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/vj-msg.h>
#include <libsubsample/subsample.h>
#include "bgsubtractgauss.h"
#include "common.h"

typedef struct {
    uint8_t *static_bg__;
    uint8_t *fg_frame__;
    uint8_t *static_bg_frame__[4];
    double *pMu;
    double *pVar;
    uint32_t bg_n;
    uint8_t *morph_frame__;
    uint8_t *mean;
    int auto_hist;
} bgsubtract_t;

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
    ve->static_bg = 1;
	ve->parallel = 0;
	ve->global = 1; /* this FX is not freed when switching between samples */
	ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

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

static void bg_init( bgsubtract_t *b, double noise, const int len )
{
	int i;
    double *pVar = b->pVar;
    double *pMu = b->pMu;
	
#pragma omp simd
    for( i = 0; i < len; i ++ ) {
		pVar[i] = noise;
		pMu[i] = 0.0;
	}
}

void *bgsubtractgauss_malloc(int width, int height)
{
    bgsubtract_t *b = (bgsubtract_t*) vj_calloc(sizeof(bgsubtract_t));
    if(!b) {
        return NULL;
    }

	b->static_bg__ = (uint8_t*) vj_malloc( (width*height*4));
	if(!b->static_bg__ ) {
        free(b);
        return NULL;
    }

	b->static_bg_frame__[0] = b->static_bg__;
	b->static_bg_frame__[1] = b->static_bg_frame__[0] + (width*height);
	b->static_bg_frame__[2] = b->static_bg_frame__[1] + (width*height);
	b->static_bg_frame__[3] = b->static_bg_frame__[2] + (width*height);

	veejay_memset( b->static_bg_frame__[1], 128, (width*height));
	veejay_memset( b->static_bg_frame__[2], 128, (width*height));
	
	b->fg_frame__ = (uint8_t*) vj_calloc( (width*height*2) );
	if(!b->fg_frame__ ) {
        free(b->static_bg__);
        free(b);
        return NULL;
    }

	b->morph_frame__ = (uint8_t*) vj_calloc( (width*height));
	if(!b->morph_frame__) {
        free(b->static_bg__);
        free(b->fg_frame__);
        free(b);
        return NULL;
    }

	b->pMu = (double*) vj_malloc( (sizeof(double) * width * height ));
	if(!b->pMu ) {
		free(b->static_bg__);
		free(b->fg_frame__); 
		free(b->morph_frame__); 
		free(b);
        return NULL;
	}

	b->pVar = (double*) vj_malloc( (sizeof(double) * width * height ));
	if( !b->pVar ) {
        free(b->static_bg__);
		free(b->fg_frame__); 
		free(b->morph_frame__);
        free(b->pMu);
		free(b);
		return NULL;
    }
	
	b->mean =  (uint8_t*) vj_calloc( (width*height) );
	if( !b->mean ) {
	    free(b->static_bg__);
		free(b->fg_frame__); 
		free(b->morph_frame__);
        free(b->pMu);
        free(b->pVar);
		free(b);
		return NULL;
    }

    const char *hist = getenv( "VEEJAY_BG_AUTO_HISTOGRAM_EQ" );
	if( hist ) {
		b->auto_hist = atoi( hist );
	}


	veejay_msg( VEEJAY_MSG_INFO,
			"You can enable/disable the histogram equalizer by setting env var VEEJAY_BG_AUTO_HISTOGRAM_EQ" );
	veejay_msg( VEEJAY_MSG_INFO,
			"Histogram equalization is %s", (b->auto_hist ? "enabled" : "disabled" ));

	b->bg_n = 0;

    bg_init( b, 50.0, width * height );

    return (void*) b;
}

void bgsubtractgauss_free(void *ptr) {
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    free(b->static_bg__);
    free(b->fg_frame__); 
	free(b->morph_frame__);
    free(b->pMu);
    free(b->pVar);
    free(b->mean);
    free(b);
}

int bgsubtractgauss_prepare(void *ptr, VJFrame *frame)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;
	
    //@ copy chroma planes only, bg is updated dynamically
	if( frame->ssm ) {
		veejay_memcpy( b->static_bg_frame__[1], frame->data[1], frame->len );
		veejay_memcpy( b->static_bg_frame__[2], frame->data[2], frame->len );
	}
	else {
		// if data is not subsampled, upsample chroma planes now 
		veejay_memcpy( b->static_bg_frame__[1], frame->data[1], frame->uv_len );
		veejay_memcpy( b->static_bg_frame__[2], frame->data[2], frame->uv_len );
		chroma_supersample( SSM_422_444, frame, b->static_bg_frame__ );
	}
		
	veejay_msg(2, "Subtract Background Gaussian: Snapped background frame (4:4:4)");

	return 1;
}

/* always returns chroma planes in 4:4:4 */
uint8_t* bgsubtractgauss_get_bg_frame(void *ptr, unsigned int plane)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;
	if( b->static_bg__ == NULL )
		return NULL;
	return b->static_bg_frame__[ plane ];
}

static void bgsubtractgauss_show_bg( bgsubtract_t *b, VJFrame *frame )
{
	veejay_memcpy( frame->data[0], b->static_bg_frame__[0], frame->len );
	veejay_memset( frame->data[1], 128, frame->uv_len );
	veejay_memset( frame->data[2], 128, frame->uv_len );
}

static void bgsubtractgauss_show_fg( bgsubtract_t *b, VJFrame *frame )
{
	veejay_memcpy( frame->data[0], b->fg_frame__, frame->len );
	veejay_memset( frame->data[1], 128, frame->uv_len );
	veejay_memset( frame->data[2], 128, frame->uv_len );
}

static void bg_subtract( bgsubtract_t *b, VJFrame *frame, double threshold, uint8_t *A )
{
	const int len = frame->len;
	const uint8_t *Y = frame->data[0];
	unsigned int i;

    double *pMu = b->pMu;
    double *pVar = b->pVar;

#pragma omp simd
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
	const int len = (w * h) - w;
	const int aw = w - 1;

	for( y = w; y < len; y += w )
	{
		for( x = 1; x < aw; x ++ )
		{
			/*if( I[x + y] == 0xff )
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
			} */
            uint8_t isfg = (I[x + y] == 0xff);

            isfg &= (I[x - 1 + y - w] &&
                             I[x + y - w] &&
                             I[x + 1 + y - w] &&
                             I[x - 1 + y] &&
                             I[x + 1 + y] &&
                             I[x - 1 + y + w] &&
                             I[x + y + w] &&
                             I[x + 1 + y + w]);

            O[x + y] = isfg ? 0xff : 0;
		}
	}
}

static void fg_dilate( uint8_t *I, const int w, const int h, uint8_t *O )
{
	unsigned int x,y;
	const int len = (w * h) - w;
	const int aw = w - 1;

	for( y = w; y < len; y += w )
	{
		for( x = 1; x < aw; x ++ )
		{
			/*if( I[x + y] == 0 )
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
			}*/
            uint8_t isfg = (I[x + y] == 0);
            isfg |= (I[x - 1 + y - w] ||
                     I[x + y - w] ||
                     I[x + 1 + y - w] ||
                     I[x - 1 + y] ||
                     I[x + 1 + y] ||
                     I[x - 1 + y + w] ||
                     I[x + y + w] ||
                     I[x + 1 + y + w]);

            O[x + y] = isfg ? 0xff : 0;
		}
	}
}

/*  
 *  The method 'bg_update' is originally from Scene 1.0.8 -- Background subtraction and object tracking for complex environments  
    Copyright (C) 2011-2015 Laurence Bender <lbender@untref.edu.ar>
    @see void BGModelGauss::UpdateCPU() 
 */
static void bg_update( bgsubtract_t *b, VJFrame *frame, double threshold, double alpha, double noise )
{
	const int len = frame->len;
	uint8_t *Y = frame->data[0];
	uint8_t *bg = b->static_bg_frame__[0];
	unsigned int i;
    double *pMu = b->pMu;
    double *pVar = b->pVar;

#pragma omp simd
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
static void bgsubtractgauss_morph( bgsubtract_t *b, VJFrame *frame, uint8_t *I, uint8_t *O )
{
	fg_erode( I, frame->width, frame->height, b->morph_frame__ );
	fg_dilate( b->morph_frame__, frame->width,frame->height, O );
}

void bgsubtractgauss_apply(void *ptr, VJFrame *frame, int *args) {
    int alpha_max = args[0];
    int threshold = args[1];
    int noise = args[2];
    int mode = args[3];
    int period = args[4];
    int morphology = args[5];

	double g_alphaMax = ( (double)alpha_max) / 100.0;
	double g_noise = ( (double) noise ) / 10.0;
	double g_threshold = ( (double) threshold) / 100.0;

    bgsubtract_t *b = (bgsubtract_t*) ptr;

	if( b->auto_hist )
		vje_histogram_auto_eq( frame );

	switch( mode )
	{
		case 0:
			/* show background */
			bg_update( b,frame, g_threshold, g_alphaMax, g_noise );
			bgsubtractgauss_show_bg( b,frame );
			break;
		case 1:
			/* show foreground, no update of bg */
			bg_subtract( b,frame, g_threshold,b->fg_frame__ ); 
			if( morphology ) {
				bgsubtractgauss_morph( b, frame, b->fg_frame__, b->fg_frame__ );
			}	

			bgsubtractgauss_show_fg( b,frame );
			break;
		case 2:
			// fill alpha channel with foreground, no update of bg 
			bg_subtract(b, frame, g_threshold,frame->data[3] );
			if( morphology ) {
				bgsubtractgauss_morph( b,frame, frame->data[3], frame->data[3] );
			}

			break;
		case 3:
			// show foreground, update bg every period frames
			bg_subtract(b, frame, g_threshold,b->fg_frame__ );
			if( (b->bg_n % period) == 0 ) {
				bg_update( b,frame, g_threshold, g_alphaMax, g_noise );
			}


			if( morphology ) {
				bgsubtractgauss_morph( b,frame, b->fg_frame__, b->fg_frame__ );
			}

			bgsubtractgauss_show_fg( b,frame );

			b->bg_n ++;
			break;
		case 4:
			// fill alpha channel with foreground, update bg every period frames 
			bg_subtract( b,frame, g_threshold,frame->data[3] );
			if( (b->bg_n % period) == 0 ) {
				bg_update(b, frame, g_threshold, g_alphaMax, g_noise );
			}

			if( morphology ) {
				bgsubtractgauss_morph( b,frame, frame->data[3], frame->data[3] );
			}

			b->bg_n ++;
			break;
	}
}
