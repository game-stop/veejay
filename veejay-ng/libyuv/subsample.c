/*
 * subsample.c:  Routines to do chroma subsampling.  ("Work In Progress")
 *
 *
 *  Copyright (C) 2001 Matthew J. Marjanovic <maddog@mir.com>
 *                2004 Niels Elburg <nelburg@looze.net>
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

/** \defgroup Planar YUV sampling
 *
 */

#include <config.h>

#ifdef HAVE_ASM_MMX
#include "mmx.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <mjpegtools/mjpeg_types.h>

#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#include <libyuv/yuvconv.h>


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

typedef struct
{
	uint8_t *buf;
        uint8_t *YUV_to_YCbCr[2];
	int     jyuv;
	uint8_t *planes[4];
} yuv_sampler_t;

static uint8_t *sample_buffer = NULL;
static int go = 0;

void *subsample_init(int len)
{
	void *ret = NULL;
	yuv_sampler_t *s = (yuv_sampler_t*) vj_malloc(sizeof(yuv_sampler_t) );
	if(!s)
		return ret;
	memset( s, 0 , sizeof( yuv_sampler_t ));
	s->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * len );
	s->YUV_to_YCbCr[0] = NULL;
	s->YUV_to_YCbCr[1] = NULL;
	if(!s->buf)
		return ret;

	return (void*) s;
}

void *subsample_init_copy(int w, int h)
{
	void *ret = NULL;
	int len = w;
	yuv_sampler_t *s = (yuv_sampler_t*) vj_malloc(sizeof(yuv_sampler_t) );
	if(!s)
		return ret;
	memset( s, 0 , sizeof( yuv_sampler_t ));
	s->buf = (uint8_t*) vj_malloc(sizeof(uint8_t) * len );
	s->YUV_to_YCbCr[0] = NULL;
	s->YUV_to_YCbCr[1] = NULL;
	if(!s->buf)
		return ret;

	s->planes[0] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h );
	s->planes[1] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h );
	s->planes[2] = (uint8_t*) vj_malloc( sizeof(uint8_t) * w * h );
	
	return (void*) s;
}



void	subsample_free(void *data)
{
	yuv_sampler_t *sampler = (yuv_sampler_t*) data;
	if(sampler)
	{
		if(sampler->buf) free(sampler->buf);
		if(sampler->YUV_to_YCbCr[0])
			free(sampler->YUV_to_YCbCr[0]);
		if(sampler->YUV_to_YCbCr[1])
			free(sampler->YUV_to_YCbCr[1]);
		if(sampler->planes[0])
			free(sampler->planes[0]);
		if(sampler->planes[1])
			free(sampler->planes[1]);
		if(sampler->planes[2])
			free(sampler->planes[2]);
		free(sampler);
	}
	sampler = NULL;
}


#define CLAMP_Y( f) ( f < 16 ? 16: f > 235 ? 235: f )
#define CLAMP_C( u) ( u < 16 ? 16: u > 240 ? 240: u )
static	void	subsample_init_YUV_to_CbCr(void *data)
{
	unsigned int n;
	yuv_sampler_t *sampler = (yuv_sampler_t*) data;

	for( n = 0; n < 2; n ++ )
	{
		sampler->YUV_to_YCbCr[n] =
			(uint8_t*) vj_malloc(sizeof(uint8_t) * 256 );
		memset( sampler->YUV_to_YCbCr[n], 0, 256 );
	}	
	uint8_t *y = sampler->YUV_to_YCbCr[0];
	uint8_t *uv = sampler->YUV_to_YCbCr[1];
	for( n = 0; n < 256; n ++ )
	{
		y[n] = CLAMP_Y( (n * 219.0 / 256.0 + 16) );
		uv[n] = CLAMP_C( (n* 224.0 / 256.0 + 16) );
	}
	sampler->jyuv = 1;
}

