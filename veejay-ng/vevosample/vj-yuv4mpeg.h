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
#ifndef VJ_YUV4MPEG_H
#define VJ_YUV4MPEG_H


void *vj_yuv4mpeg_alloc(float fps, int dst_w, int dst_h, int sar_w, int sar_h);

void vj_yuv4mpeg_free(void *v) ;

int vj_yuv_stream_start_read(void *, char *, int width, int height);

int vj_yuv_stream_write_header(void * yuv4mpeg);

int vj_yuv_stream_start_write(void *, char *);

void vj_yuv_stream_stop_read(void * yuv4mpeg);

void vj_yuv_stream_stop_write(void * yuv4mpeg);

int vj_yuv_get_frame(void *, uint8_t **);

int vj_yuv_put_frame(void * vjyuv, uint8_t **);

int vj_yuv_stream_open_pipe(void *, char *);

int vj_yuv_stream_header_pipe( void * );
#endif
