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

#include <config.h>
#include <stdint.h>
#include <pthread.h>
#include <libavcodec/avcodec.h>
#include <veejaycore/defs.h>
#include <libstream/vj-tag.h>
#include <veejaycore/vj-client.h>
#include <veejaycore/vims.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/vjmem.h>
#include <libavutil/pixfmt.h>
#include <veejaycore/vj-msg.h>
#include <libstream/vj-net.h>
#include <time.h>
#include <veejaycore/yuvconv.h>
#include <libel/avcommon.h>
#include <libel/avhelper.h>
#include <libvje/effects/common.h>

typedef struct
{
	pthread_mutex_t mutex;
	pthread_t thread;
	int have_frame;
	int state;
	vj_client *v;
	VJFrame *info;
	VJFrame *dest; // there is no full range YUV + alpha in PIX_FMT family
	void *scaler;
} threaded_t;

#define STATE_INACTIVE 0
#define STATE_RUNNING  1
#define STATE_QUIT 2
#define STATE_ERROR 4
#define MS_TO_NANO(a) (a *= 1000000)
static	void	net_delay(long ms, long sec )
{
	struct timespec ts;
	ts.tv_sec = sec;
	ts.tv_nsec = MS_TO_NANO( ms );
	clock_nanosleep( CLOCK_MONOTONIC,0, &ts, NULL );
}

static int my_screen_id = -1;
void  net_set_screen_id(int id)
{
	my_screen_id = id;
	veejay_msg(VEEJAY_MSG_DEBUG,"Network stream bound to screen %d", id );
}


static int lock(threaded_t *t)
{
	return pthread_mutex_lock( &(t->mutex ));
}

static int unlock(threaded_t *t)
{
	return pthread_mutex_unlock( &(t->mutex ));
}

static void close_client(threaded_t *t)
{
    if(t->v != NULL) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Closing connection to remote veejay");
        vj_client_close(t->v);
        vj_client_free(t->v);
        t->v = NULL;
    }
}

static int connect_client(threaded_t *t, vj_tag *tag)
{
    veejay_msg(VEEJAY_MSG_INFO, " ... Waiting for network stream to become ready [%s]",tag->source_name);

    if(t->v == NULL) {
        t->v = vj_client_alloc_stream(t->info);
    }

    int success = vj_client_connect_dat( t->v, tag->source_name,tag->video_channel );
    if(success <= 0) {
        veejay_msg(VEEJAY_MSG_ERROR,"Unable to connect to %s:%d", tag->source_name, tag->video_channel + 5 );
        return STATE_ERROR;
    }
       
    veejay_msg(VEEJAY_MSG_INFO, "Connection established with %s:%d",tag->source_name, tag->video_channel + 5);
    return STATE_RUNNING;
}

static int connect_client_mcast(threaded_t *t, vj_tag *tag)
{
    veejay_msg(VEEJAY_MSG_INFO, " ... Waiting for network stream to become ready [%s]",tag->source_name);

    if(t->v == NULL) {
        t->v = vj_client_alloc_stream(t->info);
    }

    int success = vj_client_connect( t->v, NULL, tag->source_name,tag->video_channel );
    if(success <= 0) {
        veejay_msg(VEEJAY_MSG_ERROR,"Unable to connect to %s:%d", tag->source_name, tag->video_channel + 5 );
        return STATE_ERROR;
    }
       
    veejay_msg(VEEJAY_MSG_INFO, "Connection established with %s:%d",tag->source_name, tag->video_channel + 5);
    return STATE_RUNNING;
}

static int eval_state(threaded_t *t, vj_tag *tag)
{
    int ret = t->state;
    if(t->state == STATE_INACTIVE) {
        ret = connect_client(t,tag);
    }
	return ret;
}

static int eval_state_mcast(threaded_t *t, vj_tag *tag) 
{
    int ret = t->state;
	if(t->state == STATE_INACTIVE) {
	    ret = connect_client_mcast(t,tag);
    }
	return ret;
}


