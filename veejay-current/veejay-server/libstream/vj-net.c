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
#include <veejaycore/avcommon.h>
#include <veejaycore/avhelper.h>
#include <libvje/effects/common.h>

typedef struct
{
	pthread_mutex_t mutex;
	pthread_t thread;
	int have_frame;
	int state;
	vj_client *v;
	VJFrame *info;
	void *scaler;
} threaded_t;

#define STATE_INACTIVE 0
#define STATE_RUNNING  1
#define STATE_QUIT 2
#define STATE_ERROR 4

static void timespec_add_ms(struct timespec *ts, long ms)
{
    ts->tv_nsec += (ms % 1000) * 1000000L;
    ts->tv_sec  += ms / 1000;

    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += ts->tv_nsec / 1000000000L;
        ts->tv_nsec %= 1000000000L;
    }
}

static void get_monotonic_now(struct timespec *ts)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
}

static void net_delay_abs(struct timespec *next_wakeup)
{
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, next_wakeup, NULL);
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

	struct timespec next_wakeup;
	get_monotonic_now(&next_wakeup);

	snprintf(buf,sizeof(buf), "%03d:%d;", VIMS_GET_FRAME, my_screen_id);

	for( ;; ) {

		int error = 0;
		int res   = 0;
		int ret   = 0;

		lock(t);
		t->state = eval_state(t, tag);
		unlock(t);

		switch(t->state) {

		case STATE_ERROR:
			close_client(t);
			lock(t);
			t->state = STATE_INACTIVE;
			unlock(t);

			retrieve = 0;

			timespec_add_ms(&next_wakeup, 250);
			net_delay_abs(&next_wakeup);
			continue;

		case STATE_INACTIVE:
			retrieve = 0;

			timespec_add_ms(&next_wakeup, 100);
			net_delay_abs(&next_wakeup);
			continue;

		case STATE_RUNNING:

			if (retrieve == 0) {
				ret = vj_client_send(t->v, V_CMD, (unsigned char*) buf);
				if (ret <= 0) {
					error = 1;
				} else {
					retrieve = 1;
				}

				timespec_add_ms(&next_wakeup, 5);
				net_delay_abs(&next_wakeup);
				continue;
			}

			if (!error && retrieve == 1) {
				res = vj_client_poll(t->v, V_CMD);

				if (res > 0) {
					if (vj_client_link_can_read(t->v, V_CMD)) {
						retrieve = 2;
					}
				}
				else if (res < 0) {
					error = 1;
				}
				else {
					timespec_add_ms(&next_wakeup, 8);
					net_delay_abs(&next_wakeup);
					continue;
				}
			}

			if (!error && retrieve == 2) {
				int frame_len = vj_client_read_frame_hdr(t->v);

				if (frame_len <= 0) {
					error = 1;
				}

				if (!error) {
					lock(t);
					ret = vj_client_read_frame_data(t->v, frame_len);
					if (ret) {
						t->have_frame = 1;
					}
					unlock(t);
				}

				if (ret) {
					retrieve = 0;
					timespec_add_ms(&next_wakeup, 10);
					net_delay_abs(&next_wakeup);
				} else {
					veejay_msg(VEEJAY_MSG_DEBUG,
						"Error reading video frame from %s:%d",
						tag->source_name,
						tag->video_channel);

					error = 1;
				}
			}

			if (error) {
				lock(t);
				t->state = STATE_ERROR;
				t->have_frame = 0;
				unlock(t);

				retrieve = 0;

				timespec_add_ms(&next_wakeup, 200);
				net_delay_abs(&next_wakeup);
			}

			break;

		case STATE_QUIT:
			goto NETTHREADEXIT;
		}
	}

NETTHREADEXIT:
	close_client(t);

	veejay_msg(VEEJAY_MSG_INFO,
		"Network thread with %s: %d has exited",
		tag->source_name,
		tag->video_channel+5);

	pthread_exit(NULL);
	return NULL;
}

static void	*mcast_reader_thread(void *data)
{
	vj_tag *tag = (vj_tag*) data;
	threaded_t *t = tag->priv;

	const int len = vj_tag_get_width() * vj_tag_get_height() * 4;
	const int padded = 256;
	const int max_len = padded + len;

	struct timespec next_wakeup;
	get_monotonic_now(&next_wakeup);

	for (;;) {

		int error = 0;

		lock(t);
		t->state = eval_state_mcast(t, tag);
		unlock(t);

		switch (t->state) {

		case STATE_ERROR:
			close_client(t);

			lock(t);
			t->state = STATE_INACTIVE;
			unlock(t);

			timespec_add_ms(&next_wakeup, 300);
			net_delay_abs(&next_wakeup);
			break;

		case STATE_INACTIVE:
			timespec_add_ms(&next_wakeup, 120);
			net_delay_abs(&next_wakeup);
			continue;

		case STATE_RUNNING:

			lock(t);

			if (vj_client_read_mcast_data(t->v, max_len) < 0) {
				error = 1;
			} else {
				t->have_frame = 1;
			}

			if (error) {
				t->state = STATE_ERROR;
				t->have_frame = 0;
			}

			unlock(t);

			timespec_add_ms(&next_wakeup, 10);
			net_delay_abs(&next_wakeup);

			break;

		case STATE_QUIT:
			goto NETTHREADEXIT;
		}
	}

NETTHREADEXIT:

	close_client(t);

	veejay_msg(VEEJAY_MSG_INFO,
		"Multicast receiver %s: %d has stopped",
		tag->source_name,
		tag->video_channel+5);

	return NULL;
}

