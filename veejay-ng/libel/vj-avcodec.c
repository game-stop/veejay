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


/** \defgroup avcodec FFmpeg AVCodec
 */

#include <config.h>
#include <stdint.h>
#include <ffmpeg/avcodec.h>
#include <ffmpeg/avutil.h>
#include <libel/vj-avcodec.h>
#include <libel/vj-el.h>
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include <string.h>
#include <libyuv/yuvconv.h>
#include <veejay/defs.h>
#ifdef SUPPORT_READ_DV2
#define __FALLBACK_LIBDV
#include <libel/vj-dv.h>
static vj_dv_encoder *dv_encoder = NULL;
#endif
//@@ FIXME


typedef struct
{
	AVCodec *codec;
	AVCodec *audiocodec;
	AVFrame *frame;
	AVCodecContext	*context;
	int out_fmt;
	int uv_len;
	int uv_width;
	int len;
	void *sampler;
	int sampling_mode;
	int encoder_id;
	int width;
	int height;
} vj_encoder;

#define NUM_ENCODERS 8

static int out_pixel_format = FMT_420; 

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define YUV420_ONLY_CODEC(id) ( ( id == CODEC_ID_MJPEG || id == CODEC_ID_MJPEGB || id == CODEC_ID_MSMPEG4V3 || id == CODEC_ID_MPEG4 ) ? 1: 0)


static	char*	el_get_codec_name(int codec_id )
{
	char name[20];
	switch(codec_id)
	{
		case CODEC_ID_MJPEG: sprintf(name, "MJPEG"); break;
		case CODEC_ID_MPEG4: sprintf(name, "MPEG4"); break;
		case CODEC_ID_MSMPEG4V3: sprintf(name, "DIVX"); break;
		case CODEC_ID_DVVIDEO: sprintf(name, "DVVideo"); break;
		case -1 : sprintf(name, "RAW YUV"); break;
		default:
			sprintf(name, "Unknown"); break;
	}
	char *res = strdup(name);
	return res;
}

static vj_encoder *_encoders[NUM_ENCODERS];

static vj_encoder	*vj_avcodec_new_encoder( int id, int w, int h, int pixel_format, float fps)
{
	vj_encoder *e = (vj_encoder*) vj_malloc(sizeof(vj_encoder));
	if(!e) return NULL;
	memset(e, 0, sizeof(vj_encoder));

	if(id != 998 && id != 999 )
	{
#ifdef __FALLBACK_LIBDV
		if(id != CODEC_ID_DVVIDEO)
		{
#endif
			e->codec = avcodec_find_encoder( id );
			if(!e->codec)
			{
			 char *descr = el_get_codec_name(id);
			 veejay_msg(VEEJAY_MSG_ERROR, "Cannot find Encoder codec %s", 	descr );
			 free(descr);
			}
#ifdef __FALLBACK_LIBDV
		}
#endif

	}

	if( id != 998 && id != 999 )
	{
#ifdef __FALLBACK_LIBDV
	if(id != CODEC_ID_DVVIDEO )
	{
#endif
		e->context = avcodec_alloc_context();
		e->context->bit_rate = 2750 * 1024;
		e->context->max_b_frames =0;
		//e->context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
		e->context->width = w;
 		e->context->height = h;
		e->context->time_base.den = 1;
	        e->context->time_base.num = fps; //	= (AVRational) { 1, fps };
		e->context->qcompress = 0.0;
		e->context->qblur = 0.0;
		e->context->flags = CODEC_FLAG_QSCALE;
		e->context->gop_size = 0;
		e->context->sub_id = 0;
		e->context->me_method = 0; // motion estimation algorithm
		e->context->workaround_bugs = FF_BUG_AUTODETECT;
		e->context->prediction_method = 0;
		e->context->dct_algo = FF_DCT_AUTO; //global_quality?

		//@ ffmpeg MJPEG accepts only 4:2:0
		switch( pixel_format )
		{
			case FMT_420:
				e->uv_len = (w * h ) / 4;
				e->uv_width = w / 2;
				e->context->pix_fmt = PIX_FMT_YUVJ420P;
				break;
			case FMT_422:
				e->uv_len = (w * h ) / 2;
				e->uv_width = w / 2;
				e->context->pix_fmt = PIX_FMT_YUVJ422P;
				break;
			case FMT_444:
				e->uv_len = (w * h );
				e->uv_width = w;
				e->context->pix_fmt = PIX_FMT_YUVJ444P;
				break;
		}

		if( id == CODEC_ID_MJPEG || id == CODEC_ID_MPEG4 || id == CODEC_ID_MSMPEG4V3)
		{
			if(pixel_format != FMT_420)
			{
				e->sampler = subsample_init( w );
				e->sampling_mode = (pixel_format == FMT_422 ? SSM_420_422 :
						SSM_422_444 );
				e->uv_len = (w * h ) / 4;
				e->uv_width = (w / 2);
				if( id == CODEC_ID_MJPEG )
				e->context->pix_fmt = PIX_FMT_YUVJ420P;
				else
					e->context->pix_fmt = PIX_FMT_YUV420P;
			}
		}

		if ( avcodec_open( e->context, e->codec ) < 0 )
		{
			if(e) free(e);
			return NULL;
		}

#ifdef __FALLBACK_LIBDV
		}
#endif
	}

	e->len = ( w * h );
	e->width = w;
	e->height = h;

	e->out_fmt = pixel_format;
	e->encoder_id = id;

	return e;
}

