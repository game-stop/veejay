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
#include <config.h>
#include <veejay/vj-avformat.h>
#include <veejay/vj-misc.h>
#include <libvjmsg/vj-common.h>
#include <string.h>
#if LIBAVFORMAT_BUILD >= 4620
#define m_av_seek_frame( a,b,c,d ) \
 ( av_seek_frame( a,b,c,d ) )
#else
#define m_av_seek_frame(a,b,c,d ) \
 ( av_seek_frame( a,b,c ) )
#endif 

void	vj_avformat_init(void)
{
	av_register_all();
	av_log_set_level(AV_LOG_QUIET);
}

static int64_t vj_avformat_get_timestamp(vj_avformat *av, long nframe )
{
	int64_t ret = ((int64_t)av->time_unit * (int64_t)nframe); // duration of frame in microseconds * nframe
	return ret;
}
/*
static int64_t vj_avformat_get_master_clock(vj_avformat *av)
{
	int64_t delta = ( av_gettime() - av->current_video_pts_time) / 1000000.0;
	return (av->current_video_pts + delta);
}
*/
static int	vj_avformat_set_video_position( vj_avformat *av, long nframe )
{
	int64_t pos = vj_avformat_get_timestamp(av, nframe );
	if( av->seekable ) 
	{
		if(m_av_seek_frame( av->context, av->video_index, pos,0 )==0)
		{
			return 1;
		}
		return 0;
	}	
	return 0;
}

static void	vj_avformat_err(int err)
{
   switch(err)
   {
    case AVERROR_NUMEXPECTED:
        veejay_msg(VEEJAY_MSG_ERROR, "Incorrect image filename syntax.");
        break;
    case AVERROR_INVALIDDATA:
        veejay_msg(VEEJAY_MSG_ERROR, "Error while parsing header");
        break;
    case AVERROR_NOFMT:
        veejay_msg(VEEJAY_MSG_ERROR, "Unknown format");
        break;
    default:
        veejay_msg(VEEJAY_MSG_ERROR,"Error while opening file");
        break;
   }

}

int		vj_avformat_get_audio_rate(vj_avformat *av)
{
	return (int) av->audiocct->sample_rate;
}

int		vj_avformat_get_audio_channels(vj_avformat *av)
{
	return (int) av->audiocct->channels;
}

int		vj_avformat_get_video_width(vj_avformat *av)
{
	return (int)av->cct->width;
}

int		vj_avformat_get_video_height(vj_avformat *av)
{
	return (int)av->cct->height;
}

int		vj_avformat_get_video_inter(vj_avformat *av)
{
	return 0;  
}
int		vj_avformat_get_video_pixfmt(vj_avformat *av)
{
	if(!av->cct) return -1;
	return (int) (av->cct->pix_fmt);
}

int		vj_avformat_get_video_codec(vj_avformat *av)
{
	if(!av->cct) return -1;
	return (int) (av->cct->codec_id);
}

int		vj_avformat_get_video_gop_size(vj_avformat *av)
{
	if(!av->cct) return -1;
	return (int) (av->cct->gop_size);
}

float		vj_avformat_get_video_fps(vj_avformat *av)
{
	return (float) (av->frame_rate/av->frame_rate_base);
}
float		vj_avformat_get_sar_ratio( vj_avformat *av )
{
	return 0.5;
}
long		vj_avformat_get_video_frames( vj_avformat *av )
{
	return (long)( (double)av->stream->duration / 1345.2944 );
}	


vj_avformat *vj_avformat_open_input(const char *filename)
{
	int i;
	vj_avformat *av = (vj_avformat*) vj_malloc(sizeof(struct vj_avformat_t));
	//AVFormatParameters *ap = av->av_format_par;
	int err;
	if(!av) return NULL;

	av->av_input_format = NULL;
	av->av_format_par   = NULL;
	av->stream = NULL;
	av->cct = NULL;
	av->start_time = AV_NOPTS_VALUE;
	av->time_unit = 0;
	av->video_clock = 0;
	av->current_video_pts_time = 0;
	av->current_video_pts = 0;
	av->video_last_P_pts = 0;
	av->expected_timecode = 0;
	av->video_index = -1;
	av->audio_index = -1;
	err = av_open_input_file(
			&(av->context),
			filename,
			av->av_input_format,
			0,
			av->av_format_par );


	if(err < 0 )
	{
		vj_avformat_err(err);
		free(av);
		return NULL;
	}

	err = av_find_stream_info( av->context );
	if( err < 0 )
	{
		vj_avformat_err(err);
	}

	for(i =0 ; i < av->context->nb_streams; i ++)
	{
		AVStream *stream = av->context->streams[i];
		AVCodecContext *cct = &stream->codec;

		if(cct->codec_type == CODEC_TYPE_VIDEO)
		{
			av->video_index = i;
			av->stream = av->context->streams[av->video_index];
			av->cct = &stream->codec;
			av->codec = avcodec_find_decoder(av->cct->codec_id);
			if(!av->codec)
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "Cannot use AVFormat to open this file");
				free(av);       
				return NULL;
			}
			if(av->codec->capabilities & CODEC_CAP_TRUNCATED)
    				av->cct->flags |= CODEC_FLAG_TRUNCATED;

			if(avcodec_open( av->cct, av->codec ) < 0)
			{
			  veejay_msg(VEEJAY_MSG_ERROR, "Cannot open codec");
			}
			av->seekable = 0;

			if(strcmp(av->codec->name, "mjpeg")==0)
				av->seekable = 1;
			if(strcmp(av->codec->name, "dvvideo") == 0)
				av->seekable = 1;

			av->frame_rate = av->cct->frame_rate;
			av->frame_rate_base = av->cct->frame_rate_base;
			/* wrong frame rates that seem to be generated by some codecs */
			if( av->cct->frame_rate > 1000 && av->cct->frame_rate_base==1)
			{
    				av->frame_rate_base=1000;
			}
			if( av->cct->frame_rate_base == 1 && av->cct->frame_rate == 25)
			{
				int base = av->cct->frame_rate;
				// Assuming codec sets frame_rate and frame_rate_base incorrectly
				av->frame_rate = 1000000;
				av->frame_rate_base = av->frame_rate / base; 
			}
			if(av->frame_rate > 1000)
				av->time_unit = (1000000.0 / (float)av->frame_rate) * av->frame_rate_base;
			else
				av->time_unit = av->frame_rate_base;
		}
		if( cct->codec_type == CODEC_TYPE_AUDIO )
		{
			av->audio_index = i;
			av->audio_stream = av->context->streams[av->audio_index];
			av->audiocct = &stream->codec;
			av->audiocodec = avcodec_find_decoder( av->audiocct->codec_id );
			if(!av->audiocodec)
			{
				veejay_msg(VEEJAY_MSG_DEBUG, "Unknown audio format");
				free(av);	
				return NULL;
			} 
			if(avcodec_open( av->audiocct, av->audiocodec ) < 0 )
			{
				veejay_msg(VEEJAY_MSG_ERROR, "Cannot open codec");
			}
			veejay_msg(VEEJAY_MSG_DEBUG,
				"Found audio stream at %d, codec %d", i, av->audiocct->codec_id);
		}
	}


	if(av->video_index == -1)
	{
		if(av) free(av);
		return NULL;
	}

	return av;
}

