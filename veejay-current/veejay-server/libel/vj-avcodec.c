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
#include <config.h>
#include <stdio.h>
#include <libel/vj-avcodec.h>
#include <libel/vj-el.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <stdint.h>
#include <string.h>
#include <libyuv/yuvconv.h>
#include <liblzo/lzo.h>
#include <libstream/vj-yuv4mpeg.h>
#ifdef SUPPORT_READ_DV2
#define __FALLBACK_LIBDV
#include <libel/vj-dv.h>
#endif
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libel/av.h>
#include <libel/avhelper.h>

//from gst-ffmpeg, round up a number
#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X (x, 1)
#define ROUND_UP_4(x) ROUND_UP_X (x, 2)
#define ROUND_UP_8(x) ROUND_UP_X (x, 3)
#define DIV_ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) >> (x))
#define RUP8(num)(((num)+8)&~8)

static int out_pixel_format = FMT_422F; 

char*	vj_avcodec_get_codec_name(int codec_id )
{
	char name[64];
	switch(codec_id)
	{
		case CODEC_ID_MJPEG: snprintf(name,sizeof(name),"MJPEG"); break;
		case CODEC_ID_MPEG4: snprintf(name,sizeof(name), "MPEG4"); break;
		case CODEC_ID_MJPEGB: snprintf(name,sizeof(name),"MJPEGB");break;
		case CODEC_ID_MSMPEG4V3: snprintf(name,sizeof(name), "DIVX"); break;
		case CODEC_ID_DVVIDEO: snprintf(name,sizeof(name), "DVVideo"); break;
		case CODEC_ID_LJPEG: snprintf(name,sizeof(name), "LJPEG" );break;
		case CODEC_ID_SP5X: snprintf(name,sizeof(name), "SP5x"); break;
		case CODEC_ID_THEORA: snprintf(name,sizeof(name),"Theora");break;
		case CODEC_ID_H264: snprintf(name,sizeof(name), "H264");break;
		case CODEC_ID_HUFFYUV: snprintf(name,sizeof(name),"HuffYUV");break;
		case 997 : snprintf(name,sizeof(name), "RAW YUV 4:2:2 Planar JPEG"); break;
		case 996 : snprintf(name,sizeof(name), "RAW YUV 4:2:0 Planar JPEG"); break;
		case 995 : snprintf(name,sizeof(name), "YUV4MPEG Stream 4:2:2"); break;
		case 994 : snprintf(name,sizeof(name), "YUV4MPEG Stream 4:2:0"); break;
		case 999 : snprintf(name,sizeof(name), "RAW YUV 4:2:0 Planar"); break;
		case 998 : snprintf(name,sizeof(name), "RAW YUV 4:2:2 Planar"); break;
		case 900 : snprintf(name,sizeof(name), "LZO YUV 4:2:2 Planar"); break;
		default:
			snprintf(name,sizeof(name), "Unknown"); break;
	}
	return vj_strdup(name);
}

void			vj_libav_ffmpeg_version()
{
	veejay_msg( VEEJAY_MSG_INFO, "libav versions:");
	veejay_msg( VEEJAY_MSG_INFO, "\tavcodec-%d.%d.%d (%d)", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO, LIBAVCODEC_BUILD );
}


uint8_t 		*vj_avcodec_get_buf( vj_encoder *av )
{
#ifdef SUPPORT_READ_DV2
	vj_dv_encoder *dv = av->dv;
	switch(av->encoder_id) {
		case CODEC_ID_DVVIDEO:
			return dv->dv_video;
	}
#endif
	return av->data[0];
}

