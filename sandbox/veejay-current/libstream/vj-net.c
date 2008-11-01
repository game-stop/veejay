/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2006 Niels Elburg <nelburg@looze.net>
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

#define THREAD_START 0
#define THREAD_STOP 1
#include <config.h>
#include <string.h>
#include <libstream/vj-tag.h>
#include <libvjnet/vj-client.h>
#include <libvjnet/common.h>
#include <libvjmsg/vj-common.h>
#include <veejay/vims.h>
#include <libstream/vj-net.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
typedef struct
{
	pthread_mutex_t mutex;
	pthread_t thread;
	vj_client      *remote;
	int state;
	int have_frame;
	int error;
	int grab;
	int repeat;
} threaded_t;

static void lock_(threaded_t *t, const char *f, int line)
{
//	veejay_msg(0,"lock thread by %s, line %d",f,line);
	pthread_mutex_lock( &(t->mutex ));
}

static void unlock_(threaded_t *t, const char *f, int line)
{
//	veejay_msg(0,"unlock thread by %s, line %d",f,line);
	pthread_mutex_unlock( &(t->mutex ));
}

#define lock( t ) lock_( t, __FUNCTION__, __LINE__ )
#define unlock( t ) unlock_( t, __FUNCTION__ , __LINE__ )


void	*reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->priv;
	vj_client *v = t->remote;
	int ret = 0;
	char buf[16];
#ifdef STRICT_CHECKING
	assert( v != NULL );
#endif

	sprintf(buf, "%03d:;", VIMS_GET_FRAME);

	int retrieve = 0;	
	for( ;; )
	{
		int error    = 0;

		if( t->state == 0 )
		{
			error = 1;
		}
		
		lock(t);
		
		if( t->grab && tag->source_type == VJ_TAG_TYPE_NET && retrieve== 0)
		{
			ret = vj_client_poll_w(v , V_CMD );
			if( ret )
			{
				ret =  vj_client_send( v, V_CMD, buf );
				if( ret <= 0 )
				{
					error = 1;
				}
				else
				{
					t->grab = 0;
					retrieve = 1;
				}
			}
		} 

		if (tag->source_type == VJ_TAG_TYPE_MCAST )
		{
			error = 0;
			retrieve = 1;
		}
	
		int wait_time = 0;
	
		if(!error && retrieve)
		{
			if( vj_client_poll(v, V_CMD ) )
			{
				ret = vj_client_read_i ( v, tag->socket_frame,tag->socket_len );
				if( ret <= 0 )
				{
					if( tag->source_type == VJ_TAG_TYPE_NET )
					{
						error = 1;
					}
					else
					{
						wait_time = 1000;
					}
					ret = 0;
				}
				else
				{
					 t->have_frame = ret;
					 t->grab = 1;
					 retrieve =0;
				}
			}
			else
			{
				if(tag->source_type == VJ_TAG_TYPE_MCAST )
					wait_time = 15000;
			}
		}
		unlock(t);

		if( wait_time )
		{
			usleep(wait_time);
		}

		if( error )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"Closing connection with remote veejay,");
			t->state = 0;
			t->grab = 0;
			pthread_exit( &(t->thread));
			return NULL;
		}
	}
	return NULL;
}



void	*net_threader( )
{
	threaded_t *t = (threaded_t*) vj_calloc(sizeof(threaded_t));
	return (void*) t;
}

int	net_thread_get_frame( vj_tag *tag, uint8_t *buffer[3] )
{
	threaded_t *t = (threaded_t*) tag->priv;
	vj_client *v = t->remote;
	const uint8_t *buf = tag->socket_frame;
	
	lock(t);
	if( t->state == 0 || t->error  )
	{
		if(t->repeat < 0)
			veejay_msg(VEEJAY_MSG_INFO, "Connection closed with remote host");
		t->repeat++;
		unlock(t);
		return 0;
	}

	//@ color space convert frame	
	int len = v->cur_width * v->cur_height;
	int uv_len = len;
	switch(v->cur_fmt)
	{
		case FMT_420:
		case FMT_420F:
			uv_len=len/4;
		break;
		default:
			uv_len=len/2;
		break;
	}

	if(t->have_frame == 1 )
	{
		veejay_memcpy(buffer[0], tag->socket_frame, len );
		veejay_memcpy(buffer[1], tag->socket_frame+len, uv_len );
		veejay_memcpy(buffer[2], tag->socket_frame+len+uv_len, uv_len );
		t->grab = 1;
		unlock(t);
		return 1;
	}
	else if(t->have_frame == 2 )
	{
		int b_len = v->in_width * v->in_height;
		int buvlen = b_len;
		switch(v->in_fmt)
		{
			case FMT_420:
			case FMT_420F:
				buvlen = b_len/4;
				break;
			default:
				buvlen = b_len/2;
				break;
		}
	
		VJFrame *a = yuv_yuv_template( tag->socket_frame, tag->socket_frame + b_len, tag->socket_frame+b_len+buvlen,
						v->in_width,v->in_height, get_ffmpeg_pixfmt( v->in_fmt ));
		VJFrame *b = yuv_yuv_template( buffer[0],buffer[1], buffer[2], 
						v->cur_width,v->cur_height,get_ffmpeg_pixfmt(v->cur_fmt));
		yuv_convert_any(a,b, a->format,b->format );
		free(a);
		free(b);
	}	
	t->grab = 1;
	
	unlock(t);
	return 1;
}

