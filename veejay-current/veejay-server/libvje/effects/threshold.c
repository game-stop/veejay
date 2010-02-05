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
#include "threshold.h"
#include "common.h"
#include "softblur.h"

typedef int (*morph_func)(uint8_t *kernel, uint8_t mt[9] );

vj_effect *threshold_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);	/* max */
    ve->limits[0][0] = 0;  // threshold
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;  // reverse
    ve->limits[1][1] = 1;

    ve->defaults[0] = 40;
    ve->defaults[1] = 0;
    ve->description = "Map B from threshold mask";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
	ve->param_description = vje_build_param_list( ve->num_params, "Threshold", "Reverse" );
    return ve;
}


static uint8_t *binary_img;
#define    RUP8(num)(((num)+8)&~8)

int		threshold_malloc(int w, int h )
{
	binary_img = (uint8_t*) vj_malloc(sizeof(uint8_t) * RUP8(w * h) );
	if(!binary_img) return 0;
	return 1;
}

void		threshold_free(void)
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

#ifdef HAVE_ASM_MMX
#undef HAVE_K6_2PLUS
#if !defined( HAVE_ASM_MMX2) && defined( HAVE_ASM_3DNOW )
#define HAVE_K6_2PLUS
#endif

#undef _EMMS

#ifdef HAVE_K6_2PLUS
/* On K6 femms is faster of emms. On K7 femms is directly mapped on emms. */
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

static	void	load_threshold_mm7(uint8_t v)
{
	uint8_t mm[8] = { v,v,v,v, v,v,v,v };
	uint8_t *m = (uint8_t*) &(mm[0]);
	__asm __volatile(
		"movq	(%0),	%%mm7\n\t"
		:: "r" (m) );
}

static	void	binarify( uint8_t *dst, uint8_t *src, uint8_t threshold, int reverse,int w, int h )
{
	int len = (w * h)>>3;
	int i;
	uint8_t *s = src;
	uint8_t *d = dst;
	load_threshold_mm7( threshold );


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


#else
static	void	binarify( uint8_t *dst, uint8_t *src, int threshold,int reverse, int w, int h )
{
	const int len = w*h;
	int i;
	if(!reverse)
	for( i = 0; i < len; i ++ )
	{
		dst[i] = (  src[i] <= threshold ? 0: 0xff );
	}
	else
		for( i = 0; i < len; i ++ )
			dst[i] = (src[i] > threshold ? 0: 0xff );
}
#endif

void threshold_apply( VJFrame *frame, VJFrame *frame2,int width, int height, int threshold, int reverse )
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

	softblur_apply( frame, width,height,0 );

	binarify( binary_img,Y,threshold,reverse, width,height);

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

//	veejay_memset( Y, 0, width );
//	veejay_memset( Cb, 128, width );
//	veejay_memset( Cr, 128, width );

//	veejay_memset(Y+(len-width),0, width );
//	veejay_memset(Cb+(len-width),128,width);
//	veejay_memset(Cr+(len-width),128,width);

//	len -= width;

	if(!reverse)
	{
		for(y = 0; y < len; y += width  )
		{	
			for(x = 0; x < width; x ++)
			{	
				if(binary_img[x+y]) //@ found white pixel
				{
					Y[x+y] = Y2[x+y];
					Cb[x+y] = Cb2[x+y];
					Cr[x+y] = Cr2[x+y];

				}
				else //@ black
				{
					Y[x + y] = 0;
					Cb[x + y] = 128;
					Cr[x + y] = 128;
				}
			}
		}
	}
	else
	{
		for(y = 0; y < len; y += width  )
		{	
			for(x = 0; x < width; x ++)
			{	
				if(binary_img[x+y] == 0x0) //@ found black pixel
				{
					Y[x+y] = Y2[x+y];
					Cb[x+y]= Cb2[x+y];
					Cr[x+y]= Cr2[x+y];
				}
				else
				{ 
					Y[x+y] = 0x0;
					Cb[x+y] = 128; 
					Cr[x+y] = 128;
				}
			}
		}
	}
#endif
}
