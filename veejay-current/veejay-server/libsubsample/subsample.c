/*
 * subsample.c:  Routines to do chroma subsampling.  ("Work In Progress")
 *
 *
 *  Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
 *                2004 Niels Elburg <nwelburg@gmail.com>
 *                2014 added mmx routines
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#include <config.h>
#include <stdint.h>
#ifdef HAVE_ASM_MMX
#include <libyuv/mmx.h>
#include <libyuv/mmx_macros.h>
#include "subsample-mmx.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <mjpegtools/mjpeg_types.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <libvje/vje.h>
#include <libyuv/yuvconv.h>
#include <veejay/vj-task.h>

const char *ssm_id[SSM_COUNT] = {
  "unknown",
  "420_jpeg",
  "420_mpeg2",
#if 0
  "420_dv_pal",
  "411_dv_ntsc"
#endif
};


const char *ssm_description[SSM_COUNT] = {
  "unknown/illegal",
  "4:2:0, JPEG/MPEG-1, interstitial siting",
  "4:2:0, MPEG-2, horizontal cositing",
#if 0
  "4:2:0, DV-PAL, cosited, Cb/Cr line alternating",
  "4:1:1, DV-NTSC"
  "4:2:2",
#endif
};

// forward decl
void ss_420_to_422(uint8_t *buffer, int width, int height);
void ss_422_to_420(uint8_t *buffer, int width, int height);

/*************************************************************************
 * Chroma Subsampling
 *************************************************************************/


/* vertical/horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *      C       C
 *    Y   Y   Y   Y
 *
 *    Y   Y   Y   Y
 *      C       C
 *    Y   Y   Y   Y
 *
 */

/*
static void ss_444_to_420jpeg(uint8_t *buffer, int width, int height)
{
  uint8_t *in0, *in1, *out;
  int x, y;

  in0 = buffer;
  in1 = buffer + width;
  out = buffer;
  for (y = 0; y < height; y += 2) {
    for (x = 0; x < width; x += 2) {
      *out = (in0[0] + in0[1] + in1[0] + in1[1]) >> 2;
      in0 += 2;
      in1 += 2;
      out++;
    }
    in0 += width;
    in1 += width;
  }
}
*/
/*

        using weighted averaging for subsampling 2x2 -> 1x1
	here, 4 pixels are filled in each inner loop, (weighting
        16 source pixels)
*/

static void ss_444_to_420jpeg(uint8_t *buffer, int width, int height)
{
  const uint8_t *in0, *in1;
  uint8_t *out;
  int x, y = height;
  in0 = buffer;
  in1 = buffer + width;
  out = buffer;
  for (y = 0; y < height; y += 4) {
    for (x = 0; x < width; x += 4) {
     out[0] = (in0[0] + 3 * (in0[1] + in1[0]) + (9 * in1[1]) + 8) >> 4;
     out[1] = (in0[2] + 3 * (in0[3] + in1[2]) + (9 * in1[3]) + 8) >> 4;
     out[2] = (in0[4] + 3 * (in0[5] + in1[4]) + (9 * in1[5]) + 8) >> 4;
     out[3] = (in0[6] + 3 * (in0[7] + in1[6]) + (9 * in1[7]) + 8) >> 4;

      in0 += 8;
      in1 += 8;
      out += 4;
    }
    for (  ; x < width; x +=2 )
    {
 	out[0] = (in0[0] + 3 * (in0[1] + in1[0]) + (9 * in1[1]) + 8) >> 4;
        in0 += 2;
        in1 += 2;
	out++;
    }
    in0 += width*2;
    in1 += width*2;
  }
}
static void ss_444_to_420jpeg_cp(uint8_t *buffer,uint8_t *dest, int width, int height)
{
  const uint8_t *in0, *in1;
  uint8_t *out;
  int x, y = height;
  in0 = buffer;
  in1 = buffer + width;
  out = dest;
  for (y = 0; y < height; y += 4) {
    for (x = 0; x < width; x += 4) {
     out[0] = (in0[0] + 3 * (in0[1] + in1[0]) + (9 * in1[1]) + 8) >> 4;
     out[1] = (in0[2] + 3 * (in0[3] + in1[2]) + (9 * in1[3]) + 8) >> 4;
     out[2] = (in0[4] + 3 * (in0[5] + in1[4]) + (9 * in1[5]) + 8) >> 4;
     out[3] = (in0[6] + 3 * (in0[7] + in1[6]) + (9 * in1[7]) + 8) >> 4;

      in0 += 8;
      in1 += 8;
      out += 4;
    }
    for (  ; x < width; x +=2 )
    {
 	out[0] = (in0[0] + 3 * (in0[1] + in1[0]) + (9 * in1[1]) + 8) >> 4;
        in0 += 2;
        in1 += 2;
	out++;
    }
    in0 += width*2;
    in1 += width*2;
  }
}
/* horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *    C   C   C   C     in0 
 *    Y   Y   Y   Y         
 *    C   C   C   C      
 *           
 *    Y   Y   Y   Y       
 *    C       C     	out0  
 *    Y   Y   Y   Y       
 *    C       C  
 *
 *
 */             



 