static vj_encoder	*vj_avcodec_new_encoder( int id, VJFrame *frame, char *filename)
{
	vj_encoder *e = (vj_encoder*) vj_calloc(sizeof(vj_encoder));
	if(!e) return NULL;

	int pf = get_ffmpeg_pixfmt( out_pixel_format );

#ifdef SUPPORT_READ_DV2
	if( id == CODEC_ID_DVVIDEO )
	{
		if(!is_dv_resolution(frame->width, frame->height ))
		{	
			veejay_msg(VEEJAY_MSG_ERROR,"\tSource video is not in DV resolution");
			return NULL;
		}
		else
		{
			e->dv = (void*)vj_dv_init_encoder( (void*)frame, out_pixel_format);
		}
	}
	else {
#endif
		e->data[0] = (uint8_t*) vj_calloc(sizeof(uint8_t) * RUP8(frame->len + frame->uv_len + frame->uv_len) );
		e->data[1] = e->data[0] + frame->len;
		e->data[2] = e->data[1] + frame->uv_len;
		e->data[3] = NULL;
#ifdef SUPPORT_READ_DV2
	}
#endif
	
	if( id == 900 )
	{
		e->lzo = lzo_new();
	}

	if( id == 995 || id == 994) {
		e->y4m = vj_yuv4mpeg_alloc(frame->width,frame->height,frame->fps, out_pixel_format );
		if( !e->y4m) {
			veejay_msg(0, "Error while trying to setup Y4M stream, abort.");
			return NULL;
		}

		int chroma_val = Y4M_CHROMA_422;
		if( id == 994 ) {
			chroma_val = Y4M_CHROMA_420MPEG2;
		}

		if( vj_yuv_stream_start_write( e->y4m, frame,filename,chroma_val )== -1 )
		{
			veejay_msg(0, "Unable to write header to  YUV4MPEG stream");
			vj_yuv4mpeg_free( e->y4m );
			return NULL;
		}
	}
	
	if(id != 998 && id != 999 && id != 900 && id != 997 && id != 996 && id != 995 && id != 994)
	{
#ifdef __FALLBACK_LIBDV
		if(id != CODEC_ID_DVVIDEO)
		{
#endif
			e->codec = avcodec_find_encoder( id );
			if(!e->codec)
			{
			 char *descr = vj_avcodec_get_codec_name(id);
			 veejay_msg(VEEJAY_MSG_ERROR, "Unable to find encoder '%s'", 	descr );
			 free(descr);
			}
#ifdef __FALLBACK_LIBDV
		}
#endif

	}

	if( id != 998 && id != 999 && id!= 900 && id != 997 && id != 996 && id != CODEC_ID_DVVIDEO && id != 995 && id != 994 )
	{
#ifdef __FALLBACK_LIBDV
	  if(id != CODEC_ID_DVVIDEO )
		{
#endif
#if LIBAVCODEC_BUILD > 5400
		e->context = avcodec_alloc_context3(e->codec);
#else
		e->context = avcodec_alloc_context();
#endif
		e->context->bit_rate = 2750 * 1024;
		e->context->width = frame->width;
 		e->context->height = frame->height;
#if LIBAVCODEC_BUILD > 5010
		e->context->time_base = (AVRational) { 1, frame->fps };
#else
		e->context->frame_rate = frame->fps;
		e->context->frame_rate_base = 1;
#endif
		e->context->sample_aspect_ratio.den = 1;
		e->context->sample_aspect_ratio.num = 1;
		e->context->qcompress = 0.0;
		e->context->qblur = 0.0;
		e->context->max_b_frames = 0;
		e->context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
		e->context->flags = CODEC_FLAG_QSCALE;
		e->context->gop_size = 0;
		e->context->workaround_bugs = FF_BUG_AUTODETECT;
		e->context->prediction_method = 0;
		e->context->dct_algo = FF_DCT_AUTO; 
		e->context->pix_fmt = get_ffmpeg_pixfmt( out_pixel_format );
		if( id == CODEC_ID_MJPEG ) 
			e->context->pix_fmt = ( out_pixel_format == FMT_422F ? PIX_FMT_YUVJ420P : PIX_FMT_YUV420P );

		pf = e->context->pix_fmt;

	
		char *descr = vj_avcodec_get_codec_name( id );
#if LIBAVCODEC_BUILD > 5400
		if ( avcodec_open2( e->context, e->codec, NULL ) )
#else
		if ( avcodec_open( e->context, e->codec ) < 0 )
#endif
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to open codec '%s'" , descr );
			avhelper_free_context( &(e->context) );
			if(e) free(e);
			if(descr) free(descr);
			return NULL;
		}
		else
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "\tOpened codec %s", descr );
			free(descr);
		}
#ifdef __FALLBACK_LIBDV
	}
