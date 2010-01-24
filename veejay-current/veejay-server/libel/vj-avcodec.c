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



//#define YUV420_ONLY_CODEC(id) ( ( id == CODEC_ID_MJPEG || id == CODEC_ID_MJPEGB || id == CODEC_ID_MSMPEG4V3 || id == CODEC_ID_MPEG4) ? 1: 0)

//#define YUV420_ONLY_CODEC(id) ( (id == CODEC_ID_MSMPEG4V3 || id == CODEC_ID_MPEG4 ) ? 1 : 0 )

static	int	YUV420_ONLY_CODEC(int id ) 
{
	switch(id) {
	//	case CODEC_ID_MJPEG: 
	//	case CODEC_ID_MJPEGB:
		case CODEC_ID_MPEG4:
	//	case CODEC_ID_LJPEG:
		case CODEC_ID_MSMPEG4V3:
//		case CODEC_ID_SP5X: 
//		case CODEC_ID_THEORA:
//		case CODEC_ID_H264:;

			return 1;
	}
	return 0;
}

//from gst-ffmpeg, round up a number
#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X (x, 1)
#define ROUND_UP_4(x) ROUND_UP_X (x, 2)
#define ROUND_UP_8(x) ROUND_UP_X (x, 3)
#define DIV_ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) >> (x))

static int out_pixel_format = FMT_422F; 

char*	vj_avcodec_get_codec_name(int codec_id )
{
	char name[20];
	switch(codec_id)
	{
		case CODEC_ID_MJPEG: sprintf(name, "MJPEG"); break;
		case CODEC_ID_MPEG4: sprintf(name, "MPEG4"); break;
		case CODEC_ID_MJPEGB: sprintf(name,"MJPEGB");break;
		case CODEC_ID_MSMPEG4V3: sprintf(name, "DIVX"); break;
		case CODEC_ID_DVVIDEO: sprintf(name, "DVVideo"); break;
		case CODEC_ID_LJPEG: sprintf(name, "LJPEG" );break;
		case CODEC_ID_SP5X: sprintf(name, "SP5x"); break;
		case CODEC_ID_THEORA: sprintf(name,"Theora");break;
		case CODEC_ID_H264: sprintf(name, "H264");break;
		case 997 : sprintf(name, "RAW YUV 4:2:2 Planar JPEG"); break;
		case 996 : sprintf(name, "RAW YUV 4:2:0 Planar JPEG"); break;
		case 995 : sprintf(name, "YUV4MPEG Stream 4:2:2"); break;
		case 994 : sprintf(name, "YUV4MPEG Stream 4:2:0"); break;
		case 999 : sprintf(name, "RAW YUV 4:2:0 Planar"); break;
		case 998 : sprintf(name, "RAW YUV 4:2:2 Planar"); break;
		case 900 : sprintf(name, "LZO YUV 4:2:2 Planar"); break;
		default:
			sprintf(name, "Unknown"); break;
	}
	char *res = strdup(name);
	return res;
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

static vj_encoder	*vj_avcodec_new_encoder( int id, editlist *el, char *filename)
{
	vj_encoder *e = (vj_encoder*) vj_calloc(sizeof(vj_encoder));
	if(!e) return NULL;

	int pf = get_ffmpeg_pixfmt( out_pixel_format );

#ifdef SUPPORT_READ_DV2
	if( id == CODEC_ID_DVVIDEO )
	{
		if(!is_dv_resolution(el->video_width, el->video_height ))
		{	
			veejay_msg(VEEJAY_MSG_ERROR,"\tVideo dimensions do not match required resolution");
			return NULL;
		}
		else
		{
			e->dv = (void*)vj_dv_init_encoder( (void*)el , out_pixel_format);
		}
	}
	else {
#endif
		e->data[0] = (uint8_t*) vj_calloc(sizeof(uint8_t) * el->video_width * el->video_height * 3 );
		e->data[1] = e->data[0] + el->video_width * el->video_height;
		e->data[2] = e->data[1] + el->video_width * el->video_height /2;
#ifdef SUPPORT_READ_DV2
	}
#endif
	
	if( id == 900 )
	{
		e->lzo = lzo_new();
	}

	if( id == 995 || id == 994) {
		e->y4m = vj_yuv4mpeg_alloc(el, el->video_width,
					       el->video_height,
			       		       out_pixel_format	);
		if( !e->y4m) {
			veejay_msg(0, "Error while trying to setup Y4M stream, abort.");
			return NULL;
		}

		int chroma_val = Y4M_CHROMA_422;
		if( id == 994 ) {
			chroma_val = Y4M_CHROMA_420MPEG2;
		}

		if( vj_yuv_stream_start_write( e->y4m, el,filename,chroma_val )== -1 )
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
			 veejay_msg(VEEJAY_MSG_ERROR, "Cannot find Encoder codec %s", 	descr );
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
		e->context = avcodec_alloc_context();
		e->context->bit_rate = 2750 * 1024;
		e->context->width = el->video_width;
 		e->context->height = el->video_height;
#if LIBAVCODEC_BUILD > 5010
		e->context->time_base = (AVRational) { 1, el->video_fps };
#else
		e->context->frame_rate = el->video_fps;
		e->context->frame_rate_base = 1;
#endif
		e->context->qcompress = 0.0;
		e->context->qblur = 0.0;
		e->context->max_b_frames = 0;
		e->context->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
		e->context->flags = CODEC_FLAG_QSCALE;
		e->context->gop_size = 0;
		e->context->sub_id = 0;
		e->context->workaround_bugs = FF_BUG_AUTODETECT;
		e->context->prediction_method = 0;
		e->context->dct_algo = FF_DCT_AUTO; 

		e->context->pix_fmt = get_ffmpeg_pixfmt( out_pixel_format );
	
		if(YUV420_ONLY_CODEC(id))
			e->context->pix_fmt = ( out_pixel_format == FMT_422F ? PIX_FMT_YUVJ420P : PIX_FMT_YUV420P );

		pf = e->context->pix_fmt;

	
		char *descr = vj_avcodec_get_codec_name( id );
		if ( avcodec_open( e->context, e->codec ) < 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot open codec '%s'" , descr );
			if(e->context) free(e->context);
			if(e) free(e);
			if(descr) free(descr);
			return NULL;
		}
		else
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "\tOpened encoder %s", descr );
			free(descr);
		}
#ifdef __FALLBACK_LIBDV
	}
