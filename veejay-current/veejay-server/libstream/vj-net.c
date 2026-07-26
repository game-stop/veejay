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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
#include <veejaycore/avcommon.h>
#include <veejaycore/avhelper.h>
#include <libvje/effects/common.h>

typedef struct
{
    pthread_mutex_t mutex;
    pthread_t thread;
    int state;
    vj_client *v;
    VJFrame *info;
    VJFrame *frames[2];
    int published_index;
    void *scaler;
    int scaler_src_width;
    int scaler_src_height;
    int scaler_src_format;
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

static void net_sleep_ms(long ms)
{
    struct timespec wakeup;
    get_monotonic_now(&wakeup);
    timespec_add_ms(&wakeup, ms);
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup, NULL);
}

static int my_screen_id = -1;
void net_set_screen_id(int id)
{
    my_screen_id = id;
    veejay_msg(VEEJAY_MSG_DEBUG,"Network stream bound to screen %d", id );
}

static int lock(threaded_t *t)
{
    return pthread_mutex_lock(&(t->mutex));
}

static int unlock(threaded_t *t)
{
    return pthread_mutex_unlock(&(t->mutex));
}

static int net_state_get(threaded_t *t)
{
    int state;
    lock(t);
    state = t->state;
    unlock(t);
    return state;
}

static void net_state_set(threaded_t *t, int state)
{
    lock(t);
    if(t->state != STATE_QUIT)
        t->state = state;
    unlock(t);
}

static VJFrame *net_frame_alloc_like(const VJFrame *info)
{
    VJFrame *frame = (VJFrame*) vj_malloc(sizeof(VJFrame));
    if(!frame)
        return NULL;

    veejay_memcpy(frame, info, sizeof(VJFrame));

    size_t y_len = (size_t)info->len;
    size_t uv_len = (size_t)info->uv_len;
    size_t alpha_len = 0;

    if(info->stride[3] > 0 && info->height > 0)
        alpha_len = (size_t)info->stride[3] * (size_t)info->height;

    size_t total = y_len + uv_len + uv_len + alpha_len;
    uint8_t *data = (uint8_t*) vj_malloc(total + 256);
    if(!data) {
        free(frame);
        return NULL;
    }

    frame->data[0] = data;
    frame->data[1] = data + y_len;
    frame->data[2] = data + y_len + uv_len;
    frame->data[3] = alpha_len ? data + y_len + uv_len + uv_len : NULL;

    if(alpha_len)
        veejay_memset(frame->data[3], 255, alpha_len);

    return frame;
}

static void net_frame_free(VJFrame **frame)
{
    if(!frame || !*frame)
        return;

    free((*frame)->data[0]);
    free(*frame);
    *frame = NULL;
}

static void net_copy_frame(VJFrame *dst, const VJFrame *src)
{
    size_t y_len = src->len < dst->len ? (size_t)src->len : (size_t)dst->len;
    size_t uv_len = src->uv_len < dst->uv_len ? (size_t)src->uv_len : (size_t)dst->uv_len;

    veejay_memcpy(dst->data[0], src->data[0], y_len);
    veejay_memcpy(dst->data[1], src->data[1], uv_len);
    veejay_memcpy(dst->data[2], src->data[2], uv_len);

    if(src->data[3] && dst->data[3]) {
        size_t src_alpha = (size_t)src->stride[3] * (size_t)src->height;
        size_t dst_alpha = (size_t)dst->stride[3] * (size_t)dst->height;
        size_t alpha_len = src_alpha < dst_alpha ? src_alpha : dst_alpha;
        veejay_memcpy(dst->data[3], src->data[3], alpha_len);
    }
}

static void net_scaler_reset(threaded_t *t)
{
    if(t->scaler) {
        yuv_free_swscaler(t->scaler);
        t->scaler = NULL;
    }

    t->scaler_src_width = 0;
    t->scaler_src_height = 0;
    t->scaler_src_format = 0;
}

static int net_publish_decoded(threaded_t *t)
{
    if(!t->v || !t->v->decoder)
        return 0;

    VJFrame *src = avhelper_get_decoded_video(t->v->decoder);
    if(!src)
        return 0;

    if(!t->scaler ||
       t->scaler_src_width != src->width ||
       t->scaler_src_height != src->height ||
       t->scaler_src_format != src->format)
    {
        sws_template sws_templ;
        veejay_memset(&sws_templ, 0, sizeof(sws_template));
        sws_templ.flags = yuv_which_scaler();

        net_scaler_reset(t);
        t->scaler = yuv_init_swscaler(src,
                                      t->frames[0],
                                      &sws_templ,
                                      yuv_sws_get_cpu_flags());
        if(!t->scaler)
            return 0;

        t->scaler_src_width = src->width;
        t->scaler_src_height = src->height;
        t->scaler_src_format = src->format;
    }

    lock(t);
    int back = t->published_index == 0 ? 1 : 0;
    unlock(t);

    yuv_convert_and_scale(t->scaler, src, t->frames[back]);

    lock(t);
    t->published_index = back;
    unlock(t);

    return 1;
}