#ifdef HAVE_ASM_MMX
//! Clamp Y to 16-235 - MMX optimized, 64 bytes at a time.
/*!
 \param plane Source
 \param dplane Destination
 \param len Size
 */
static	void	subsample_y_clamp_plane(
		uint8_t *plane,
		uint8_t *dplane,
		int len )
{
	uint8_t mi[8] = { 16,16,16,16, 16,16,16,16 };
	uint8_t ma[8] = { 235,235,235,235,  235,235,235,235};
	const uint8_t *min = &mi[0];
	const uint8_t *max = &ma[0];
	unsigned int k = 0;
	unsigned int align = (len/8);
	
	for( k = 0; k < align ; k ++ )
	{
		__asm__ __volatile__ ( 
#if defined HAVE_ASM_MMX2 || defined HAVE_ASM_SSE
			"prefetchnta (%0)\n"
			"prefetchnta (%1)\n"
			"prefetchnta (%2)\n"
#endif
			"movq (%0), %%mm0\n"
			"movq (%1), %%mm1\n" //eb
			"movq (%2), %%mm2\n" //10
			"pmaxub %%mm0, %%mm2\n"
			"pminub %%mm1, %%mm2\n"
			"movntq %%mm2, (%3)\n"
          	  :: "r" (plane),"r" (max), "r" (min), "r" (dplane) : "memory");

		plane += 8;
		dplane += 8;
	}
	if( (align*8) < len)
	{
		for( k = 8*align; k < len; k ++ )
			*(dplane++) = CLAMP_Y( (*plane++) );

	}
}
#else
static	void	subsample_y_clamp_plane(
		uint8_t *plane,
		uint8_t *dplane,
		int len )
{
	unsigned int n;
	for( n = 0 ; n < len; n ++ )
		dplane[n] = CLAMP_Y( plane[n] );
}
#endif

#ifdef HAVE_ASM_MMX
//! Clamp Chroma planes 16-240, MMX 8 pixel at a time
static void 	subsample_uv_clamp_plane(
		uint8_t *plane_u,
		uint8_t *plane_v,
		uint8_t *dplane_u,
		uint8_t *dplane_v,
		int len)
{
	uint8_t mi[8] = { 16,16,16,16, 16,16,16,16 };
	uint8_t ma[8] = { 240,240,240,240,  240,240,240,240 };
	const uint8_t *min = &mi[0];
	const uint8_t *max = &ma[0];
	unsigned int k = 0;
	unsigned int align = (len/8);
	
	for( k = 0; k < align ; k ++ )
	{
		__asm__ __volatile__ (
#if defined HAVE_ASM_MMX2 || defined HAVE_ASM_SSE  
			"prefetchnta (%0)\n"
			"prefetchnta (%1)\n"
			"prefetchnta (%2)\n"
			"prefetchnta (%3)\n"
#endif
			"movq (%0), %%mm0\n"
			"movq (%1), %%mm1\n"
			"movq (%2), %%mm2\n"
			"pmaxub %%mm0, %%mm2\n"
			"pminub %%mm1, %%mm2\n"
			"movntq %%mm2, (%3)\n"
      			:: "r" (plane_u),"r" (max), "r" (min), "r" (dplane_u) : "memory");

		plane_u += 8;
		dplane_u += 8;
	}
	
	for( k = 0; k < align ; k ++ )
	{
		__asm__ __volatile__ (
    			"prefetchnta (%0)\n"
			"prefetchnta (%1)\n"
			"prefetchnta (%2)\n"
 	    		"prefetchnta (%3)\n"
			"movq (%0), %%mm0\n"
			"movq (%1), %%mm1\n"
			"movq (%2), %%mm2\n"
			"pmaxub %%mm0, %%mm2\n"
			"pminub %%mm1, %%mm2\n"
			"movntq %%mm2, (%3)\n"
          	  :: "r" (plane_v),"r" (max), "r" (min), "r" (dplane_v) : "memory");

		plane_v += 8;
		dplane_v += 8;
	}
}
#else
static	void	subsample_uv_clamp_plane(
		uint8_t *plane_u,
		uint8_t *plane_v,
		uint8_t *dplane_u,
		uint8_t *dplane_v,
		int len )
{
	unsigned int n;
	for( n = 0 ; n < len; n ++ )
	{
		dplane_u[n] = CLAMP_Y( plane_u[n] );
		dplane_v[n] = CLAMP_C( plane_v[n] );
	}
}
#endif
static	void	subsample_ycbcr_itu601_plane(
	       	uint8_t *plane,
		int	 len,
		uint8_t *lookup,
	        uint8_t *dplane	)
{
	unsigned int n;
	for( n = 0 ; n < len; n ++ )
		dplane[n] = lookup[ plane[n] ];
}

