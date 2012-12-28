/* 
 * Linux VeeJay
 *
 * Copyright(C)2006 Niels Elburg <nwelburg@gmail.com>
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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include "diffmap.h"
#include "common.h"
#include "softblur.h"

typedef int (*morph_func)(uint8_t *kernel, uint8_t mt[9] );

vj_effect *differencemap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  // threshold
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;  // reverse
    ve->limits[1][1] = 1;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1; // show map
    ve->defaults[0] = 40;
    ve->defaults[1] = 0;
    ve->defaults[2] = 1;
    ve->description = "Map B to A (bitmask)";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Reverse", "Show");
    return ve;
}


static uint8_t *binary_img = NULL;
static int nframe = 0;
#define    RUP8(num)(((num)+8)&~8)

int		differencemap_malloc(int w, int h )
{
	binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w*h*2) );
	nframe = 0;
	if(!binary_img) return 0;
	return 1;
}

void		differencemap_free(void)
{
	if(binary_img) 
		free(binary_img);
	binary_img = NULL;
}

#ifndef MIN
#define MIN(a,b) ( (a)>(b) ? (b) : (a) )
#endif
#ifndef MAX
#define MAX(a,b) ( (a)>(b) ? (a) : (b) )
#endif

static int _dilate_kernel3x3( uint8_t *kernel, uint8_t img[9])
{
	register int x;
	/* consider all background pixels (0) in input image */	
	for(x = 0; x < 9; x ++ )
		if((kernel[x] * img[x]) > 0 )
			return 1;
	return 0;
}
/*
#ifdef HAVE_ASM_MMX
#undef HAVE_K6_2PLUS
#if !defined( HAVE_ASM_MMX2) && defined( HAVE_ASM_3DNOW )
#define HAVE_K6_2PLUS
#endif

#undef _EMMS

#ifdef HAVE_K6_2PLUS
#define _EMMS     "femms"
#else
#define _EMMS     "emms"
#endif

static	inline	void	load_binary_map( uint8_t *mask )
{
	__asm __volatile(
		"movq	(%0),	%%mm0\n\t"
		:: "r" (mask) 
	);
}

static		inline	void	map_luma( uint8_t *dst, uint8_t *B )
//static	inline	void	map_luma( uint8_t *dst, uint8_t *B, uint8_t *mask )
{
	__asm __volatile(
	//	"movq	(%0),	%%mm0\n\t"
		"movq	(%0),	%%mm1\n\t"
		"pand	%%mm0,  %%mm1\n\t"
		"movq	%%mm1,  (%1)\n\t"
	//	:: "r" (mask), "r" (B), "r" (dst) 
		:: "r" (B) , "r" (dst)
	);
}

static	inline	void	load_chroma( uint8_t val )
{
	uint8_t mask[8] = { val,val,val,val, val,val,val,val };
	uint8_t *m = &mask[0];

	__asm __volatile(
		"movq	(%0),	%%mm3\n\t # mm3: 128,128,128,128, ..."  
		:: "r" (m)
	);
}

static	inline	void	map_chroma( uint8_t *dst, uint8_t *B )
{
	__asm	__volatile(
		"movq	(%0),	%%mm1\n\t"
		"pand	%%mm0,   %%mm1\n\t"
		"pxor	%%mm5,  %%mm5\n\t"
		"pcmpeqb %%mm1,%%mm5\n\t"
		"pand	%%mm3,%%mm5\n\t"
		"paddb	%%mm5,%%mm1\n\t"
		"movq	%%mm1,  (%1) \n\t"
		:: "r" (B), "r" (dst)
	);

}

static	void	load_differencemapmm7(uint8_t v)
{
	uint8_t mm[8] = { v,v,v,v, v,v,v,v };
	uint8_t *m = (uint8_t*) &(mm[0]);
	__asm __volatile(
		"movq	(%0),	%%mm7\n\t"
		:: "r" (m) );
}
#endif



static	void	binarify( uint8_t *dst, uint8_t *src, uint8_t *prev, uint8_t threshold, int reverse,int w, int h )
{
	int len = (w * h)>>3;
	int i;
	uint8_t *s = src;
	uint8_t *d = dst;
	load_differencemapmm7( threshold );


	uint8_t *p = dst;

	for( i = 0; i < len ; i ++ )
	{
		__asm __volatile(
			"movq (%0),%%mm0\n\t"
			"pcmpgtb %%mm7,%%mm0\n\t"
			"movq %%mm0,(%1)\n\t"
			:: "r" (s), "r" (d)
		);
		s += 8;
		d += 8;
	}

	if( reverse )
	{
		__asm __volatile(
			"pxor	%%mm4,%%mm4" ::
			 );
		for( i = 0; i < len ; i ++ )
		{
			__asm __volatile(
			     "movq	(%0), %%mm0\n\t"
	      		     "pcmpeqb  %%mm4,  %%mm0\n\t"
        		     "movq   %%mm0,  (%1)\n\t"
			:: "r" (p), "r" (p) 
			);
			p += 8;
		}
	}
}


#else*/
static	void	binarify( uint8_t *dst, uint8_t *src,int threshold,int reverse, int w, int h )
{
	const int len = w*h;
	int i;
	if(!reverse)
	{
		for( i = 0; i < len; i ++ )
			dst[i] = (  src[i] <= threshold ? 0: 0xff );
	}
	else
	{
		for( i = 0; i < len; i ++ )
			dst[i] = ( src[i] >= threshold ? 0: 0xff );
	}
}

