/*
 * Copyright (C) 2002 Niels Elburg <elburg@hio.hen.nl>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef VJ_FFMPEG_H
#define VJ_FFMPEG_H
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "avcodec.h"
#include "editlist.h"

typedef struct {
    AVCodec *codec;
    AVCodecContext *c;
    AVCodecContext *ec;
    AVFrame *picture;
    uint8_t *outbuf;
    int outbuf_size;
    int mode;
    //int pix_fmt;
    //int frame_size;
    int encode_fmt;
    uint8_t *tmp_buf[3];
} vj_ffmpeg;


enum {
	FFMPEG_DECODE_MJPEG = 0,
	FFMPEG_ENCODE_MJPEG = 1,
	FFMPEG_DECODE_DIVX = 2,
	FFMPEG_ENCODE_DIVX = 3,
	FFMPEG_DECODE_DV    = 4,
	FFMPEG_ENCODE_DV    = 5,
	FFMPEG_ENCODE_MPEG4 = 7,
	FFMPEG_DECODE_MPEG4 = 6,
};


vj_ffmpeg *vj_ffmpeg_alloc();
/* init decoder (mode=0), encoder (mode=1) */
int vj_ffmpeg_init(vj_ffmpeg * ffmpeg, EditList * el, int mode, int palette);

/* encode and write file to opened lav file */
int vj_ffmpeg_write_frame(vj_ffmpeg * ffmpeg, uint8_t * yuv_420_frame[3],
			  int n);
/* close ffmpeg */
void vj_ffmpeg_close(vj_ffmpeg * ffmpeg);
/* open codec */
int vj_ffmpeg_open_codec(vj_ffmpeg * ffmpeg);
/* decode a frame into YUV420P */
int vj_ffmpeg_decode_frame(vj_ffmpeg * ffmpeg, uint8_t * buff, int buf_len,
			   uint8_t * Y, uint8_t * Cb, uint8_t * Cr);
/* close decoder */
int vj_ffmpeg_close_decoder(vj_ffmpeg * ffmpeg);
/* encode frame to codec format */
int vj_ffmpeg_encode_frame(vj_ffmpeg * ffmpeg, uint8_t * yuv_420_frame[3],
			   uint8_t * buf, int buf_len);

int vj_ffmpeg_deinterlace(vj_ffmpeg *ffmpeg, uint8_t *Y,uint8_t *Cb, uint8_t *Cr, int w, int h ) ;



#endif
