/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <config.h>
#include <stdint.h>
#include <unistd.h>
#include <libyuv/yuvconv.h>
#include <veejay/defs.h>
#include <ffmpeg/swscale.h>
#ifdef USE_SWSCALER
#include <libpostproc/swscale.h>
#endif
#include <libvjmsg/vj-common.h>

#include <ffmpeg/avutil.h>
#include <ffmpeg/avcodec.h>
/* this routine is the same as frame_YUV422_to_YUV420P , unpack
 * libdv's 4:2:2-packed into 4:2:0 planar 
 * See http://mjpeg.sourceforge.net/ (MJPEG Tools) (lav-common.c)
 */

#ifdef STRICT_CHECKING
#include <assert.h>
#endif


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

#endif


static	int		    sws_context_flags_ = 0;

void	yuv_init_lib()
{
	sws_context_flags_ = yuv_sws_get_cpu_flags();
}

static struct  {
	const char *name;
	int id;
} pixel_format_descr[] = 
{
	{	"PIX_FMT_YUV420P", PIX_FMT_YUV420P },
	{	"PIX_FMT_YUV422P",	PIX_FMT_YUV422P },
	{	"PIX_FMT_YUV444P", PIX_FMT_YUV444P },
	{	"PIX_FMT_YUVJ420P", PIX_FMT_YUVJ420P },
	{	"PIX_FMT_YUVJ422P", PIX_FMT_YUVJ422P },
	{	"PIX_FMT_YUVJ444P", PIX_FMT_YUVJ444P },		
	{	"PIX_FMT_YUYV422",	PIX_FMT_YUYV422 },
	{	NULL,			0 }
};


static	char *find_pixel_format_descr( int id )
{
	int i;
	for( i = 0; pixel_format_descr[i].name != NULL ; i ++ )
		if( id == pixel_format_descr[i].id )
			return pixel_format_descr[i].name ;
	return NULL;
}

VJFrame	*yuv_yuv_template( uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int fmt )
{
	VJFrame *f = (VJFrame*) vj_calloc(sizeof(VJFrame));
	f->format = fmt;
	f->data[0] = Y;
	f->data[1] = U;
	f->data[2] = V;
	f->data[3] = NULL;
	f->width   = w;
	f->height  = h;

	veejay_msg(0, "%s: %dx%d, fmt = %s ", __FUNCTION__, w,h, find_pixel_format_descr(fmt) );

	switch(fmt)
	{
		case PIX_FMT_YUV422P:
		case PIX_FMT_YUVJ422P:
			f->uv_width = w/2;
			f->uv_height= f->height;		
			f->stride[0] = w;
			f->stride[1] = f->stride[2] = f->stride[0]/2;
			break;
		case PIX_FMT_YUV420P:
		case PIX_FMT_YUVJ420P:
			f->uv_width = w/2;
			f->uv_height=f->height/2;
			f->stride[0] = w;
			f->stride[1] = f->stride[2] = f->stride[0]/2;
			break;
		case PIX_FMT_YUV444P:
		case PIX_FMT_YUVJ444P:
			f->uv_width = w;
			f->uv_height=f->height;
			f->stride[0] = w;
			f->stride[1] = f->stride[2] = f->stride[0];
			break;
		case PIX_FMT_YUYV422:
			f->uv_width = w;
			f->uv_height=f->height;
			f->stride[0] = w;
			f->stride[1] = f->stride[2] = f->stride[0] / 2;
			break;
		default:
#ifdef STRICT_CHECKING
			assert(0);
#endif
		break;
	}


	return f;
}

VJFrame	*yuv_rgb_template( uint8_t *rgb_buffer, int w, int h, int fmt )
{
#ifdef STRICT_CHECKING
	assert( fmt == PIX_FMT_RGB24 || fmt == PIX_FMT_BGR24 ||
		fmt == PIX_FMT_RGBA || fmt == PIX_FMT_RGB32_1 || fmt == PIX_FMT_RGB32 || fmt == PIX_FMT_BGR32);
	assert( w > 0 );
	assert( h > 0 );
#endif
	VJFrame *f = (VJFrame*) vj_calloc(sizeof(VJFrame));
	f->format = fmt;
	f->data[0] = rgb_buffer;
	f->data[1] = NULL;
	f->data[2] = NULL;
	f->data[3] = NULL;
	f->width   = w;
	f->height  = h;

	switch( fmt )
	{
		case PIX_FMT_RGB24:
		case PIX_FMT_BGR24:
				f->stride[0] = w * 3;
		break;
		default:
				f->stride[0] = w * 4;
		break;
	}
	f->stride[1] = 0;
	f->stride[2] = 0;

	return f;
}

#define ru4(num)  (((num)+3)&~3)

void	yuv_convert_any( VJFrame *src, VJFrame *dst, int src_fmt, int dst_fmt )
{
#ifdef STRICT_CHECKING
	assert( dst_fmt >= 0 && dst_fmt < 32 );
	assert( src_fmt == PIX_FMT_YUV420P || src_fmt == PIX_FMT_YUVJ420P ||
		src_fmt == PIX_FMT_YUV422P || src_fmt == PIX_FMT_YUVJ422P ||	
		src_fmt == PIX_FMT_YUV444P || src_fmt == PIX_FMT_YUVJ444P ||
		src_fmt == PIX_FMT_RGB24   || src_fmt == PIX_FMT_RGBA ||
		src_fmt == PIX_FMT_BGR24   || src_fmt == PIX_FMT_RGB32 ||
		src_fmt == PIX_FMT_BGR32   || src_fmt == PIX_FMT_RGB32_1  );
	assert( src->width > 0 );
	assert( dst->width > 0 );
#endif

	veejay_msg(0, "%s: from %s to %s", __FUNCTION__,
		find_pixel_format_descr( src_fmt ),
		find_pixel_format_descr( dst_fmt ) );
	
	struct SwsContext  *ctx = sws_getContext(
			src->width,
			src->height,
			src_fmt,
			dst->width,
			dst->height,
			dst_fmt,
			sws_context_flags_,
			NULL,NULL,NULL );

	sws_scale( ctx, src->data, src->stride,0, src->height,dst->data, dst->stride );

	sws_freeContext( ctx );
}