static	void		vj_avcodec_close_encoder( vj_encoder *av )
{
	if(av)
	{
		if(av->context)
		{
			avcodec_close( av->context );
			free(av->context);	
		}
		free(av);
	}
	av = NULL;
}


int		vj_avcodec_init(int w, int h , double dfps, int fmt, int norm)
{
	float fps = (float) dfps;
	_encoders[ENCODER_MJPEG] = vj_avcodec_new_encoder( CODEC_ID_MJPEG, w,h, fmt,fps );
	if(!_encoders[ENCODER_MJPEG]) 
	{
		veejay_msg(0 ,"Unable to initialize MJPEG codec");
		return 0;
	}
#ifdef __FALLBACK_LIBDV
	dv_encoder = vj_dv_init_encoder( w,h,norm, out_pixel_format);
	if(!dv_encoder)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to initialize quasar DV codec");
		return 0;
	}
#else
	_encoders[ENCODER_DVVIDEO] = vj_avcodec_new_encoder( CODEC_ID_DVVIDEO, w,h, fmt,fps );
	if(!_encoders[ENCODER_DVVIDEO])
	{
		veejay_msg(0, "Unable to initialize DV codec");
		return 0;
	}
#endif
	_encoders[ENCODER_DIVX] = vj_avcodec_new_encoder( CODEC_ID_MSMPEG4V3 , w,h, fmt,fps);
	if(!_encoders[ENCODER_DIVX]) 
	{
		veejay_msg(0, "Unable to initialize DIVX (msmpeg4v3) codec");
		return 0;
	}
	_encoders[ENCODER_MPEG4] = vj_avcodec_new_encoder( CODEC_ID_MPEG4, w,h, fmt,fps);
	if(!_encoders[ENCODER_MPEG4])
	{
		veejay_msg(0, "Unable to initialize MPEG4 codec");
		return 0;
	}
	_encoders[ENCODER_YUV420] = vj_avcodec_new_encoder( 999, w,h,fmt,fps);
	if(!_encoders[ENCODER_YUV420]) 
	{
		veejay_msg(0, "Unable to initialize YUV 4:2:0 planer (RAW)");
		return 0;
	}
	_encoders[ENCODER_YUV422] = vj_avcodec_new_encoder( 998, w,h,fmt,fps);
	if(!_encoders[ENCODER_YUV422]) 
	{
		veejay_msg(0, "Unable to initialize YUV 4:2:2 planar (RAW)");
		return 0;
	}
	return 1;
}

int		vj_avcodec_free()
{
	int i;
	for( i = 0; i < NUM_ENCODERS; i++)
	{
		if(_encoders[i]) vj_avcodec_close_encoder(_encoders[i]);
	}
#ifdef __FALLBACK_LIBDV
	vj_dv_free_encoder(dv_encoder);
#endif
	return 1;
}
void	yuv422p_to_yuv420p3( uint8_t *src, uint8_t *dst[3], int w, int h)
{
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));

	pict1.data[0] = src;
	pict1.data[1] = src+(w*h);
	pict1.data[2] = src+(w*h)+((w*h)/2);
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst[0];
	pict2.data[1] = dst[1];
	pict2.data[2] = dst[2];
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	

	img_convert( &pict2, PIX_FMT_YUV420P, &pict1, PIX_FMT_YUV422P, w, h );
	return;
}
void	yuv422p_to_yuv420p2( uint8_t *src[3], uint8_t *dst[3], int w, int h)
{
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));

	pict1.data[0] = src[0];
	pict1.data[1] = src[1];
	pict1.data[2] = src[2];
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst[0];
	pict2.data[1] = dst[1];
	pict2.data[2] = dst[2];
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	

	img_convert( &pict2, PIX_FMT_YUV420P, &pict1, PIX_FMT_YUV422P, w, h );
	return;
}

