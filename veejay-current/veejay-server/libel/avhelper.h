/* veejay - Linux VeeJay
 *           (C) 2002-2015 Niels Elburg <nwelburg@gmail.com> 
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
 * GNU General Public License for more details//.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef AVHELPER_H
#define AVHELPER_H

#define CODEC_ID_YUV420 999
#define CODEC_ID_YUV422 998
#define CODEC_ID_YUV422F 997
#define CODEC_ID_YUV420F 996
#define CODEC_ID_YUVLZO 900

int 	avhelper_get_codec_by_id(int id);

int	avhelper_get_codec_by_name( const char *compr );

void	*avhelper_get_codec_ctx( void *ptr );

void	*avhelper_get_codec( void *ptr );

void	avhelper_close_decoder( void *ptr );

int	avhelper_decode_video( void *ptr, uint8_t *data, int len, uint8_t *dst[3] );

void	*avhelper_get_decoder( const char *filename, int dst_pixfmt, int dst_width, int dst_height );

void	avhelper_free_context(AVCodecContext **avctx);

void	avhelper_frame_unref(AVFrame *ptr);

#endif
