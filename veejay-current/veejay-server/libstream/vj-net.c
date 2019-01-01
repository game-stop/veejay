/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include <stdint.h>
#include <pthread.h>
#include <libavcodec/avcodec.h>
#include <libstream/vj-tag.h>
#include <libvjnet/vj-client.h>
#include <veejay/vims.h>
#include <libyuv/yuvconv.h>
#include <libvjmem/vjmem.h>
#include <libavutil/pixfmt.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vims.h>
#include <libstream/vj-net.h>
#include <time.h>
#include <libyuv/yuvconv.h>
#include <libel/avcommon.h>
#include <libel/avhelper.h>
#include <libvje/effects/common.h>

typedef struct
{
	pthread_mutex_t mutex;
	pthread_t thread;
	int state;
	int have_frame;
	vj_client *v;
	VJFrame *info;
	VJFrame *dest; // there is no full range YUV + alpha in PIX_FMT family
	void *scaler;
} threaded_t;

#define STATE_INACTIVE 0
#define STATE_RUNNING  1

static int lock(threaded_t *t)
{
	return pthread_mutex_lock( &(t->mutex ));
}

static int unlock(threaded_t *t)
{
	return pthread_mutex_unlock( &(t->mutex ));
}

#define MS_TO_NANO(a) (a *= 1000000)
static	void	net_delay(long ms, long sec )
{
	struct timespec ts;
	ts.tv_sec = sec;
	ts.tv_nsec = MS_TO_NANO( ms );
	clock_nanosleep( CLOCK_REALTIME,0, &ts, NULL );
}

static int my_screen_id = -1;
void  net_set_screen_id(int id)
{
	my_screen_id = id;
	veejay_msg(VEEJAY_MSG_DEBUG,"Network stream bound to screen %d", id );
}

static void	*reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->priv;
	char buf[16];
	int retrieve = 0;
	int success  = 0;

	t->v = vj_client_alloc_stream(t->info);
	if(t->v == NULL) {
		return NULL;
	}

	snprintf(buf,sizeof(buf), "%03d:%d;", VIMS_GET_FRAME, my_screen_id);
	success = vj_client_connect_dat( t->v, tag->source_name,tag->video_channel );
	
	if( success > 0 ) {
		veejay_msg(VEEJAY_MSG_INFO, "Connecton established with %s:%d",tag->source_name, tag->video_channel + 5);
	}
	else 
	{
		veejay_msg(0, "Unable to connect to %s: %d", tag->source_name, tag->video_channel+5);
		goto NETTHREADEXIT;
	}

	lock(t);
	t->state = STATE_RUNNING;
	unlock(t);

	for( ;; ) {
		int error	 = 0;
		int res		 = 0;
		int ret = 0;
		
		if( retrieve == 0 && t->have_frame == 0 ) {
			ret = vj_client_send( t->v, V_CMD,(unsigned char*) buf );
			if( ret <= 0 ) {
				error = 1;
			}
			else {
				retrieve = 1;
			}
		}

		if(!error && retrieve == 1 ) {
			res = vj_client_poll(t->v, V_CMD );
			if( res ) {	
				if(vj_client_link_can_read( t->v, V_CMD ) ) {
					retrieve = 2;
				}
			} 
			else if ( res < 0 ) {
				error = 1;
			} else if ( res == 0 ) {
				net_delay(10,0);
				continue;
			}
		}

		if(!error && retrieve == 2) {
			int frame_len = vj_client_read_frame_hdr( t->v );
			if( frame_len <= 0 ) {
				error = 1;
			}

			if( error == 0 ) {
				lock(t);
				ret = vj_client_read_frame_data( t->v, frame_len );
				unlock(t);
			}

			if(ret) {
				t->have_frame = 1;
				retrieve = 0;
			}

			if( ret <= 0 ) {
				veejay_msg(VEEJAY_MSG_DEBUG,"Error reading video frame from %s:%d",tag->source_name,tag->video_channel );
				error = 1;
			}
		}
NETTHREADRETRY:

		if( error )
		{
			vj_client_close(t->v);

			veejay_msg(VEEJAY_MSG_INFO, " ZZzzzzz ... waiting for Link %s:%d to become ready", tag->source_name, tag->video_channel );
			net_delay( 0, 5 );

			success = vj_client_connect_dat( t->v, tag->source_name,tag->video_channel );
	
			if( t->state == 0 )
			{
				veejay_msg(VEEJAY_MSG_INFO, "Network thread with %s: %d was told to exit",tag->source_name,tag->video_channel+5);
				goto NETTHREADEXIT;
			}

			if( success <= 0 )
			{
				goto NETTHREADRETRY;
			}
			else
			{
				veejay_msg(VEEJAY_MSG_INFO, "Connecton re-established with %s:%d",tag->source_name,tag->video_channel + 5);
			}
		
			retrieve = 0;
		}


		if( t->state == 0 )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Network thread with %s: %d was told to exit",tag->source_name,tag->video_channel+5);
			goto NETTHREADEXIT;
		}
	}

NETTHREADEXIT:
	
	if(t->v) {
		vj_client_close(t->v);
		vj_client_free(t->v);
		t->v = NULL;
	}

	veejay_msg(VEEJAY_MSG_INFO, "Network thread with %s: %d has exited",tag->source_name,tag->video_channel+5);

	return NULL;
}