static void close_client(threaded_t *t)
{
    vj_client *client;

    lock(t);
    client = t->v;
    t->v = NULL;
    unlock(t);

    if(client) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Closing connection to remote veejay");
        vj_client_close(client);
        vj_client_free(client);
    }

    net_scaler_reset(t);
}

static int net_install_client(threaded_t *t, vj_client *client)
{
    lock(t);
    if(t->state == STATE_QUIT) {
        unlock(t);
        vj_client_close(client);
        vj_client_free(client);
        return STATE_QUIT;
    }
    t->v = client;
    unlock(t);
    return STATE_RUNNING;
}

static int connect_client(threaded_t *t, vj_tag *tag)
{
    veejay_msg(VEEJAY_MSG_INFO, " ... Waiting for network stream to become ready [%s]",tag->source_name);

    vj_client *client = vj_client_alloc_stream(t->info);
    if(!client)
        return STATE_ERROR;

    int success = vj_client_connect_dat(client, tag->source_name, tag->video_channel);
    if(success <= 0) {
        veejay_msg(VEEJAY_MSG_ERROR,"Unable to connect to %s:%d", tag->source_name, tag->video_channel + 5 );
        vj_client_close(client);
        vj_client_free(client);
        return STATE_ERROR;
    }

    int state = net_install_client(t, client);
    if(state == STATE_RUNNING)
        veejay_msg(VEEJAY_MSG_INFO, "Connection established with %s:%d",tag->source_name, tag->video_channel + 5);
    return state;
}

static int connect_client_mcast(threaded_t *t, vj_tag *tag)
{
    veejay_msg(VEEJAY_MSG_INFO, " ... Waiting for network stream to become ready [%s]",tag->source_name);

    vj_client *client = vj_client_alloc_stream(t->info);
    if(!client)
        return STATE_ERROR;

    int success = vj_client_connect(client, NULL, tag->source_name, tag->video_channel);
    if(success <= 0) {
        veejay_msg(VEEJAY_MSG_ERROR,"Unable to connect to %s:%d", tag->source_name, tag->video_channel + 5 );
        vj_client_close(client);
        vj_client_free(client);
        return STATE_ERROR;
    }

    int state = net_install_client(t, client);
    if(state == STATE_RUNNING)
        veejay_msg(VEEJAY_MSG_INFO, "Connection established with %s:%d",tag->source_name, tag->video_channel + 5);
    return state;
}

static void *reader_thread(void *data)
{
    vj_tag *tag = (vj_tag*) data;
    threaded_t *t = tag->priv;
    char buf[16];
    int retrieve = 0;

    snprintf(buf, sizeof(buf), "%03d:%d;", VIMS_GET_FRAME, my_screen_id);

    for(;;) {
        int state = net_state_get(t);
        int error = 0;

        if(state == STATE_QUIT)
            break;

        if(state == STATE_ERROR) {
            close_client(t);
            net_state_set(t, STATE_INACTIVE);
            retrieve = 0;
            net_sleep_ms(250);
            continue;
        }

        if(state == STATE_INACTIVE) {
            net_state_set(t, connect_client(t, tag));
            retrieve = 0;
            continue;
        }

        if(retrieve == 0) {
            if(vj_client_send(t->v, V_CMD, (unsigned char*)buf) <= 0)
                error = 1;
            else
                retrieve = 1;
        }

        if(!error && retrieve == 1) {
            int res = vj_client_poll(t->v, V_CMD);
            if(res > 0) {
                if(vj_client_link_can_read(t->v, V_CMD))
                    retrieve = 2;
            }
            else if(res < 0) {
                error = 1;
            }
            else {
                net_sleep_ms(1);
                continue;
            }
        }

        if(!error && retrieve == 2) {
            int frame_len = vj_client_read_frame_hdr(t->v);
            if(frame_len <= 0) {
                error = 1;
            }
            else {
                int ret = vj_client_read_frame_data(t->v, frame_len);

                if(ret && net_publish_decoded(t))
                {
                    retrieve = 0;
                }
                else {
                    veejay_msg(VEEJAY_MSG_DEBUG,
                               "Error reading video frame from %s:%d",
                               tag->source_name,
                               tag->video_channel);
                    error = 1;
                }
            }
        }

        if(error) {
            net_state_set(t, STATE_ERROR);
            retrieve = 0;
            net_sleep_ms(200);
        }
    }

    close_client(t);

    veejay_msg(VEEJAY_MSG_INFO,
               "Network thread with %s: %d has exited",
               tag->source_name,
               tag->video_channel + 5);

    return NULL;
}