static	void	subsample_ycbcr_itu601_chroma_plane(
	       	uint8_t *planeU,
		uint8_t *planeV,
		int	 len,
		uint8_t *lookup,
	        uint8_t *dplaneU,
		uint8_t *dplaneV	)
{
	unsigned int n;
	for( n = 0 ; n < len; n ++ )
	{
		dplaneU[n] = lookup[ (planeU[n]) ];
		dplaneV[n] = lookup[ (planeV[n]) ];
	}
}

void	subsample_ycbcr_itu601(void *data, VJFrame *frame)
{
	yuv_sampler_t *sampler = (yuv_sampler_t*) data;

	if(!sampler->jyuv)
		subsample_init_YUV_to_CbCr(sampler);
	
	subsample_ycbcr_itu601_plane(
			frame->data[0],
			frame->len,
			sampler->YUV_to_YCbCr[0],
		        frame->data[0]	);
	subsample_ycbcr_itu601_chroma_plane(
			frame->data[1],
			frame->data[2],
			frame->uv_len,
			sampler->YUV_to_YCbCr[1],
		        frame->data[1],
			frame->data[2]	);
}

void	subsample_ycbcr_itu601_copy(void *data, VJFrame *src_frame, VJFrame *dst_frame)
{
	yuv_sampler_t *sampler = (yuv_sampler_t*) data;
	if(!sampler->YUV_to_YCbCr[0])
		subsample_init_YUV_to_CbCr(sampler);
	
	subsample_ycbcr_itu601_plane(
			src_frame->data[0],
			src_frame->len,
			sampler->YUV_to_YCbCr[0],
		        dst_frame->data[0]	);
	subsample_ycbcr_itu601_chroma_plane(
			src_frame->data[1],
			src_frame->data[2],
			src_frame->uv_len,
			sampler->YUV_to_YCbCr[1],
		        dst_frame->data[1],
			dst_frame->data[2]	);
}

void	subsample_ycbcr_clamp_itu601_copy(VJFrame *frame, VJFrame *dst_frame)
{
	subsample_y_clamp_plane(
			frame->data[0],
			dst_frame->data[0],
			frame->len );
	subsample_uv_clamp_plane(
			frame->data[1],
			frame->data[2],
			dst_frame->data[1],
			dst_frame->data[2],
			frame->uv_len );
#ifdef HAVE_ASM_MMX
	__asm__ __volatile__ ("sfence":::"memory");
        __asm__ __volatile__ ("emms":::"memory");
#endif
}
/*
#ifdef HAVE_ASM_MMX
void	subsample_clear_plane( uint8_t bval, uint8_t *plane, uint32_t plane_len )
{
	unsigned int k = 0;
	unsigned int align = (plane_len/64);

	double val = (double) bval;

	__asm__ __volatile__ (
			"pxor	%%mm0,	%%mm0\n"
			"movq	%%mm0,	(%0)\n"
		:: "r" (val) );
	
	for( k = 0; k < align ; k ++ )
	{
		__asm__ __volatile__ (
			"prefetchnta (%0)\n"
			"prefetchnta 8(%0)\n"
			"prefetchnta 16(%0)\n"
			"prefetchnta 24(%0)\n"
			"prefetchnta 32(%0)\n"
			"prefetchnta 40(%0)\n"
			"prefetchnta 49(%0)\n"
			"prefetchnta 56(%0)\n"
			"movntq %%mm0,(%0)\n"
			"movntq %%mm0,8(%0)\n"
			"movntq %%mm0,16(%0)\n"
			"movntq %%mm0,24(%0)\n"
			"movntq %%mm0,32(%0)\n"
			"movntq %%mm0,40(%0)\n"
			"movntq %%mm0,48(%0)\n"
			"movntq %%mm0,56(%0)\n"
        	    :: "r" (plane) : "memory");
		plane += 64;
	}
	__asm__ __volatile__ ("sfence":::"memory");
        __asm__ __volatile__ ("emms":::"memory");
}*/
//#else
void	subsample_clear_plane( uint8_t bval, uint8_t *plane, uint32_t plane_len )
{
	memset( plane, bval, plane_len );
}