/* vertical/horizontal interstitial siting
 *
 *    Y   Y   Y   Y
 *      C       C       C      inm
 *    Y   Y   Y   Y           
 *                  
 *    Y   Y   Y - Y           out0
 *      C     | C |     C      in0
 *    Y   Y   Y - Y           out1
 *
 *
 *      C       C       C      inp
 *
 *
 *  Each iteration through the loop reconstitutes one 2x2 block of 
 *   pixels from the "surrounding" 3x3 block of samples...
 *  Boundary conditions are handled by cheap reflection; i.e. the
 *   center sample is simply reused.
 *              
 */             


#define BLANK_CRB in0[1]
#define BLANK_CRB_2 (in0[1] << 1)

static void tr_420jpeg_to_444(uint8_t *data, uint8_t *buffer, int width, int height)
{
  uint8_t *inm, *in0, *inp, *out0, *out1;
  uint8_t cmm, cm0, cmp, c0m, c00, c0p, cpm, cp0, cpp;
  int x, y;

  uint8_t *saveme = data;

  veejay_memcpy(saveme, buffer, width);

  in0 = buffer + ( width * height /4) - 2;
  inm = in0 - width/2;
  inp = in0 + width/2;
  out1 = buffer + (width * height) - 1;
  out0 = out1 - width;

  for (y = height; y > 0; y -= 2) {
    if (y == 2) {
      in0 = saveme + width/2 - 2;
      inp = in0 + width/2;
    }
    for (x = width; x > 0; x -= 2) {
#if 0
      if ((x == 2) && (y == 2)) {
	cmm = in0[1];
	cm0 = in0[1];
	cmp = in0[2];
	c0m = in0[1];
	c0p = in0[2];
	cpm = inp[1];
	cp0 = inp[1];
	cpp = inp[2];
      } else if ((x == 2) && (y == height)) {
	cmm = inm[1];
	cm0 = inm[1];
	cmp = inm[2];
	c0m = in0[1];
	c0p = in0[2];
	cpm = in0[1];
	cp0 = in0[1];
	cpp = in0[2];
      } else if ((x == width) && (y == height)) {
	cmm = inm[0];
	cm0 = inm[1];
	cmp = inm[1];
	c0m = in0[0];
	c0p = in0[1];
	cpm = in0[0];
	cp0 = in0[1];
	cpp = in0[1];
      } else if ((x == width) && (y == 2)) {
	cmm = in0[0];
	cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else if (x == 2) {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else if (y == 2) {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else if (x == width) {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else if (y == height) {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      } else {
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
      }
      c00 = in0[1];

      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
#else
      cmm = ((x == 2) || (y == 2)) ? BLANK_CRB : inm[0];
      cm0 = (y == 2) ? BLANK_CRB : inm[1];
      cmp = ((x == width) || (y == 2)) ? BLANK_CRB : inm[2];
      c0m = (x == 2) ? BLANK_CRB : in0[0];
      c00 = in0[1];
      c0p = (x == width) ? BLANK_CRB : in0[2];
      cpm = ((x == 2) || (y == height)) ? BLANK_CRB : inp[0];
      cp0 = (y == height) ? BLANK_CRB : inp[1];
      cpp = ((x == width) || (y == height)) ? BLANK_CRB : inp[2];
#endif
      inm--;
      in0--;
      inp--;

      *(out1--) = (1*cpp + 3*(cp0+c0p) + 9*c00 + 8) >> 4;
      *(out1--) = (1*cpm + 3*(cp0+c0m) + 9*c00 + 8) >> 4;
      *(out0--) = (1*cmp + 3*(cm0+c0p) + 9*c00 + 8) >> 4;
      *(out0--) = (1*cmm + 3*(cm0+c0m) + 9*c00 + 8) >> 4;
    }
    out1 -= width;
    out0 -= width;
  }
}

// lame box filter
// the dampening of high frequencies depend
// on the directions these frequencies occur in the
// image, resulting in clear edges between certain
// group of pixels.

static void ss_420jpeg_to_444(uint8_t *buffer, int width, int height)
{
#ifndef HAVE_ASM_MMX
	uint8_t *in, *out0, *out1;
	unsigned int x, y;
	in = buffer + (width * height / 4) - 1;
	out1 = buffer + (width * height) - 1;
	out0 = out1 - width;
	for (y = height - 1; y >= 0; y -= 2) {
		for (x = width - 1; x >= 0; x -=2) {
			uint8_t val = *(in--);
			*(out1--) = val;
			*(out1--) = val;
			*(out0--) = val;
			*(out0--) = val;
		}
		out0 -= width;
		out1 -= width;
	}
#else
	int x,y;
	const int mmx_stride = width >> 3;
	uint8_t *src = buffer + ((width * height) >> 2)-1;
	uint8_t *dst = buffer + (width * height) -1;
	uint8_t *dst2 = dst - width;

	for( y = height-1; y >= 0; y -= 2)
	{
		for( x = 0; x < mmx_stride; x ++ )
		{
			movq_m2r( *src,mm0 );
			movq_m2r( *src,mm1 );
			movq_r2m(mm0, *dst );
			movq_r2m(mm1, *(dst+8) );
			movq_r2m(mm0, *dst2 );
			movq_r2m(mm1, *(dst2+8) );
			dst += 16;
			dst2 += 16;
			src += 8;
		}
		dst -= width;	
		dst2 -= width;
	}

	__asm__(_EMMS"       \n\t"
            	SFENCE"     \n\t"
            	:::"memory");
#endif
}


void ss_420_to_422(uint8_t *buffer, int width, int height)
{

	//todo, 1x2 super sampling (box)
}

void ss_422_to_420(uint8_t *buffer, int width, int height )
{

	//todo 2x1 down sampling (box)
}

static inline void downsample2x1( const uint8_t *src, uint8_t *dst, const int width )
{
	unsigned int x;
	unsigned int x1=0;	
	for(x=0; x < width; x+=2 , x1++)
	{
		dst[x1] = ( src[x] + src[x+1] + 1 ) >> 1;
	}
}

#ifdef HAVE_ASM_MMX
static inline void downsample16x8( const uint8_t *src, uint8_t *dst, const int width, const int left )
{
	unsigned int x;
	unsigned int x1 = 0;
	unsigned int i;

	for( x= 0; x < width; x += 16, x1 += 8 ) {
		subsample_down_1x16to1x8( &dst[x1], &src[x] );
	}
	
	for(i=0; i < left; i+=2, x1++) {
		dst[x1] = ( src[x + i] + src[x + i + 1] + 1 ) >> 1;
	}
}

static inline void downsample32x16( const uint8_t *src, uint8_t *dst, const int width, const int left )
{
	unsigned int x;
	unsigned int x1 = 0;
	unsigned int i;

	for( x= 0; x < width; x += 32, x1 += 16 ) {
		subsample_down_1x32to1x16( &dst[x1], &src[x] );
	}
	
	for(i=0; i < left; i+=2, x1++) {
		dst[x1] = ( src[x + i] + src[x + i + 1] + 1 ) >> 1;
	}

}
#endif

static void ss_444_to_422_cp(uint8_t *buffer, uint8_t *dest, int width, int height)
{
	const unsigned int dst_stride = width >> 1;
	int x,y;
#ifdef HAVE_ASM_MMX
	const unsigned int left = dst_stride % 8;
	subsample_load_mask16to8();
#endif

	for( y = 0; y < height; y ++ ) {
		uint8_t *src = buffer + (y*width);
		uint8_t *dst = dest + (y*dst_stride);
	
#ifdef HAVE_ASM_MMX
		downsample32x16( src, dst, width,left );
#else
		downsample2x1( src, dst, width  );
#endif
	}

#ifdef HAVE_ASM_MMX
	__asm__(_EMMS"       \n\t"
           	SFENCE"     \n\t"
            	:::"memory");
#endif
}



static void tr_422_to_444( uint8_t *buffer, int width, int height)
{
	int x,y;
	const int stride = width >> 1;

#ifndef HAVE_ASM_MMX
	for( y = height-1; y > 0 ; y -- ) {
		uint8_t *dst = buffer + (y * width);
		uint8_t *src = buffer + (y * stride);
		for(x=0; x < stride; x++) // for 1 row
		{
			dst[0] = src[x]; //put to dst
			dst[1] = src[x];
			dst+=2; // increment dst
		}
	}
#else
	const int mmx_stride = stride >> 3;
	int left = (mmx_stride % 16);
	for( y = height -1 ; y > 0; y -- ) {
		uint8_t *src = buffer + (y* stride);
		uint8_t *dst = buffer + (y* width);
		unsigned int x1 = 0,x2=0;	
		for( x = 0; x < stride; x += 16, x1 += 32 ) {
			subsample_up_1x16to1x32( &src[x], &dst[x1] );
		}
	}

	__asm__(_EMMS"       \n\t"
           	SFENCE"     \n\t"
            	:::"memory");
#endif
}

static void tr_422_to_444t(uint8_t *out, uint8_t *in, int width, int height)
{
	int x,y;
	const int stride = width >> 1;
#ifndef HAVE_ASM_MMX
	for( y = height; y > 0 ; y -- ) {
		uint8_t *d = out + (y * width);
		uint8_t *s = in + (y * stride);
		for(x=0; x < stride; x++) // for 1 row
		{
			d[0] = s[x]; //put to dst
			d[1] = s[x];
			d+=2; // increment dst
		}
	}
#else
	const int mmx_stride = stride >> 3;
	int left = (mmx_stride % 16);
	for( y = height -1 ; y > 0; y -- ) {
		uint8_t *src = in + (y* stride);
		uint8_t *dst = out + (y* width);
		unsigned int x1 = 0,x2=0;	
		for( x = 0; x < stride; x += 16, x1 += 32 ) {
			subsample_up_1x16to1x32(&src[x], &dst[x1] );
		}
	}

	__asm__(_EMMS"       \n\t"
           	SFENCE"     \n\t"
            	:::"memory");
#endif

}

/* vertical intersitial siting; horizontal cositing
 *
 *    Y   Y   Y   Y
 *    C       C
 *    Y   Y   Y   Y
 *
 *    Y   Y   Y   Y
 *    C       C
 *    Y   Y   Y   Y
 *
 * [1,2,1] kernel for horizontal subsampling:
 *
 *    inX[0] [1] [2]
 *        |   |   |
 *    C   C   C   C
 *         \  |  /
 *          \ | /
 *            C
 */

static void ss_444_to_420mpeg2(uint8_t *buffer, int width, int height)
{
  uint8_t *in0, *in1, *out;
  int x, y;

  in0 = buffer;          /* points to */
  in1 = buffer + width;  /* second of pair of lines */
  out = buffer;
  for (y = 0; y < height; y += 2) {
    /* first column boundary condition -- just repeat it to right */
    *out = (in0[0] + (2 * in0[0]) + in0[1] +
	    in1[0] + (2 * in1[0]) + in1[1]) >> 3;
    out++;
    in0++;
    in1++;
    /* rest of columns just loop */
    for (x = 2; x < width; x += 2) {
      *out = (in0[0] + (2 * in0[1]) + in0[2] +
	      in1[0] + (2 * in1[1]) + in1[2]) >> 3;
      in0 += 2;
      in1 += 2;
      out++;
    }
    in0 += width + 1;
    in1 += width + 1;
  }
}
      
static void	chroma_subsample_task( void *ptr )
{
	vj_task_arg_t *f = (vj_task_arg_t*) ptr;

	switch (f->iparam) {
		case SSM_420_JPEG_BOX:
		case SSM_420_JPEG_TR: 
			ss_444_to_420jpeg(f->input[1], f->width, f->height);
  			ss_444_to_420jpeg(f->input[2], f->width, f->height);
    		break;
		case SSM_420_MPEG2:
			ss_444_to_420mpeg2(f->input[1], f->width, f->height);
			ss_444_to_420mpeg2(f->input[2], f->width, f->height);
		break;
		case SSM_422_444:
	   	 	ss_444_to_422_cp(f->output[1],f->input[1],f->width,f->height);
		    	ss_444_to_422_cp(f->output[2],f->input[2],f->width,f->height);
    		break;
		case SSM_420_422:
    			ss_422_to_420(f->input[1],f->width,f->height);
   			ss_422_to_420(f->input[2],f->width,f->height);
   		break;
		default:
		break;

	}
}
static void chroma_supersample_task( void *ptr )
{
	vj_task_arg_t *f = (vj_task_arg_t*) ptr; 

	switch (f->iparam) {
	 case SSM_420_JPEG_BOX:
      		ss_420jpeg_to_444(f->input[1], f->width, f->height);
   	 	ss_420jpeg_to_444(f->input[2], f->width, f->height);
    		break;
 	 case SSM_420_JPEG_TR:
   		tr_420jpeg_to_444(f->priv,f->input[1], f->width, f->height);
   		tr_420jpeg_to_444(f->priv,f->input[2], f->width, f->height);
   		break;
  	 case SSM_422_444:
		tr_422_to_444t(f->input[1],f->output[1],f->width,f->height);
   		tr_422_to_444t(f->input[2],f->output[2],f->width,f->height);
    		break;
  	case SSM_420_422:
  		ss_420_to_422( f->input[1], f->width, f->height );
    		ss_420_to_422( f->input[2], f->width, f->height );
    		break;
  	default:
   	 	break;
  }

}

void chroma_subsample_cp(subsample_mode_t mode,VJFrame *frame, uint8_t *ycbcr[], uint8_t *dcbcr[])
{
	switch (mode) {
		case SSM_420_JPEG_BOX:
		case SSM_420_JPEG_TR:
			ss_444_to_420jpeg_cp(ycbcr[1],dcbcr[1], frame->width, frame->height);
			ss_444_to_420jpeg_cp(ycbcr[2],dcbcr[2], frame->width, frame->height);
			break;
		case SSM_420_MPEG2:
 			break;
		case SSM_422_444:
  			ss_444_to_422_cp(ycbcr[1],dcbcr[1],frame->width,frame->height);
			ss_444_to_422_cp(ycbcr[2],dcbcr[2],frame->width,frame->height);
		    break;
  		default:
   		break;
  	}
}

void chroma_subsample(subsample_mode_t mode, VJFrame *frame, uint8_t *ycbcr[] )
{
	switch (mode) {
		case SSM_420_JPEG_BOX:
		case SSM_420_JPEG_TR: 
			ss_444_to_420jpeg(ycbcr[1], frame->width, frame->height);
 			ss_444_to_420jpeg(ycbcr[2], frame->width, frame->height);
			break;
		case SSM_420_MPEG2:
			ss_444_to_420mpeg2(ycbcr[1], frame->width, frame->height);
			ss_444_to_420mpeg2(ycbcr[2], frame->width, frame->height);
			break;
		case SSM_422_444:
		    ss_444_to_422_cp(ycbcr[1],ycbcr[1],frame->width,frame->height);
		    ss_444_to_422_cp(ycbcr[2],ycbcr[2],frame->width,frame->height);
    		break;
		case SSM_420_422:
			ss_422_to_420(ycbcr[1],frame->width,frame->height);
			ss_422_to_420(ycbcr[2],frame->width,frame->height);
			break;
		default:
		break;
  	}
}


static uint8_t *_chroma_supersample_data = NULL;

void chroma_supersample(subsample_mode_t mode,VJFrame *frame, uint8_t *ycbcr[] )
{
	if( _chroma_supersample_data == NULL && mode == SSM_420_JPEG_TR ) {
 		_chroma_supersample_data = (uint8_t*) vj_calloc( sizeof(uint8_t) * frame->width * 2 );
	}

	switch (mode) {
		case SSM_420_JPEG_BOX:
	      		ss_420jpeg_to_444(ycbcr[1], frame->width, frame->height);
	    		ss_420jpeg_to_444(ycbcr[2], frame->width, frame->height);
		break;
		case SSM_420_JPEG_TR:
			tr_420jpeg_to_444(_chroma_supersample_data,ycbcr[1], frame->width, frame->height);
			tr_420jpeg_to_444(_chroma_supersample_data,ycbcr[2], frame->width, frame->height);
		break;
		case SSM_422_444:
 			tr_422_to_444(ycbcr[1],frame->width,frame->height);
 			tr_422_to_444(ycbcr[2],frame->width,frame->height);
    		break;
 		case SSM_420_422:
    			ss_420_to_422( ycbcr[1], frame->width, frame->height );
    			ss_420_to_422( ycbcr[2], frame->width, frame->height );
    		break;
  		default:
   		break;
 	 }
}
