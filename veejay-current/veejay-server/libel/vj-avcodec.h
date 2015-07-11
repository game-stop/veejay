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
#include <libavcodec/avcodec.h>
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
#define ENCODER_MJPEGB 11
#define ENCODER_LJPEG 12
#define ENCODER_YUV4MPEG 13
#define ENCODER_YUV4MPEG420 14
#define ENCODER_HUFFYUV 15
#define NUM_ENCODERS 16

typedef struct
{
	AVCodec *codec;
	AVCodec *audiocodec;
//	AVFrame *frame;
	AVCodecContext	*context;
	int out_fmt;
	int uv_len;
	int len;
	int sub_sample;	
	int super_sample;
	int encoder_id;
	int width;
	int height;
	uint8_t *data[4];
	void *lzo;
	int	shift_y;
	int	shift_x;
	void	*dv;
	void	*y4m;
} vj_encoder;

int		vj_avcodec_init(int pix, int verbose);
char		vj_avcodec_find_lav(int format);
int		vj_avcodec_encode_frame(void *encoder,long nframe, int format, uint8_t *src[4], uint8_t *dst, int dst_len, int pixel_format);
uint8_t 		*vj_avcodec_get_buf( vj_encoder *av );
const char		*vj_avcodec_get_encoder_name(int encoder);
int		vj_avcodec_free();
void vj_libav_ffmpeg_version();

/* color space conversion routines, should go somewhere else someday
   together with subsample.c/colorspace.c into some lib
 */

void	yuv_planar_to_rgb24(uint8_t *src[3], int fmt, uint8_t *dst, int w, int h );

// from yuv 4:2:0 planar to yuv 4:2:2 planar
int		yuv420p_to_yuv422p( uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *dst[3], int w, int h );

void	yuv422p_to_yuv420p2( uint8_t *src[3], uint8_t *dst[3], int w, int h, int f );

int		yuv420p_to_yuv422p2( uint8_t *sY,uint8_t *sCb, uint8_t *sCr, uint8_t *dst[3], int w, int h );

void	yuv422p_to_yuv420p3( uint8_t *src, uint8_t *dst[3], int w, int h);

void 		*vj_avcodec_start( VJFrame *frame, int encoder, char *filename );

int		vj_avcodec_stop( void *encoder , int fmt);

void               vj_avcodec_close_encoder( vj_encoder *av );


void	yuv_scale_pixels_from_yuv_copy( uint8_t *plane, uint8_t *dst, float min, float max, int len );

#endif