void	yuv_convert_any3( VJFrame *src, int src_stride[3], VJFrame *dst, int src_fmt, int dst_fmt )
{
#ifdef STRICT_CHECKING
	assert( dst_fmt >= 0 && dst_fmt < 32 );
	assert( src_fmt == PIX_FMT_YUV420P || src_fmt == PIX_FMT_YUVJ420P ||
		src_fmt == PIX_FMT_YUV422P || src_fmt == PIX_FMT_YUVJ422P ||	
		src_fmt == PIX_FMT_YUV444P || src_fmt == PIX_FMT_YUVJ444P ||
		src_fmt == PIX_FMT_RGB24   || src_fmt == PIX_FMT_RGBA  );
	assert( src_stride[0] > 0 );
	assert( dst->width > 0 );
	assert( dst->height > 0 );
	assert( dst->data[0] != NULL );
	assert( dst->data[1] != NULL );
	assert( dst->data[2] != NULL );
#endif
	struct SwsContext *ctx = sws_getContext(
			src->width,
			src->height,
			src_fmt,
			dst->width,
			dst->height,
			dst_fmt,
			sws_context_flags_,
			NULL,NULL,NULL );
	int dst_stride[3] = { ru4(dst->width),ru4(dst->uv_width),ru4(dst->uv_width) };
	sws_scale( ctx, src->data, src_stride, 0, src->height, dst->data, dst_stride);
	veejay_msg(0, "%s: from %s to %s, dst_stride[%d,%d,%d], src_stride[%d,%d,%d]",
		__FUNCTION__,
		find_pixel_format_descr(src_fmt),
		find_pixel_format_descr(dst_fmt),
		dst_stride[0],
		dst_stride[1],
		dst_stride[2],
		src_stride[0],	
		src_stride[1],
		src_stride[2] );
	
	sws_freeContext( ctx );
}


/* convert 4:2:0 to yuv 4:2:2 packed */
void yuv422p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int width,
		       int height)
{
    unsigned int x, y;


    for (y = 0; y < height; ++y) {
	uint8_t *Y = yuv420[0] + y * width;
	uint8_t *Cb = yuv420[1] + (y / 2) * (width);
	uint8_t *Cr = yuv420[2] + (y / 2) * (width);
	for (x = 0; x < width; x +=2) {
	    *(dest + 0) = Y[0];
	    *(dest + 1) = Cb[0];
	    *(dest + 2) = Y[1];
	    *(dest + 3) = Cr[0];
	    dest += 4;
	    Y += 2;
	    ++Cb;
	    ++Cr;
	}
    }
}



/* convert 4:2:0 to yuv 4:2:2 */
void yuv420p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int width,
		       int height)
{
    unsigned int x, y;

    for (y = 0; y < height; ++y) {
	uint8_t *Y = yuv420[0] + y * width;
	uint8_t *Cb = yuv420[1] + (y >> 1) * (width >> 1);
	uint8_t *Cr = yuv420[2] + (y >> 1) * (width >> 1);
	for (x = 0; x < width; x += 2) {
	    *(dest + 0) = Y[0];
	    *(dest + 1) = Cb[0];
	    *(dest + 2) = Y[1];
	    *(dest + 3) = Cr[0];
	    dest += 4;
	    Y += 2;
	    ++Cb;
	    ++Cr;
	}
    }
}

#ifdef HAVE_ASM_MMX 
#include "mmx_macros.h"
#include "mmx.h"

/*****************************************************************

  _yuv_yuv_mmx.c

  Copyright (c) 2001-2002 by Burkhard Plaum - plaum@ipf.uni-stuttgart.de

  http://gmerlin.sourceforge.net

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.


	- took yuy2 -> planar 422 and 422 planar -> yuy2 mmx conversion
							     routines.
	  (Niels, 02/2005)

*****************************************************************/

static mmx_t mmx_00ffw =   { 0x00ff00ff00ff00ffLL };

#ifdef HAVE_ASM_MMX2
//#ifdef MMXEXT
#define MOVQ_R2M(reg,mem) movntq_r2m(reg, mem)
#else
#define MOVQ_R2M(reg,mem) movq_r2m(reg, mem)
#endif

#define PLANAR_TO_YUY2   movq_m2r(*src_y, mm0);/*   mm0: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */\
                         movd_m2r(*src_u, mm1);/*   mm1: 00 00 00 00 U6 U4 U2 U0 */\
                         movd_m2r(*src_v, mm2);/*   mm2: 00 00 00 00 V6 V4 V2 V0 */\
                         pxor_r2r(mm3, mm3);/*      Zero mm3                     */\
                         movq_r2r(mm0, mm7);/*      mm7: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */\
                         punpcklbw_r2r(mm3, mm0);/* mm0: 00 Y3 00 Y2 00 Y1 00 Y0 */\
                         punpckhbw_r2r(mm3, mm7);/* mm7: 00 Y7 00 Y6 00 Y5 00 Y4 */\
                         pxor_r2r(mm4, mm4);     /* Zero mm4                     */\
                         punpcklbw_r2r(mm1, mm4);/* mm4: U6 00 U4 00 U2 00 U0 00 */\
                         pxor_r2r(mm5, mm5);     /* Zero mm5                     */\
                         punpcklbw_r2r(mm2, mm5);/* mm5: V6 00 V4 00 V2 00 V0 00 */\
                         movq_r2r(mm4, mm6);/*      mm6: U6 00 U4 00 U2 00 U0 00 */\
                         punpcklwd_r2r(mm3, mm6);/* mm6: 00 00 U2 00 00 00 U0 00 */\
                         por_r2r(mm6, mm0);      /* mm0: 00 Y3 U2 Y2 00 Y1 U0 Y0 */\
                         punpcklwd_r2r(mm3, mm4);/* mm4: 00 00 U6 00 00 00 U4 00 */\
                         por_r2r(mm4, mm7);      /* mm7: 00 Y7 U6 Y6 00 Y5 U4 Y4 */\
                         pxor_r2r(mm6, mm6);     /* Zero mm6                     */\
                         punpcklwd_r2r(mm5, mm6);/* mm6: V2 00 00 00 V0 00 00 00 */\
                         por_r2r(mm6, mm0);      /* mm0: V2 Y3 U2 Y2 V0 Y1 U0 Y0 */\
                         punpckhwd_r2r(mm5, mm3);/* mm3: V6 00 00 00 V4 00 00 00 */\
                         por_r2r(mm3, mm7);      /* mm7: V6 Y7 U6 Y6 V4 Y5 U4 Y4 */\
                         MOVQ_R2M(mm0, *dst);\
                         MOVQ_R2M(mm7, *(dst+8));