static void	*reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->priv;
	char buf[16];
	int retrieve = 0;

	snprintf(buf,sizeof(buf), "%03d:%d;", VIMS_GET_FRAME, my_screen_id);

	for( ;; ) {
		int error	 = 0;
		int res		 = 0;
		int ret = 0;

        lock(t);
		t->state = eval_state(t, tag);
        unlock(t);

		switch(t->state) {
            case STATE_ERROR:
                close_client(t);
                lock(t);
                t->state = STATE_INACTIVE;
                unlock(t);
                net_delay(0,1);
            break;
			case STATE_INACTIVE:
				net_delay(10,0);
				retrieve = 0;
				continue;
			break;
			case STATE_RUNNING:
				if( retrieve == 0 ) {
					ret = vj_client_send( t->v, V_CMD,(unsigned char*) buf );
					if( ret <= 0 ) {
						error = 1;
					}
					else {
						retrieve = 1; /* queried a frame, try to fetch it */
					}
				}
				if(!error && retrieve == 1 ) {
					res = vj_client_poll(t->v, V_CMD );
					if( res ) {	
						if(vj_client_link_can_read( t->v, V_CMD ) ) {
							retrieve = 2; /* data waiting at socket */
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
						if(ret) {
							t->have_frame = 1;
						}
						unlock(t);
					}

					if(ret) {
						retrieve = 0;
					}
                    else {
						veejay_msg(VEEJAY_MSG_DEBUG,"Error reading video frame from %s:%d",tag->source_name,tag->video_channel );
						error = 1;
					}
				}
				if( error ) {
					lock(t);
					t->state = STATE_ERROR;
					t->have_frame = 0;
					unlock(t);
					retrieve = 0;

				}

				break;
				case STATE_QUIT:
				{
					goto NETTHREADEXIT;
				}
		}

	}

NETTHREADEXIT:

    close_client(t);

	veejay_msg(VEEJAY_MSG_INFO, "Network thread with %s: %d has exited",tag->source_name,tag->video_channel+5);

	pthread_exit(NULL);

	return NULL;
}


static void	*mcast_reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->priv;
	const int padded = 256;
	int max_len = padded + RUP8( 1920 * 1080 * 3 );
		
	for( ;; ) {
		int error = 0;

		lock(t);
		t->state = eval_state_mcast(t, tag);
        unlock(t);
		
        switch(t->state) {
            case STATE_ERROR:
                close_client(t);
                lock(t);
                t->state = STATE_INACTIVE;
                unlock(t);

                 net_delay(0,1);
                 break;
			case STATE_INACTIVE:
				net_delay(10,0);
				continue;
				break;
			case STATE_RUNNING:
				lock(t);
				if( vj_client_read_mcast_data( t->v, max_len ) < 0 ) {
					error = 1;
				}
				
                if( error ) {
					t->state = STATE_ERROR;
					t->have_frame = 0;
				}
                else {
                    t->have_frame = 1;
                }

				unlock(t);

				break;
			case STATE_QUIT:
				goto NETTHREADEXIT;
				break;
		}
	}

NETTHREADEXIT:
	
    close_client(t);
	
	veejay_msg(VEEJAY_MSG_INFO, "Multicast receiver %s: %d has stopped",tag->source_name,tag->video_channel+5);

	return NULL;
}

int	net_thread_get_frame( vj_tag *tag, VJFrame *dst )
{
	threaded_t *t = (threaded_t*) tag->priv;
	
	/* frame ready ? */
	lock(t);
	if(!(t->state == STATE_RUNNING) || t->have_frame == 0) {
		unlock(t);
		return 1;
	}
	VJFrame *src = avhelper_get_decoded_video(t->v->decoder);

	if(t->scaler == NULL) {
		sws_template sws_templ;
		memset( &sws_templ, 0, sizeof(sws_template));
		sws_templ.flags = yuv_which_scaler();
		t->scaler = yuv_init_swscaler( src,t->dest, &sws_templ, yuv_sws_get_cpu_flags() );
	}
	
	yuv_convert_and_scale( t->scaler, src,dst );
	unlock(t);

	return 1;
}

int	net_thread_start(vj_tag *tag, VJFrame *info)
{
    threaded_t *t = (threaded_t*) vj_calloc(sizeof(threaded_t));
    if(t == NULL ) {
        return 0;
    }

    int p_err = 0;

	t->dest = yuv_yuv_template(NULL,NULL,NULL, info->width,info->height, alpha_fmt_to_yuv(info->format));
	if(t->dest == NULL) {
        free(t);
		return 0;
	}

	pthread_mutex_init( &(t->mutex), NULL );
	
	t->info = info;
    tag->priv = t;

	if( tag->source_type == VJ_TAG_TYPE_MCAST ) {
		p_err = pthread_create( &(t->thread), NULL, &mcast_reader_thread, (void*) tag );
	}
	else {
		p_err = pthread_create( &(t->thread), NULL, &reader_thread, (void*) tag );
	}
		
	if( p_err ==0) {
		veejay_msg(VEEJAY_MSG_INFO, "Created new input stream [%d] (%s) to veejay host %s port %d",
			tag->id,
			tag->source_type == VJ_TAG_TYPE_MCAST ? 
			"multicast" : "unicast", tag->source_name,tag->video_channel);
	}

	return 1; 
}

//FIXME: net_thread_set_state or modular structure

void	net_thread_stop(vj_tag *tag)
{
	int p_err = 0;
	threaded_t *t = (threaded_t*)tag->priv;
	
	lock(t);
	if( t->state == STATE_QUIT ) {
		veejay_msg(VEEJAY_MSG_ERROR, "Stream was already stopped");
		unlock(t);
		return;
	}
	t->state = STATE_QUIT;
	unlock(t);
    pthread_join(t->thread, NULL);
	pthread_mutex_destroy( &(t->mutex));
	if(t->dest) {
		free(t->dest);
		t->dest = NULL;
	}

	free(t);
	tag->priv = NULL;

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