//#endif

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

static void tr_420jpeg_to_444(void *data, uint8_t *buffer, int width, int height)
{
  uint8_t *inm, *in0, *inp, *out0, *out1;
  uint8_t cmm, cm0, cmp, c0m, c00, c0p, cpm, cp0, cpp;
  int x, y;

  yuv_sampler_t *sampler = (yuv_sampler_t*) data;

  uint8_t *saveme = sampler->buf;

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
  int x, y;
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
	const int mmx_stride = width/8;
	uint8_t *src = buffer + (width * height/4)-1;
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
#endif
}


void ss_420_to_422(uint8_t *buffer, int width, int height)
{
	//todo, 1x2 super sampling (box)
}

void ss_422_to_420(uint8_t *buffer, int width, int height )
{
	veejay_msg(0, "%s: Implement me", __FUNCTION__);
	
	//todo 2x1 down sampling (box)
}

#ifndef HAVE_ASM_MMX
static void ss_444_to_422(void *data, uint8_t *buffer, int width, int height)
{
	const int dst_stride = width/2;
	int x,y;
	yuv_sampler_t *sampler = (yuv_sampler_t*) data;
	
	for(y = 0; y < height; y ++)
	{
		uint8_t *src = sampler->buf;
		uint8_t *dst = buffer + (y*dst_stride);
		veejay_memcpy( src, buffer + (y*width), width );
		for(x=0; x < dst_stride; x++)
		{
			*(dst++) = ( src[0] + src[1] ) >> 1;
			src += 2;
		}
	}

}

#else

/* mmx_average_2_u8 (function taken from mpeg2dec, a free MPEG-2 video
 * stream decoder 
 *
 * Copyright (C) 2000-2003 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 */

static mmx_t mask1 = {0xfefefefefefefefeLL};
static mmx_t round4 = {0x0002000200020002LL};