#define YUY2_TO_YUV_PLANAR movq_m2r(*src,mm0);\
                           movq_m2r(*(src+8),mm1);\
                           movq_r2r(mm0,mm2);/*       mm2: V2 Y3 U2 Y2 V0 Y1 U0 Y0 */\
                           pand_m2r(mmx_00ffw,mm2);/* mm2: 00 Y3 00 Y2 00 Y1 00 Y0 */\
                           pxor_r2r(mm4, mm4);/*      Zero mm4 */\
                           packuswb_r2r(mm4,mm2);/*   mm2: 00 00 00 00 Y3 Y2 Y1 Y0 */\
                           movq_r2r(mm1,mm3);/*       mm3: V6 Y7 U6 Y6 V4 Y5 U4 Y4 */\
                           pand_m2r(mmx_00ffw,mm3);/* mm3: 00 Y7 00 Y6 00 Y5 00 Y4 */\
                           pxor_r2r(mm6, mm6);/*      Zero mm6 */\
                           packuswb_r2r(mm3,mm6);/*   mm6: Y7 Y6 Y5 Y4 00 00 00 00 */\
                           por_r2r(mm2,mm6);/*        mm6: Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0 */\
                           psrlw_i2r(8,mm0);/*        mm0: 00 V2 00 U2 00 V0 00 U0 */\
                           psrlw_i2r(8,mm1);/*        mm1: 00 V6 00 U6 00 V4 00 U4 */\
                           packuswb_r2r(mm1,mm0);/*   mm0: V6 U6 V4 U4 V2 U2 V0 U0 */\
                           movq_r2r(mm0,mm1);/*       mm1: V6 U6 V4 U4 V2 U2 V0 U0 */\
                           pand_m2r(mmx_00ffw,mm0);/* mm0: 00 U6 00 U4 00 U2 00 U0 */\
                           psrlw_i2r(8,mm1);/*        mm1: 00 V6 00 V4 00 V2 00 V0 */\
                           packuswb_r2r(mm4,mm0);/*   mm0: 00 00 00 00 U6 U4 U2 U0 */\
                           packuswb_r2r(mm4,mm1);/*   mm1: 00 00 00 00 V6 V4 V2 V0 */\
                           MOVQ_R2M(mm6, *dst_y);\
                           movd_r2m(mm0, *dst_u);\
                           movd_r2m(mm1, *dst_v);

#define MMX_YUV422_YUYV "                                                 \n\
movq       (%1), %%mm0  # Load 8 Y            y7 y6 y5 y4 y3 y2 y1 y0     \n\
movd       (%2), %%mm1  # Load 4 Cb           00 00 00 00 u3 u2 u1 u0     \n\
movd       (%3), %%mm2  # Load 4 Cr           00 00 00 00 v3 v2 v1 v0     \n\
punpcklbw %%mm2, %%mm1  #                     v3 u3 v2 u2 v1 u1 v0 u0     \n\
movq      %%mm0, %%mm2  #                     y7 y6 y5 y4 y3 y2 y1 y0     \n\
punpcklbw %%mm1, %%mm2  #                     v1 y3 u1 y2 v0 y1 u0 y0     \n\
movq      %%mm2, (%0)   # Store low YUYV                                  \n\
punpckhbw %%mm1, %%mm0  #                     v3 y7 u3 y6 v2 y5 u2 y4     \n\
movq      %%mm0, 8(%0)  # Store high YUYV                                 \n\
"


void	yuv422_to_yuyv(uint8_t *src[3], uint8_t *dstI, int w, int h)
{
	int j,jmax,imax,i;
	uint8_t *dst = dstI;
	uint8_t *src_y = src[0];
	uint8_t *src_u = src[1];
	uint8_t *src_v = src[2];
	
	jmax = w >> 3;
	imax = h;

	for( i = imax; i-- ; )
	{
		for( j = jmax ; j -- ; )
		{
			__asm__( ".align 8" MMX_YUV422_YUYV
				: : "r" (dst), "r" (src_y), "r" (src_u),
				    "r" (src_v) );

			dst += 16;
			src_y += 8;
			src_u += 4;
			src_v += 4;
		}
	}
#ifdef HAVE_ASM_MMX
        __asm__ __volatile__ ( _EMMS:::"memory");
#endif
}


void	yuy2toyv16(uint8_t *dst_y, uint8_t *dst_u, uint8_t *dst_v, uint8_t *srcI, int w, int h )
{
	int j,jmax,imax,i;
	uint8_t *src = srcI;
	
	jmax = w / 8;
	imax = h;

	for( i = 0; i < imax ;i ++ )
	{
		for( j = 0; j < jmax ; j ++ )
		{
			YUY2_TO_YUV_PLANAR;
			src += 16;
			dst_y += 8;
			dst_u += 4;
			dst_v += 4;
		}
	}
#ifdef HAVE_ASM_MMX
        __asm__ __volatile__ ( _EMMS:::"memory");
#endif	
}

void yuy2toyv12(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
		int width, int height)
{
	int j,jmax,imax,i;
	uint8_t *src = input;
	uint8_t *dst_y = _y;
	uint8_t *dst_u = _u; 
	uint8_t *dst_v = _v;
	jmax = width / 8;
	imax = height;

	for( i = 0; i < imax ;i ++ )
	{
		for( j = 0; j < jmax ; j ++ )
		{
			YUY2_TO_YUV_PLANAR;
			src += 16;
			dst_y += 8;
			dst_u += 4;
			dst_v += 4;
		}
		dst_u += width;
		dst_v += width;
	}
#ifdef HAVE_ASM_MMX
        __asm__ __volatile__ ( _EMMS:::"memory");
#endif
}
#else
// non mmx functions