#endif
	}

	e->width = frame->width;
	e->height = frame->height;
	e->encoder_id = id;
	e->shift_y = frame->shift_v;
	e->shift_x = frame->shift_h;
	e->len = frame->len;
	e->uv_len = frame->uv_len;
	e->out_fmt = pixfmt_to_vj( pf );
	
	return e;
}
void		vj_avcodec_close_encoder( vj_encoder *av )
{
	if(av)
	{
		if(av->context)
		{
			avcodec_close( av->context );
			avhelper_free_context( &(av->context) );
		}
		if(av->data[0])
			free(av->data[0]);
		if(av->lzo)
			lzo_free(av->lzo);
#ifdef SUPPORT_READ_DV2
		if(av->dv)
			vj_dv_free_encoder( (vj_dv_encoder*) av->dv );
#endif
		if(av->y4m)
			vj_yuv4mpeg_free( (vj_yuv*) av->y4m );
		free(av);
	}
	av = NULL;
}

int		vj_avcodec_find_codec( int encoder )
{
	switch( encoder)
	{
		case ENCODER_MJPEG:
		case ENCODER_QUICKTIME_MJPEG:
			return CODEC_ID_MJPEG;
		case ENCODER_DVVIDEO:
		case ENCODER_QUICKTIME_DV:
			return CODEC_ID_DVVIDEO;
		case ENCODER_MJPEGB:
			return CODEC_ID_MJPEGB;
		case ENCODER_HUFFYUV:
			return CODEC_ID_HUFFYUV;
		case ENCODER_LJPEG:
			return CODEC_ID_LJPEG;	
		case ENCODER_YUV420:
			return 999;
		case ENCODER_YUV422:
			return 998;
		case ENCODER_YUV422F:
			return 997;
		case ENCODER_YUV420F:
			return 996;
		case ENCODER_LZO:
			return 900;
		case ENCODER_YUV4MPEG:
			return 995;
		case ENCODER_YUV4MPEG420:
			return 994;
		default:
			veejay_msg(VEEJAY_MSG_DEBUG, "Unknown format %d selected", encoder );
			return 0;
	}
	return 0;
}

char		vj_avcodec_find_lav( int encoder )
{
	switch( encoder)
	{
		case ENCODER_MJPEG:
		case ENCODER_HUFFYUV:
			return 'a';
		case ENCODER_QUICKTIME_MJPEG:
		       	return 'q';
		case ENCODER_DVVIDEO:
			return 'd';
		case ENCODER_QUICKTIME_DV:
			return 'Q';
		case ENCODER_MJPEGB:
			return 'c';
		case ENCODER_LJPEG:
			return 'l';
		case ENCODER_YUV420:
			return 'Y';
		case ENCODER_YUV422:
			return 'P';
		case ENCODER_YUV422F:
			return 'V';
		case ENCODER_YUV420F:
			return 'v';
		case ENCODER_LZO:
			return 'L';
		case ENCODER_YUV4MPEG:
		case ENCODER_YUV4MPEG420:
			return 'S';
		default:
			veejay_msg(VEEJAY_MSG_DEBUG, "Unknown format %d selected", encoder );
			return 0;
	}
	return 0;
}


static struct {
	const char *descr;
	int   encoder_id;
} encoder_names[] = {
	{ "Invalid codec", -1 },
	{ "DV2", ENCODER_DVVIDEO },
	{ "MJPEG", ENCODER_MJPEG },
	{ "MJPEGB", ENCODER_MJPEGB },
	{ "HuffYUV", ENCODER_HUFFYUV },
	{ "YUV 4:2:2 Planar, 0-255 full range", ENCODER_YUV422F },
	{ "YUV 4:2:0 Planar, 0-255 full range", ENCODER_YUV420F },
	{ "YUV 4:2:2 Planar, CCIR 601. 16-235/16-240", ENCODER_YUV422 },
	{ "YUV 4:2:0 Planar, CCIR 601, 16-235/16-240", ENCODER_YUV420 },
	{ "YUV 4:2:2 Planar, LZO compressed (experimental)", ENCODER_LZO },
	{ "DIVX",  ENCODER_DIVX },
	{ "Quicktime DV", ENCODER_QUICKTIME_DV },
	{ "Quicktime MJPEG", ENCODER_QUICKTIME_MJPEG },	
	{ "YUV4MPEG Stream 4:2:2", ENCODER_YUV4MPEG },
	{ "YUV4MPEG Stream 4:2:0 for MPEG2", ENCODER_YUV4MPEG420 },
	{ NULL, 0 }
};

