/* veejay - Linux VeeJay
 *           (C) 2002-2015 Niels Elburg <nwelburg@gmail.com> 
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
 * GNU General Public License for more details//.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include <libvje/vje.h>
#include <libyuv/yuvconv.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libel/avhelper.h>

static struct
{
        const char *name;
        int  id;
} _supported_codecs[] = 
{
	{ "vj20", CODEC_ID_YUV420F	},
	{ "vj22", CODEC_ID_YUV422F	},
        { "mjpg" ,CODEC_ID_MJPEG 	},
	{ "mjpb", CODEC_ID_MJPEGB	},
        { "i420", CODEC_ID_YUV420	},
        { "i422", CODEC_ID_YUV422	},
	{ "dmb1", CODEC_ID_MJPEG	},
	{ "dmb1", CODEC_ID_MJPEG	},
	{ "jpeg", CODEC_ID_MJPEG	},
	{ "jpeg", CODEC_ID_MJPEG	},
	{ "mjpa", CODEC_ID_MJPEG	},
	{ "mjpb", CODEC_ID_MJPEG	},
	{ "jfif", CODEC_ID_MJPEG	},
	{ "jfif", CODEC_ID_MJPEG	},
	{ "png", CODEC_ID_PNG		},
	{ "mpng", CODEC_ID_PNG		},
#if LIBAVCODEC_BUILD > 4680
	{ "sp5x", CODEC_ID_SP5X		}, 
#endif
	{ "jpgl", CODEC_ID_MJPEG 	},
	{ "jpgl", CODEC_ID_MJPEG	},
	{ "dvsd", CODEC_ID_DVVIDEO	},
	{ "dvcp", CODEC_ID_DVVIDEO	},
	{ "dv",	CODEC_ID_DVVIDEO	},
	{ "dvhd", CODEC_ID_DVVIDEO	},
	{ "dvp", CODEC_ID_DVVIDEO	},
	{ "yuv", CODEC_ID_YUV420	},
	{ "iyuv", CODEC_ID_YUV420	},
	{ "i420", CODEC_ID_YUV420	},
	{ "yv16", CODEC_ID_YUV422	},
	{ "yv12", CODEC_ID_YUV420	},
	{ "mlzo", CODEC_ID_YUVLZO	},
	{ "pict", 0xffff		}, 
	{ "hfyu", CODEC_ID_HUFFYUV	},
	{ "cyuv", CODEC_ID_CYUV		},
	{ "svq1", CODEC_ID_SVQ1		},
	{ "svq3", CODEC_ID_SVQ3		},
	{ "rpza", CODEC_ID_RPZA		},
	{ NULL  , 0,			},
};


typedef struct
{
	AVCodec *codec;
	AVCodecContext *codec_ctx;
	AVFormatContext *avformat_ctx;
	AVPacket pkt;
	AVFrame *frame;
	int pixfmt;
	int codec_id;
	VJFrame *output;
	VJFrame *input;
	void *scaler;
} el_decoder_t;

int 	avhelper_get_codec_by_id(int id)
{
	int i;
	for( i = 0; _supported_codecs[i].name != NULL ; i ++ )
	{
		if( _supported_codecs[i].id == id)
		{
			return i;
		}
	}
	return -1;
}


int	avhelper_get_codec_by_name( const char *compr )
{
	int i;
	int len = strlen( compr );
	for( i = 0; _supported_codecs[i].name != NULL ; i ++ ) {
		if( strncasecmp( compr, _supported_codecs[i].name,len ) == 0 ) {
			return _supported_codecs[i].id;
		}
	}
	return -1;
}

void	*avhelper_get_codec_ctx( void *ptr )
{
	el_decoder_t *e = (el_decoder_t*) ptr;
	return e->codec_ctx;
}


void	*avhelper_get_codec( void *ptr )
{
	el_decoder_t *e = (el_decoder_t*) ptr;
	return e->codec;
}

#if LIBAVCODEC_BUILD > 5400
static int avcodec_decode_video( AVCodecContext *avctx, AVFrame *picture, int *got_picture, uint8_t *data, int pktsize ) {
	AVPacket pkt;
	veejay_memset( &pkt, 0, sizeof(AVPacket));
	pkt.data = data;
	pkt.size = pktsize;
	return avcodec_decode_video2( avctx, picture, got_picture, &pkt );
}
#endif

void avhelper_frame_unref(AVFrame *ptr)
{
#if LIBAVCODEC_VERSION_MAJOR > 55 && LIBAVCODEC_VERSION_MINOR > 40
	av_frame_unref( ptr );
#endif
}

void avhelper_free_context(AVCodecContext **avctx)
{
#if LIBAVCODEC_VERSION_MAJOR > 55 && LIBAVCODEC_VERSION_MINOR > 40
	avcodec_free_context( avctx );
#else
	if( avctx )
		free( avctx );
	avctx = NULL;
#endif
}

static void avhelper_close_input_file( AVFormatContext *s ) {
#if LIBAVCODEC_BUILD > 5400
	avformat_close_input(&s);
#else
	av_close_input_file(s);
#endif
}

void	*avhelper_get_decoder( const char *filename, int dst_pixfmt, int dst_width, int dst_height ) {
	char errbuf[512];
	el_decoder_t *x = (el_decoder_t*) vj_calloc( sizeof( el_decoder_t ));
	if(!x) {
		return NULL;
	}

#if LIBAVCODEC_BUILD > 5400
	int err = avformat_open_input( &(x->avformat_ctx), filename, NULL, NULL );
#else
	int err = av_open_input_file( &(x->avformat_ctx),filename,NULL,0,NULL );
#endif

	if(err < 0 ) {
		av_strerror( err, errbuf, sizeof(errbuf));
		veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s", filename,errbuf );
		free(x);
		return NULL;
	}

#if LIBAVCODEC_BUILD > 5400
	/* avformat_find_stream_info leaks memory */
	err = avformat_find_stream_info( x->avformat_ctx, NULL );