void yuy2toyv12(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
		int width, int height)
{
  int i, j, w2;
    uint8_t *y, *u, *v;

    w2 = width / 2;

    //I420
    y = _y;
    v = _v;
    u = _u;

    for (i = 0; i < height; i += 4) {
	/* top field scanline */
	for (j = 0; j < w2; j++) {
	    /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
	    *(y++) = *(input++);
	    *(u++) = *(input++);
	    *(y++) = *(input++);
	    *(v++) = *(input++);
	}
	for (j = 0; j < w2; j++)
	{
	    *(y++) = *(input++);
	    *(u++) = *(input++);
	    *(y++) = *(input++);
	    *(v++) = *(input++);
	
	}

	/* next two scanlines, one frome each field , interleaved */
	for (j = 0; j < w2; j++) {
	    /* skip every second line for U and V */
	    *(y++) = *(input++);
	    input++;
	    *(y++) = *(input++);
	    input++;
	}
	/* bottom field scanline*/
	for (j = 0; j < w2; j++) {
	    /* skip every second line for U and V */
	    *(y++) = *(input++);
	    input++;
	    *(y++) = *(input++);
	    input++;
	}

    }
}

void yuy2toyv16(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
		int width, int height)
{

    int i, j, w2;
    uint8_t *y, *u, *v;

    w2 = width / 2;

    //YV16
    y = _y;
    v = _v;
    u = _u;

    for (i = 0; i < height; i ++ )
    {
	for (j = 0; j < w2; j++) {
	    /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
	    *(y++) = *(input++);
	    *(u++) = *(input++);
	    *(y++) = *(input++);
	    *(v++) = *(input++);
	}
    }
}
#define MMX_YUV422_YUYV "                                                 \n\
movq       (%1), %%mm0  # Load 8 Y            y7 y6 y5 y4 y3 y2 y1 y0     \n\
movd       (%2), %%mm1  # Load 4 Cb           00 00 00 00 u3 u2 u1 u0     \n\
movd       (%3), %%mm2  # Load 4 Cr           00 00 00 00 v3 v2 v1 v0     \n\
punpcklbw %%mm2, %%mm1  #                     v3 u3 v2 u2 v1 u1 v0 u0     \n\
movq      %%mm0, %%mm2  #                     y7 y6 y5 y4 y3 y2 y1 y0     \n\
punpcklbw %%mm1, %%mm2  #                     v1 y3 u1 y2 v0 y1 u0 y0     \n\
movq      %%mm2, (%0)   # Store low YUYV                                  \n\
punpckhbw %%mm1, %%mm0  #                     v3 y7 u3 y6 v2 y5 u2 y4     \n\
movq      %%mm0, 8(%0)  # Store high YUYV                                 \n\
"


void	yuv422_to_yuyv(uint8_t *src[3], uint8_t *dstI, int w, int h)
{
	int j,jmax,imax,i;
	uint8_t *dst = dstI;
	uint8_t *src_y = src[0];
	uint8_t *src_u = src[1];
	uint8_t *src_v = src[2];
	
	jmax = w >> 3;
	imax = h;

	for( i = imax; i-- ; )
	{
		for( j = jmax ; j -- ; )
		{
			__asm__( ".align 8" MMX_YUV422_YUYV
				: : "r" (dst), "r" (src_y), "r" (src_u),
				    "r" (src_v) );

			dst += 16;
			src_y += 8;
			src_u += 4;
			src_v += 4;
		}
	}
#ifdef HAVE_ASM_MMX
        __asm__ __volatile__ ( _EMMS:::"memory");
#endif
}
/*
void yuv422_to_yuyv(uint8_t *yuv422[3], uint8_t *pixels, int w, int h)
{
    int x,y;
    uint8_t *Y = yuv422[0];
    uint8_t *U = yuv422[1];
    uint8_t *V = yuv422[2]; // U Y V Y


	for(y = 0; y < h; y ++ )
	{
		Y = yuv422[0] + y * w;
		U = yuv422[1] + (y>>1) * w;
		V = yuv422[2] + (y>>1) * w;
		for( x = 0 ; x < w ; x += 4 )
		{
			*(pixels + 0) = Y[0];
			*(pixels + 1) = U[0];
			*(pixels + 2) = Y[1];
			*(pixels + 3) = V[0];
			*(pixels + 4) = Y[2];
			*(pixels + 5) = U[1];
			*(pixels + 6) = Y[3];
			*(pixels + 7) = V[1];
			pixels += 8;
			Y+=4;
			U+=2;
			V+=2;
		}
    }
}*/

#endif


/* lav_common - some general utility functionality used by multiple
	lavtool utilities. */

/* Copyright (C) 2000, Rainer Johanni, Andrew Stevens */
/* - added scene change detection code 2001, pHilipp Zabel */
/* - broke some code out to lav_common.h and lav_common.c 
 *   July 2001, Shawn Sulma <lavtools@athos.cx>.  In doing this,
 *   I replaced the large number of globals with a handful of structs
 *   that are passed into the appropriate methods.  Check lav_common.h
 *   for the structs.  I'm sure some of what I've done is inefficient,
 *   subtly incorrect or just plain Wrong.  Feedback is welcome.
 */


int luminance_mean(uint8_t * frame[], int w, int h)
{
    uint8_t *p;
    uint8_t *lim;
    int sum = 0;
    int count = 0;
    p = frame[0];
    lim = frame[0] + w * (h - 1);
    while (p < lim) {
	sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
	p += 31;
	count += 4;
    }

    w = w / 2;
    h = h / 2;

    p = frame[1];
    lim = frame[1] + w * (h - 1);
    while (p < lim) {
	sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
	p += 31;
	count += 4;
    }
    p = frame[2];
    lim = frame[2] + w * (h - 1);
    while (p < lim) {
	sum += (p[0] + p[1]) + (p[w - 3] + p[w - 2]);
	p += 31;
	count += 4;
    }
    return sum / count;
}