static void	*mcast_reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->priv;
	int success  = 0;

	t->v = vj_client_alloc_stream(t->info);
	if(t->v == NULL) {
		return NULL;
	}

	success = vj_client_connect( t->v, NULL, tag->source_name, tag->video_channel );

	if( success > 0 ) {
		veejay_msg(VEEJAY_MSG_INFO, "Multicast connecton established with %s:%d",tag->source_name, tag->video_channel + 5);
	}
	else 
	{
		veejay_msg(0, "Unable to connect to %s: %d", tag->source_name, tag->video_channel+5);
		goto NETTHREADEXIT;
	}

	lock(t);
	t->state = STATE_RUNNING;
	unlock(t);

	const int padded = 256;
	int max_len = padded + RUP8( 1920 * 1080 * 3 );
		
	for( ;; ) {
		int error = 0;
			
		if( vj_client_read_mcast_data( t->v, max_len ) < 0 ) {
			error = 1;
		}
	
		if(error == 0) {
			t->have_frame = 1;
		}

NETTHREADRETRY:

		if( error )
		{
			vj_client_close(t->v);

			veejay_msg(VEEJAY_MSG_INFO, " ZZzzzzz ... waiting for multicast server %s:%d to become ready", tag->source_name, tag->video_channel );
			net_delay( 0, 5 );

			success = vj_client_connect( t->v,NULL,tag->source_name,tag->video_channel  );
	
			if( t->state == 0 )
			{
				veejay_msg(VEEJAY_MSG_INFO, "Multicast receiver %s: %d was told to stop",tag->source_name,tag->video_channel+5);
				goto NETTHREADEXIT;
			}

			if( success <= 0 )
			{
				goto NETTHREADRETRY;
			}
			else
			{
				veejay_msg(VEEJAY_MSG_INFO, "Multicast receiver has re-established with %s:%d",tag->source_name,tag->video_channel + 5);
			}
		}

		if( t->state == 0 )
		{
			veejay_msg(VEEJAY_MSG_INFO, "Multicast receiver %s: %d was told to stop",tag->source_name,tag->video_channel+5);
			goto NETTHREADEXIT;
		}
	}

NETTHREADEXIT:
	
	if(t->v) {
		vj_client_close(t->v);
		vj_client_free(t->v);
		t->v = NULL;
	}

	veejay_msg(VEEJAY_MSG_INFO, "Multicast receiver %s: %d has stopped",tag->source_name,tag->video_channel+5);

	return NULL;
}

void	*net_threader(VJFrame *frame)
{
	threaded_t *t = (threaded_t*) vj_calloc(sizeof(threaded_t));
	return (void*) t;
}

int	net_thread_get_frame( vj_tag *tag, VJFrame *dst )
{
	threaded_t *t = (threaded_t*) tag->priv;
	
	int state = 0;

	/* frame ready ? */
	lock(t);
	state = t->state;
	if( state == 0 || t->have_frame == 0 ) {
		unlock(t);
		return 1; // not active or no frame
	}	

	VJFrame *src = avhelper_get_decoded_video(t->v->decoder);

	if(t->scaler == NULL) {
		sws_template sws_templ;
		memset( &sws_templ, 0, sizeof(sws_template));
		sws_templ.flags = yuv_which_scaler();
		t->scaler = yuv_init_swscaler( src,t->dest, &sws_templ, yuv_sws_get_cpu_flags() );
	}
	
	yuv_convert_and_scale( t->scaler, src,dst );
	
	t->have_frame = 0; // frame is consumed
	
	unlock(t);

	return 1;
}

int	net_thread_start(vj_tag *tag, VJFrame *info)
{
	threaded_t *t = (threaded_t*)tag->priv;
	int p_err = 0;

	t->dest = yuv_yuv_template(NULL,NULL,NULL, info->width,info->height, alpha_fmt_to_yuv(info->format));
	if(t->dest == NULL) {
		return 0;
	}

	pthread_mutex_init( &(t->mutex), NULL );
	
	t->have_frame = 0;
	t->info = info;


	if( tag->source_type == VJ_TAG_TYPE_MCAST ) {
		p_err = pthread_create( &(t->thread), NULL, &mcast_reader_thread, (void*) tag );
	}
	else {
		p_err = pthread_create( &(t->thread), NULL, &reader_thread, (void*) tag );
	}
		
	if( p_err ==0) {
		veejay_msg(VEEJAY_MSG_INFO, "Created new %s threaded stream to veejay host %s port %d",
			tag->source_type == VJ_TAG_TYPE_MCAST ? 
			"multicast" : "unicast", tag->source_name,tag->video_channel);
	}

	return 1; 
}

void	net_thread_stop(vj_tag *tag)
{
	threaded_t *t = (threaded_t*)tag->priv;
	
	lock(t);
	if( t->state == 0 ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Stream was already stopped");
		unlock(t);
		return;
	}
	t->state = 0;
	unlock(t);
	
	pthread_mutex_destroy( &(t->mutex));
	if(t->dest) {
		free(t->dest);
	}

	veejay_msg(VEEJAY_MSG_INFO, "Disconnected from Veejay host %s:%d", tag->source_name,tag->video_channel);
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