#else
	err = av_find_stream_info( x->avformat_ctx );
#endif
	if( err < 0 ) {
		av_strerror( err, errbuf, sizeof(errbuf));
		veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s" ,filename,errbuf );
	}

	if(err < 0 ) {
		avhelper_close_input_file( x->avformat_ctx );
		free(x);
		return NULL;
	}
	
	unsigned int i,j;
	unsigned int n = x->avformat_ctx->nb_streams;
	int vi = -1;

	for( i = 0; i < n; i ++ )
	{
		if( !x->avformat_ctx->streams[i]->codec )
			continue;

		if( x->avformat_ctx->streams[i]->codec->codec_type > CODEC_ID_FIRST_SUBTITLE ) 
			continue;
		
		if( x->avformat_ctx->streams[i]->codec->codec_type < CODEC_ID_FIRST_AUDIO )
		{
				int sup_codec = 0;
				for( j = 0; _supported_codecs[j].name != NULL; j ++ ) {
					if( x->avformat_ctx->streams[i]->codec->codec_id == _supported_codecs[j].id ) {
						sup_codec = 1;
						goto further;
					}
				}	
further:
				if( !sup_codec ) {
					avhelper_close_input_file( x->avformat_ctx );
					free(x);
					return NULL;
				}
				x->codec = avcodec_find_decoder( x->avformat_ctx->streams[i]->codec->codec_id );
				if(x->codec == NULL ) 
				{
					avhelper_close_input_file( x->avformat_ctx );
					free(x);
					return NULL;
				}
				vi = i;

				veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg: video stream %d, codec_id %d", vi, x->avformat_ctx->streams[i]->codec->codec_id);

				break;
		}
	}

	if( vi == -1 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "FFmpeg: No video streams found");
		avhelper_close_input_file( x->avformat_ctx );
		free(x);
		return NULL;
	}

	x->codec_ctx = x->avformat_ctx->streams[vi]->codec;

	int wid = dst_width;
	int hei = dst_height;

	if( wid == -1 && hei == -1 ) {
		wid = x->codec_ctx->width;
		hei = x->codec_ctx->height;
	}

