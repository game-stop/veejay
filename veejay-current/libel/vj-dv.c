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
#include <libel/vj-dv.h>
#include <libel/vj-avcodec.h>
#include <string.h>

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
static int out_format; 
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
void vj_dv_init_encoder(editlist * el, int pixel_format)
{
    vj_dv_encoder = dv_encoder_new(0, 0, 0);
    vj_dv_encoder->isPAL = (el->video_norm == 'p' ? 1 : 0);
    vj_dv_encoder->is16x9 = FALSE;
    vj_dv_encoder->vlc_encode_passes = 3;
    vj_dv_encoder->static_qno = 0;
    vj_dv_encoder->force_dct = DV_DCT_AUTO;
    out_format = pixel_format;

    vj_dv_encode_buf =
	(uint8_t *) malloc(sizeof(uint8_t) * 3 *
			   (vj_dv_encoder->
			    isPAL ? DV_PAL_SIZE : DV_NTSC_SIZE));
    memset( vj_dv_encode_buf, 0 ,  (3 *
			   (vj_dv_encoder->
			    isPAL ? DV_PAL_SIZE : DV_NTSC_SIZE)));
}

/* encode frame to dv format, dv frame will be in output_buf */
int vj_dv_encode_frame(uint8_t * input_buf[3], uint8_t * output_buf)
{

    time_t now = time(NULL);
    uint8_t *pixels[3];
    int w=0; int h = 0;
    if (!input_buf)
	return 0;

    pixels[0] = (uint8_t *) vj_dv_encode_buf;
    if (vj_dv_encoder->isPAL)
    {
		h = PAL_H;
		w = PAL_W;
    }
    else
    {
		h = NTSC_H;
		w = NTSC_W;
    }

	pixels[2] = (uint8_t *) vj_dv_encode_buf + (w * h);
	pixels[1] = (uint8_t *) vj_dv_encode_buf + (w * h * 5) / 4;
    

    if( out_format == FMT_420)
    {  // convert to 422 packed
		yuv420p_to_yuv422(input_buf,vj_dv_encode_buf, w, h);
    }
    else
    {  // convert 422 planar to packed
		yuv422p_to_yuv422(input_buf,vj_dv_encode_buf,w,h);
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
