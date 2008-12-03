/* veejay - Linux VeeJay
 * 	     (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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
#ifndef VJ_AVCODEC_H
#define VJ_AVCODEC_H
//bad
#include AVCODEC_INC
#include "vj-el.h"

#define ENCODER_MJPEG 0
#define ENCODER_DVVIDEO 1
#define ENCODER_DIVX 2
#define ENCODER_MPEG4 3
#define ENCODER_YUV420 4
#define ENCODER_YUV422 5
#define ENCODER_QUICKTIME_DV 6
#define ENCODER_QUICKTIME_MJPEG 7
#define ENCODER_LZO 8
#define ENCODER_YUV420F 9
#define ENCODER_YUV422F 10
#define NUM_ENCODERS 11

typedef struct
{
	AVCodec *codec;
	AVCodec *audiocodec;
	AVFrame *frame;
	AVCodecContext	*context;
	int out_fmt;
	int uv_len;
	int len;
	int sub_sample;	
	int super_sample;
	int encoder_id;
	int width;
	int height;
	uint8_t *data[3];
	void *lzo;
} vj_encoder;

int		vj_avcodec_init(int pix, int verbose);

int		vj_avcodec_encode_frame(void *encoder,int nframe, int format, uint8_t *src[3], uint8_t *dst, int dst_len, int pixel_format);
uint8_t 		*vj_avcodec_get_buf( vj_encoder *av );

int		vj_avcodec_free();

/* color space conversion routines, should go somewhere else someday
   together with subsample.c/colorspace.c into some lib
 */

void	yuv_planar_to_rgb24(uint8_t *src[3], int fmt, uint8_t *dst, int w, int h );

// from yuv 4:2:0 planar to yuv 4:2:2 planar
int		yuv420p_to_yuv422p( uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *dst[3], int w, int h );

void	yuv422p_to_yuv420p2( uint8_t *src[3], uint8_t *dst[3], int w, int h, int f );

int		yuv420p_to_yuv422p2( uint8_t *sY,uint8_t *sCb, uint8_t *sCr, uint8_t *dst[3], int w, int h );

void	yuv422p_to_yuv420p3( uint8_t *src, uint8_t *dst[3], int w, int h);

void 		*vj_avcodec_start( editlist *el, int encoder );

int		vj_avcodec_stop( void *encoder , int fmt);

void               vj_avcodec_close_encoder( vj_encoder *av );


#endif