#endif
	}

	e->width = el->video_width;
	e->height = el->video_height;
	e->encoder_id = id;


	switch(pf) {
		case PIX_FMT_YUVJ422P:
		case PIX_FMT_YUV422P:
			e->shift_y = 0;
			e->shift_x = 1;
			e->len = el->video_width * el->video_height;
			e->uv_len = e->len / 2;
			e->out_fmt = (pf == PIX_FMT_YUVJ422P ? FMT_422F : FMT_422 );
			break;
		case PIX_FMT_YUVJ420P:
		case PIX_FMT_YUV420P:
			e->shift_y = 1;
			e->shift_x = 1;
			e->len = el->video_width * el->video_height;
			e->uv_len = e->len / 4;
			e->out_fmt = (pf == PIX_FMT_YUVJ420P ? FMT_420F : FMT_420 );
			break;
		}


/*
	if( el->has_audio )
	{
		e->audiocodec = avcodec_find_encoder( CODEC_ID_PCM_U8 );
		if(!e->audiocodec)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Error initializing audio codec");
			if(e) free(e);
		}
		e->context->sample_rate = el->audio_rate;
		e->context->channels	= el->audio_chans;
		if( avcodec_open( e->context, e->audiocodec ) < 0)
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Cannot open audio context");
			if(e) free(e);
			return NULL;
		}

	}
*/
	return e;
}
void		vj_avcodec_close_encoder( vj_encoder *av )
{
	if(av)
	{
		if(av->context)
		{
			avcodec_close( av->context );
			free(av->context);	
			av->context = NULL;
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
		case ENCODER_MPEG4:
			return CODEC_ID_MPEG4;
		case ENCODER_DIVX:
			return CODEC_ID_MSMPEG4V3;
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
		case ENCODER_MPEG4:
			return 'M';
		case ENCODER_DIVX:
			return 'D';
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
	{ "LJPEG", ENCODER_LJPEG },
	{ "MJPEGB", ENCODER_MJPEGB },
	{ "YUV 4:2:2 Planar, 0-255 full range", ENCODER_YUV422F },
	{ "YUV 4:2:0 Planar, 0-255 full range", ENCODER_YUV420F },
	{ "YUV 4:2:2 Planar, CCIR 601. 16-235/16-240", ENCODER_YUV422 },
	{ "YUV 4:2:0 Planar, CCIR 601, 16-235/16-240", ENCODER_YUV420 },
	{ "YUV 4:2:2 Planar, LZO compressed (experimental)", ENCODER_LZO },
	{ "MPEG4", ENCODER_MPEG4 },
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

void 		*vj_avcodec_start( editlist *el, int encoder, char *filename )
{
	int codec_id = vj_avcodec_find_codec( encoder );
	void *ee = NULL;
#ifndef SUPPORT_READ_DV2
	if( codec_id == CODEC_ID_DVVIDEO ) {
		veejay_msg(VEEJAY_MSG_ERROR, "No support for DV encoding built in.");
		return NULL;
	}
#endif	
	ee = vj_avcodec_new_encoder( codec_id, el ,filename);
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
#ifndef STRICT_CHECKING
	av_log_set_level( AV_LOG_QUIET);
#else
	av_log_set_level( AV_LOG_VERBOSE );
#endif
	av_register_all();

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
	int size1 = 0, size2=0,size3=0;
	int i;
	
	i = lzo_compress( av->lzo, src[0], dstI, &size1 , av->len);
	if( i == 0 )
	{
		veejay_msg(0,"\tunable to compress Y plane");
		return 0;
	}
	dstI += size1;

	i = lzo_compress( av->lzo, src[1], dstI, &size2 , av->uv_len );
	if( i == 0 )
	{
		veejay_msg(0,"\tunable to compress U plane");
		return 0;
	}
	
	dstI += size2;
	i = lzo_compress( av->lzo, src[2], dstI, &size3 , av->uv_len );
	if( i == 0 )
	{
		veejay_msg(0,"\tunable to compress V plane");
		return 0;
	}
		
	
	long2str( dst, size1 );
	long2str( dst+4,size2);
	long2str( dst+8,size3);
	
	return (size1 + size2 + size3 + 12);
}
static	int	vj_avcodec_copy_frame( vj_encoder  *av, uint8_t *src[3], uint8_t *dst, int in_fmt )
{
	if(!av)
	{
		veejay_msg(VEEJAY_MSG_ERROR, "No encoder !!");
		return 0;
	}

	if( av->encoder_id == 999 )
	{
		uint8_t *dest[3] = { dst, dst + (av->len), dst + (av->len + av->len/4) };
		veejay_memcpy( dest[0], src[0], av->len );
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
		uint8_t *dest[3] = { dst, dst + (av->len), dst + (av->len + av->len/4) };
		veejay_memcpy( dest[0], src[0], av->len );
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
		uint8_t *dest[3] = { dst, dst + (av->len), dst + (av->len + av->uv_len) };

		veejay_memcpy( dest[0], src[0],av->len );	
		veejay_memcpy( dest[1], src[1],av->uv_len);
		veejay_memcpy( dest[2], src[2],av->uv_len );

		if(in_fmt == FMT_422F ) 
		{
			yuv_scale_pixels_from_y( dest[0], av->len );
			yuv_scale_pixels_from_uv( dest[1], av->uv_len * 2 );
		}

		return ( av->len + av->len );
	}

	if( av->encoder_id == 997 )
	{
		uint8_t *dest[3] = { dst, dst + (av->len), dst + (av->len + av->uv_len) };

		veejay_memcpy( dest[0], src[0],av->len );	
		veejay_memcpy( dest[1], src[1],av->uv_len);
		veejay_memcpy( dest[2], src[2],av->uv_len );

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



int		vj_avcodec_encode_frame(void *encoder, int nframe,int format, uint8_t *src[3], uint8_t *buf, int buf_len,
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
	int res=0;
	int stride,h2,w2,size,size2,stride2;
	veejay_memset( &pict, 0, sizeof(pict));

	pict.quality = 1;
	pict.pts = (int64_t)( (int64_t)nframe );
	pict.data[0] = src[0];
	pict.data[1] = src[1];
	pict.data[2] = src[2];

	stride = ROUND_UP_4( av->width );
	h2 = ROUND_UP_X( av->height, av->shift_y );
	size = stride * h2;
	w2 = DIV_ROUND_UP_X(av->width, av->shift_x);
	stride2 = ROUND_UP_4( w2 );
	h2 = DIV_ROUND_UP_X(av->height, av->shift_y );
	size2 = stride2 * h2;
	pict.linesize[0] = stride;
	pict.linesize[1] = stride2;
	pict.linesize[2] = stride2;

	if( YUV420_ONLY_CODEC(av->encoder_id)) {
		veejay_memcpy( av->data[0], src[0], av->width * av->height);
		yuv422to420planar(src , av->data, av->width,av->height );
		pict.data[0] = av->data[0];
		pict.data[1] = av->data[1];
		pict.data[2] = av->data[2];
	}

	return avcodec_encode_video( av->context, buf, buf_len, &pict );
}

int		vj_avcodec_encode_audio( void *encoder, int format, uint8_t *src, uint8_t *dst, int len, int nsamples )
{
	if(format == ENCODER_YUV420 || ENCODER_YUV422 == format)
		return 0;
	vj_encoder *av = encoder;
	int ret = avcodec_encode_audio( av->context, src, len, nsamples );
	return ret;
}
