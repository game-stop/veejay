/* veejay - Linux VeeJay
 * 	     (C) 2002-2015 Niels Elburg <nwelburg@gmail.com> 
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

#ifndef AV_H
#define AV_H
#include <libavcodec/version.h>

#define FF_VJE_BACKPORT (LIBAVCODEC_VERSION_MAJOR >= 56 && LIBAVCODEC_VERSION_MINOR >= 1)

//#define FF_NO_VIDEO_ENCODE (LIBAVCODEC_VERSION_MICRO <= 0 && LIBAVCODEC_VERSION_MAJOR >= 56)
extern int avcodec_encode_video(AVCodecContext *avctx, uint8_t *buf, int buf_size,const AVFrame *pict) __attribute__((weak));

extern int avcodec_encode_video2(AVCodecContext *avctx, AVPacket *avpkt,const AVFrame *frame, int *got_packet_ptr) __attribute__((weak));

extern void avcodec_free_context(AVCodecContext **avctx) __attribute__((weak));

extern void av_frame_unref(AVFrame *ptr) __attribute((weak));


#if FF_VJE_BACKPORT

#ifndef CODEC_ID_MJPEGB
#define CODEC_ID_MJPEGB AV_CODEC_ID_MJPEGB
#endif

#ifndef CODEC_ID_MPEG4
#define CODEC_ID_MPEG4 AV_CODEC_ID_MPEG4
#endif

#ifndef CODEC_ID_MSMPEG4V3
#define CODEC_ID_MSMPEG4V3 AV_CODEC_ID_MSMPEG4V3
#endif

#ifndef CODEC_ID_DVVIDEO
#define CODEC_ID_DVVIDEO AV_CODEC_ID_DVVIDEO
#endif

#ifndef CODEC_ID_LJPEG
#define CODEC_ID_LJPEG AV_CODEC_ID_LJPEG
#endif

#ifndef CODEC_ID_SP5X
#define CODEC_ID_SP5X AV_CODEC_ID_SP5X
#endif

#ifndef CODEC_ID_THEORA
#define CODEC_ID_THEORA AV_CODEC_ID_THEORA
#endif

#ifndef CODEC_ID_H264
#define CODEC_ID_H264 AV_CODEC_ID_H264
#endif

#ifndef CODEC_ID_MJPEG
#define CODEC_ID_MJPEG AV_CODEC_ID_MJPEG
#endif

#ifndef CODEC_ID_PNG
#define CODEC_ID_PNG AV_CODEC_ID_PNG
#endif

#ifndef CODEC_ID_MSMPEG4V2
#define CODEC_ID_MSMPEG4V2 AV_CODEC_ID_MSMPEG4V2
#endif

#ifndef CODEC_ID_MSMPEG4V1
#define CODEC_ID_MSMPEG4V1 AV_CODEC_ID_MSMPEG4V1
#endif

#ifndef CODEC_ID_HUFFYUV
#define CODEC_ID_HUFFYUV AV_CODEC_ID_HUFFYUV
#endif

#ifndef CODEC_ID_CYUV
#define CODEC_ID_CYUV AV_CODEC_ID_CYUV
#endif

#ifndef CODEC_ID_SVQ1
#define CODEC_ID_SVQ1 AV_CODEC_ID_SVQ1
#endif

#ifndef CODEC_ID_SVQ3
#define CODEC_ID_SVQ3 AV_CODEC_ID_SVQ3
#endif

#ifndef CODEC_ID_RPZA
#define CODEC_ID_RPZA AV_CODEC_ID_RPZA
#endif

#ifndef CODEC_ID_FIRST_AUDIO
#define CODEC_ID_FIRST_AUDIO AV_CODEC_ID_FIRST_AUDIO
#endif

#ifndef CODEC_ID_FIST_SUBTITLE
#define CODEC_ID_FIRST_SUBTITLE AV_CODEC_ID_FIRST_SUBTITLE
#endif

#endif

#endif