void	vj_avformat_close_input( vj_avformat *av )
{
	if(av)
	{
		av_close_input_file( av->context );
		if(av) free(av);		
	}
}

int	vj_avformat_get_video_frame( vj_avformat *av, uint8_t *yuv420[3], long nframe, int fmt )
{

	AVPacket packet;
	AVFrame  frame;
	int got_picture = 0;
	int ret = 0;
	int delay = (int) av->time_unit;
	double pts = 0;

	// what frame do we want ?
	if(nframe == -1)
		av->requested_timecode = av->expected_timecode + av->time_unit;
	else
		av->requested_timecode = vj_avformat_get_timestamp(av,nframe);

	// get a master clock like in ffplay.c !!!!

		

	// seek if necessary
	if(av->seekable && av->requested_timecode != (av->time_unit + av->expected_timecode) )
	{
		if( av->requested_timecode != (av->expected_timecode-av->time_unit))
		{
			if(!vj_avformat_set_video_position( av, nframe ))
			{
				return 0;
			}
		}
	}

	memset( &packet, 0, sizeof( packet ) );
	memset( &frame,  0, sizeof( packet ) );

	while( !got_picture && ret>= 0 )
	{
		// read one packet from the media and put in in packet	
		pts = 0;
		ret = av_read_frame( av->context, &packet );
		if( ret < 0)
		{
			if( m_av_seek_frame( av->context,av->video_index ,av->context->start_time,0 ) != 0)
				return 0; 	
			ret = av_read_frame( av->context, &packet );
			if(ret < 0) return 0;
		}

		if( packet.pts != AV_NOPTS_VALUE )
			pts = (double) packet.pts;

		// deal with video from video_index
		if ( ret >= 0 && packet.stream_index == av->video_index && packet.size > 0)
		{

			int video_len = packet.size;
			int tmp_len = 0;
			
			uint8_t *src_ptr = packet.data;

			if(av->cct->codec_id != CODEC_ID_RAWVIDEO)
			{
				while( video_len > 0)
				{
					tmp_len = avcodec_decode_video( av->cct, &frame, &got_picture,
						src_ptr, video_len );
					if(tmp_len < 0)
					{
						return 0;
					}

					video_len -= tmp_len;
					src_ptr += tmp_len;
				}
				if(!got_picture)
					return 0;
			}
			else
			{
				avpicture_fill( (AVPicture *)&frame, src_ptr, av->cct->pix_fmt, av->cct->width, av->cct->height);
				frame.pict_type = FF_I_TYPE;
			}

			if ( pts != 0 )
			{
				av->expected_timecode = pts;
			}
			else
			{
				pts = av->expected_timecode;
			}		

			if ( frame.repeat_pict )
			{
				delay += ( frame.repeat_pict * delay );
			}
			//av->expected_timecode += delay;
		}
		av_free_packet( &packet );
	}

	if(got_picture) 
	{
		AVPicture pict;
		int dst_fmt = (fmt==FMT_420 ? PIX_FMT_YUV420P : PIX_FMT_YUV422P);
		pict.data[0] = yuv420[0];
		pict.data[1] = yuv420[1];
		pict.data[2] = yuv420[2];
		pict.linesize[0] = av->cct->width;
//		if( fmt == FMT_420)
//		{
			pict.linesize[1] = av->cct->width >> 1;
			pict.linesize[2] = av->cct->width >> 1;
//		}
//		else
//		{
//			pict.linesize[1] = av->cct->width;
//			pict.linesize[2] = av->cct->width;
//		}
		img_convert( 	&pict, 
				dst_fmt,
				(const AVPicture*) &frame,	
				av->cct->pix_fmt,	
				av->cct->width,
				av->cct->height );
	}
	return 1;
}


int	vj_avformat_get_audio( vj_avformat *av, uint8_t *dst, long nframe )
{
	return 0;
}
