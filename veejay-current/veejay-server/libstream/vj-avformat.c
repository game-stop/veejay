/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 - 2018 Niels Elburg <nwelburg@gmail.com>
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
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <veejaycore/defs.h>
#include <libavutil/time.h>
#include <libavcodec/avcodec.h>
#include <libstream/vj-tag.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/vjmem.h>
#include <libavutil/pixfmt.h>
#include <veejaycore/vj-msg.h>
#include <libstream/vj-avformat.h>
#include <time.h>
#include <veejaycore/yuvconv.h>
#include <libel/avcommon.h>
#include <libel/avhelper.h>

typedef struct
{
	pthread_mutex_t mutex;
	pthread_mutexattr_t attr;
	pthread_t thread;
	int state;
	int have_frame;
	void *decoder;
	VJFrame *info;
	VJFrame *dest; // there is no full range YUV + alpha in PIX_FMT family
	void *scaler;
	double spvf;
	double elapsed;
} threaded_t;

#define STATE_INACTIVE 0
#define STATE_RUNNING  1
#define STATE_QUIT 2
#define STATE_ERROR 4

static int lock(threaded_t *t)
{
	return pthread_mutex_lock( &(t->mutex) );
}
static int trylock(threaded_t *t)
{
	return pthread_mutex_trylock( &(t->mutex) );
}

static int unlock(threaded_t *t)
{
	return pthread_mutex_unlock( &(t->mutex) );
}

static int eval_state(threaded_t *t, vj_tag *tag)
{
	int ret = 0;
	lock(t);

	if(t->state == STATE_ERROR || (t->decoder == NULL && t->state != STATE_QUIT)) {
			if(t->decoder) {
				avhelper_close_decoder(t->decoder);
				t->decoder = NULL;
			}
			veejay_msg(VEEJAY_MSG_INFO, "[%s] ... Waiting for stream to start playing",tag->source_name);

			t->decoder = avhelper_get_stream_decoder( tag->source_name, t->info->format, t->info->width, t->info->height );
			if(t->decoder != NULL) {
				t->spvf = avhelper_get_spvf(t->decoder);
				t->state = STATE_INACTIVE;

                veejay_msg(VEEJAY_MSG_INFO, "[%s] Ready", tag->source_name );
			} else {
                t->state = STATE_ERROR;
            }
	}

	ret = t->state;

	unlock(t);
	return ret;
}

static void	*reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->priv;

	for( ;; ) {
		int got_picture  = 0;
		int result = 0;
	
		int state_eval = eval_state(t,tag);
		switch(state_eval) {
			case STATE_INACTIVE:
				{
					unsigned int usec = (unsigned int) ( t->spvf  * 1000000.0);
					av_usleep( usec );		
					continue;	
				}
				break;
			case STATE_RUNNING:	
				{
					while( (result = avhelper_recv_frame_packet(t->decoder))== 2) {}
					if( result < 0 ) {
						veejay_msg(0, "[%s] There was an error retrieving the frame", tag->source_name);
						lock(t);
						t->state = STATE_ERROR;
						t->have_frame = 0;
						unlock(t);
					}
					
					if( result == 1 ) {
						lock(t);
						result = avhelper_recv_decode( t->decoder, &got_picture );
						if( result < 0 ) {
							veejay_msg(0,"[%s] There was an error decoding the frame", tag->source_name);
							t->have_frame = 0;
							t->state = STATE_ERROR;
						}
						if( got_picture ) {
							t->have_frame = 1;
						}
						unlock(t);
					}
				}
				break;
			case STATE_ERROR:
				{
					av_usleep(1000000);
				}
				break;
			case STATE_QUIT:
				{
					goto NETTHREADEXIT;
				}
		}
	}

NETTHREADEXIT:

	lock(t);
	if(t->decoder) {
		avhelper_close_decoder(t->decoder);
		t->decoder = NULL;
	}
	unlock(t);

	veejay_msg(VEEJAY_MSG_ERROR, "[%s] Thread was told to exit", tag->source_name);

	pthread_exit(NULL);

	return NULL;
}

int	avformat_thread_set_state(vj_tag *tag, int new_state)
{
	threaded_t *t = (threaded_t*) tag->priv;
	if(trylock(t) == 0 ) {
		t->state = new_state;
		unlock(t);
        veejay_msg(VEEJAY_MSG_DEBUG, "Changed state of %s to %d", tag->source_name, new_state);
		return 1;
	} else {
		veejay_msg(VEEJAY_MSG_WARNING,"[%s] Thread is busy, cannot set state to %d",tag->source_name, new_state); 
	}
	return 0;
}

void	*avformat_thread_allocate(VJFrame *frame)
{
	threaded_t *t = (threaded_t*) vj_calloc(sizeof(threaded_t));
	return (void*) t;
}

int	avformat_thread_get_frame( vj_tag *tag, VJFrame *dst, int ms_elapsed_since_last_frame )
{
	threaded_t *t = (threaded_t*) tag->priv;

	lock(t);  
	if( !(t->state==STATE_RUNNING) || t->have_frame == 0 ) {
		unlock(t);
		return 1;
	}
	VJFrame *src = avhelper_get_decoded_video(t->decoder);
	if(t->scaler == NULL) {
		sws_template sws_templ;
		memset( &sws_templ, 0, sizeof(sws_template));
		sws_templ.flags = yuv_which_scaler();
		t->scaler = yuv_init_swscaler( src,t->dest, &sws_templ, yuv_sws_get_cpu_flags() );
	}

	t->elapsed = (double) (ms_elapsed_since_last_frame > 0 ? ms_elapsed_since_last_frame / 1000.0 : 0.0 );

	yuv_convert_and_scale( t->scaler, src,dst );
	unlock(t);

	return 1;
}

int	avformat_thread_start(vj_tag *tag, VJFrame *info)
{
	threaded_t *t = (threaded_t*)tag->priv;
	int p_err = 0;

	t->dest = yuv_yuv_template(NULL,NULL,NULL, info->width,info->height, alpha_fmt_to_yuv(info->format));
	if(t->dest == NULL) {
		return 0;
	}

	if((p_err = pthread_mutexattr_settype( &(t->attr), PTHREAD_MUTEX_ERRORCHECK)) != 0 ) {
		veejay_msg(VEEJAY_MSG_DEBUG, "%s", strerror(errno));
	}

	pthread_mutex_init( &(t->mutex), NULL );

	t->have_frame = 0;
	t->info = info;
	t->state = STATE_ERROR;

	p_err = pthread_create( &(t->thread), NULL, &reader_thread, (void*) tag );
		
	if( p_err ==0) {
		veejay_msg(VEEJAY_MSG_INFO, "[%s] Created new input stream %d",tag->source_name, tag->id );
	}

	return 1; 
}

void	avformat_thread_stop(vj_tag *tag)
{
	int p_err = 0;
	threaded_t *t = (threaded_t*)tag->priv;
	
	lock(t);
	t->state = STATE_QUIT;
	unlock(t);

	veejay_msg(VEEJAY_MSG_DEBUG, "Waiting for termination of thread ...");
	if((p_err = pthread_join( t->thread, NULL )) != 0) {
		veejay_msg(VEEJAY_MSG_ERROR, "Error %d (%s)",p_err,strerror(p_err));
	}

	pthread_mutex_destroy( &(t->mutex));
	if(t->dest) {
		free(t->dest);
		t->dest = NULL;
	}

	free(t);
	t = NULL;

	veejay_msg(VEEJAY_MSG_INFO, "[%s] Thread has exited", tag->source_name);
}

