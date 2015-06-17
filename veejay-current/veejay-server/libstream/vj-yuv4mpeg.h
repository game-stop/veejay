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
#ifndef VJ_YUV4MPEG_H
#define VJ_YUV4MPEG_H
#include <mjpegtools/mpegconsts.h>

typedef struct {
	pthread_mutex_t mutex;
	pthread_t thread;
	pthread_cond_t task;
	int state;
    y4m_stream_info_t streaminfo;
    y4m_frame_info_t frameinfo;
    y4m_ratio_t sar;
    y4m_ratio_t dar;
    int plane_w[4];
    int plane_h[4];
    int plane_size[4];
    int chroma;
    int width;
    int height;
    int fd;
    int has_audio;
    int audio_bits;
    int format;
    float video_fps;
    long audio_rate;
    void *scaler;
    uint8_t *buf[4];
    int  is_jpeg;
    VJFrame *dst;
	VJFrame *src;
} vj_yuv;

vj_yuv *vj_yuv4mpeg_alloc(int dst_w, int dst_h,float fps, int out_pix_fmt);


uint8_t *vj_yuv_get_buf( void *v );

void vj_yuv4mpeg_free(vj_yuv *v) ;

int vj_yuv_stream_start_read_fd( vj_yuv *, int fd, int width,int height );

int vj_yuv_stream_start_read(vj_yuv *, char *, int width, int height);

int vj_yuv_stream_write_header(vj_yuv * yuv4mpeg, VJFrame *frame, int outchroma);

int vj_yuv_stream_start_write(vj_yuv *, VJFrame *frame, char *, int);

void vj_yuv_stream_stop_read(vj_yuv * yuv4mpeg);

void vj_yuv_stream_stop_write(vj_yuv * yuv4mpeg);

int vj_yuv_get_frame(vj_yuv *, uint8_t **);

int vj_yuv_put_frame(vj_yuv * vjyuv, uint8_t **src);

int vj_yuv_get_aframe(vj_yuv * vjyuv, uint8_t * audio);

int vj_yuv_write_wave_header(VJFrame *frame, char *outfile);

int vj_yuv_stream_open_pipe(vj_yuv *, char *);

int vj_yuv_stream_header_pipe( vj_yuv *, VJFrame *frame);
#endif
