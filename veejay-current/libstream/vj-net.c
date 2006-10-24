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

typedef struct
{
//	pthread_mutex_t mutex;
//	pthread_t thread;
	vj_client      *remote;
	int state;
	int error;
} threaded_t;
/*
#define STOP 0
#define PLAY 1

static void lock_(threaded_t *t, const char *f, int line)
{
//	veejay_msg(0,"lock thread by %s, line %d",f,line);
	pthread_mutex_lock( &(t->mutex ));
}

static void unlock_(threaded_t *t, const char *f, int line)
{
//	veejay_msg(0,"unlock thread by %s, line %d",f,line);
	pthread_mutex_unlock( &(t->mutex ));
}*/

#define lock( t ) lock_( t, __FUNCTION__, __LINE__ )
#define unlock( t ) unlock_( t, __FUNCTION__ , __LINE__ )
/*
void	*reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->private;
	vj_client *v = t->remote;
	int ret = 0;
	int error = 0;
	int have_sent =0;
	char buf[16];
	sprintf(buf, "%03d:;", VIMS_GET_FRAME);

	for( ;; )
	{
		lock(t);
		if( t->state == STOP )
		{
			unlock(t);
			return NULL;
		}
		
		if( tag->source_type == VJ_TAG_TYPE_NET )
		{
			if( vj_client_poll(v, V_STATUS ) )
			{
				vj_client_flush	( v, V_STATUS);
			}	
			ret =  vj_client_send( v, V_CMD, buf );
			if( ret <= 0 )
				error = 1;
		}
		
		if(!error)
		{
			ret = vj_client_read_i ( v, tag->socket_frame );
			if( ret <= 0 )
				error = 1;
		}
		
		if( error )
		{
			veejay_msg(VEEJAY_MSG_ERROR,
					"Error retrieving frame from remote veejay,");
			t->error = THREAD_STOP;
			t->state = STOP;
		}
		unlock(t);
		g_sleep( 25000 );
	}
	return NULL;
}*/



void	*net_threader( )
{
	threaded_t *t = (threaded_t*) vj_calloc(sizeof(threaded_t));
//	pthread_mutex_init( &(t->mutex), NULL );
	return (void*) t;
}

void	net_thread_remote( void *p, void *r )
{
	threaded_t *t = (threaded_t*) p;
	t->remote = r;
}

void	net_thread_exit(vj_tag *tag)
{
/*	threaded_t *t = tag->private;

	lock(t);
*/
	if(tag->socket_frame)
		free(tag->socket_frame);

/*	unlock(t);

	pthread_mutex_destroy( &(t->mutex));
*/	
}

int	net_thread_get_frame( vj_tag *tag, uint8_t *buffer[3], vj_client *v )
{
/*	threaded_t *t = (threaded_t*) tag->private;
	lock(t);
	if( t->state == 0 || t->error  )
	{
		unlock(t);
		return 0;
	}
	
	veejay_memcpy( buffer[0], tag->socket_frame, v->planes[0] );
	veejay_memcpy( buffer[1], tag->socket_frame + v->planes[0], v->planes[1] );
	veejay_memcpy( buffer[2], tag->socket_frame + v->planes[0] + v->planes[1] , v->planes[2] );
	
	unlock(t);
	return 1;*/
	char buf[16];
	sprintf(buf, "%03d:;", VIMS_GET_FRAME);


	if( vj_client_poll(v, V_STATUS ) )
	{
		vj_client_flush	( v, V_STATUS);
	}	
	int ret =  vj_client_send( v, V_CMD, buf );
	if( ret <= 0 )
		return 0;
	
	ret = vj_client_read_i ( v, tag->socket_frame );
	if( ret <= 0 )
		return 0;

	veejay_memcpy( buffer[0], tag->socket_frame, v->planes[0] );
	veejay_memcpy( buffer[1], tag->socket_frame + v->planes[0], v->planes[1] );
	veejay_memcpy( buffer[2], tag->socket_frame + v->planes[0] + v->planes[1] , v->planes[2] );
	
	return ret;
}

int	net_thread_start(vj_client *v, vj_tag *tag)
{
	int success = 0;
	int res = 0;
	if(tag->source_type == VJ_TAG_TYPE_MCAST )
		success = vj_client_connect( v, NULL,tag->source_name,tag->video_channel);
	else
		success = vj_client_connect( v, tag->source_name,NULL,tag->video_channel);
	
	if( success <= 0 )
	{
		veejay_msg(VEEJAY_MSG_ERROR, "Unable to establish connection with %s on port %d",
				tag->source_name, tag->video_channel );
		return 0;
	}

/*	threaded_t *t = (threaded_t*)tag->private;

	t->error = 0;
	t->state = 1;
*/
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
			vj_client_close( v );
			return 0;
		}	
	}
/*	int p_err = pthread_create( &(t->thread), NULL, &reader_thread, (void*) tag );
	if( p_err ==0)

	{*/
	veejay_msg(VEEJAY_MSG_INFO, "Created new %s stream",
			tag->source_type == VJ_TAG_TYPE_MCAST ? 
			"multicast" : "unicast");
	return 1;
//	}
//	return 0;		
}

void	net_thread_stop(vj_client *v , vj_tag *tag)
{
	char mcast_stop[6];
	threaded_t *t = (threaded_t*)tag->private;
	int ret = 0;
//	lock(t);
	
	if(tag->source_type == VJ_TAG_TYPE_MCAST)
	{
		sprintf(mcast_stop, "%03d:;", VIMS_VIDEO_MCAST_STOP );
		ret = vj_client_send( v , V_CMD, mcast_stop);
	}
	if(tag->source_type == VJ_TAG_TYPE_NET)
	{
		sprintf(mcast_stop, "%03d:;", VIMS_CLOSE );
		ret = vj_client_send( v, V_CMD, 	mcast_stop);
	}
	
//	t->state = STOP;

//	unlock(t);
//	pthread_join( &(t->thread), (void*) tag );
		
	vj_client_close( v );

	veejay_msg(VEEJAY_MSG_INFO, "Disconnected from %s", tag->source_name);
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
		    if (strcmp(sourcename, filename) == 0)
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