typedef struct
{
	struct SwsContext *sws;
	SwsFilter	  *src_filter;
	SwsFilter	  *dst_filter;
} vj_sws;

void*	yuv_init_swscaler(VJFrame *src, VJFrame *dst, sws_template *tmpl, int cpu_flags)
{
	vj_sws *s = (vj_sws*) vj_malloc(sizeof(vj_sws));
	if(!s)
		return NULL;

	int	sws_type = 0;
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	veejay_memset( s, 0, sizeof(vj_sws) );

	switch(tmpl->flags)
	{
		case 1:
			sws_type = SWS_FAST_BILINEAR;
			break;
		case 2:
			sws_type = SWS_BILINEAR;
			break;
		case 3:
			sws_type = SWS_BICUBIC;
			break;
		case 4:
			sws_type = SWS_POINT;
			break;
		case 5:
			sws_type = SWS_X;
			break;
		case 6:
			sws_type = SWS_AREA;
			break;
		case 7:
			sws_type = SWS_BICUBLIN;
			break;
		case 8: 
			sws_type = SWS_GAUSS;
			break;
		case 9:
			sws_type = SWS_SINC;
			break;
		case 10:
			sws_type = SWS_LANCZOS;
			break;
		case 11:
			sws_type = SWS_SPLINE;
			break;
	}	

	s->sws = sws_getContext(
			src->width,
			src->height,
			src->format,
			dst->width,
			dst->height,
			dst->format,
			sws_type | cpu_flags,
			s->src_filter,
			s->dst_filter,
			NULL
		);

	if(!s->sws)
	{
		if(s)free(s);
		return NULL;
	}	

	return ((void*)s);

}

void  yuv_crop(VJFrame *src, VJFrame *dst, VJRectangle *rect )
{
	int x;
	int y;
	uint8_t *sy = src->data[0];
	uint8_t *su = src->data[1];
	uint8_t *sv = src->data[2];

	uint8_t *dstY = dst->data[0];	
	uint8_t *dstU = dst->data[1];
	uint8_t *dstV = dst->data[2];
	int i = 0;
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	for( i = 0 ; i < 3 ; i ++ )
	{
		int j = 0;
		uint8_t *srcPlane = src->data[i];
		uint8_t *dstPlane = dst->data[i];
		for( y = rect->top ; y < ( src->height - rect->bottom ); y ++ )
		{
			for ( x = rect->left ; x < ( src->width - rect->right ); x ++ )
			{
				dstPlane[j] = srcPlane[ y * src->width + x ];
				j++;
			}
		}
	}

}

VJFrame	*yuv_allocate_crop_image( VJFrame *src, VJRectangle *rect )
{
	int w = src->width - rect->left - rect->right;
	int h = src->height - rect->top - rect->bottom;

	if( w <= 0 )
		return NULL;
	if( h <= 0 )
		return NULL;

	VJFrame *new = (VJFrame*) vj_malloc(sizeof(VJFrame));
	if(!new)	
		return NULL;
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	new->width = w;
	new->height = h;	
	new->uv_len = (w >> src->shift_h) * (h >> src->shift_v );
	new->len = w * h;
	new->uv_width  = (w >> src->shift_h );
	new->uv_height = (h >> src->shift_v );
	new->shift_v = src->shift_v;
	new->shift_h = src->shift_h;

	return new;
}


void	yuv_free_swscaler(void *sws)
{
	if(sws)
	{
		vj_sws *s = (vj_sws*) sws;
		if(s->sws)
			sws_freeContext( s->sws );
		if(s) free(s);
	}
}

void	yuv_convert_and_scale_gray_rgb(void *sws,VJFrame *src, VJFrame *dst)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	vj_sws *s = (vj_sws*) sws;
	int src_stride[3] = { src->width,0,0 };
	int dst_stride[3] = { src->width * 3, 0,0 };

	sws_scale( s->sws, src->data,src_stride, 0,src->height,
		dst->data, dst_stride );
}


void	yuv_convert_and_scale_rgb(void *sws , VJFrame *src, VJFrame *dst)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	vj_sws *s = (vj_sws*) sws;
	int src_stride[3] = { src->width,src->uv_width,src->uv_width };
	int dst_stride[3] = { dst->width*3,0,0 };

	sws_scale( s->sws, src->data, src_stride, 0, src->height,
		dst->data, dst_stride );
//#ifdef HAVE_ASM_MMX
  //      __asm__ __volatile__ ( _EMMS:::"memory");
//#endif
}
void	yuv_convert_and_scale(void *sws , VJFrame *src, VJFrame *dst)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	vj_sws *s = (vj_sws*) sws;
	int src_stride[3] = { src->width,src->uv_width,src->uv_width };
	int dst_stride[3] = { dst->width,dst->uv_width,dst->uv_width };

	sws_scale( s->sws, src->data, src_stride, 0, src->height,
		dst->data, dst_stride );
//#ifdef HAVE_ASM_MMX
  //      __asm__ __volatile__ ( _EMMS:::"memory");
//#endif
}

int	yuv_sws_get_cpu_flags(void)
{
	int cpu_flags = 0;
	return 0;
#ifdef HAVE_ASM_MMX
	cpu_flags = cpu_flags | SWS_CPU_CAPS_MMX;
#endif
#ifdef HAVE_ASM_3DNOW
	cpu_flags = cpu_flags | SWS_CPU_CAPS_3DNOW;
#endif
#ifdef HAVE_ASM_MMX2
	cpu_flags = cpu_flags | SWS_CPU_CAPS_MMX2;
#endif
#ifdef HAVE_ALTIVEC
	cpu_flags = cpu_flags | SWS_CPU_CAPS_ALTIVEC;
#endif

	cpu_flags = cpu_flags | SWS_FULL_CHR_H_INT;

	cpu_flags = cpu_flags | SWS_FULL_CHR_H_INP;

	return cpu_flags;
}

