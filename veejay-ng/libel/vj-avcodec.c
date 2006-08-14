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
	int64_t time_unit;
} vj_encoder;

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define YUV420_ONLY_CODEC(id) ( ( id == CODEC_ID_MJPEG || id == CODEC_ID_MJPEGB || id == CODEC_ID_MSMPEG4V3 || id == CODEC_ID_MPEG4 ) ? 1: 0)

#define CODEC_ID_YUV420 998
#define CODEC_ID_YUV422 999
#define CODEC_ID_YUV444 1000

static struct
{
	int encoder_id;
	int avcodec_id;
	char *name;
} encoder_list_[] = {
	{	ENCODER_MJPEG,	CODEC_ID_MJPEG, "Motion JPEG" },
	{	ENCODER_MJPEGB, CODEC_ID_MJPEGB, "MJPEGB" },
	{	ENCODER_DVVIDEO, CODEC_ID_DVVIDEO, "Digital Video" },
	{	ENCODER_DIVX, CODEC_ID_MSMPEG4V3 , "Divx 3;-)"},
	{	ENCODER_YUV420, 998, "YUV 4:2:0 planar" },
	{	ENCODER_YUV422, 999, "YUV 4:2:2 planar" },
	{       ENCODER_YUV444, 1000, "YUV 4:4:4 planar" },
	{	ENCODER_LOSSLESS, CODEC_ID_LJPEG, "Lossless JPEG" },
	{	ENCODER_HUFFYUV, CODEC_ID_HUFFYUV, "Lossless HuffYUV" },
	{	ENCODER_MPEG4, CODEC_ID_MPEG4, "MPEG4" },
	{	-1,-1, NULL }
};

static	int	get_codec_id( int id )
{
	int i;
	for( i =0; encoder_list_[i].encoder_id != -1 ; i ++ )
	{
		if( encoder_list_[i].encoder_id == id )
			return encoder_list_[i].avcodec_id;
	}
	return -1;
}

char*	get_codec_name(int id )
{
	int i;
	for( i =0; encoder_list_[i].encoder_id != -1 ; i ++ )
	{
		if( encoder_list_[i].encoder_id == id )
			return encoder_list_[i].name;
	}
	return NULL;
}


void	*vj_avcodec_new_encoder( int id, int w, int h, int pixel_format, double dfps)
{
	int avcodec_id = get_codec_id( id );
	char *descr    = get_codec_name( id );
	float fps = (float) dfps;
	int sampling = 0;
	if( avcodec_id == -1 )
	{
		veejay_msg(0, "Invalid codec '%d'", id );
		return NULL;
	}
	
	vj_encoder *e = (vj_encoder*) vj_malloc(sizeof(vj_encoder));
	if(!e) return NULL;
	
	memset(e, 0, sizeof(vj_encoder));
	//@quality bad!!
	if( id != ENCODER_YUV420 && id != ENCODER_YUV422 && id != ENCODER_YUV444)
	{
		e->codec = avcodec_find_encoder( avcodec_id );

		if(!e->codec )
		{
			free(e);
			veejay_msg(0, "Cannot open codec '%s'",
					descr );
			return NULL;
		}
		e->context = avcodec_alloc_context();

//		e->context->bit_rate = 5750 * 1024;
		e->context->max_b_frames =0;
		e->context->width = w;
 		e->context->height = h;
		e->context->time_base.den = 1;
		e->context->time_base.num = fps; //	= (AVRational) { 1, fps };
		e->context->qcompress = 0.0;
		e->context->qblur = 0.0;
		e->context->flags = CODEC_FLAG_QSCALE;
		e->context->gop_size = 0;
		e->context->b_frame_strategy = 0;
		e->context->sub_id = 0;
		e->context->me_method = 0; // motion estimation algorithm
		e->context->workaround_bugs = FF_BUG_AUTODETECT;
		e->context->prediction_method = 0;
		e->context->dct_algo = FF_DCT_AUTO; //global_quality?
		e->context->global_quality = 1;
		e->context->strict_std_compliance = FF_COMPLIANCE_INOFFICIAL;
		e->time_unit = 1000000 / e->context->time_base.num;
		
		switch( avcodec_id )
		{
			case CODEC_ID_MJPEG:
			case CODEC_ID_MJPEGB:
			case CODEC_ID_LJPEG:
				e->context->pix_fmt = PIX_FMT_YUVJ420P;
				if( pixel_format != FMT_420 )
					sampling = 1;
			
				break;
			case CODEC_ID_MPEG4:
			case CODEC_ID_MSMPEG4V3:
				e->context->pix_fmt = PIX_FMT_YUV420P;
				if( pixel_format != FMT_420 )
					sampling = 1;
			break;
			case CODEC_ID_HUFFYUV:
				if(pixel_format == FMT_422 ) {
					e->context->pix_fmt = PIX_FMT_YUV422P;
				} else if ( pixel_format == FMT_420 ) {
					e->context->pix_fmt = PIX_FMT_YUV420P;
				} else if ( pixel_format == FMT_444 ) {
					e->context->pix_fmt = PIX_FMT_YUV422P;
					sampling = 1;
				}
				break;
		}
		
		if ( avcodec_open( e->context, e->codec ) < 0 )
		{
			free(e->context);	
			free( e );
			return NULL;
		}
	}
	
	switch( avcodec_id )
	{
		case CODEC_ID_YUV420:
		case CODEC_ID_YUV422:
			if( pixel_format == FMT_444 )
				sampling = 1;
			break;
		case CODEC_ID_YUV444:
			if( pixel_format != FMT_444 )
			{
				veejay_msg(0, "Please run veejay -P2 for YUV 4:4:4 planar support");
				free(e);
				return NULL;	
			}
			break;
	}

	if(sampling)
	{
		e->sampler = subsample_init_copy( w,h );
		switch(pixel_format)
		{
			case FMT_444:
				e->sampling_mode = SSM_422_444;
				e->uv_width = w;
				break;
			case FMT_422:
				e->sampling_mode = SSM_420_422;
				e->uv_width = w;
				break;
			default:
				e->uv_width =w /2;
				break;
		}
	}

	if( avcodec_id == CODEC_ID_YUV420 )
	{
		switch( pixel_format )
		{
			case FMT_422:
				e->sampling_mode = SSM_420_422;
				break;
			case FMT_444:
				e->sampling_mode = SSM_420_JPEG_BOX;
				break;
		}
	} else if (avcodec_id == CODEC_ID_YUV422 )
	{
		switch( pixel_format )
		{
			case FMT_420:
				e->sampling_mode = SSM_420_422;
				break;
			case FMT_444:
				e->sampling_mode = SSM_420_422;
				break;
		}

	}
	
	e->len = ( w * h );
	switch( pixel_format )
	{
		case FMT_444:
			e->uv_len = e->len; break;
		case FMT_422:
			e->uv_len = e->len/2; break;
		case FMT_420:
			e->uv_len = e->len/4;break;
	}
	e->width = w;
	e->height = h;

	e->out_fmt = pixel_format;
	e->encoder_id = avcodec_id;

	return (void*) e;
}