#if LIBAVCODEC_BUILD > 5400
	if ( avcodec_open2( x->codec_ctx, x->codec, NULL ) < 0 )
#else
	if ( avcodec_open( x->codec_ctx, x->codec ) < 0 ) 
#endif
	{
		avhelper_close_input_file( x->avformat_ctx );
		free(x);
		return NULL;
	}

	veejay_memset( &(x->pkt), 0, sizeof(AVPacket));
	AVFrame *f = avcodec_alloc_frame();
	x->output = yuv_yuv_template( NULL,NULL,NULL, wid, hei, dst_pixfmt );

	int got_picture = 0;
	while(1) {
	    int ret = av_read_frame(x->avformat_ctx, &(x->pkt));
		if( ret < 0 )
			break;

		if ( x->pkt.stream_index == vi ) {
			avcodec_decode_video( x->codec_ctx,f,&got_picture, x->pkt.data, x->pkt.size );
			avhelper_frame_unref( f );
		}
				
		av_free_packet( &(x->pkt) );	

		if( got_picture )
			break;
	}
	av_free(f);

	if(!got_picture) {
		veejay_msg(VEEJAY_MSG_ERROR, "FFmpeg: Unable to get whole picture from %s", filename );
		avcodec_close( x->codec_ctx );
		avhelper_close_input_file( x->avformat_ctx );
		free(x->output);
		free(x);
		return NULL;
	}

	x->pixfmt = x->codec_ctx->pix_fmt;
	x->codec_id = x->codec_ctx->codec_id;
	x->frame = avcodec_alloc_frame();
	x->input = yuv_yuv_template( NULL,NULL,NULL, x->codec_ctx->width,x->codec_ctx->height, x->pixfmt );

	sws_template sws_tem;
    veejay_memset(&sws_tem, 0,sizeof(sws_template));
    sws_tem.flags = yuv_which_scaler();
    x->scaler = yuv_init_swscaler( x->input,x->output, &sws_tem, yuv_sws_get_cpu_flags());
	
	if( x->scaler == NULL ) {
		veejay_msg(VEEJAY_MSG_ERROR,"FFmpeg: Failed to get scaler context for %dx%d in %d to %dx%d in %d",
				x->codec_ctx->width,x->codec_ctx->height, x->pixfmt,
				wid,hei,dst_pixfmt);
		av_free(f);
		avcodec_close( x->codec_ctx );
		avhelper_close_input_file( x->avformat_ctx );
		free(x->output);
		free(x->input);
		free(x);
		return NULL;
	}
	
	return (void*) x;
}

void	avhelper_close_decoder( void *ptr ) 
{
	el_decoder_t *e = (el_decoder_t*) ptr;
	avcodec_close( e->codec_ctx );
	avhelper_close_input_file( e->avformat_ctx );
	yuv_free_swscaler( e->scaler );
	if(e->input)
		free(e->input);
	if(e->output)
		free(e->output);
	if(e->frame)
		av_free(e->frame);
	free(e);
}

int	avhelper_decode_video( void *ptr, uint8_t *data, int len, uint8_t *dst[3] ) 
{
	int got_picture = 0;
	el_decoder_t * e = (el_decoder_t*) ptr;

	int result = avcodec_decode_video( e->codec_ctx, e->frame, &got_picture, data, len );

	if(!got_picture || result <= 0) {
		avhelper_frame_unref( e->frame );
		return 0;
	}

	e->input->data[0] = e->frame->data[0];
	e->input->data[1] = e->frame->data[1];
	e->input->data[2] = e->frame->data[2];
	e->input->data[3] = e->frame->data[3];

	e->output->data[0] = dst[0];
	e->output->data[1] = dst[1];
	e->output->data[2] = dst[2];

	yuv_convert_any3( e->scaler, e->input, e->frame->linesize, e->output, e->input->format, e->pixfmt );

	avhelper_frame_unref( e->frame );

	return 1;
}