void	yuv_deinterlace(
		uint8_t *data[3],
		const int width,
		const int height,
		int out_pix_fmt,
		int shift,
		uint8_t *Y,uint8_t *U, uint8_t *V )
{
	veejay_msg(0, "%s: deinterlace %s",__FUNCTION__, find_pixel_format_descr( out_pix_fmt ) );

	AVPicture p,q;
	p.data[0] = data[0];
	p.data[1] = data[1];
	p.data[2] = data[2];
	p.linesize[0] = width;
	p.linesize[1] = width >> shift;
	p.linesize[2] = width >> shift;
	q.data[0] = Y;
	q.data[1] = U;
	q.data[2] = V;
	q.linesize[0] = width;
	q.linesize[1] = width >> shift;
	q.linesize[2] = width >> shift;
	avpicture_deinterlace( &p,&q, out_pix_fmt, width, height );
}

//! YUV 4:2:0 Planar to 4:4:4 Packed: Y, V, U, Y,V, U , .... */
static void yuv420_444_1plane(
		VJFrame *frame,
		uint8_t *dst_buffer)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	unsigned int x,y;
	const unsigned int width = frame->uv_width;
	const unsigned int height = frame->uv_height;
	const uint8_t *yp = frame->data[0];
	const uint8_t *pu = frame->data[1];
	const uint8_t *pv = frame->data[2];
	const int len = frame->len / 4;
//	const uint8_t *su = frame->data[1];
//	const uint8_t *sv = frame->data[2];
       	
/*	while( x < len )
	{
		*(dst_buffer++) = yp[0];
		*(dst_buffer++) = sv[0];
		*(dst_buffer++) = su[0];
		*(dst_buffer++) = yp[1];
		*(dst_buffer++) = sv[0];
		*(dst_buffer++) = su[0];
		*(dst_buffer++) = yp[2];
		*(dst_buffer++) = sv[0];
		*(dst_buffer++) = su[0];
		*(dst_buffer++) = yp[3];
		*(dst_buffer++) = sv[0];
		*(dst_buffer++) = su[0];
		su+=1;
		sv+=1;
		yp += 4;
		x++;
	}*/
	
	for( y = 0; y < height; y ++ )
	{
		const uint8_t *su = pu + ( y * width);
		const uint8_t *sv = pv + ( y * width );
		for( x = 0; x < width; x++ )
		{
			*(dst_buffer++) = *(yp++);
			*(dst_buffer++) = *(sv);
			*(dst_buffer++) = *(su);
			*(dst_buffer++) = *(yp++);
			*(dst_buffer++) = *(sv);
			*(dst_buffer++) = *(su);
			*(dst_buffer++) = *(yp++);
			*(dst_buffer++) = *(sv);
			*(dst_buffer++) = *(su);
			*(dst_buffer++) = *(yp++);
			*(dst_buffer++) = *(sv);
			*(dst_buffer++) = *(su);

			su ++;
			sv ++;
		}
	}
	/*
	uint8_t *py = dst_buffer;
	for( x = 0; x < (width * height) ; x ++ )
	{
		*(py) = *(yp++);
		py += 3;	
	}
	uint8_t *xu = dst_buffer + 1;
	for( y = 0; y < height; y ++ )
	{
		for( x = 0; x < width; x ++ )
		{
			*(xu) = *(pv);
		        *(xu++) = *(pu);
			xu++;
			*(xu++) = *(pv);	
		}
	}	
	*/
}

void yuv444_to_yuyv(void *sampler, uint8_t *data[3], uint8_t *pixels, int w, int h)
{

	VJFrame *src = yuv_yuv_template( data[0],data[1],data[2], w,h, PIX_FMT_YUV444P );
	VJFrame *dst = yuv_yuv_template( pixels, NULL,NULL,w,h,PIX_FMT_YUYV422 );

	yuv_convert_any(src,dst, PIX_FMT_YUV444P, PIX_FMT_YUYV422 );

	free(src);
	free(dst);

/*	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	int x,y;
	chroma_subsample( SSM_422_444, sampler, data, w, h );
	yuv422_to_yuyv( data, pixels, w,h);*/



	/*for(y = 0; y < h; y ++ )
	{
		uint8_t *Y = data[0] + y * w;
		uint8_t *U = data[1] + y * w;
		uint8_t *V = data[2] + y * w;

		for( x = 0 ; x < w ; x += 4 )
		{
			*(pixels + 0) = Y[0];
			*(pixels + 1) = (U[0] + U[1]) >> 1;
			*(pixels + 2) = Y[1];
			*(pixels + 3) = (V[0] + V[1]) >> 1;
			*(pixels + 4) = Y[2];
			*(pixels + 5) = (U[2] + U[3]) >> 1;
			*(pixels + 6) = Y[3];
			*(pixels + 7) =  (V[2] + V[3]) >> 1;
			pixels += 8;
			Y+=4;
			U+=2;
			V+=2;
		}
	}*/
}

//! YUV 4:2:2 Planar to 4:4:4 Packed: Y, V, U, Y,V, U , .... */
//
//
#define packv0__( y0,u0,v0,y1 ) (( (int) y0 ) & 0xff ) +\
		( (((int) u0 ) & 0xff) << 8) +\
		( ((((int) v0) & 0xff) << 16 )) +\
		( ((((int) y1) & 0xff) << 24 ) )

#define packv1__( u1,v1,y2,u2 )(( (int) u1 ) & 0xff ) +\
		( (((int) v1 ) & 0xff) << 8) +\
		( ((((int) y2) & 0xff) << 16 )) +\
		( ((((int) u2) & 0xff) << 24 ) )


#define packv2__( v2,y3,u3,v3 )(( (int) v2 ) & 0xff ) +\
		( (((int) y3 ) & 0xff) << 8) +\
		( ((((int) u3) & 0xff) << 16 )) +\
		( ((((int) v3) & 0xff) << 24 ) )


#define pack0__( y0,u0,v0,y1 ) (( (int) y0[0] ) & 0xff ) +\
		( (((int) u0[0] ) & 0xff) << 8) +\
		( ((((int) v0[0]) & 0xff) << 16 )) +\
		( ((((int) y1[0]) & 0xff) << 24 ) )

#define pack1__( u1,v1,y2,u2 )(( (int) u1[0] ) & 0xff ) +\
		( (((int) v1[0] ) & 0xff) << 8) +\
		( ((((int) y2[0]) & 0xff) << 16 )) +\
		( ((((int) u2[0]) & 0xff) << 24 ) )