const char		*vj_avcodec_get_encoder_name( int encoder_id )
{
	int i;
	for( i =1 ; encoder_names[i].descr != NULL ; i ++ ) {
		if( encoder_names[i].encoder_id == encoder_id ) {
			return encoder_names[i].descr;
		}
	}
	return encoder_names[0].descr;
}

int		vj_avcodec_stop( void *encoder , int fmt)
{
	if(!encoder)
		return 0;
	vj_encoder *env = (vj_encoder*) encoder;

	if( fmt == 900 )
	{
		return 1;
	}

	vj_avcodec_close_encoder( env );
	encoder = NULL;
	return 1;
}

void 		*vj_avcodec_start( VJFrame *frame, int encoder, char *filename )
{
	int codec_id = vj_avcodec_find_codec( encoder );
	void *ee = NULL;
#ifndef SUPPORT_READ_DV2
	if( codec_id == CODEC_ID_DVVIDEO ) {
		veejay_msg(VEEJAY_MSG_ERROR, "No support for DV encoding built in.");
		return NULL;
	}
#endif	
	ee = vj_avcodec_new_encoder( codec_id, frame ,filename);
	if(!ee)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "\tFailed to start encoder %x",encoder);
		return NULL;
	}
	return ee;
}


int		vj_avcodec_init( int pixel_format, int verbose)
{
	out_pixel_format = pixel_format;
	
	av_log_set_level( AV_LOG_QUIET);
	
	//av_log_set_level( AV_LOG_VERBOSE );
	
#if LIBAVCODEC_BUILD < 5400
	avcodec_register_all();
#else
	av_register_all();
#endif
	return 1;
}

int		vj_avcodec_free()
{
	return 1;
}

static void long2str(unsigned char *dst, int32_t n)
{
   dst[0] = (n    )&0xff;
   dst[1] = (n>> 8)&0xff;
   dst[2] = (n>>16)&0xff;
   dst[3] = (n>>24)&0xff;
}


static	int	vj_avcodec_lzo( vj_encoder  *av, uint8_t *src[3], uint8_t *dst , int buf_len )
{
	uint8_t *dstI = dst + (3 * 4);
	uint32_t s1,s2,s3;
	uint32_t *size1 = &s1, *size2=&s2,*size3=&s3;
	int32_t res;
	int32_t sum = 0;

	res = lzo_compress( av->lzo, src[0], dstI, size1 , av->len);
	if( res == 0 )
	{
		veejay_msg(0,"\tunable to compress Y plane");
		return 0;
	}

	long2str( dst, res );
	sum += res;

	res = lzo_compress( av->lzo, src[1], dstI + sum, size2 , av->uv_len );
	if( res == 0 )
	{
		veejay_msg(0,"\tunable to compress U plane");
		return 0;
	}

	long2str( dst + 4, res );
	sum += res;

	res = lzo_compress( av->lzo, src[2], dstI + sum, size3 , av->uv_len );
	if( res == 0 )
	{
		veejay_msg(0,"\tunable to compress V plane");
		return 0;
	}

	sum += res;
	long2str( dst + 8, res );
	
	return (sum + 12);
}
static	int	vj_avcodec_copy_frame( vj_encoder  *av, uint8_t *src[4], uint8_t *dst, int in_fmt )
{
	if(!av)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No encoder !!");
		return 0;
	}

	if( av->encoder_id == 999 )
	{
		uint8_t *dest[4] = { dst, dst + (av->len), dst + (av->len + av->len/4),NULL };
		vj_frame_copy1(src[0], dest[0],  av->len );
		yuv422to420planar( src,dest, av->width,av->height );

		if(in_fmt == FMT_422F ) 
		{
			yuv_scale_pixels_from_y( dest[0],  av->len );
			yuv_scale_pixels_from_uv( dest[1], av->len/2 );
		}

		return ( av->len + (av->len/2) );
	}
	if( av->encoder_id == 996 )
	{
		uint8_t *dest[4] = { dst, dst + (av->len), dst + (av->len + av->len/4),NULL };
		vj_frame_copy1( src[0], dest[0], av->len );
		yuv422to420planar( src,dest, av->width,av->height );

		if(in_fmt == FMT_422 ) 
		{
			yuv_scale_pixels_from_ycbcr( dest[0], 16.0f,235.0f, av->len );
			yuv_scale_pixels_from_ycbcr( dest[1], 16.0f,240.0f, av->len/2 );
		}

		return ( av->len + (av->len/2) );
	}


	if( av->encoder_id == 998 )
	{
		uint8_t *dest[4] = { dst, dst + (av->len), dst + (av->len + av->uv_len), NULL };
		int strides[4] = { av->len, av->uv_len, av->uv_len, 0 };
		vj_frame_copy( src, dest, strides );

		if(in_fmt == FMT_422F ) 
		{
			yuv_scale_pixels_from_y( dest[0], av->len );
			yuv_scale_pixels_from_uv( dest[1], av->uv_len * 2 );
		}

		return ( av->len + av->len );
	}

	if( av->encoder_id == 997 )
	{
		uint8_t *dest[4] = { dst, dst + (av->len), dst + (av->len + av->uv_len), NULL };
		int strides[4] = { av->len, av->uv_len,av->uv_len, 0 };
		vj_frame_copy( src, dest, strides );

		if(in_fmt == FMT_422 ) 
		{
			yuv_scale_pixels_from_ycbcr( dest[0], 16.0f,235.0f, av->len );
			yuv_scale_pixels_from_ycbcr( dest[1], 16.0f,240.0f, av->uv_len * 2 );
		}

		return ( av->len + av->len );
	}

	veejay_msg(VEEJAY_MSG_DEBUG, "Unknown encoder select: %d", av->encoder_id);	

	return 0;
}