static void *mcast_reader_thread(void *data)
{
    vj_tag *tag = (vj_tag*) data;
    threaded_t *t = tag->priv;

    const int len = vj_tag_get_width() * vj_tag_get_height() * 4;
    const int padded = 256;
    const int max_len = padded + len;

    for(;;) {
        int state = net_state_get(t);

        if(state == STATE_QUIT)
            break;

        if(state == STATE_ERROR) {
            close_client(t);
            net_state_set(t, STATE_INACTIVE);
            net_sleep_ms(300);
            continue;
        }

        if(state == STATE_INACTIVE) {
            net_state_set(t, connect_client_mcast(t, tag));
            continue;
        }

        int ret = vj_client_read_mcast_data(t->v, max_len);

        if(ret < 0) {
            net_state_set(t, STATE_ERROR);
            continue;
        }

        if(ret > 0) {
            if(!net_publish_decoded(t))
                net_state_set(t, STATE_ERROR);
        }
        else {
            net_sleep_ms(1);
        }
    }

    close_client(t);

    veejay_msg(VEEJAY_MSG_INFO,
               "Multicast receiver %s: %d has stopped",
               tag->source_name,
               tag->video_channel + 5);

    return NULL;
}

int net_thread_get_frame(vj_tag *tag, VJFrame *dst)
{
    threaded_t *t = (threaded_t*)tag->priv;
    if(!t)
        return 0;

    lock(t);

    if(t->published_index < 0) {
        unlock(t);
        return 0;
    }

    net_copy_frame(dst, t->frames[t->published_index]);
    unlock(t);

    return 1;
}

int net_thread_start(vj_tag *tag, VJFrame *info)
{
    threaded_t *t = (threaded_t*) vj_calloc(sizeof(threaded_t));
    if(!t)
        return 0;

    pthread_mutex_init(&(t->mutex), NULL);

    t->info = info;
    t->state = STATE_INACTIVE;
    t->v = NULL;
    t->scaler = NULL;
    t->published_index = -1;
    t->frames[0] = net_frame_alloc_like(info);
    t->frames[1] = net_frame_alloc_like(info);

    if(!t->frames[0] || !t->frames[1]) {
        net_frame_free(&t->frames[0]);
        net_frame_free(&t->frames[1]);
        pthread_mutex_destroy(&(t->mutex));
        free(t);
        return 0;
    }

    tag->priv = t;

    int p_err;
    if(tag->source_type == VJ_TAG_TYPE_MCAST)
        p_err = pthread_create(&(t->thread), NULL, &mcast_reader_thread, (void*)tag);
    else
        p_err = pthread_create(&(t->thread), NULL, &reader_thread, (void*)tag);

    if(p_err == 0) {
        veejay_msg(VEEJAY_MSG_INFO,
                   "Created new input stream [%d] (%s) to veejay host %s port %d",
                   tag->id,
                   tag->source_type == VJ_TAG_TYPE_MCAST ? "multicast" : "unicast",
                   tag->source_name,
                   tag->video_channel);

        tag->active = 1;
        return 1;
    }

    net_frame_free(&t->frames[0]);
    net_frame_free(&t->frames[1]);
    pthread_mutex_destroy(&(t->mutex));
    free(t);
    tag->priv = NULL;

    return 0;
}

void net_thread_stop(vj_tag *tag)
{
    threaded_t *t = (threaded_t*)tag->priv;
    if(!t)
        return;

    lock(t);

    if(t->state == STATE_QUIT) {
        unlock(t);
        veejay_msg(VEEJAY_MSG_ERROR, "Stream was already stopped");
        return;
    }

    t->state = STATE_QUIT;
    if(t->v && tag->source_type == VJ_TAG_TYPE_NET)
        vj_client_shutdown(t->v, V_CMD);
    unlock(t);

    pthread_join(t->thread, NULL);

    net_scaler_reset(t);
    net_frame_free(&t->frames[0]);
    net_frame_free(&t->frames[1]);
    pthread_mutex_destroy(&(t->mutex));

    free(t);
    tag->priv = NULL;

    veejay_msg(VEEJAY_MSG_INFO,
               "Disconnected from Veejay host %s:%d",
               tag->source_name,
               tag->video_channel);
}

int net_already_opened(const char *filename, int n, int channel)
{
    if(!filename || n <= 0)
        return 0;

    char sourcename[255];
    int i;

    for(i = 1; i < n; i++)
    {
        if(!vj_tag_exists(i))
            continue;

        vj_tag_get_source_name(i, sourcename, sizeof(sourcename));

        if(sourcename[0] == '\0')
            continue;

        if(strcasecmp(sourcename, filename) != 0)
            continue;

        vj_tag *tt = vj_tag_get(i);
        if(!tt)
            continue;

        if(tt->source_type == VJ_TAG_TYPE_NET || tt->source_type == VJ_TAG_TYPE_MCAST)
        {
            if(tt->video_channel == channel)
            {
                veejay_msg(VEEJAY_MSG_WARNING, "Already streaming from %s:%d in stream %d", filename, channel, tt->id);
                return 1;
            }
        }
    }

    return 0;
}