#define pack2__( v2,y3,u3,v3 )(( (int) v2[0] ) & 0xff ) +\
		( (((int) y3[0] ) & 0xff) << 8) +\
		( ((((int) u3[0]) & 0xff) << 16 )) +\
		( ((((int) v3[0]) & 0xff) << 24 ) )


#define packpl__( y0,u0,v0 ) (( (int) y0[0] ) & 0xff ) + \
			( (((int) u0[0] ) & 0xff ) << 8 ) + \
			( ((((int) v0[0]) & 0xff ) << 16 )) 


static void yuv422_444_1plane(
		VJFrame *frame,
		uint8_t *dst_buffer)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	unsigned int x,y;
	const unsigned int stride = frame->uv_width;
	const unsigned int height = frame->height;
	const uint8_t *yp = frame->data[0];
	const uint8_t *up = frame->data[1];
	const uint8_t *vp = frame->data[2];
	for( y = 0 ; y < height ; y ++ )
	{
		uint8_t *su = up + (y * stride );
		uint8_t *sv = vp + (y * stride );
		for( x = 0; x < stride; x++ )
		{
			*(dst_buffer++) = *(yp++);
			*(dst_buffer++) = *(sv);
			*(dst_buffer++) = *(su);
			*(dst_buffer++) = *(yp++);
			*(dst_buffer++) = *(sv);
			*(dst_buffer++) = *(su);

			*(su++);
			*(sv++);

		}
	}
}

//prefetchnta
//! YUV 4:2:4 Planar to 4:4:4 Packed: Y, V, U, Y,V, U , .... */
static void yuv444_444_1plane(
		VJFrame *frame,
		uint8_t *dst_buffer)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	/* YUV 4:4:4 Planar to 4:4:4 Packed: Y, V, U */
	unsigned int x,k=0;
	uint8_t *yp = frame->data[0];
	uint8_t *up = frame->data[2];
	uint8_t *vp = frame->data[1];
	int len = frame->len;
	uint8_t *dst = dst_buffer;
	for( x = 0 ; x < len ; x ++ )
	{
		dst[k+0] = yp[x];
		dst[k+1] = up[x]; //@ chroma not ok, FIXME. k+1,k+2 = 128 
		dst[k+2] = vp[x];
		k+=3;
	}
	
}

static	void	yuv444a_444a_1plane(
		VJFrame *frame,
		uint8_t *dst_buffer)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

#ifdef HAVE_ASM_MMX
	int len = frame->len / 8;
	const uint8_t *y = frame->data[0];
	const uint8_t *u = frame->data[1];
	const uint8_t *v = frame->data[2];
	const uint8_t *a = frame->data[3];
	const uint8_t *yuva = dst_buffer;
	int k;
	for( k = 0; k < len; k ++ )
	{
		__asm__ __volatile__ (
			"movd	(%0), %%mm0\n"
			"movd	(%1), %%mm1\n"
			"punpcklbw %%mm0,%%mm1\n"
			"movd	(%2), %%mm2\n"
			"movd	(%3), %%mm3\n"
			"punpcklbw %%mm3,%%mm2\n"
			"punpcklbw %%mm2,%%mm1\n"
			"movntq	%%mm1,(%4)\n"
			:: "r" (u),
			   "r" (y),
			   "r" (v),
			   "r" (a), 
			   "r" (yuva) : "memory"
		  );
		yuva += 8;
	}
#endif
}
static void	yuv_planar_copy( VJFrame *dst, const uint8_t *plane, int len, int uv_len )
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	veejay_memcpy(dst->data[0], 
			plane,
			len );
	veejay_memcpy(dst->data[1],
			plane + len,
			uv_len );
        veejay_memcpy(dst->data[2],
			plane + len + uv_len,
			uv_len );	
}

static	void	yuv_planar_grow( VJFrame *dst, subsample_mode_t mode, void *data, int fmt, const uint8_t *plane )
{
	int w = dst->width;
	int h = dst->height;

	switch(fmt)
	{
		case FMT_420:
			yuv_planar_copy(dst, plane, w*h, (w*h)/4);
			break;
		case FMT_422:
			yuv_planar_copy(dst, plane, w*h, (w*h)/2 );
			break;
	}
	
	chroma_supersample( mode,
			data,
			dst->data,
			w,
		        h
			);
}
static	void	yuv_planar_shrink( VJFrame *dst, subsample_mode_t mode, void *data, int fmt, const uint8_t *plane )
{
	int w = dst->width;
	int h = dst->height;

	switch(fmt)
	{
		case FMT_422:
			yuv_planar_copy(dst, plane, w*h, (w*h)/2 );
			break;
		case FMT_444:
			yuv_planar_copy(dst,plane,w*h,w*h);
			break;
	}
	
	chroma_subsample( mode,
			data,
			dst->data,
		        w,
	       		h
		  	);
}

void	yuv_420_1plane_to_planar(VJFrame *dst, const uint8_t *plane, void *sampler)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );

	switch(dst->format)
	{
		case FMT_420:
			yuv_planar_copy( dst, plane,dst->len,dst->uv_len);
			break;
		case FMT_422:
			/*yuv420p_to_yuv422p(
				plane,
                                plane+dst->len,
                                plane+dst->len+(dst->len/4),
                                dst->data,	
				dst->width,
				dst->height); */
#ifdef STRICT_CHECKING
			assert(0);
#endif
			break;
		case FMT_444:
			yuv_planar_grow( dst, SSM_420_JPEG_BOX, sampler, FMT_420, plane );
			break;
	}
}
void	yuv_422_1plane_to_planar(VJFrame *dst, const uint8_t *plane, void *sampler)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );


	switch(dst->format)
	{
		case FMT_420:
#ifdef STRICT_CHECKING
			assert(0);
#endif
	//		yuv422p_to_yuv420p3( plane, dst->data, 
	//			dst->width, dst->height );	
			break;
		case FMT_422:
			yuv_planar_copy(dst, plane,dst->len,dst->uv_len);
			break;
		case FMT_444:
			yuv_planar_grow( dst, SSM_422_444, sampler, FMT_422, plane );
			break;
	}
}