void differencemap_apply( VJFrame *frame, VJFrame *frame2,int width, int height, int threshold, int reverse,
		int show )
{
	unsigned int i,x,y;
	int len = (width * height);
    	uint8_t *Y = frame->data[0];
	uint8_t *Cb = frame->data[1];
	uint8_t *Cr = frame->data[2];
	uint8_t *Y2 = frame2->data[0];
	uint8_t *Cb2=frame2->data[1];
	uint8_t *Cr2=frame2->data[2];

	const uint8_t kernel[9] = { 1,1,1, 1,1,1, 1,1,1 };
	uint8_t *bmap = binary_img;

//	morph_func	p = _dilate_kernel3x3;
	uint8_t *previous_img = binary_img + len;
//@	take copy of image
	vj_frame_copy1( Y, previous_img, len );

	VJFrame tmp;
	veejay_memcpy(&tmp, frame, sizeof(VJFrame));
	tmp.data[0] = previous_img;
	softblur_apply( &tmp, width,height,0 );

	binarify( binary_img,previous_img,threshold,reverse, width,height);
/*

#ifdef HAVE_ASM_MMX
	int work = (width*height)>>3;
	load_chroma( 128 );
	for( y = 0 ; y < work; y ++ )
	{
		load_binary_map( bmap );
		map_luma(Y , Y2 );
		map_chroma( Cb, Cb2 );
		map_chroma( Cr, Cr2 );
		//@ we could mmx-ify dilation
		Y  += 8;
		Y2 += 8;	
		Cb += 8;
		Cb2 += 8;
		Cr += 8;
		Cr2 +=8;
		bmap += 8; 
	}

 	 __asm__ __volatile__ ( _EMMS:::"memory");
#else
*/

	//@ clear image

	if(show)
	{
		vj_frame_copy1( binary_img, frame->data[0], len );
		vj_frame_clear1( frame->data[1],128, len);
		vj_frame_clear1(frame->data[2],128, len);
		return;
	}

	veejay_memset( Y, 0, width );
	veejay_memset( Cb, 128, width );
	veejay_memset( Cr, 128, width );

	len -= width;

//	if(!reverse)
//	{
		for(y = width; y < len; y += width  )
		{	
			for(x = 1; x < width-1; x ++)
			{	
				if(binary_img[x+y]) //@ found white pixel
				{
				/*	uint8_t mt[9] = {
					binary_img[x-1+y-width], binary_img[x+y-width], binary_img[x+1+y-width],
					binary_img[x-1+y], 	binary_img[x+y]	    , binary_img[x+1+y],
					binary_img[x-1+y+width], binary_img[x+y+width], binary_img[x+1+y+width]
					};
					if( p( kernel, mt ) ) //@ replace pixel for B
					{
						 Y[x + y] = Y2[x+y];
						Cb[x + y] = Cb2[1][x+y];
						Cr[x + y] = Cr[2][x+y];
					}
					else //@ black
					{
						Y[x + y] = 0;
						Cb[x + y] = 128;
						Cr[x+ y] = 128;
					}*/
					Y[x+y] = Y2[x+y];
					Cb[x+y] = Cb2[x+y];
					Cr[x+y] = Cr2[x+y];
				}
				else
				{
					Y[x+y] = 0;
					Cb[x+y] = 128;
					Cr[x+y] = 128;
				}
			}
		}
//	}
/*	else
	{
		for(y = width; y < len; y += width  )
		{	
			for(x = 1; x < width-1; x ++)
			{	
				if(!binary_img[x+y]) //@ found black pixel
				{
				uint8_t mt[9] = {
					0xff-binary_img[x-1+y-width], 0xff-binary_img[x+y-width], 0xff-binary_img[x+1+y-width],
					0xff-binary_img[x-1+y], 	0xff-binary_img[x+y]	    , 0xff-binary_img[x+1+y],
					0xff-binary_img[x-1+y+width], 0xff-binary_img[x+y+width], 0xff-binary_img[x+1+y+width]
					};
				if( p( kernel, mt ) )
				{
					 Y[x + y] = frame2->data[0][x+y];
					Cb[x + y] = frame2->data[1][x+y];
					Cr[x + y] = frame2->data[2][x+y];
				}
				else
				{
					Y[x + y] = 0;
					Cb[x + y] = 128;
					Cr[x + y] = 128;
				}
			}
		}
	}
//#endif
*/
}