static inline void mmx_average_4_U8 (uint8_t * dest, const uint8_t * src1,
                                     const uint8_t * src2,
                                     const uint8_t * src3,
                                     const uint8_t * src4)
{
    /* *dest = (*src1 + *src2 + *src3 + *src4 + 2)/ 4; */

    movq_m2r (*src1, mm1);      /* load 8 src1 bytes */
    movq_r2r (mm1, mm2);        /* copy 8 src1 bytes */

    punpcklbw_r2r (mm0, mm1);   /* unpack low src1 bytes */
    punpckhbw_r2r (mm0, mm2);   /* unpack high src1 bytes */

    movq_m2r (*src2, mm3);      /* load 8 src2 bytes */
    movq_r2r (mm3, mm4);        /* copy 8 src2 bytes */

    punpcklbw_r2r (mm0, mm3);   /* unpack low src2 bytes */
    punpckhbw_r2r (mm0, mm4);   /* unpack high src2 bytes */

    paddw_r2r (mm3, mm1);       /* add lows */
    paddw_r2r (mm4, mm2);       /* add highs */

    /* now have partials in mm1 and mm2 */

    movq_m2r (*src3, mm3);      /* load 8 src3 bytes */
    movq_r2r (mm3, mm4);        /* copy 8 src3 bytes */

    punpcklbw_r2r (mm0, mm3);   /* unpack low src3 bytes */
    punpckhbw_r2r (mm0, mm4);   /* unpack high src3 bytes */

    paddw_r2r (mm3, mm1);       /* add lows */
    paddw_r2r (mm4, mm2);       /* add highs */

    movq_m2r (*src4, mm5);      /* load 8 src4 bytes */
    movq_r2r (mm5, mm6);        /* copy 8 src4 bytes */

    punpcklbw_r2r (mm0, mm5);   /* unpack low src4 bytes */
    punpckhbw_r2r (mm0, mm6);   /* unpack high src4 bytes */

    paddw_r2r (mm5, mm1);       /* add lows */
    paddw_r2r (mm6, mm2);       /* add highs */

    /* now have subtotal in mm1 and mm2 */

    paddw_m2r (round4, mm1);
    psraw_i2r (2, mm1);         /* /4 */
    paddw_m2r (round4, mm2);
    psraw_i2r (2, mm2);         /* /4 */

    packuswb_r2r (mm2, mm1);    /* pack (w/ saturation) */
    movq_r2m (mm1, *dest);      /* store result in dest */
}


static inline void mmx_average_2_U8 (uint8_t * dest, const uint8_t * src1,
				     const uint8_t * src2)
{
    /* *dest = (*src1 + *src2 + 1)/ 2; */

    movq_m2r (*src1, mm1);	/* load 8 src1 bytes */
    movq_r2r (mm1, mm2);	/* copy 8 src1 bytes */

    movq_m2r (*src2, mm3);	/* load 8 src2 bytes */
    movq_r2r (mm3, mm4);	/* copy 8 src2 bytes */

    pxor_r2r (mm1, mm3);	/* xor src1 and src2 */
    pand_m2r (mask1, mm3);	/* mask lower bits */
    psrlq_i2r (1, mm3);		/* /2 */
    por_r2r (mm2, mm4);		/* or src1 and src2 */
    psubb_r2r (mm3, mm4);	/* subtract subresults */
    movq_r2m (mm4, *dest);	/* store result in dest */
}
static void ss_444_to_422(void *data,uint8_t *buffer, int width, int height)
{
	const int dst_stride = width/2;
	const int len = width * height;
	const int mmx_stride = dst_stride / 8;
	int x,y;

	yuv_sampler_t *sampler = (yuv_sampler_t*) data;

	for(y = 0; y < height; y ++)
	{
		uint8_t *src = sampler->buf;
		uint8_t *dst = buffer + (y*dst_stride);
		veejay_memcpy( src, buffer + (y*width), width );
		for(x=0; x < mmx_stride; x++)
		{
			mmx_average_2_U8( dst,src, src+8 );
			src += 16;
			dst += 8;
		}
	}

}
#endif