int net_thread_get_frame(vj_tag *tag, VJFrame *dst)
{
    threaded_t *t = (threaded_t*) tag->priv;
    if (!t)
        return 0;

    lock(t);

    if (t->state != STATE_RUNNING || t->have_frame == 0) {
        unlock(t);
        return 0;
    }

    if (!t->v || !t->v->decoder) {
        unlock(t);
        return 0;
    }

    VJFrame *src = avhelper_get_decoded_video(t->v->decoder);
    if (!src) {
        unlock(t);
        return 0;
    }

    if (t->scaler == NULL) {
        sws_template sws_templ;
        memset(&sws_templ, 0, sizeof(sws_template));
        sws_templ.flags = yuv_which_scaler();

        t->scaler = yuv_init_swscaler(
            src,
            dst,
            &sws_templ,
            yuv_sws_get_cpu_flags()
        );

        if (!t->scaler) {
            unlock(t);
            return 0;
        }
    }

    yuv_convert_and_scale(t->scaler, src, dst);

    unlock(t);
    return 1;
}

int net_thread_start(vj_tag *tag, VJFrame *info)
{
    threaded_t *t = (threaded_t*) vj_calloc(sizeof(threaded_t));
    if (t == NULL)
        return 0;

    int p_err = 0;

    pthread_mutex_init(&(t->mutex), NULL);

    t->info = info;
    t->state = STATE_INACTIVE;
    t->have_frame = 0;
    t->v = NULL;
    t->scaler = NULL;

    tag->priv = t;

    if (tag->source_type == VJ_TAG_TYPE_MCAST) {
        p_err = pthread_create(&(t->thread), NULL, &mcast_reader_thread, (void*) tag);
    } else {
        p_err = pthread_create(&(t->thread), NULL, &reader_thread, (void*) tag);
    }

    if (p_err == 0) {
        veejay_msg(
            VEEJAY_MSG_INFO,
            "Created new input stream [%d] (%s) to veejay host %s port %d",
            tag->id,
            tag->source_type == VJ_TAG_TYPE_MCAST ? "multicast" : "unicast",
            tag->source_name,
            tag->video_channel
        );

        tag->active = 1;
        return 1;
    }

    pthread_mutex_destroy(&(t->mutex));
    free(t);
    tag->priv = NULL;

    return 0;
}

void net_thread_stop(vj_tag *tag)
{
    threaded_t *t = (threaded_t*) tag->priv;
    if (!t)
        return;

    lock(t);

    if (t->state == STATE_QUIT) {
        unlock(t);
        veejay_msg(VEEJAY_MSG_ERROR, "Stream was already stopped");
        return;
    }

    t->state = STATE_QUIT;
    unlock(t);

    pthread_join(t->thread, NULL);

    if (t->scaler) {
        yuv_free_swscaler(t->scaler);
        t->scaler = NULL;
    }

    pthread_mutex_destroy(&(t->mutex));

    free(t);
    tag->priv = NULL;

    veejay_msg(
        VEEJAY_MSG_INFO,
        "Disconnected from Veejay host %s:%d",
        tag->source_name,
        tag->video_channel
    );
}

int net_already_opened(const char *filename, int n, int channel)
{
    if (!filename || n <= 0)
        return 0;

    char sourcename[255];
    int i;

    for (i = 1; i < n; i++)
    {
        if (!vj_tag_exists(i))
            continue;

        vj_tag_get_source_name(i, sourcename, sizeof(sourcename));

        if (sourcename[0] == '\0')
            continue;

        if (strcasecmp(sourcename, filename) != 0)
            continue;

        vj_tag *tt = vj_tag_get(i);
        if (!tt)
            continue;

        if (tt->source_type == VJ_TAG_TYPE_NET || tt->source_type == VJ_TAG_TYPE_MCAST)
        {
            if (tt->video_channel == channel)
            {
                veejay_msg(VEEJAY_MSG_WARNING, "Already streaming from %s:%d in stream %d", filename, channel, tt->id);
                return 1;
            }
        }
    }

    return 0;
}