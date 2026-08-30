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
#include <libavcodec/avcodec.h>

#if LIBAVCODEC_VERSION_MAJOR < 60
#error "VeeJay requires FFmpeg libavcodec 60 or newer"
#endif

extern void avhelper_decode_finish( void *ptr );

/* Retained temporarily for the legacy vj-el.c/rawdv.c source vocabulary. */
#ifndef CODEC_ID_DVVIDEO
#define CODEC_ID_DVVIDEO AV_CODEC_ID_DVVIDEO
#endif

#ifndef CODEC_ID_MJPEG
#define CODEC_ID_MJPEG AV_CODEC_ID_MJPEG
#endif

#ifndef CODEC_ID_HUFFYUV
#define CODEC_ID_HUFFYUV AV_CODEC_ID_HUFFYUV
#endif

#endif