static void tr_422_to_444(uint8_t *buffer, int width, int height)
{
	/* YUV 4:2:2 Planar to 4:4:4 Planar */

	const int stride = width/2;
	const int len = stride * height; 
#ifdef HAVE_ASM_MMX
	const int mmx_stride = stride / 8;
#endif
	int x,y;
	for( y = height-1; y > 0 ; y -- )
	{
		uint8_t *dst = buffer + (y * width);
		uint8_t *src = buffer + (y * stride);
#ifdef HAVE_ASM_MMX
		for( x = 0; x < mmx_stride; x ++ )
		{
			movq_m2r( *src,mm0 );
			movq_m2r( *src,mm1 );
			movq_r2m(mm0, *dst );
			movq_r2m(mm1, *(dst+8) );
			dst += 16;
			src += 8;
		}
#else
		for(x=0; x < stride; x++) // for 1 row
		{
			dst[0] = src[x]; //put to dst
			dst[1] = src[x];
			dst+=2; // increment dst
		}
#endif
	}
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
      



void chroma_subsample_copy(subsample_mode_t mode, void *data, VJFrame *frame,
		      int width, int height, uint8_t *res[])
{
  yuv_sampler_t *sampler = (yuv_sampler_t*) data;
 
  veejay_memcpy( sampler->planes[1], frame->data[1], frame->uv_len );
  veejay_memcpy( sampler->planes[2], frame->data[2], frame->uv_len );    
  
  switch (mode) {
  case SSM_420_JPEG_BOX:
  case SSM_420_JPEG_TR: 
    ss_444_to_420jpeg(sampler->planes[1], width, height);
    ss_444_to_420jpeg(sampler->planes[2], width, height);
#ifdef HAVE_ASM_MMX
	emms();
#endif
    break;
  case SSM_420_MPEG2:
    ss_444_to_420mpeg2(sampler->planes[1], width, height);
    ss_444_to_420mpeg2(sampler->planes[2], width, height);
    break;
  case SSM_422_444:
    ss_444_to_422(data,sampler->planes[1],width,height);
    ss_444_to_422(data,sampler->planes[2],width,height);
#ifdef HAVE_ASM_MMX
	emms();
#endif
    break;
  case SSM_420_422:
    ss_422_to_420(sampler->planes[1],width,height);
    ss_422_to_420(sampler->planes[2],width,height);
    break;
  default:
    break;
  }

    res[0] = frame->data[0];
    res[1] = sampler->planes[1];
    res[2] = sampler->planes[2];    
}

void chroma_subsample(subsample_mode_t mode, void *data, uint8_t *ycbcr[],
		      int width, int height)
{

  switch (mode) {
  case SSM_420_JPEG_BOX:
  case SSM_420_JPEG_TR: 
    ss_444_to_420jpeg(ycbcr[1], width, height);
    ss_444_to_420jpeg(ycbcr[2], width, height);
#ifdef HAVE_ASM_MMX
	emms();
#endif
    break;
  case SSM_420_MPEG2:
    ss_444_to_420mpeg2(ycbcr[1], width, height);
    ss_444_to_420mpeg2(ycbcr[2], width, height);
    break;
  case SSM_422_444:
    ss_444_to_422(data,ycbcr[1],width,height);
    ss_444_to_422(data,ycbcr[2],width,height);
#ifdef HAVE_ASM_MMX
	emms();
#endif
    break;
  case SSM_420_422:
    ss_422_to_420(ycbcr[1],width,height);
    ss_422_to_420(ycbcr[2],width,height);
    break;
  default:
    break;
  }
}


void chroma_supersample(subsample_mode_t mode,void *data, uint8_t *ycbcr[],
			int width, int height)
{
  switch (mode) {
  case SSM_420_JPEG_BOX:
    ss_420jpeg_to_444(ycbcr[1], width, height);
    ss_420jpeg_to_444(ycbcr[2], width, height);
#ifdef HAVE_ASM_MMX
	emms();
#endif
    break;
  case SSM_420_JPEG_TR:
    tr_420jpeg_to_444(data,ycbcr[1], width, height);
    tr_420jpeg_to_444(data,ycbcr[2], width, height);
    break;
  case SSM_422_444:
    tr_422_to_444(ycbcr[2],width,height);
    tr_422_to_444(ycbcr[1],width,height);
#ifdef HAVE_ASM_MMX
	emms();
#endif
    break;
  case SSM_420_422:
    ss_420_to_422( ycbcr[1], width, height );
    ss_420_to_422( ycbcr[2], width, height );
	exit(0);
    break;
  case SSM_420_MPEG2:
    //    ss_420mpeg2_to_444(ycbcr[1], width, height);
    //    ss_420mpeg2_to_444(ycbcr[2], width, height);
    exit(4);
    break;
  default:
    break;
  }
}