void	yuv_1plane_to_planar( int fmt, uint8_t *plane, VJFrame *dst, void *sampler )
{

	switch(fmt)
	{
		case FMT_420:
			yuv_420_1plane_to_planar( dst, plane,sampler );
			break;
		case FMT_422:
			yuv_422_1plane_to_planar(dst, plane,sampler );
			break;
		case FMT_444:
			yuv_444_1plane_to_planar(dst,plane,sampler);
			break;
	}
}

//! Fade A->B , P = (Ai * alphai) + (B * (255-alphai)) / 255. MMX optimized. Blends by using 8 bit alpha channel
/*
void	yuv_1plane_blend_alpha_channel( uint8_t *A, uint8_t *B, int alpha int slen )
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );


	const uint8_t *a = A;
	const uint8_t *b = B;
	const uint8_t *alpha = Alpha;
	uint8_t *ab = dst_plane;
	int op0 = alpha;
	int op1 = 255 - op0;

	int i;
	for( i = 0; i < slen; i ++ )
	{
		B[i] = (A[i] * op0 + B[i] * op1 ) >> 8;
	}

	
	int len = slen / 8;
	int k;
	for( k = 0; k < len; k ++ )
	{
		__asm__ __volatile__ (
			"movd	(%0), %%mm0\n" // copy A
			"movd   (%1), %%mm1\n"  // copy B
			"movd  4(%0), %%mm3\n" // copy A + 4 to mm3
			"movd  4(%1), %%mm4\n"
			"movd   (%2), %%mm7\n"
			"pxor   %%mm6,%%mm6\n"  // mm6 = 0
			"punpcklbw %%mm6,%%mm7\n"
			"punpcklbw %%mm6,%%mm0\n"
			"punpcklbw %%mm6,%%mm1\n"
			"punpcklbw %%mm6,%%mm3\n"
			"punpcklbw %%mm6,%%mm4\n"
			"psubw	%%mm1,%%mm0\n"
			"psubw	%%mm4,%%mm3\n"
			"pmullw	%%mm7,%%mm0\n" // mul A1
			"pmullw %%mm7,%%mm3\n"
			"psrlw	$8,	%%mm0\n" // shift 8
			"psrlw  $8,	%%mm3\n"
			"paddb  %%mm1,%%mm0\n"
			"paddb  %%mm4,%%mm3\n"
			"packuswb %%mm6,%%mm0\n"
			"packuswb %%mm6,%%mm3\n"
			"movq   %%mm0, (%2)\n"	
			"movq   %%mm3, 4(%3)\n"

		:: "r" (a), "r" (b),"r" (alpha), "r" (ab) : "memory");

		a += 8;
		b += 8;
		ab += 8;
		alpha += 8;
	}	
	__asm__ __volatile__("emms");  
	
}*/
//! Fade A->B , P = (A * o1) + (B * (255-o1)) / 255. MMX optimized 
static void	yuv_1plane_blend_opacity( uint8_t *A, uint8_t *B,uint8_t alpha, int Alen )
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );


	unsigned int k;
	const unsigned int len = Alen;
	const uint8_t op0 = alpha;
	const uint8_t op1 = 255 - op0;
	
	for( k =0 ; k < len; k ++ )
		B[k] = (A[k] * op0 + B[k] * op1 ) >> 8;




/*	uint8_t alpha_v[8] = { alpha,alpha,alpha,alpha,alpha,alpha,
				alpha,alpha };
	const uint8_t *alpha1 = &alpha_v[0];
	const uint8_t *a = A;
	const uint8_t *b = B;
	uint8_t *ab = dst;

	for( k = 0; k < len; k ++ )
	{
		__asm__ __volatile__ (
			"prefetchnta	(%0)\n"
			"prefetchnta	(%1)\n"
			"movd	(%0), %%mm0\n" // copy A
			"movd   (%1), %%mm1\n"  // copy B
			"punpcklbw %%mm6,%%mm0\n"
			"punpcklbw %%mm6,%%mm1\n"
			"psubw	%%mm1,%%mm0\n"
			"pmullw	%%mm7,%%mm0\n" // mul A1
			"psrlw	$8,	%%mm0\n" // shift 8
			"paddb  %%mm1,%%mm0\n" // add
			"packuswb %%mm1,%%mm0\n"
			"movntq   %%mm0, (%2)\n"
		:: "r" (a), "r" (b), "r" (ab) : "memory");

		a += 4;
		b += 4;
		ab += 4;
	}*/
}

void	yuv_blend_opacity( VJFrame *A, VJFrame *B, uint8_t alpha )
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );


	yuv_1plane_blend_opacity( A->data[0], B->data[0], alpha, A->len );
	yuv_1plane_blend_opacity( A->data[1], B->data[1], alpha, A->uv_len );
	yuv_1plane_blend_opacity( A->data[2], B->data[2], alpha, A->uv_len );
}

void	yuv_444_1plane_to_planar(VJFrame *dst,const uint8_t *plane, void *sampler)
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );


	switch(dst->format)
	{
		case FMT_420:
			yuv_planar_shrink( dst, SSM_420_JPEG_BOX, sampler, FMT_444, plane );
			break;
		case FMT_422:
			yuv_planar_shrink( dst, SSM_422_444, sampler, FMT_444, plane );
			break;
		case FMT_444:
			yuv_planar_copy(dst,plane, dst->len,dst->uv_len);
			break;
	}
}




void	yuv_planar_to_packed_444yvu( VJFrame *frame, uint8_t *dst_buffer )
{
	veejay_msg(0, "%s:%d",__FUNCTION__, __LINE__ );


	switch(frame->format)
	{
		case FMT_420:
		case FMT_420F:
			yuv420_444_1plane(frame,dst_buffer);
			break;
		case FMT_422:
		case FMT_422F:
			yuv422_444_1plane(frame,dst_buffer);
			break;
		case FMT_444:
		case FMT_444F:
			yuv444_444_1plane(frame,dst_buffer);
			break;
	}
}