int	yuv422p_to_yuv420p( uint8_t *src[3], uint8_t *dst, int w, int h)
{

	int len = w* h ;
	int uv_len = len / 4;
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));

	pict1.data[0] = src[0];
	pict1.data[1] = src[1];
	pict1.data[2] = src[2];
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst;
	pict2.data[1] = dst + len;
	pict2.data[2] = dst + len + uv_len;
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	

	img_convert( &pict2, PIX_FMT_YUV420P, &pict1, PIX_FMT_YUV422P, w, h );
	return (len + uv_len + uv_len);

}
int	yuv420p_to_yuv422p2( uint8_t *sY,uint8_t *sCb, uint8_t *sCr, uint8_t *dst[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 2; 
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));
	pict1.data[0] = sY;
	pict1.data[1] = sCb;
	pict1.data[2] = sCr;
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst[0];
	pict2.data[1] = dst[1];
	pict2.data[2] = dst[2];
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	

	img_convert( &pict2, PIX_FMT_YUV422P, &pict1, PIX_FMT_YUV420P, w, h );
	return (len + uv_len + uv_len);

}
int	yuv420p_to_yuv422p( uint8_t *sY,uint8_t *sCb, uint8_t *sCr, uint8_t *dst[3], int w, int h )
{
	const int len = w * h;
	const int uv_len = len / 2; 
	AVPicture pict1,pict2;
	memset(&pict1,0,sizeof(pict1));
	memset(&pict2,0,sizeof(pict2));
	pict1.data[0] = sY;
	pict1.data[1] = sCb;
	pict1.data[2] = sCr;
	pict1.linesize[0] = w;
	pict1.linesize[1] = w >> 1;
	pict1.linesize[2] = w >> 1;
	pict2.data[0] = dst[0];
	pict2.data[1] = dst[1];
	pict2.data[2] = dst[2];
	pict2.linesize[0] = w;
	pict2.linesize[1] = w >> 1;
	pict2.linesize[2] = w >> 1;	
	img_convert( &pict2, PIX_FMT_YUV422P, &pict1, PIX_FMT_YUV420P, w, h );
	return (len + uv_len + uv_len);
}

static	int	vj_avcodec_copy_frame( vj_encoder  *av, uint8_t *src[3], uint8_t *dst )
{
	if(!av)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No encoder !!");
		return 0;
	}
	if( (av->encoder_id == 999 && av->out_fmt == PIX_FMT_YUV420P) || (av->encoder_id == 998 && av->out_fmt == PIX_FMT_YUV422P))
	{
		/* copy */
		veejay_memcpy( dst, src[0], av->len );
		veejay_memcpy( dst+(av->len), src[1], av->uv_len );
		veejay_memcpy( dst+(av->len+av->uv_len) , src[2], av->uv_len);
		return ( av->len + av->uv_len + av->uv_len );
	}
	/* copy by converting */
	if( av->encoder_id == 999 && av->out_fmt == PIX_FMT_YUV422P) 
	{
		yuv422p_to_yuv420p( src, dst, av->width, av->height);
		return ( av->len + (av->len/4) + (av->len/4));
	}

	if( av->encoder_id == 998 && av->out_fmt == PIX_FMT_YUV420P)
	{
		uint8_t *d[3];
		d[0] = dst;
		d[1] = dst + av->len;
		d[2] = dst + av->len + (av->len / 2);
		yuv420p_to_yuv422p2( src[0],src[1],src[2], d, av->width,av->height );
		return ( av->len + av->len );
	}

	return 0;
}

int		vj_avcodec_encode_frame( int format, uint8_t **src, uint8_t *buf, int buf_len)
{
	AVFrame pict;
	vj_encoder *av = _encoders[format];
#ifdef STRICT_CHECKING
	assert( av != NULL );
#endif
	int res=0;
	memset( &pict, 0, sizeof(pict));

	if(format == ENCODER_YUV420) // no compression, just copy
		return vj_avcodec_copy_frame( _encoders[ENCODER_YUV420],src, buf );
	if(format == ENCODER_YUV422) // no compression, just copy
		return vj_avcodec_copy_frame( _encoders[ENCODER_YUV422],src, buf );

#ifdef __FALLBACK_LIBDV
	if(format == ENCODER_DVVIDEO )
		return vj_dv_encode_frame( dv_encoder,src, buf );
#endif

	pict.quality = 1;
	pict.data[0] = src[0];
	pict.data[1] = src[1];
	pict.data[2] = src[2];

	pict.linesize[0] = av->context->width;
	pict.linesize[1] = av->uv_width;
	pict.linesize[2] = av->uv_width;
	
	if(av->sampler)
	{
		chroma_subsample( av->sampling_mode,
				  av->sampler,
				  src,
				  av->width,
				  av->height );
	}

	res = avcodec_encode_video( av->context, buf, buf_len, &pict );

	return res;
}

