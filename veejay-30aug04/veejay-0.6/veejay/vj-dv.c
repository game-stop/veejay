/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nelburg@looze.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */
#include <config.h>

#ifdef SUPPORT_READ_DV2
#include <libdv/dv.h>
#include <stdint.h>
#include "vj-dv.h"

#define NTSC_W 720
#define NTSC_H 480
#define PAL_W 720
#define PAL_H 576
#define DV_PAL_SIZE 144000
#define DV_NTSC_SIZE 120000

static dv_decoder_t *vj_dv_decoder;
static dv_encoder_t *vj_dv_encoder;
static uint8_t *vj_dv_video[3];
static uint8_t *vj_dv_encode_buf;

/* init the dv decoder and decode buffer*/
void vj_dv_init(int width, int height)
{
	
    vj_dv_decoder = dv_decoder_new(1, 1, 0);
    vj_dv_decoder->quality = DV_QUALITY_BEST;
    vj_dv_video[0] =
	(uint8_t *) malloc(width * height * 4 * sizeof(uint8_t));
    vj_dv_video[1] = NULL;
    vj_dv_video[2] = NULL;
    memset( vj_dv_video[0], 0, (width*height*4));
}

/* init the dv encoder and encode buffer */
void vj_dv_init_encoder(EditList * el)
{
    vj_dv_encoder = dv_encoder_new(0, 0, 0);
    vj_dv_encoder->isPAL = (el->video_norm == 'p' ? 1 : 0);
    vj_dv_encoder->is16x9 = FALSE;
    vj_dv_encoder->vlc_encode_passes = 3;
    vj_dv_encoder->static_qno = 0;
    vj_dv_encoder->force_dct = DV_DCT_AUTO;
    vj_dv_encode_buf =
	(uint8_t *) malloc(sizeof(uint8_t) * 3 *
			   (vj_dv_encoder->
			    isPAL ? DV_PAL_SIZE : DV_NTSC_SIZE));
    memset( vj_dv_encode_buf, 0 ,  (3 *
			   (vj_dv_encoder->
			    isPAL ? DV_PAL_SIZE : DV_NTSC_SIZE)));
}

/* this routine is the same as frame_YUV422_to_YUV420P , unpack
 * libdv's 4:2:2-packed into 4:2:0 planar 
 * See http://mjpeg.sourceforge.net/ (MJPEG Tools) (lav-common.c)
 */
void yuy2toyv12(uint8_t * _y, uint8_t * _u, uint8_t * _v, uint8_t * input,
		int width, int height)
{

    int i, j, w2;
    uint8_t *y, *u, *v;

    w2 = width / 2;

    //I420
    y = _y;
    v = _v;
    u = _u;

    for (i = 0; i < height; i += 4) {
	/* top field scanline */
	for (j = 0; j < w2; j++) {
	    /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
	    *(y++) = *(input++);
	    *(u++) = *(input++);
	    *(y++) = *(input++);
	    *(v++) = *(input++);
	}
	for (j = 0; j < w2; j++)
	{
	    *(y++) = *(input++);
	    *(u++) = *(input++);
	    *(y++) = *(input++);
	    *(v++) = *(input++);
	
	}

	/* next two scanlines, one frome each field , interleaved */
	for (j = 0; j < w2; j++) {
	    /* skip every second line for U and V */
	    *(y++) = *(input++);
	    input++;
	    *(y++) = *(input++);
	    input++;
	}
	/* bottom field scanline*/
	for (j = 0; j < w2; j++) {
	    /* skip every second line for U and V */
	    *(y++) = *(input++);
	    input++;
	    *(y++) = *(input++);
	    input++;
	}

    }
}

/* convert 4:2:0 to yuv 4:2:2 */
void yuv420p_to_yuv422(uint8_t * yuv420[3], uint8_t * dest, int width,
		       int height)
{
    unsigned int x, y;


    for (y = 0; y < height; ++y) {
	uint8_t *Y = yuv420[0] + y * width;
	uint8_t *Cb = yuv420[1] + (y / 2) * (width / 2);
	uint8_t *Cr = yuv420[2] + (y / 2) * (width / 2);
	for (x = 0; x < width; x += 2) {
	    *(dest + 0) = Y[0];
	    *(dest + 1) = Cb[0];
	    *(dest + 2) = Y[1];
	    *(dest + 3) = Cr[0];
	    dest += 4;
	    Y += 2;
	    ++Cb;
	    ++Cr;
	}
    }
}

