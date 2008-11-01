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
#ifndef VJ_AVCODEC_H
#define VJ_AVCODEC_H


void	*vj_avcodec_new_encoder( int id, int w, int h, int pixel_format, double dfps);

int		vj_avcodec_encode_frame( void *codec, int format, void *dsrc, uint8_t *buf, int buf_len, uint64_t nframe);

/* color space conversion routines, should go somewhere else someday
   together with subsample.c/colorspace.c into some lib
 */

void	yuv_planar_to_rgb24(uint8_t *src[3], int fmt, uint8_t *dst, int w, int h );

// from yuv 4:2:0 planar to yuv 4:2:2 planar
int		yuv420p_to_yuv422p( uint8_t *Y, uint8_t *Cb, uint8_t *Cr, uint8_t *dst[3], int w, int h );

void	yuv422p_to_yuv420p2( uint8_t *src[3], uint8_t *dst[3], int w, int h );

int		yuv420p_to_yuv422p2( uint8_t *sY,uint8_t *sCb, uint8_t *sCr, uint8_t *dst[3], int w, int h );

void	yuv422p_to_yuv420p3( uint8_t *src, uint8_t *dst[3], int w, int h);

#endif
