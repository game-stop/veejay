/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2006 Niels Elburg <nwelburg@gmail.com>
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
#include <libstream/vj-tag.h>
#include <libvjnet/vj-client.h>
#include <veejay/vims.h>
#include <libyuv/yuvconv.h>
#include <libvjmem/vjmem.h>
#include <libavutil/pixfmt.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vims.h>
#include <libstream/vj-net.h>
#include <liblzo/lzo.h>
#include <time.h>
#include <libyuv/yuvconv.h>

#define        RUP8(num)(((num)+8)&~8)

typedef struct
{
	pthread_mutex_t mutex;
	pthread_t thread;
	int state;
	int have_frame;
	int error;
	int repeat;
	int w;
	int h;
	int f;
	int in_fmt;
	int in_w;
	int in_h;
	int af;
	uint8_t *buf;
	void *scaler;
	VJFrame *a;
	VJFrame *b;
	size_t bufsize;
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

void	*reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->priv;
	
	char buf[16];
	snprintf(buf,sizeof(buf)-1, "%03d:;", VIMS_GET_FRAME);

	int retrieve = 0;
	int success  = 0;
	
	vj_client *v = vj_client_alloc( t->w, t->h, t->af );
	v->lzo = lzo_new();

	success = vj_client_connect_dat( v, tag->source_name,tag->video_channel );

	if( success > 0 ) {
			veejay_msg(VEEJAY_MSG_INFO, "Connecton established with %s:%d",tag->source_name,
				tag->video_channel + 5);
	}
	else if ( tag->source_type != VJ_TAG_TYPE_MCAST ) 
	{
		veejay_msg(0, "Unable to connect to %s: %d", tag->source_name, tag->video_channel+5);
		goto NETTHREADEXIT;
	}

	lock(t);
	t->state = STATE_RUNNING;
	unlock(t);

	for( ;; ) {
		int error    = 0;
		int res      = 0;

		int ret = 0;
	        if( retrieve == 0 && t->have_frame == 0 ) {
			ret = vj_client_send( v, V_CMD,(unsigned char*) buf );
			if( ret <= 0 ) {
				error = 1;
			}
			else {
				retrieve = 1;
			}
		}

		if(!error && retrieve == 1 ) {
			res = vj_client_poll(v, V_CMD );
			if( res ) {	
				if(vj_client_link_can_read( v, V_CMD ) ) {
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
			int ret = 0;
			int strides[3] = { 0,0,0};
			int compr_len = 0;

			if( vj_client_read_frame_header( v, &(t->in_w), &(t->in_h), &(t->in_fmt), &compr_len, &strides[0],&strides[1],&strides[2]) == 0 ) {
				error = 1;
			}
			
			if(!error) {
				int need_rlock = 0;
				if( compr_len <= 0 )
					need_rlock = 1;

				if( need_rlock ) {
					lock(t);
				}

				if( t->bufsize < (t->in_w * t->in_h * 3) || t->buf == NULL ) {
					t->bufsize = t->in_w * t->in_h * 3;
					t->buf = (uint8_t*) realloc( t->buf, RUP8(t->bufsize));
				}

				ret = vj_client_read_frame_data( v, compr_len, strides[0], strides[1], strides[2], t->buf );
				if( ret == 2 ) {
					if(!need_rlock) {
						lock(t);
						vj_client_decompress_frame_data( v, t->buf, t->in_fmt, t->in_w, t->in_h, compr_len, strides[0],strides[1],strides[2] );
						unlock(t);
					}
				}

				if( need_rlock ) {
					unlock(t);
				}
			}

		//	lock(t);
			//t->buf = vj_client_read_i( v, t->buf,&(t->bufsize), &ret );
			if(ret && t->buf) {
				t->have_frame = 1;
                    	        t->in_fmt = v->in_fmt;
                     	        t->in_w   = v->in_width;
                    	        t->in_h   = v->in_height;
				retrieve = 0;
			}
			if( ret <= 0 || t->buf == NULL ) {
				if( tag->source_type == VJ_TAG_TYPE_NET )
				{
					veejay_msg(VEEJAY_MSG_DEBUG,"Error reading video frame from %s:%d",tag->source_name,tag->video_channel );
					error = 1;
				}
			}
		//	unlock(t);
		}
NETTHREADRETRY:

		if( error )
		{
			int success = 0;
			
			vj_client_close(v);

			veejay_msg(VEEJAY_MSG_INFO, " ZZzzzzz ... waiting for Link %s:%d to become ready", tag->source_name, tag->video_channel );
			net_delay( 0, 5 );

			if(tag->source_type == VJ_TAG_TYPE_MCAST )
				success = vj_client_connect( v,NULL,tag->source_name,tag->video_channel  );
			else
				success = vj_client_connect_dat( v, tag->source_name,tag->video_channel );
	
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

	if(t->buf)
		free(t->buf);
	t->buf = NULL;
	if(v) {
	       	vj_client_close(v);
		vj_client_free(v);
		v = NULL;
	}

	veejay_msg(VEEJAY_MSG_INFO, "Network thread with %s: %d has exited",tag->source_name,tag->video_channel+5);
	//pthread_exit( &(t->thread));

	return NULL;
}

static void	net_thread_free(vj_tag *tag)
{
	threaded_t *t = (threaded_t*) tag->priv;

	if( t->scaler )
		yuv_free_swscaler( t->scaler );

	if( t->a )
		free(t->a);
	if( t->b )
		free(t->b);

	t->a = NULL;
	t->b = NULL;
	t->scaler = NULL;
}	

void	*net_threader(VJFrame *frame)
{
	threaded_t *t = (threaded_t*) vj_calloc(sizeof(threaded_t));
	return (void*) t;
}

int	net_thread_get_frame( vj_tag *tag, uint8_t *buffer[3] )
{
	threaded_t *t = (threaded_t*) tag->priv;
	
	int state = 0;

	/* frame ready ? */
	lock(t);
	state = t->state;
	if( state == 0 || t->bufsize == 0 || t->buf == NULL ) {
		unlock(t);
		return 1; // not active or no frame
	}	// just continue when t->have_frame == 0

	//@ color space convert frame	
	int b_len = t->in_w * t->in_h;
	int buvlen = b_len;

	if( PIX_FMT_YUV420P == t->in_fmt || PIX_FMT_YUVJ420P == t->in_fmt )
		buvlen = b_len/4;
	else
		buvlen = b_len/2;
	
	if( t->a == NULL )
		t->a = yuv_yuv_template( t->buf, t->buf + b_len, t->buf+ b_len+ buvlen,t->in_w,t->in_h, t->in_fmt);
	
	if( t->b == NULL ) 
		t->b = yuv_yuv_template( buffer[0],buffer[1], buffer[2],t->w,t->h,t->f);
	
	if( t->scaler == NULL ) {
		sws_template sws_templ;
		memset( &sws_templ, 0, sizeof(sws_template));
		sws_templ.flags = yuv_which_scaler();
		t->scaler = yuv_init_swscaler( t->a,t->b, &sws_templ, yuv_sws_get_cpu_flags() );
	}

	yuv_convert_and_scale( t->scaler, t->a,t->b );
	
	t->have_frame = 0;
	unlock(t);

	return 1;
}

int	net_thread_start(vj_tag *tag, int wid, int height, int pixelformat)
{
	if(tag->source_type == VJ_TAG_TYPE_MCAST ) {
		veejay_msg(0, "Not in this version");
		return 0;
	}
	
	threaded_t *t = (threaded_t*)tag->priv;

	pthread_mutex_init( &(t->mutex), NULL );
	t->w = wid;
	t->h = height;
	t->af = pixelformat;
	t->f = get_ffmpeg_pixfmt(pixelformat);
	t->have_frame = 0;

	int p_err = pthread_create( &(t->thread), NULL, &reader_thread, (void*) tag );
	if( p_err ==0)

	{
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

	net_thread_free(tag);

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