int	net_thread_start(vj_client *v, vj_tag *tag)
{
	int success = 0;
	int res = 0;

	if(tag->source_type == VJ_TAG_TYPE_MCAST )
		success = vj_client_connect( v,NULL,tag->source_name,tag->video_channel  );
	else
		success = vj_client_connect_dat( v, tag->source_name,tag->video_channel );
	
	if( success <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to establish connection with %s on port %d",
				tag->source_name, tag->video_channel + 5);
		return 0;
	}
	else
	{
		veejay_msg(VEEJAY_MSG_INFO, "Connecton established with %s:%d",tag->source_name,
				tag->video_channel + 5);
	}
	
	threaded_t *t = (threaded_t*)tag->priv;

	pthread_mutex_init( &(t->mutex), NULL );
	v->lzo = lzo_new();
	t->repeat = 0;
	t->have_frame = 0;
	t->error = 0;
	t->state = 1;
	t->remote = v;
	t->grab = 1;
	if( tag->source_type == VJ_TAG_TYPE_MCAST )
	{
		char start_mcast[6];
		sprintf(start_mcast, "%03d:;", VIMS_VIDEO_MCAST_START);
		
		veejay_msg(VEEJAY_MSG_DEBUG, "Request mcast stream from %s port %d",
				tag->source_name, tag->video_channel);
		
		res = vj_client_send( v, V_CMD, start_mcast );
		if( res <= 0 )
		{
			veejay_msg(VEEJAY_MSG_ERROR, "Unable to send to %s port %d",
					tag->source_name, tag->video_channel );
			return 0;
		}
		else
			veejay_msg(VEEJAY_MSG_INFO, "Requested mcast stream from Veejay group %s port %d",
					tag->source_name, tag->video_channel );	
	}
	int p_err = pthread_create( &(t->thread), NULL, &reader_thread, (void*) tag );
	if( p_err ==0)

	{
		veejay_msg(VEEJAY_MSG_INFO, "Created new %s threaded stream with Veejay host %s port %d",
			tag->source_type == VJ_TAG_TYPE_MCAST ? 
			"multicast" : "unicast", tag->source_name,tag->video_channel);
		return 1;
	}

	return 0;		
}

void	net_thread_stop(vj_tag *tag)
{
	char mcast_stop[6];
	threaded_t *t = (threaded_t*)tag->priv;
	int ret = 0;
	lock(t);
	
	if(tag->source_type == VJ_TAG_TYPE_MCAST)
	{
		sprintf(mcast_stop, "%03d:;", VIMS_VIDEO_MCAST_STOP );
		ret = vj_client_send( t->remote , V_CMD, mcast_stop);
		if(ret)
			veejay_msg(VEEJAY_MSG_INFO, "Stopped multicast stream");
	}
	if(tag->source_type == VJ_TAG_TYPE_NET)
	{
		sprintf(mcast_stop, "%03d:;", VIMS_CLOSE );
		ret = vj_client_send( t->remote, V_CMD, mcast_stop);
		if(ret)
			veejay_msg(VEEJAY_MSG_INFO, "Stopped unicast stream");
	}
	
	t->state = 0;

	unlock(t);
	
	pthread_mutex_destroy( &(t->mutex));

	veejay_msg(VEEJAY_MSG_INFO, "Disconnected from Veejay host %s:%d", tag->source_name,
		tag->video_channel);
}

int	net_already_opened(const char *filename, int n, int channel)
{
	char sourcename[255];
	int i;
	for (i = 1; i < n; i++)
	{	
		if (vj_tag_exists(i) )
		{
		    vj_tag_get_source_name(i, sourcename);
		    if (strcasecmp(sourcename, filename) == 0)
		    {
			vj_tag *tt = vj_tag_get( i );
			if( tt->source_type == VJ_TAG_TYPE_NET || tt->source_type == VJ_TAG_TYPE_MCAST )
			{
				if( tt->video_channel == channel )
				{
					veejay_msg(VEEJAY_MSG_WARNING, "Already streaming from %s:%p in stream %d",
						filename,channel, tt->id);
					return 1;
				}
			}
	    	    }
		}
    	}
	return 0;
}