/* convert 4:2:0 to yuv 4:2:2 
static void convert_yuv420p_to_yuv422(uint8_t * yuv_in[3],
				      uint8_t * yuv422, int width,
				      int height)
{
    unsigned int x, y;
    unsigned int i = 0;

    for (y = 0; y < height; ++y) {
	uint8_t *Y = yuv_in[0] + y * width;
	uint8_t *Cb = yuv_in[1] + (y / 2) * (width / 2);
	uint8_t *Cr = yuv_in[2] + (y / 2) * (width / 2);
	for (x = 0; x < width; x += 2) {
	    *(yuv422 + i) = Y[0];
	    *(yuv422 + i + 1) = Cb[0];
	    *(yuv422 + i + 2) = Y[1];
	    *(yuv422 + i + 3) = Cr[0];
	    i += 4;
	    Y += 2;
	    ++Cb;
	    ++Cr;
	}
    }
}
*/

/* encode frame to dv format, dv frame will be in output_buf */
int vj_dv_encode_frame(uint8_t * input_buf[3], uint8_t * output_buf)
{

    time_t now = time(NULL);
    uint8_t *pixels[3];

    if (!input_buf)
	return 0;

    pixels[0] = (uint8_t *) vj_dv_encode_buf;

    if (vj_dv_encoder->isPAL) {
	pixels[2] = (uint8_t *) vj_dv_encode_buf + (PAL_W * PAL_H);
	pixels[1] = (uint8_t *) vj_dv_encode_buf + (PAL_W * PAL_H * 5) / 4;
	yuv420p_to_yuv422(input_buf,vj_dv_encode_buf, PAL_W, PAL_H);
    } else {
	pixels[2] = (uint8_t *) vj_dv_encode_buf + (NTSC_W * NTSC_H);
	pixels[1] =
	    (uint8_t *) vj_dv_encode_buf + (NTSC_W * NTSC_H * 5) / 4;
	yuv420p_to_yuv422(input_buf,vj_dv_encode_buf, NTSC_W , NTSC_H);
    }
  
    dv_encode_full_frame(vj_dv_encoder, pixels, e_dv_color_yuv,
			 output_buf);
    dv_encode_metadata(output_buf, vj_dv_encoder->isPAL,
		       vj_dv_encoder->is16x9, &now, 0);
    dv_encode_timecode(output_buf, vj_dv_encoder->isPAL, 0);

    if(vj_dv_encoder->isPAL) return DV_PAL_SIZE;
    return DV_NTSC_SIZE;
}

void vj_dv_free_encoder()
{
    dv_encoder_free(vj_dv_encoder);
    if(vj_dv_encode_buf) free(vj_dv_encode_buf);
}

void vj_dv_free_decoder() {
	if(vj_dv_video[0]) free(vj_dv_video[0]);
	dv_decoder_free(vj_dv_decoder);
}

int vj_dv_decode_frame(uint8_t * input_buf, uint8_t * Y,
		       uint8_t * Cb, uint8_t * Cr, int width, int height)
{

    int pitches[3];

    if (!input_buf)
	return 0;

    if (dv_parse_header(vj_dv_decoder, input_buf) < 0)
	return 0;

    if (!((vj_dv_decoder->num_dif_seqs == 10)
	  || (vj_dv_decoder->num_dif_seqs == 12)))
	return 0;

    /*
       switch(vj_dv_decoder->sampling) {
       case e_dv_sample_411:fprintf(stderr,"sample 411\n"); break;
       case e_dv_sample_422:fprintf(stderr,"sample 422\n");break;
       case e_dv_sample_420:fprintf(stderr,"sample 420\n");break;
       default: fprintf(stderr,"unknown\n");break;
       }
     */

    if (vj_dv_decoder->sampling == e_dv_sample_411 ||
	vj_dv_decoder->sampling == e_dv_sample_422 ||
	vj_dv_decoder->sampling == e_dv_sample_420) {

	pitches[0] = width * 2;
	pitches[1] = 0;
	pitches[2] = 0;


	dv_decode_full_frame(vj_dv_decoder, input_buf,
			     e_dv_color_yuv, vj_dv_video, pitches);

	yuy2toyv12(Y, Cb, Cr, vj_dv_video[0], width, height);

	return 1;
    }

    return 0;
}

#endif