void		vj_avcodec_close_encoder( vj_encoder *av )
{
	if(av)
	{
		if(av->context)
		{
			avcodec_close( av->context );
			free(av->context);	
			if(av->sampler)
				subsample_free(av->sampler);
		}
		free(av);
	}
	av = NULL;
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
	uint8_t *yuv[3];
	if(!av)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No encoder !!");
		return 0;
	}


	if (av->encoder_id == CODEC_ID_YUV420 )
	{
		switch( av->out_fmt )
		{
			case FMT_420:
				veejay_memcpy( dst, src[0], av->len );
				veejay_memcpy( dst + av->len, src[1], av->uv_len );
				veejay_memcpy( dst + av->len + av->uv_len, src[2], av->uv_len );
				return (av->len + av->uv_len + av->uv_len);
				break;
			case FMT_422:
			case FMT_444:
				chroma_subsample_copy( av->sampling_mode,
					av->sampler,
					src,
					av->width,
					av->height,
					yuv );

				veejay_memcpy( dst, yuv[0], av->len );
				veejay_memcpy( dst+av->len, yuv[1], av->len/4 );
				veejay_memcpy( dst+av->len+(av->len/4),yuv[2], av->len/4);

				return (av->len + (av->len/4) + (av->len/4) );
				break;
		}
	}
	else if ( av->encoder_id == CODEC_ID_YUV422 )
	{
		switch(av->out_fmt)
		{
			case FMT_422:
				veejay_memcpy( dst, src[0], av->len );
				veejay_memcpy( dst + av->len, src[1], av->uv_len );
				veejay_memcpy( dst + av->len + av->uv_len, src[2], av->uv_len );
				return (av->len + av->uv_len + av->uv_len);
			case FMT_444:
			case FMT_420:
				chroma_subsample_copy( av->sampling_mode,
					av->sampler,
					src,
					av->width,
					av->height,
					yuv );

				veejay_memcpy( dst, yuv[0], av->len );
				veejay_memcpy( dst+av->len, yuv[1], av->len/2 );
				veejay_memcpy( dst+av->len+(av->len/2),yuv[2], av->len/2);

				return (av->len + (av->len/2) + (av->len/2) );
		}
	} else if( av->encoder_id == CODEC_ID_YUV444 ){

		switch(av->out_fmt)
		{
			case FMT_444:
				veejay_memcpy( dst, src[0], av->len );
				veejay_memcpy( dst + av->len, src[1], av->uv_len );
				veejay_memcpy( dst + av->len + av->len, src[2], av->uv_len );
				return (av->len + av->uv_len + av->uv_len);
			default:
#ifdef STRICT_CHECKING
				assert(0);
#endif
				break;
		}

	}

	return 0;
}


int		vj_avcodec_encode_frame( void *codec, int format, void *dsrc, uint8_t *buf, int buf_len, uint64_t nframe)
{
	AVFrame p;
	VJFrame *src = (VJFrame*) dsrc;
	uint8_t *yuv[3];
	vj_encoder *av = (vj_encoder*) codec;
#ifdef STRICT_CHECKING
	if( av == NULL )
		veejay_msg(0 ,"Invalid format: %d",format );
	assert( av != NULL );
#endif

	int res=0;
	
	if( av->encoder_id == CODEC_ID_YUV420 || av->encoder_id == CODEC_ID_YUV422 || av->encoder_id == CODEC_ID_YUV444 )
	{
		return	vj_avcodec_copy_frame( av, src->data, buf );
	}

	memset( &p, 0, sizeof(AVFrame));

	if(av->sampler)
	{
		chroma_subsample_copy( av->sampling_mode,
				  av->sampler,
				  src,
				  av->width,
				  av->height,
			          yuv	);
	}

	p.data[0] = yuv[0];
	p.data[1] = yuv[1];
	p.data[2] = yuv[2];
	p.linesize[0] = av->width;
	p.linesize[1] = av->uv_width;
	p.linesize[2] = av->uv_width;
	p.pts = av->time_unit * nframe;
	p.quality = 1;

	res = avcodec_encode_video( av->context, buf, buf_len, &p );
	
	return res;
}

