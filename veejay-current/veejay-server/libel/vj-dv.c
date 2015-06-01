/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <stdio.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#ifdef SUPPORT_READ_DV2
#include <libdv/dv.h>
#include <stdint.h>
#include <libvje/vje.h>
#include <libel/vj-dv.h>
#include <libel/vj-avcodec.h>
#include <libyuv/yuvconv.h> 
#include <string.h>

#define NTSC_W 720
#define NTSC_H 480
#define PAL_W 720
#define PAL_H 576
#define DV_PAL_SIZE 144000
#define DV_NTSC_SIZE 120000
#define DV_AUDIO_MAX_SAMPLES 1944

int	is_dv_resolution(int w, int h)
{
	if( h == NTSC_H && w == NTSC_W )
		return 1;
	if( h == PAL_H && w == PAL_W )
		return 1;
	return 0;
}

/* init the dv decoder and decode buffer*/
vj_dv_decoder *vj_dv_decoder_init(int quality, int width, int height, int pixel_format)
{
	int dv_q = DV_QUALITY_COLOR;
	vj_dv_decoder *d = (vj_dv_decoder*)vj_malloc(sizeof(vj_dv_decoder));
	if(!d) return NULL;
	d->decoder = dv_decoder_new( 1,1,0 );
	if( quality == 0 )
		dv_q = DV_QUALITY_FASTEST;
	if( quality == 1 )
		dv_q = DV_QUALITY_BEST;

	d->decoder->quality = dv_q;
	d->dv_video = (uint8_t*) vj_malloc(sizeof(uint8_t) * width * height * 4);
    	memset( d->dv_video, 0, (width*height*4));
	d->fmt = pixel_format;
	d->audio = 0; // audio off
	return d;
}

static	sws_template dv_templ;

/* init the dv encoder and encode buffer */
vj_dv_encoder *vj_dv_init_encoder(void *ptr, int pixel_format)
{
	VJFrame *frame = (VJFrame*) ptr;
	vj_dv_encoder *e = (vj_dv_encoder*) vj_malloc(sizeof(vj_dv_encoder));
	if(!e) return NULL;
	e->encoder = dv_encoder_new(0,0,0);
	e->encoder->isPAL = (frame->height == 480 ? 0 : 1 );
	e->encoder->is16x9 = (frame->width / frame->height >= 1.777 ? 1: 0);
	e->encoder->vlc_encode_passes = 3;
    	e->encoder->static_qno = 0;
    	e->encoder->force_dct = DV_DCT_AUTO;
    	e->fmt = pixel_format;
	e->buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * frame->width * frame->height * 3 );
    	e->dv_video =
	(uint8_t *) vj_calloc(sizeof(uint8_t) * 
			   (e->encoder->isPAL ?
			    	DV_PAL_SIZE : DV_NTSC_SIZE));

	veejay_memset(&dv_templ,0,sizeof(sws_template));
	dv_templ.flags = yuv_which_scaler();

	e->scaler = NULL;

	return e;
}


/* encode frame to dv format, dv frame will be in output_buf */
int vj_dv_encode_frame(vj_dv_encoder *encoder, uint8_t *input_buf[3])
{

    time_t now = time(NULL);
    uint8_t *pixels[3];
    int w=0; int h = 0;

     pixels[0] = (uint8_t*) encoder->buffer;

    if (encoder->encoder->isPAL) {
		h = PAL_H;
		w = PAL_W;
    } else {
		h = NTSC_H;
		w = NTSC_W;
    }

	

	pixels[1] = NULL;
	pixels[2] = NULL;

    int src_fmt = PIX_FMT_YUVJ422P;
    if( encoder->fmt == FMT_422 )
	    src_fmt = PIX_FMT_YUV422P;

    VJFrame *src = yuv_yuv_template( input_buf[0],input_buf[1],input_buf[2],
		    w,h, src_fmt );
    VJFrame *dst = yuv_yuv_template( encoder->buffer,NULL,NULL,
		    w,h, PIX_FMT_YUYV422);


    if( encoder->scaler == NULL ) {
	    encoder->scaler = yuv_init_swscaler( src,dst,&dv_templ, yuv_sws_get_cpu_flags() );
	   }

    yuv_convert_and_scale_packed( encoder->scaler, src,dst );

    dv_encode_full_frame( encoder->encoder, pixels, e_dv_color_yuv,encoder->dv_video);
    dv_encode_metadata(encoder->dv_video, encoder->encoder->isPAL,encoder->encoder->is16x9, &now, 0);
    dv_encode_timecode(encoder->dv_video, encoder->encoder->isPAL, 0);
  
    free(src);
    free(dst);
    if(encoder->encoder->isPAL) 
	    return DV_PAL_SIZE;
    return DV_NTSC_SIZE;
}