static int vj_avcodec_encode_video( AVCodecContext *ctx, uint8_t *buf, int len, AVFrame *frame )
{
	if( avcodec_encode_video2) {
		AVPacket pkt;
		veejay_memset(&pkt,0,sizeof(pkt));
		int got_packet_ptr = 0;
		pkt.data = buf;
		pkt.size = len;

		int res = avcodec_encode_video2( ctx, &pkt, frame, &got_packet_ptr);

		if( res == 00 ) {
			return pkt.size;
		}

		return 0;
	}
	else if( avcodec_encode_video ) {
		return avcodec_encode_video(ctx,buf,len,frame);
	}

	return 0;
}

int		vj_avcodec_encode_frame(void *encoder, long nframe,int format, uint8_t *src[4], uint8_t *buf, int buf_len,
	int in_fmt)
{
	vj_encoder *av = (vj_encoder*) encoder;

	if(format == ENCODER_LZO )
		return vj_avcodec_lzo( encoder, src, buf, buf_len );
	
	if(format == ENCODER_YUV420 || format == ENCODER_YUV422 || format == ENCODER_YUV422F || format == ENCODER_YUV420F) // no compression, just copy
		return vj_avcodec_copy_frame( encoder,src, buf, in_fmt );

	if(format == ENCODER_YUV4MPEG || format == ENCODER_YUV4MPEG420 ) {
		if( in_fmt == FMT_422 ) {
			vj_yuv_put_frame( av->y4m, src );
			return ( av->width * av->height ) * 2;
		} else {
			yuv_scale_pixels_from_yuv( src,av->data,av->width*av->height);
			vj_yuv_put_frame(av->y4m, av->data );
			return ( av->width * av->height ) * 2;
		}
	}

#ifdef __FALLBACK_LIBDV
	if(format == ENCODER_DVVIDEO || format == ENCODER_QUICKTIME_DV )
	{
		vj_dv_encoder *dv = av->dv;
		return vj_dv_encode_frame( dv,src );
	}
#endif
	AVFrame pict;
	int stride,w2,stride2;
	veejay_memset( &pict, 0, sizeof(pict));

	pict.quality = 1;
	pict.pts = (int64_t)( (int64_t)nframe );
	pict.data[0] = src[0];
	pict.data[1] = src[1];
	pict.data[2] = src[2];

	stride = ROUND_UP_4( av->width );
	w2 = DIV_ROUND_UP_X(av->width, av->shift_x);
	stride2 = ROUND_UP_4( w2 );
	pict.linesize[0] = stride;
	pict.linesize[1] = stride2;
	pict.linesize[2] = stride2;

	return vj_avcodec_encode_video( av->context, buf, buf_len, &pict );
}

