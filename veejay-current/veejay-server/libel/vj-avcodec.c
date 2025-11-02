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
#include <stdlib.h>
#include <stdint.h>
#include <veejaycore/defs.h>
#include <libel/vj-avcodec.h>
#include <libel/vj-el.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <stdint.h>
#include <string.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/lzo.h>
#include <libstream/vj-yuv4mpeg.h>
#ifdef SUPPORT_READ_DV2
#define __FALLBACK_LIBDV
#include <libel/vj-dv.h>
#endif
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <veejaycore/av.h>
#include <veejaycore/avhelper.h>
#include <veejaycore/avcommon.h>
#define QOI_IMPLEMENTATION 1
#include <libel/qoi.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

//from gst-ffmpeg, round up a number
#define GEN_MASK(x) ((1<<(x))-1)
#define ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) & ~GEN_MASK(x))
#define ROUND_UP_2(x) ROUND_UP_X (x, 1)
#define ROUND_UP_4(x) ROUND_UP_X (x, 2)
#define ROUND_UP_8(x) ROUND_UP_X (x, 3)
#define DIV_ROUND_UP_X(v,x) (((v) + GEN_MASK(x)) >> (x))

extern int avhelper_set_num_decoders();

static int out_pixel_format = FMT_422F; 

static char*	vj_avcodec_get_codec_name(int codec_id )
{
	char name[64];
	switch(codec_id)
	{
		case CODEC_ID_MJPEG: snprintf(name,sizeof(name),"MJPEG"); break;
#if LIBAVCODEC_VERSION_MAJOR >= 59
		case AV_CODEC_ID_QOI: snprintf(name, sizeof(name), "QOI (ffmpeg)"); break;
#endif
		case CODEC_ID_MPEG4: snprintf(name,sizeof(name), "MPEG4"); break;
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
		case 993 : snprintf(name,sizeof(name), "QOI YUV 4:2:2 Planar (experimental)"); break;
		case 999 : snprintf(name,sizeof(name), "RAW YUV 4:2:0 Planar"); break;
		case 998 : snprintf(name,sizeof(name), "RAW YUV 4:2:2 Planar"); break;
		case 900 : snprintf(name,sizeof(name), "LZO YUV 4:2:2 Planar (experimental)"); break;
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

int 			vj_avcodec_get_buf_size( vj_encoder *av )
{
	return av->out_frame->len + av->out_frame->uv_len + av->out_frame->uv_len;
}

static vj_encoder	*vj_avcodec_new_encoder( int id, VJFrame *frame, char *filename)
{
	vj_encoder *e = (vj_encoder*) vj_calloc(sizeof(vj_encoder));
	char errbuf[512];

	if(!e) return NULL;

	int selected_out_pixfmt = out_pixel_format;
	int chroma_val = -1;
	
	switch( id ) {
		case 997:
			selected_out_pixfmt = FMT_422F;
			break;
		case 996:
			selected_out_pixfmt = FMT_420F;
			break;
		case 995:
			selected_out_pixfmt = FMT_422;
			chroma_val = Y4M_CHROMA_422;
			break;
		case 994:
			selected_out_pixfmt = FMT_420;
			chroma_val = (out_pixel_format == FMT_422F || out_pixel_format == FMT_420F ? Y4M_CHROMA_420JPEG : Y4M_CHROMA_420MPEG2);
			break;
		case 999:
			selected_out_pixfmt = FMT_420;
			break;
		case 998:
			selected_out_pixfmt = FMT_422;
			break;
		case CODEC_ID_HUFFYUV:
			selected_out_pixfmt = FMT_422;
			break;
		default:
			break;
	}

	int pf = get_ffmpeg_pixfmt( selected_out_pixfmt );

	e->out_frame = yuv_yuv_template( NULL,NULL,NULL, frame->width, frame->height, pf );
	e->in_frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
	veejay_memcpy( e->in_frame, frame, sizeof(VJFrame));

	e->data[0] = (uint8_t*) vj_malloc((e->out_frame->len * 4));
	e->data[1] = e->data[0] + e->out_frame->len;
	e->data[2] = e->data[1] + e->out_frame->len;
	e->data[3] = NULL;

	e->out_frame->data[0] = e->data[0];
	e->out_frame->data[1] = e->data[1];
	e->out_frame->data[2] = e->data[2];
	 
	e->in_frame->data[0] = NULL;
	e->in_frame->data[1] = NULL;
	e->in_frame->data[2] = NULL;
	e->in_frame->data[3] = NULL;

	veejay_memset( e->data[0], 0, e->out_frame->len);
	veejay_memset( e->data[1], 128, e->out_frame->uv_len);
	veejay_memset( e->data[2], 128, e->out_frame->uv_len );


	// strip any A*
	e->in_frame->format = vj_to_pixfmt( out_pixel_format );
	e->in_frame->stride[3] = 0;

	veejay_msg(VEEJAY_MSG_DEBUG, "Selected output pixel format: %s (internal out fmt %d, chroma %d). Source is %s", yuv_get_pixfmt_description(pf), selected_out_pixfmt, chroma_val,
	 	yuv_get_pixfmt_description(e->in_frame->format));


	if( id > 900 ) {
		sws_template tmpl;
		tmpl.flags = 1;
		e->scaler = yuv_init_swscaler( e->in_frame,e->out_frame, &tmpl, yuv_sws_get_cpu_flags());
		if(e->scaler == NULL) {
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize scaler context");
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			free(e);
			return NULL;
		}
	}

#ifdef SUPPORT_READ_DV2
	if( id == CODEC_ID_DVVIDEO )
	{
		if(!is_dv_resolution(frame->width, frame->height ))
		{	
			veejay_msg(VEEJAY_MSG_ERROR,"\tSource video is not in DV resolution");
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			free(e);
			return NULL;
		}
		else
		{
			e->dv = (void*)vj_dv_init_encoder( (void*)frame, pf );
		}
	}
	else {
#endif
		
#ifdef SUPPORT_READ_DV2
	}
#endif
	
	if( id == 900 )
	{
		e->lzo = lzo_new(frame->format, frame->width, frame->height, 0 );
	}

	if( id == 995 || id == 994) {
		e->y4m = vj_yuv4mpeg_alloc(frame->width,frame->height,frame->fps, selected_out_pixfmt );
		if( !e->y4m) {
			veejay_msg(0, "Error while trying to setup Y4M stream, abort");
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			yuv_free_swscaler(e->scaler);

			free(e);
			
			return NULL;
		}

		if( vj_yuv_stream_start_write( e->y4m, frame,filename,chroma_val )== -1 )
		{
			veejay_msg(0, "Unable to write header to  YUV4MPEG stream");
			vj_yuv4mpeg_free( e->y4m );
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			yuv_free_swscaler(e->scaler);
			
			free(e);
			return NULL;
		}
	}
	
	if(id != 998 && id != 999 && id != 900 && id != 997 && id != 996 && id != 995 && id != 994 && id != 993)
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
			 free(e->out_frame);
			 free(e->in_frame);
			 free(e->data[0]);
			 free(e);
			 return NULL;
			}
#ifdef __FALLBACK_LIBDV
		}
#endif

	}

	if( id != 998 && id != 999 && id!= 900 && id != 997 && id != 996 && id != CODEC_ID_DVVIDEO && id != 995 && id != 994 && id != 993)
	{
#ifdef __FALLBACK_LIBDV
	  if(id != CODEC_ID_DVVIDEO )
		{
#endif
#if LIBAVCODEC_VERSION_MAJOR > 54  
   	    e->context = avcodec_alloc_context3(e->codec);
#else
		e->context = avcodec_alloc_context();
#endif
		e->context->bit_rate = 2750 * 1024;
		e->context->width = frame->width;
 		e->context->height = frame->height;
		
#if LIBAVCODEC_VERSION_MAJOR >= 50
		e->context->time_base = (AVRational) { 1, frame->fps };
#else
		e->context->frame_rate = frame->fps;
		e->context->frame_rate_base = 1;
#endif
#if LIBAVCODEC_VERSION_MAJOR >= 60
		e->packet = av_packet_alloc();
		e->frame = av_frame_alloc();
		e->frame->format = get_ffmpeg_pixfmt( selected_out_pixfmt );
		e->frame->width = frame->width;
		e->frame->height = frame->height;

	    int av_ret = av_frame_get_buffer(e->frame, 0);
		if( av_ret < 0 ) {
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to allocate buffers for encoder");
			 free(e->out_frame);
			 free(e->in_frame);
			 free(e->data[0]);
			 av_packet_free(&(e->packet));
			 av_frame_free(&(e->frame));
			 free(e);
		
			return NULL;
		}

		e->context->framerate = (AVRational) { 1, frame->fps };
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
#if LIBAVCODEC_VERSION_MAJOR < 60
		e->context->prediction_method = 0;
#endif
		e->context->dct_algo = FF_DCT_AUTO; 
		e->context->pix_fmt = pf;

		//pf = e->context->pix_fmt;
		char *descr = vj_avcodec_get_codec_name( id );
#if LIBAVCODEC_VERSION_MAJOR > 54

		int n_threads = avhelper_set_num_decoders();

		if (e->codec->capabilities & AV_CODEC_CAP_FRAME_THREADS) {
			e->context->thread_type = FF_THREAD_FRAME;
			e->context->thread_count = n_threads;	
		}
		else if (e->codec->capabilities & AV_CODEC_CAP_SLICE_THREADS) {
			e->context->thread_type = FF_THREAD_SLICE;
			e->context->thread_count = n_threads;	
		}

		int ret = avcodec_open2( e->context, e->codec, NULL );
#else
		int ( avcodec_open( e->context, e->codec ) < 0 );
#endif
		if( ret < 0 ) {
			av_strerror( ret, errbuf, sizeof(errbuf));
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to open codec '%s': %s" , descr, errbuf );
			avhelper_free_context( &(e->context) );
			free(e->out_frame);
			free(e->in_frame);
			free(e->data[0]);
			free(e);
			if(descr) free(descr);
			return NULL;
		}
		else
		{
			veejay_msg(VEEJAY_MSG_DEBUG, "\tOpened codec %s [in pixfmt=%d]", descr, e->context->pix_fmt );
			if(e->context->color_range == AVCOL_RANGE_JPEG ) {
				veejay_msg(VEEJAY_MSG_INFO, "color range is jpeg");
			}
			if(e->context->color_range == AVCOL_RANGE_UNSPECIFIED) {
				veejay_msg(VEEJAY_MSG_WARNING, "Color range not specified" );
			}

			if(e->context->color_range == AVCOL_RANGE_MPEG ) {
				veejay_msg(VEEJAY_MSG_INFO, "color range is mpeg");
			}

			free(descr);
		}
#ifdef __FALLBACK_LIBDV
	}
#endif
	}

	e->width = e->out_frame->width;
	e->height = e->out_frame->height;
	e->encoder_id = id;
	e->shift_y = e->out_frame->shift_v;
	e->shift_x = e->out_frame->shift_h;
	e->len = e->out_frame->len;
	e->uv_len = e->out_frame->uv_len;

	return e;
}
void		vj_avcodec_close_encoder( vj_encoder *av )
{
	if(av)
	{
		if(av->context)
		{
#if LIBAVCODEC_VERSION_MAJOR > 59
			avcodec_free_context( &(av->context) );
#else
			avcodec_close( av->context );
#endif
		}
		if(av->data[0])
			free(av->data[0]);
		if(av->lzo)
			lzo_free(av->lzo);
#ifdef SUPPORT_READ_DV2
		if(av->dv)
			vj_dv_free_encoder( (vj_dv_encoder*) av->dv );
#endif
		if(av->scaler)
			yuv_free_swscaler(av->scaler);

	    if(av->out_frame)
			free(av->out_frame);
		if(av->in_frame)
			free(av->in_frame);
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
		case ENCODER_QOI:
			return 993;
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
		case ENCODER_HUFFYUV:
			return 'H';
		case ENCODER_QUICKTIME_MJPEG:
		       	return 'q';
		case ENCODER_DVVIDEO:
			return 'd';
		case ENCODER_QUICKTIME_DV:
			return 'Q';
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
		case ENCODER_QOI:
			return 'o';
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
	{ "HuffYUV", ENCODER_HUFFYUV },
	{ "YUV 4:2:2 Planar, 0-255 full range", ENCODER_YUV422F },
	{ "YUV 4:2:0 Planar, 0-255 full range", ENCODER_YUV420F },
	{ "YUV 4:2:2 Planar, CCIR 601. 16-235/16-240", ENCODER_YUV422 },
	{ "YUV 4:2:0 Planar, CCIR 601, 16-235/16-240", ENCODER_YUV420 },
	{ "YUV 4:2:2 Planar, LZO compressed (experimental)", ENCODER_LZO },
	{ "QOI grayscale, QOI (experimental)", ENCODER_QOI },
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

#if LIBAVCODEC_MAJOR >= 60 

#endif

	vj_avcodec_close_encoder( env );

#if LIBAVCODEC_MAJOR >= 60
	av_packet_free( &(env->packet) );
	av_frame_free( &(env->frame) );
#endif

	encoder = NULL;
	return 1;
}

void 		*vj_avcodec_start( VJFrame *frame, int encoder, char *filename )
{
	int codec_id = vj_avcodec_find_codec( encoder );
	void *ee = NULL;
#ifndef SUPPORT_READ_DV2
	if( codec_id == CODEC_ID_DVVIDEO ) {
		veejay_msg(VEEJAY_MSG_ERROR, "No support for DV encoding built in");
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
	
	char *av_log_setting = getenv("VEEJAY_AV_LOG");
	if(av_log_setting != NULL) {
		int level = atoi(av_log_setting);
		veejay_msg(VEEJAY_MSG_DEBUG, "ffmpeg/libav log level set to %d", level);
		av_log_set_level(level);
	}
	else {
		veejay_msg(VEEJAY_MSG_DEBUG, "ffmpeg/libav log level not set (use VEEJAY_AV_LOG=level)");
		av_log_set_level( AV_LOG_QUIET);
	}

	veejay_msg(VEEJAY_MSG_INFO, "ffmpeg/libav library version %d.%d.%d", LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO);

#if LIBAVCODEC_VERSION_MAJOR < 54
	avcodec_register_all();
	
#else
#if LIBAVCODEC_VERSION_MAJOR < 60
	av_register_all();
#endif
#endif
	return 1;
}

int		vj_avcodec_free()
{
	return 1;
}


static	int	vj_avcodec_copy_frame( vj_encoder  *av, uint8_t *src[4], uint8_t *dst)
{
	VJFrame *A = av->in_frame;
	VJFrame *B = av->out_frame;

	A->data[0] = src[0]; 
	A->data[1] = src[1]; 
	A->data[2] = src[2]; 
	A->data[3] = NULL;
	
	B->data[0] = dst;    
	B->data[1] = dst + B->len; 
	B->data[2] = dst + B->len + B->uv_len;
	B->data[3] = NULL;

	yuv_convert_and_scale( av->scaler, A, B);

	veejay_msg(VEEJAY_MSG_DEBUG, "From %s -> %s (%d, %d)",  yuv_get_pixfmt_description(A->format), yuv_get_pixfmt_description(B->format), B->len, B->uv_len);
	
	return (B->len + B->uv_len + B->uv_len);
}

static int vj_avcodec_encode_video( AVPacket *pkt, AVCodecContext *ctx, uint8_t *buf, int len, AVFrame *frame )
{
#if LIBAVCODEC_VERSION_MAJOR < 60
	if( avcodec_encode_video2) {
		char errbuf[512];
		int got_packet_ptr = 0;
		pkt->data = buf;
		pkt->size = len;

		int res = avcodec_encode_video2( ctx, pkt, frame, &got_packet_ptr);
		if( res < 0) {
			av_strerror( res, errbuf, sizeof(errbuf));
			veejay_msg(0, "Unable to encode frame: %s", errbuf);
			return -1;
		}

		if( res == 0 ) {
			veejay_msg(VEEJAY_MSG_DEBUG, "Encoded frame to %d bytes", pkt->size);
			return pkt->size;
		}

		return -1;
	}
	else if( avcodec_encode_video ) {
		return avcodec_encode_video(ctx,buf,len,frame);
	}
#else

	int ret = av_frame_make_writable(frame);
	if(ret < 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to make buffer writable");
		return -1;
	}

	ret = avcodec_send_frame( ctx, frame );
	if( ret < 0 ) {
		veejay_msg(0, "Error sending frame to decoder: %s", av_err2str(ret));
		return -1;
	}

	pkt->data = buf;
	pkt->size = len;

	 while (ret >= 0)
     {
         ret = avcodec_receive_packet(ctx, pkt);

         if (ret == AVERROR(EAGAIN) || ret == 0) {  // need new input, we are ready
		 	ret = pkt->size;
		 	break;
		 }
         if (ret < 0) {
			veejay_msg( VEEJAY_MSG_ERROR, "Encoding failed: %s", av_err2str(ret));
			break;
		 }

		
     }

	 if(pkt->size > 0)
	   ret = pkt->size;
 
	av_packet_unref(pkt);
	if( ret > 0 ) {
		char name[256];
		sprintf(name, "frame-%d.jpg", ret);
		FILE *f = fopen( name, "wb");
		fwrite( buf, ret, 1, f);
		fclose(f);
	} 

	return ret;

#endif

	return -1;
}

int		vj_avcodec_encode_frame(void *encoder, long nframe,int format, uint8_t *src[4], uint8_t *buf, int buf_len,
	int in_fmt)
{
	vj_encoder *av = (vj_encoder*) encoder;

	if(format == ENCODER_QOI) {
		int res = 0;
	    qoi_desc d;
		d.channels = 1;
		d.colorspace = QOI_LINEAR;
		d.height = av->height;
		d.width = av->width;
	
	
		const unsigned char *tmp[4] = { src[0], src[1], src[2], src[3] };
	    	qoi_encode( tmp, &d, &res, buf, buf_len );

		return res;
	}

	if(format == ENCODER_LZO )
		return lzo_compress_frame( av->lzo, av->in_frame,src, buf );
		
	if(format == ENCODER_YUV420 || format == ENCODER_YUV422 || format == ENCODER_YUV422F || format == ENCODER_YUV420F)
		return vj_avcodec_copy_frame( encoder,src, buf );

	if(format == ENCODER_YUV4MPEG || format == ENCODER_YUV4MPEG420 ) {
			if( in_fmt == FMT_422 ) {
					vj_yuv_put_frame( av->y4m, src );
					return ( av->width * av->height ) * 2;
			} else {
					yuv_scale_pixels_from_yuv( src,av->data,av->len, av->uv_len);
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
#if LIBAVCODEC_VERSION_MAJOR < 60
	AVFrame pict;
	veejay_memset( &pict, 0, sizeof(pict));

	pict.quality = 1;
	pict.pts = (int64_t)( (int64_t)nframe );
	pict.data[0] = src[0];
	pict.data[1] = src[1];
	pict.data[2] = src[2];
	pict.format  = av->out_frame->format;

	pict.linesize[0] = ROUND_UP_4( av->out_frame->width );
	pict.linesize[1] = ROUND_UP_4( av->out_frame->uv_width );
	pict.linesize[2] = ROUND_UP_4( av->out_frame->uv_width );


	pict.width = av->out_frame->width;
	pict.height = av->out_frame->height;

	AVPacket pkt;
	veejay_memset(&pkt,0,sizeof(pkt));
		
	return vj_avcodec_encode_video( &pkt, av->context, buf, buf_len, &pict );
#else
	av->frame->pts = (int64_t) nframe;
	av->frame->quality = FF_QP2LAMBDA * 3.0;
	
	av->frame->data[0] = src[0];
	av->frame->data[1] = src[1];
	av->frame->data[2] = src[2];

	av->frame->linesize[0] = av->out_frame->width;
	av->frame->linesize[1] = av->out_frame->uv_width;
	av->frame->linesize[2] = av->out_frame->uv_width;

	//av->frame->quality = 1;
	//av->frame->key_frame = 1;
	av->frame->pict_type = AV_PICTURE_TYPE_I;

	//av->frame->quality = 1;
	int ret = vj_avcodec_encode_video( av->packet, av->context, buf, buf_len, av->frame );

	veejay_msg(VEEJAY_MSG_DEBUG, "Encoded frame %ld to %d bytes" ,nframe, ret );

	return ret;
#endif
}