void vj_dv_free_encoder(vj_dv_encoder *e)
{
	if(e)
	{
		if(e->encoder)
			dv_encoder_free( e->encoder);
		if(e->dv_video)
			free(e->dv_video);
		if(e->buffer)
			free(e->buffer);
		if(e->scaler)
			yuv_free_swscaler(e->scaler);
		free(e);
	}
}

void vj_dv_free_decoder(vj_dv_decoder *d) {
	if(d->decoder)
		dv_decoder_free( d->decoder );
	if(d->dv_video)
		free(d->dv_video);
	if(d) 
		free(d);
}

void	vj_dv_decoder_set_audio(vj_dv_decoder *d, int audio)
{
}

void	   vj_dv_decoder_get_audio(vj_dv_decoder *d, uint8_t *audio_buf)
{

	if(!d->audio) return;

	int n_samples = dv_get_num_samples( d->decoder);
//	int channels  = dv_get_num_channels( d->decoder );
	int i;
	int16_t *ch0  = d->audio_buffers[0];
	int16_t *ch1  = d->audio_buffers[1];
	// convert short to uint8_t, 
	// interleave audio into single buffer
	for(i = 0; i < n_samples; i ++ )
	{
		*(audio_buf)   = ch0[i] & 0xff;			//lo
		*(audio_buf+1) = (ch0[i] >> 8) & 0xff; 		//hi
		*(audio_buf+2) = ch1[i] & 0xff;			//lo
		*(audio_buf+3) = (ch1[i] >> 8) & 0xff;		//hi
	}


}
/*
 *
  lav_common - some general utility functionality used by multiple
	lavtool utilities. 
 */
static void frame_YUV422_to_planar_411(uint8_t **output, uint8_t *input,
				       int width, int height)
{
    int i, j, w4;
    uint8_t *y, *cb, *cr;

    w4 = width/4;
    y = output[0];
    cb = output[1];
    cr = output[2];

    for (i=0; i<height;) {
	/* process two scanlines (one from each field, interleaved) */
        /* ...top-field scanline */
        for (j=0; j<w4; j++) {
            /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
            *(y++) =  *(input++);
            *(cb++) = *(input++);       // NTSC-specific: assert( j%2==0 || cb[-1]==cb[-2]);
	    *(y++) =  *(input++);
            *(cr++) = *(input++);       // NTSC-specific: assert( j%2==0 || cr[-1]==cr[-2]);

            *(y++) =  *(input++);
            (input++);                  // NTSC-specific: assert( j%2==0 || cb[-1]==cb[-2]);
            *(y++) =  *(input++);
            (input++);                  // NTSC-specific: assert( j%2==0 || cr[-1]==cr[-2]);
        }
	i++;
        /* ...bottom-field scanline */
        for (j=0; j<w4; j++) {
            /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
            *(y++) =  *(input++);
            *(cb++) = *(input++);       // NTSC-specific: assert( j%2==0 || cb[-1]==cb[-2]);
	    *(y++) =  *(input++);
            *(cr++) = *(input++);       // NTSC-specific: assert( j%2==0 || cr[-1]==cr[-2]);
  
  	    *(y++) =  *(input++);
            (input++);                  // NTSC-specific: assert( j%2==0 || cb[-1]==cb[-2]);
            *(y++) =  *(input++);
            (input++);                  // NTSC-specific: assert( j%2==0 || cr[-1]==cr[-2]);
        }
	i++;
    }
}

static inline void frame_YUV422_to_planar(uint8_t **output, uint8_t *input,
			    int width, int height, int chroma422)
{
    int i, j, w2;
    uint8_t *y, *cb, *cr;

    w2 = width/2;
    y = output[0];
    cb = output[1];
    cr = output[2];

    for (i=0; i<height;) {
	/* process two scanlines (one from each field, interleaved) */
        /* ...top-field scanline */
        for (j=0; j<w2; j++) {

            /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
            *(y++) =  *(input++);
            *(cb++) = *(input++);
            *(y++) =  *(input++);
            *(cr++) = *(input++);
        }
	i++;
        /* ...bottom-field scanline */
        for (j=0; j<w2; j++) {
            /* packed YUV 422 is: Y[i] U[i] Y[i+1] V[i] */
            *(y++) =  *(input++);
            *(cb++) = *(input++);
            *(y++) =  *(input++);
            *(cr++) = *(input++);
        }
	i++;
	if (chroma422 == 0)
	  continue;
	/* process next two scanlines (one from each field, interleaved) */
        /* ...top-field scanline */
	for (j=0; j<w2; j++) {
	  /* skip every second line for U and V */
	  *(y++) = *(input++);
	  input++;
	  *(y++) = *(input++);
	  input++;
	}
	i++;
        /* ...bottom-field scanline */
	for (j=0; j<w2; j++) {
	  /* skip every second line for U and V */
	  *(y++) = *(input++);
	  input++;
	  *(y++) = *(input++);
	  input++;
	}
	i++;
    }
}

