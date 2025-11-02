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
#include <libavcodec/version.h>

// This is not a library, it is a collection of helper functions for various purposes

void	*avhelper_alloc_frame();

int 	avhelper_get_codec_by_id(int id);

int	avhelper_get_codec_by_key( int key );

void	*avhelper_get_codec_ctx( void *ptr );

const void	*avhelper_get_codec( void *ptr );

void	avhelper_close_decoder( void *ptr );

int	avhelper_decode_video( void *ptr, uint8_t *data, int len);

int	avhelper_rescale_video(void *ptr, uint8_t *dst[4]);

int avhelper_decode_video_direct( void *ptr, uint8_t *data, int len, uint8_t *dst[4], int pf, int w, int h );

void	*avhelper_get_decoder( const char *filename, int dst_pixfmt, int dst_width, int dst_height );

void	*avhelper_get_stream_decoder( const char *filename, int dst_pixfmt, int dst_width, int dst_height );

VJFrame	*avhelper_get_decoded_video(void *ptr);

int avhelper_decode_video3( AVCodecContext *avctx, AVFrame *frame, int *got_picture,AVPacket *pkt);

void	avhelper_free_context(AVCodecContext **avctx);

void	avhelper_frame_unref(AVFrame *ptr);

void	*avhelper_get_mjpeg_decoder(VJFrame *output_info);

void    avhelper_free_packet(AVPacket *pkt);

VJFrame	*avhelper_get_input_frame( void *ptr );

VJFrame *avhelper_get_output_frame( void *ptr);

int avhelper_recv_decode( void *decoder, int *got_picture );

int avhelper_recv_frame_packet( void *decoder );

int	avhelper_decode_video_buffer( void *ptr, uint8_t *data, int len );

double avhelper_get_spvf( void *decoder );

#endif