int	vj_dv_scan_frame( vj_dv_decoder *d, uint8_t * input_buf )
{
	if (dv_parse_header(d->decoder, input_buf) < 0)
	{
		veejay_msg(0, "Unable to read DV header");
		return -1;
	}
	if( d->decoder->system == e_dv_system_none )
	{
		veejay_msg(0, "No valid PAL or NTSC video frame detected");
		return -1;
	}

	char sampling[8];
	switch( d->decoder->sampling )
	{
		case e_dv_sample_411:
				sprintf(sampling , "4:1:1"); break;
		case e_dv_sample_420:
				sprintf(sampling, "4:2:0"); break;
		case e_dv_sample_422:
				sprintf(sampling, "4:2:2"); break;
		case e_dv_sample_none:
				veejay_msg(0 ,"No sampling format, cant handle this file (yet)");
				return -1;
		default:
			veejay_msg(0, "Unknown sampling format in DV file");
			return -1;
	}

	veejay_msg( VEEJAY_MSG_DEBUG, "\tDetected DV sampling format %s", sampling );

	if ( d->decoder->sampling == e_dv_sample_422 || d->decoder->sampling == e_dv_sample_411)
		return PIX_FMT_YUV422P;
	if( d->decoder->sampling == e_dv_sample_420 )
		return PIX_FMT_YUV420P;
	return -1;
}

int vj_dv_decode_frame(vj_dv_decoder *d, uint8_t * input_buf, uint8_t * Y,
		       uint8_t * Cb, uint8_t * Cr, int width, int height, int fmt)
{

	int pitches[3];
   	if (!input_buf)
		return 0;

	if (dv_parse_header(d->decoder, input_buf) < 0)
	{
		veejay_msg(0, "Unable to read DV header");
		return 0;
	}

	if( d->decoder->system == e_dv_system_none )
	{
		veejay_msg(0, "No valid PAL or NTSC video frame detected");
		return 0;
	}

	if( d->decoder->system == e_dv_system_625_50 )
	{
		d->yuy2 = 0;
	}
	else
	{
		d->yuy2 = 1;
	}


      	if (!((d->decoder->num_dif_seqs == 10)
	  || (d->decoder->num_dif_seqs == 12)))
	{
		veejay_msg(0, "Dont know how to handle %d dif seqs",
				d->decoder->num_dif_seqs );
		return 0;
	}
       
    	if ( d->decoder->sampling == e_dv_sample_422 || d->decoder->sampling ==
				e_dv_sample_411 )
	{
		pitches[0] = width * 2;
		pitches[1] = 0;
		pitches[2] = 0;
		int offset = ( d->decoder->sampling == e_dv_sample_411 ? width /4 : width /2) * height;
		uint8_t *pixels[3] = { d->dv_video, d->dv_video + (width * height),
	       		d->dv_video + (width * height ) + offset	};

		dv_decode_full_frame(d->decoder, input_buf,
				     e_dv_color_yuv, pixels, pitches);

		//@ this works
		VJFrame *src = yuv_yuv_template( d->dv_video, NULL,NULL,width,height,PIX_FMT_YUYV422 );
		VJFrame *dst = yuv_yuv_template( Y,Cb,Cr,width,height, d->fmt );

		yuv_convert_any_ac( src,dst, src->format,dst->format );
		free(src);
		free(dst);
	/*	if(d->decoder->sampling == e_dv_sample_422 ) {
			frame_YUV422_to_planar( frame, d->dv_video, width, height, 1);
		else
			frame_YUV422_to_planar_411( frame, d->dv_video, width, height  );*/

		return 1;
    	} else if( d->decoder->sampling == e_dv_sample_420 )
	{
	//remove this	
		uint8_t *pixels[3];
                pixels[0] = d->dv_video;
                pixels[1] = d->dv_video + (width * height);
                pixels[2] = d->dv_video + (width * height * 5)/4;
 		pitches[0] = width * 2;
		pitches[1] = 0;
		pitches[2] = 0;

		dv_decode_full_frame( d->decoder, input_buf, e_dv_color_yuv, pixels,pitches);

                yuy2toyv16( Y,Cb,Cr, d->dv_video, width ,height );

		if( yuv_use_auto_ccir_jpeg() && fmt == PIX_FMT_YUVJ422P) {
			yuv_scale_pixels_from_ycbcr(
					Y, 16.0f, 235.0f, width * height);
			yuv_scale_pixels_from_ycbcr(
					Cb,16.0f, 240.0f, width * height/2);
			yuv_scale_pixels_from_ycbcr(
					Cr,16.0f, 240.0f, width * height/2);
		}

		return 1;
	}

   	return 0;
}

#endif
