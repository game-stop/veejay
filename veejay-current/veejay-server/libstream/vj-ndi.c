/* 
 * veejay  
 *
 * Copyright (C) 2000-2026 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
/*
 * NDI(R) is a registered trademark of Vizrt NDI AB.
 */
#include <config.h>
#include <stdint.h>
#include <veejaycore/defs.h>
#include <libstream/vj-ndi.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-ndi-runtime.h>

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_NDI
#include <dlfcn.h>
#include <Processing.NDI.Lib.h>
#endif

#define VJ_NDI_AUDIO_RING_SECONDS 4
#define VJ_NDI_CAPTURE_TIMEOUT_MS 100
#define VJ_NDI_TIMECODE_TICKS_PER_SECOND 10000000.0

#ifdef HAVE_NDI

typedef const NDIlib_v5 *(*vj_ndi_load_fn)(void);

typedef struct {
    pthread_mutex_t mutex;
    void *handle;
    const NDIlib_v5 *api;
    int initialized;
    int probed;
    int references;
    int load_error_logged;
    char version[160];
    char last_error[512];
} vj_ndi_runtime_state;

static vj_ndi_runtime_state ndi_runtime = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .version = "unavailable",
    .last_error = "NDI runtime has not been probed"
};

static char *vj_ndi_strdup(const char *text)
{
    if(!text)
        return NULL;
    size_t length = strlen(text) + 1;
    char *copy = (char*)malloc(length);
    if(copy)
        memcpy(copy, text, length);
    return copy;
}

static double vj_ndi_monotonic_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1.0e-9;
}

static int vj_ndi_api_has_discovery(const NDIlib_v5 *api)
{
    return api && api->find_create_v2 && api->find_destroy &&
           api->find_get_current_sources && api->find_wait_for_sources;
}

static int vj_ndi_api_has_receiver(const NDIlib_v5 *api)
{
    return api && api->recv_create_v3 && api->recv_destroy &&
           api->recv_capture_v3 && api->recv_free_video_v2 &&
           api->recv_free_audio_v3 && api->recv_free_metadata;
}

static int vj_ndi_api_has_sender(const NDIlib_v5 *api)
{
    return api && api->send_create && api->send_destroy &&
           api->send_send_video_async_v2;
}

struct vj_ndi_receiver {
    const NDIlib_v5 *api;
    NDIlib_recv_instance_t instance;
    char *source_name;
    int width;
    int height;
    double fps;
    int audio_rate;
    int audio_channels;
    int audio_bits;
    int audio_bytes_per_frame;

    pthread_t video_thread;
    pthread_t audio_thread;
    int video_thread_started;
    int audio_thread_started;
    volatile int stop;
    volatile int active;
    pthread_mutex_t state_mutex;
    pthread_cond_t state_cond;

    pthread_mutex_t video_mutex;
    uint8_t *video_storage;
    uint8_t *video_planes[3][3];
    int *video_map_storage;
    int *video_x_map;
    int *video_y_map;
    int video_map_source_width;
    int video_map_source_height;
    int video_write_index;
    int video_ready_index;
    int video_read_index;
    uint64_t video_sequence;
    uint64_t video_consumed_sequence;

    pthread_mutex_t audio_mutex;
    pthread_mutex_t audio_ring_mutex;
    int16_t *audio_ring;
    size_t audio_capacity_frames;
    uint64_t audio_read_pos;
    uint64_t audio_write_pos;
    double audio_pull_accum;
    int audio_present;
    int16_t *audio_scratch;
    size_t audio_scratch_capacity_frames;
    float *audio_previous;
    int audio_previous_valid;
    double audio_resample_phase;
    int audio_source_rate;

    pthread_mutex_t stats_mutex;
    vj_ndi_stats stats;
    int logged_unsupported;
    int clock_valid;
    int64_t clock_source_anchor;
    int64_t clock_source_last;
    double clock_local_anchor;
    double clock_local_arrival;
};

struct vj_ndi_sender {
    const NDIlib_v5 *api;
    NDIlib_send_instance_t instance;
    char *name;
    int width;
    int height;
    int frame_rate_n;
    int frame_rate_d;
    int audio_rate;
    int audio_channels;
    uint8_t *video_buffers[2];
    int video_stride;
    int video_index;
    int *video_map_storage;
    int *video_x0_map;
    int *video_x1_map;
    int *video_y_map;
    int video_map_source_width;
    int video_map_source_height;
    pthread_mutex_t mutex;
    pthread_mutex_t video_mutex;
    uint64_t next_telemetry_frame;
    vj_ndi_stats stats;
};

static void vj_ndi_runtime_error(const char *path, const char *detail)
{
    snprintf(ndi_runtime.last_error, sizeof(ndi_runtime.last_error), "%s%s%s",
             path && *path ? path : "NDI runtime",
             detail && *detail ? ": " : "",
             detail && *detail ? detail : "load failed");
}

static int vj_ndi_runtime_try_library(const char *path)
{
    vj_ndi_load_fn load_fn = NULL;
    void *symbol = NULL;

    if(!path || !*path)
        return 0;

    dlerror();
    ndi_runtime.handle = dlopen(path, RTLD_LOCAL | RTLD_LAZY);
    if(!ndi_runtime.handle) {
        vj_ndi_runtime_error(path, dlerror());
        return 0;
    }

    dlerror();
    symbol = dlsym(ndi_runtime.handle, "NDIlib_v5_load");
    const char *symbol_error = dlerror();
    if(!symbol || symbol_error) {
        vj_ndi_runtime_error(path, symbol_error ? symbol_error : "NDIlib_v5_load is missing");
        dlclose(ndi_runtime.handle);
        ndi_runtime.handle = NULL;
        return 0;
    }

    memcpy(&load_fn, &symbol, sizeof(load_fn));
    ndi_runtime.api = load_fn();
    if(!ndi_runtime.api || !ndi_runtime.api->initialize ||
       !ndi_runtime.api->initialize()) {
        vj_ndi_runtime_error(path, "NDI runtime initialization failed");
        ndi_runtime.api = NULL;
        dlclose(ndi_runtime.handle);
        ndi_runtime.handle = NULL;
        return 0;
    }

    ndi_runtime.initialized = 1;
    ndi_runtime.load_error_logged = 0;
    ndi_runtime.last_error[0] = '\0';
    snprintf(ndi_runtime.version, sizeof(ndi_runtime.version), "%s",
             ndi_runtime.api->version ? ndi_runtime.api->version() : "NDI runtime");
    return 1;
}

static int vj_ndi_runtime_try_candidate(const char *candidate, void *opaque)
{
    (void)opaque;
    return vj_ndi_runtime_try_library(candidate);
}

static int vj_ndi_runtime_acquire(void)
{
    int result = 0;
    pthread_mutex_lock(&ndi_runtime.mutex);
    if(!ndi_runtime.initialized && !ndi_runtime.probed) {
        ndi_runtime.probed = 1;

#ifdef NDILIB_LIBRARY_NAME
        const char *preferred_library = NDILIB_LIBRARY_NAME;
#else
        const char *preferred_library = NULL;
#endif
#ifdef NDILIB_REDIST_FOLDER
        const char *redist_env = NDILIB_REDIST_FOLDER;
#else
        const char *redist_env = NULL;
#endif
        vj_ndi_runtime_foreach_candidate(preferred_library, redist_env,
                                         vj_ndi_runtime_try_candidate, NULL);

        if(!ndi_runtime.initialized && !ndi_runtime.load_error_logged) {
            veejay_msg(VEEJAY_MSG_WARNING,
                       "NDI runtime load failed: %s. VeeJay also checks $HOME/opt/ndi-sdk6/lib/<arch>; set NDI_RUNTIME_DIR_V6 to override the runtime directory",
                       ndi_runtime.last_error[0] ? ndi_runtime.last_error : "libndi.so.6 was not found");
            ndi_runtime.load_error_logged = 1;
        }
    }

    if(ndi_runtime.initialized) {
        ndi_runtime.references++;
        result = 1;
    }
    pthread_mutex_unlock(&ndi_runtime.mutex);
    return result;
}

static void vj_ndi_runtime_release(void)
{
    pthread_mutex_lock(&ndi_runtime.mutex);
    if(ndi_runtime.references > 0)
        ndi_runtime.references--;
    pthread_mutex_unlock(&ndi_runtime.mutex);
}

int vj_ndi_runtime_available(void)
{
    if(!vj_ndi_runtime_acquire())
        return 0;
    vj_ndi_runtime_release();
    return 1;
}

const char *vj_ndi_runtime_version(void)
{
    if(!vj_ndi_runtime_acquire())
        return "unavailable";
    vj_ndi_runtime_release();
    return ndi_runtime.version;
}

void vj_ndi_runtime_shutdown(void)
{
    pthread_mutex_lock(&ndi_runtime.mutex);
    if(ndi_runtime.references == 0) {
        if(ndi_runtime.initialized && ndi_runtime.api && ndi_runtime.api->destroy)
            ndi_runtime.api->destroy();
        if(ndi_runtime.handle)
            dlclose(ndi_runtime.handle);
        ndi_runtime.handle = NULL;
        ndi_runtime.api = NULL;
        ndi_runtime.initialized = 0;
        ndi_runtime.probed = 0;
        ndi_runtime.load_error_logged = 0;
        snprintf(ndi_runtime.version, sizeof(ndi_runtime.version), "%s", "unavailable");
        snprintf(ndi_runtime.last_error, sizeof(ndi_runtime.last_error), "%s", "NDI runtime has not been probed");
    }
    pthread_mutex_unlock(&ndi_runtime.mutex);
}

int vj_ndi_discover(vj_ndi_source_info *sources, int max_sources, int timeout_ms)
{
    if(!sources || max_sources <= 0 || !vj_ndi_runtime_acquire())
        return 0;

    const NDIlib_v5 *api = ndi_runtime.api;
    if(!vj_ndi_api_has_discovery(api)) {
        vj_ndi_runtime_release();
        return 0;
    }
    NDIlib_find_create_t create_desc;
    memset(&create_desc, 0, sizeof(create_desc));
    create_desc.show_local_sources = true;

    NDIlib_find_instance_t finder = api->find_create_v2(&create_desc);
    if(!finder) {
        vj_ndi_runtime_release();
        return 0;
    }

    if(timeout_ms > 0)
        api->find_wait_for_sources(finder, (uint32_t)timeout_ms);

    uint32_t count = 0;
    const NDIlib_source_t *found = api->find_get_current_sources(finder, &count);
    int copied = (int)count < max_sources ? (int)count : max_sources;
    for(int i = 0; i < copied; i++) {
        snprintf(sources[i].name, sizeof(sources[i].name), "%s",
                 found[i].p_ndi_name ? found[i].p_ndi_name : "");
        snprintf(sources[i].url, sizeof(sources[i].url), "%s",
                 found[i].p_url_address ? found[i].p_url_address : "");
    }

    api->find_destroy(finder);
    vj_ndi_runtime_release();
    return copied;
}

static void vj_ndi_payload_append_escaped(char **cursor, size_t *remaining,
                                          const char *text)
{
    if(!cursor || !*cursor || !remaining || *remaining == 0)
        return;
    if(!text) {
        **cursor = '\0';
        return;
    }
    for(const unsigned char *p = (const unsigned char*)text;
        *p && *remaining > 1; p++) {
        char c = (char)*p;
        if(c == '\t' || c == '\n' || c == '\r' || c == '\\') {
            if(*remaining <= 2)
                break;
            *(*cursor)++ = '\\';
            *(*cursor)++ = c == '\t' ? 't' : c == '\n' ? 'n' :
                           c == '\r' ? 'r' : '\\';
            *remaining -= 2;
        } else {
            *(*cursor)++ = c;
            (*remaining)--;
        }
    }
    **cursor = '\0';
}

char *vj_ndi_discovery_payload(int timeout_ms)
{
    vj_ndi_source_info sources[256];
    int count = vj_ndi_discover(sources, 256, timeout_ms);
    size_t capacity = 32;
    for(int i = 0; i < count; i++)
        capacity += strlen(sources[i].name) * 2 + strlen(sources[i].url) * 2 + 32;

    char *payload = (char*)malloc(capacity);
    if(!payload)
        return NULL;
    char *cursor = payload;
    size_t remaining = capacity;
    int n = snprintf(cursor, remaining, "%d\n", count);
    if(n < 0 || (size_t)n >= remaining) {
        free(payload);
        return NULL;
    }
    cursor += n;
    remaining -= (size_t)n;

    for(int i = 0; i < count; i++) {
        n = snprintf(cursor, remaining, "%d\t", i);
        if(n < 0 || (size_t)n >= remaining)
            break;
        cursor += n;
        remaining -= (size_t)n;
        vj_ndi_payload_append_escaped(&cursor, &remaining, sources[i].name);
        if(remaining > 1) {
            *cursor++ = '\t';
            *cursor = '\0';
            remaining--;
        }
        vj_ndi_payload_append_escaped(&cursor, &remaining, sources[i].url);
        if(remaining > 1) {
            *cursor++ = '\n';
            *cursor = '\0';
            remaining--;
        }
    }
    return payload;
}

static void vj_ndi_receiver_update_connection(vj_ndi_receiver *receiver, int connected)
{
    int changed = 0;
    pthread_mutex_lock(&receiver->stats_mutex);
    if(receiver->stats.connected != connected) {
        receiver->stats.connected = connected;
        changed = 1;
    }
    pthread_mutex_unlock(&receiver->stats_mutex);
    if(changed)
        veejay_msg(connected ? VEEJAY_MSG_INFO : VEEJAY_MSG_WARNING,
                   "NDI source '%s' %s", receiver->source_name,
                   connected ? "is online" : "is offline; waiting for automatic reconnection");
}

static int vj_ndi_receiver_connection_count(vj_ndi_receiver *receiver)
{
    if(receiver->api->recv_get_no_connections)
        return receiver->api->recv_get_no_connections(receiver->instance);
    pthread_mutex_lock(&receiver->stats_mutex);
    const int connected = receiver->stats.connected > 0;
    pthread_mutex_unlock(&receiver->stats_mutex);
    return connected;
}

static void vj_ndi_receiver_store_metadata_text(vj_ndi_receiver *receiver,
                                                const char *text)
{
    if(!text || !*text)
        return;
    pthread_mutex_lock(&receiver->stats_mutex);
    snprintf(receiver->stats.last_metadata,
             sizeof(receiver->stats.last_metadata), "%s", text);
    pthread_mutex_unlock(&receiver->stats_mutex);
}

static void vj_ndi_receiver_store_metadata(vj_ndi_receiver *receiver,
                                           const NDIlib_metadata_frame_t *metadata)
{
    if(!metadata || !metadata->p_data)
        return;
    vj_ndi_receiver_store_metadata_text(receiver, metadata->p_data);
}

static void vj_ndi_receiver_update_clock_locked(vj_ndi_receiver *receiver,
                                                int64_t timestamp,
                                                double arrival)
{
    if(timestamp <= 0)
        return;

    if(!receiver->clock_valid || timestamp <= receiver->clock_source_last) {
        receiver->clock_valid = 1;
        receiver->clock_source_anchor = timestamp;
        receiver->clock_source_last = timestamp;
        receiver->clock_local_anchor = arrival;
        receiver->clock_local_arrival = arrival;
        receiver->stats.clock_drift_ms = 0.0;
    } else {
        const double source_elapsed =
            (double)(timestamp - receiver->clock_source_anchor) /
            VJ_NDI_TIMECODE_TICKS_PER_SECOND;
        const double local_elapsed = arrival - receiver->clock_local_anchor;
        const double error = source_elapsed - local_elapsed;
        if(fabs(error) > 5.0) {
            receiver->clock_source_anchor = timestamp;
            receiver->clock_local_anchor = arrival;
            receiver->stats.clock_drift_ms = 0.0;
        } else {
            receiver->stats.clock_drift_ms = error * 1000.0;
        }
        receiver->clock_source_last = timestamp;
        receiver->clock_local_arrival = arrival;
    }
    receiver->stats.clock_available = 1;
    receiver->stats.clock_age_ms = 0;
}

static void vj_ndi_build_scale_map(int *map, int dst_size, int src_size)
{
    for(int i = 0; i < dst_size; i++) {
        int source = (int)(((int64_t)i * src_size) / dst_size);
        map[i] = source < src_size ? source : src_size - 1;
    }
}

static void vj_ndi_convert_uyvy_to_444(const NDIlib_video_frame_v2_t *src,
                                       uint8_t *dst_y,
                                       uint8_t *dst_u,
                                       uint8_t *dst_v,
                                       int dst_w,
                                       int dst_h,
                                       const int *x_map,
                                       const int *y_map)
{
    const int src_w = src->xres;
    const int src_h = src->yres;
    const int packed_w = (src_w + 1) & ~1;
    const int stride = src->line_stride_in_bytes > 0 ?
                       src->line_stride_in_bytes : packed_w * 2;

    if(src_w == dst_w && src_h == dst_h) {
        for(int y = 0; y < dst_h; y++) {
            const uint8_t *row = src->p_data + (size_t)y * (size_t)stride;
            const size_t dst_row = (size_t)y * (size_t)dst_w;
            int x = 0;
            for(; x + 1 < dst_w; x += 2) {
                const size_t offset = (size_t)x * 2u;
                const size_t p0 = dst_row + (size_t)x;
                const uint8_t u = row[offset];
                const uint8_t v = row[offset + 2u];
                dst_y[p0] = row[offset + 1u];
                dst_y[p0 + 1u] = row[offset + 3u];
                dst_u[p0] = u;
                dst_u[p0 + 1u] = u;
                dst_v[p0] = v;
                dst_v[p0 + 1u] = v;
            }
            if(x < dst_w) {
                const size_t offset = (size_t)(x & ~1) * 2u;
                const size_t p0 = dst_row + (size_t)x;
                dst_u[p0] = row[offset];
                dst_y[p0] = row[offset + 1u];
                dst_v[p0] = row[offset + 2u];
            }
        }
        return;
    }

    for(int y = 0; y < dst_h; y++) {
        const int sy = y_map ? y_map[y] :
                       (int)(((int64_t)y * src_h) / dst_h);
        const uint8_t *row = src->p_data + (size_t)sy * (size_t)stride;
        const size_t dst_row = (size_t)y * (size_t)dst_w;
        for(int x = 0; x < dst_w; x++) {
            int sx = x_map ? x_map[x] :
                     (int)(((int64_t)x * src_w) / dst_w);
            if(sx >= src_w)
                sx = src_w - 1;
            const int pair = sx & ~1;
            const size_t offset = (size_t)pair * 2u;
            const size_t dx = dst_row + (size_t)x;
            dst_u[dx] = row[offset];
            dst_y[dx] = row[offset + (size_t)(sx & 1 ? 3 : 1)];
            dst_v[dx] = row[offset + 2u];
        }
    }
}

static void vj_ndi_receiver_prepare_video_map(vj_ndi_receiver *receiver,
                                               int source_width,
                                               int source_height)
{
    if(!receiver->video_x_map || !receiver->video_y_map)
        return;
    if(receiver->video_map_source_width == source_width &&
       receiver->video_map_source_height == source_height)
        return;

    vj_ndi_build_scale_map(receiver->video_x_map, receiver->width, source_width);
    vj_ndi_build_scale_map(receiver->video_y_map, receiver->height, source_height);
    receiver->video_map_source_width = source_width;
    receiver->video_map_source_height = source_height;
}

static int vj_ndi_receiver_convert_video(vj_ndi_receiver *receiver,
                                         const NDIlib_video_frame_v2_t *frame)
{
    const int packed_width = (frame->xres + 1) & ~1;
    if(!frame->p_data || frame->xres < 2 || frame->yres <= 0 ||
       (frame->line_stride_in_bytes > 0 &&
        frame->line_stride_in_bytes < packed_width * 2))
        return 0;

    if(frame->FourCC != NDIlib_FourCC_video_type_UYVY &&
       frame->FourCC != NDIlib_FourCC_video_type_UYVA) {
        pthread_mutex_lock(&receiver->stats_mutex);
        receiver->stats.unsupported_video_frames++;
        pthread_mutex_unlock(&receiver->stats_mutex);
        if(!receiver->logged_unsupported) {
            receiver->logged_unsupported = 1;
            veejay_msg(VEEJAY_MSG_WARNING,
                       "NDI source '%s' supplied unsupported FourCC 0x%08x; requesting fastest UYVY remains enabled",
                       receiver->source_name, (unsigned int)frame->FourCC);
        }
        return 0;
    }

    vj_ndi_receiver_prepare_video_map(receiver, frame->xres, frame->yres);
    const int index = receiver->video_write_index;
    vj_ndi_convert_uyvy_to_444(frame,
                               receiver->video_planes[index][0],
                               receiver->video_planes[index][1],
                               receiver->video_planes[index][2],
                               receiver->width,
                               receiver->height,
                               receiver->video_x_map,
                               receiver->video_y_map);

    pthread_mutex_lock(&receiver->video_mutex);
    if(!__sync_fetch_and_add(&receiver->active, 0)) {
        pthread_mutex_unlock(&receiver->video_mutex);
        return 0;
    }
    receiver->video_ready_index = index;
    receiver->video_sequence++;
    for(int step = 1; step <= 3; step++) {
        const int candidate = (index + step) % 3;
        if(candidate != receiver->video_ready_index &&
           candidate != receiver->video_read_index) {
            receiver->video_write_index = candidate;
            break;
        }
    }
    pthread_mutex_unlock(&receiver->video_mutex);

    pthread_mutex_lock(&receiver->stats_mutex);
    receiver->stats.video_frames++;
    const uint64_t received_frames = receiver->stats.video_frames;
    receiver->stats.width = frame->xres;
    receiver->stats.height = frame->yres;
    receiver->stats.source_fps_n = frame->frame_rate_N;
    receiver->stats.source_fps_d = frame->frame_rate_D;
    receiver->stats.last_video_timecode = frame->timecode;
    receiver->stats.last_video_timestamp = frame->timestamp;
    vj_ndi_receiver_update_clock_locked(receiver, frame->timestamp,
                                        vj_ndi_monotonic_seconds());
    pthread_mutex_unlock(&receiver->stats_mutex);
    if(received_frames == 1)
        veejay_msg(VEEJAY_MSG_INFO, "NDI source '%s' delivered first video frame (%dx%d)",
                   receiver->source_name, frame->xres, frame->yres);
    if(frame->p_metadata)
        vj_ndi_receiver_store_metadata_text(receiver, frame->p_metadata);
    vj_ndi_receiver_update_connection(receiver, 1);
    return 1;
}

static uint64_t vj_ndi_audio_ring_push_locked(vj_ndi_receiver *receiver,
                                               const int16_t *samples,
                                               size_t frames)
{
    if(!samples || frames == 0 || receiver->audio_capacity_frames == 0)
        return 0;

    uint64_t dropped = 0;
    uint64_t used = receiver->audio_write_pos - receiver->audio_read_pos;
    if(frames > receiver->audio_capacity_frames) {
        samples += (frames - receiver->audio_capacity_frames) *
                   (size_t)receiver->audio_channels;
        frames = receiver->audio_capacity_frames;
    }
    if(used + frames > receiver->audio_capacity_frames) {
        dropped = used + frames - receiver->audio_capacity_frames;
        receiver->audio_read_pos += dropped;
    }

    const size_t frame_bytes = (size_t)receiver->audio_bytes_per_frame;
    const size_t write_index = (size_t)(receiver->audio_write_pos %
                                        receiver->audio_capacity_frames);
    const size_t first = frames < receiver->audio_capacity_frames - write_index ?
                         frames : receiver->audio_capacity_frames - write_index;
    memcpy(receiver->audio_ring + write_index * (size_t)receiver->audio_channels,
           samples, first * frame_bytes);
    if(first < frames) {
        memcpy(receiver->audio_ring,
               samples + first * (size_t)receiver->audio_channels,
               (frames - first) * frame_bytes);
    }
    receiver->audio_write_pos += frames;
    __sync_lock_test_and_set(&receiver->audio_present, 1);
    return dropped;
}

static float vj_ndi_audio_sample_for_channel(const NDIlib_audio_frame_v3_t *frame,
                                             int sample,
                                             int dst_channel,
                                             int dst_channels)
{
    const int stride = frame->channel_stride_in_bytes > 0 ?
                       frame->channel_stride_in_bytes :
                       frame->no_samples * (int)sizeof(float);

    if(dst_channels == 1 && frame->no_channels > 1) {
        double sum = 0.0;
        for(int c = 0; c < frame->no_channels; c++) {
            const float *channel = (const float*)(frame->p_data +
                                   (size_t)c * (size_t)stride);
            sum += channel[sample];
        }
        return (float)(sum / (double)frame->no_channels);
    }

    const int source_channel = frame->no_channels == 1 ? 0 :
                               (dst_channel < frame->no_channels ?
                                dst_channel : frame->no_channels - 1);
    const float *channel = (const float*)(frame->p_data +
                           (size_t)source_channel * (size_t)stride);
    return channel[sample];
}

static int16_t vj_ndi_float_to_s16(float sample)
{
    if(sample >= 1.0f)
        return INT16_MAX;
    if(sample <= -1.0f)
        return INT16_MIN;
    return (int16_t)lrintf(sample * 32767.0f);
}

static void vj_ndi_receiver_store_audio(vj_ndi_receiver *receiver,
                                        const NDIlib_audio_frame_v3_t *frame)
{
    if(!frame->p_data || frame->sample_rate <= 0 || frame->no_channels <= 0 ||
       frame->no_samples <= 0 || frame->FourCC != NDIlib_FourCC_audio_type_FLTP)
        return;

    uint64_t dropped = 0;
    size_t out_frames = 0;
    pthread_mutex_lock(&receiver->audio_mutex);
    if(!__sync_fetch_and_add(&receiver->active, 0)) {
        pthread_mutex_unlock(&receiver->audio_mutex);
        return;
    }

    if(receiver->audio_source_rate != frame->sample_rate) {
        receiver->audio_source_rate = frame->sample_rate;
        receiver->audio_resample_phase = 0.0;
        receiver->audio_previous_valid = 0;
    }

    const double step = (double)frame->sample_rate / (double)receiver->audio_rate;
    double source_position = receiver->audio_resample_phase;
    if(frame->sample_rate == receiver->audio_rate) {
        out_frames = (size_t)frame->no_samples;
        if(out_frames > receiver->audio_scratch_capacity_frames) {
            dropped += out_frames - receiver->audio_scratch_capacity_frames;
            out_frames = receiver->audio_scratch_capacity_frames;
        }
        for(size_t i = 0; i < out_frames; i++) {
            for(int c = 0; c < receiver->audio_channels; c++) {
                receiver->audio_scratch[i * (size_t)receiver->audio_channels +
                                        (size_t)c] =
                    vj_ndi_float_to_s16(vj_ndi_audio_sample_for_channel(
                        frame, (int)i, c, receiver->audio_channels));
            }
        }
        source_position = (double)frame->no_samples;
        receiver->audio_previous_valid = 0;
    }
    while(frame->sample_rate != receiver->audio_rate &&
          out_frames < receiver->audio_scratch_capacity_frames) {
        int a = 0;
        int b = 0;
        float fraction = 0.0f;
        const int crosses_packet = source_position < 0.0;

        if(crosses_packet) {
            if(!receiver->audio_previous_valid)
                source_position = 0.0;
            else {
                a = -1;
                b = 0;
                fraction = (float)(source_position + 1.0);
            }
        }

        if(source_position >= 0.0) {
            if(source_position >= (double)frame->no_samples)
                break;
            a = (int)source_position;
            b = a + 1;
            if(b >= frame->no_samples)
                break;
            fraction = (float)(source_position - (double)a);
        }

        for(int c = 0; c < receiver->audio_channels; c++) {
            const float sa = a < 0 ? receiver->audio_previous[c] :
                vj_ndi_audio_sample_for_channel(frame, a, c,
                                                receiver->audio_channels);
            const float sb = vj_ndi_audio_sample_for_channel(frame, b, c,
                                                              receiver->audio_channels);
            receiver->audio_scratch[out_frames * (size_t)receiver->audio_channels +
                                    (size_t)c] =
                vj_ndi_float_to_s16(sa + (sb - sa) * fraction);
        }
        out_frames++;
        source_position += step;
    }

    receiver->audio_resample_phase = source_position - (double)frame->no_samples;
    if(frame->sample_rate != receiver->audio_rate) {
        for(int c = 0; c < receiver->audio_channels; c++)
            receiver->audio_previous[c] =
                vj_ndi_audio_sample_for_channel(frame, frame->no_samples - 1, c,
                                                receiver->audio_channels);
        receiver->audio_previous_valid = 1;
    }

    if(out_frames == receiver->audio_scratch_capacity_frames &&
       source_position < (double)frame->no_samples) {
        dropped += (uint64_t)ceil(((double)frame->no_samples - source_position) / step);
        receiver->audio_resample_phase = 0.0;
        receiver->audio_previous_valid = 0;
    }
    pthread_mutex_unlock(&receiver->audio_mutex);

    pthread_mutex_lock(&receiver->audio_ring_mutex);
    if(!__sync_fetch_and_add(&receiver->active, 0)) {
        pthread_mutex_unlock(&receiver->audio_ring_mutex);
        return;
    }
    dropped += vj_ndi_audio_ring_push_locked(receiver,
                                              receiver->audio_scratch,
                                              out_frames);
    pthread_mutex_unlock(&receiver->audio_ring_mutex);

    pthread_mutex_lock(&receiver->stats_mutex);
    receiver->stats.audio_frames += out_frames;
    receiver->stats.dropped_audio_frames += dropped;
    receiver->stats.audio_rate = frame->sample_rate;
    receiver->stats.audio_channels = frame->no_channels;
    receiver->stats.last_audio_timecode = frame->timecode;
    receiver->stats.last_audio_timestamp = frame->timestamp;
    pthread_mutex_unlock(&receiver->stats_mutex);
    if(frame->p_metadata)
        vj_ndi_receiver_store_metadata_text(receiver, frame->p_metadata);
    vj_ndi_receiver_update_connection(receiver, 1);
}

static int vj_ndi_receiver_wait_until_active(vj_ndi_receiver *receiver)
{
    pthread_mutex_lock(&receiver->state_mutex);
    while(!__sync_fetch_and_add(&receiver->stop, 0) &&
          !__sync_fetch_and_add(&receiver->active, 0))
        pthread_cond_wait(&receiver->state_cond, &receiver->state_mutex);
    const int running = !__sync_fetch_and_add(&receiver->stop, 0);
    pthread_mutex_unlock(&receiver->state_mutex);
    return running;
}

static void *vj_ndi_video_thread(void *data)
{
    vj_ndi_receiver *receiver = (vj_ndi_receiver*)data;
    int timeout_count = 0;
    while(vj_ndi_receiver_wait_until_active(receiver)) {
        NDIlib_video_frame_v2_t video;
        NDIlib_metadata_frame_t metadata;
        memset(&video, 0, sizeof(video));
        memset(&metadata, 0, sizeof(metadata));
        NDIlib_frame_type_e type = receiver->api->recv_capture_v3(
            receiver->instance, &video, NULL, &metadata, VJ_NDI_CAPTURE_TIMEOUT_MS);

        if(type != NDIlib_frame_type_none)
            timeout_count = 0;
        if(type == NDIlib_frame_type_video) {
            vj_ndi_receiver_convert_video(receiver, &video);
            receiver->api->recv_free_video_v2(receiver->instance, &video);
        } else if(type == NDIlib_frame_type_metadata) {
            vj_ndi_receiver_store_metadata(receiver, &metadata);
            receiver->api->recv_free_metadata(receiver->instance, &metadata);
        } else if(type == NDIlib_frame_type_status_change) {
            vj_ndi_receiver_update_connection(receiver,
                vj_ndi_receiver_connection_count(receiver) > 0);
        } else if(type == NDIlib_frame_type_none) {
            timeout_count++;
            if(timeout_count >= 10) {
                timeout_count = 0;
                vj_ndi_receiver_update_connection(receiver,
                    vj_ndi_receiver_connection_count(receiver) > 0);
            }
        }
    }
    return NULL;
}

static void *vj_ndi_audio_thread(void *data)
{
    vj_ndi_receiver *receiver = (vj_ndi_receiver*)data;
    int timeout_count = 0;
    while(vj_ndi_receiver_wait_until_active(receiver)) {
        NDIlib_audio_frame_v3_t audio;
        NDIlib_metadata_frame_t metadata;
        memset(&audio, 0, sizeof(audio));
        memset(&metadata, 0, sizeof(metadata));
        NDIlib_frame_type_e type = receiver->api->recv_capture_v3(
            receiver->instance, NULL, &audio, &metadata, VJ_NDI_CAPTURE_TIMEOUT_MS);

        if(type != NDIlib_frame_type_none)
            timeout_count = 0;
        if(type == NDIlib_frame_type_audio) {
            vj_ndi_receiver_store_audio(receiver, &audio);
            receiver->api->recv_free_audio_v3(receiver->instance, &audio);
        } else if(type == NDIlib_frame_type_metadata) {
            vj_ndi_receiver_store_metadata(receiver, &metadata);
            receiver->api->recv_free_metadata(receiver->instance, &metadata);
        } else if(type == NDIlib_frame_type_status_change) {
            vj_ndi_receiver_update_connection(receiver,
                vj_ndi_receiver_connection_count(receiver) > 0);
        } else if(type == NDIlib_frame_type_none) {
            timeout_count++;
            if(timeout_count >= 10) {
                timeout_count = 0;
                vj_ndi_receiver_update_connection(receiver,
                    vj_ndi_receiver_connection_count(receiver) > 0);
            }
        }
    }
    return NULL;
}

static int vj_ndi_source_matches_label(const char *advertised, const char *requested)
{
    if(!advertised || !requested)
        return 0;
    if(strcmp(advertised, requested) == 0)
        return 1;
    const size_t alen = strlen(advertised);
    const size_t rlen = strlen(requested);
    if(alen <= rlen + 3 || advertised[alen - 1] != ')')
        return 0;
    const size_t open = alen - rlen - 2;
    return advertised[open] == '(' &&
           (open == 0 || advertised[open - 1] == ' ') &&
           memcmp(advertised + open + 1, requested, rlen) == 0;
}

static int vj_ndi_resolve_source(const char *requested,
                                 char *resolved_name, size_t name_size,
                                 char *resolved_url, size_t url_size)
{
    if(!requested || !*requested)
        return 0;
    snprintf(resolved_name, name_size, "%s", requested);
    if(resolved_url && url_size)
        resolved_url[0] = '\0';
    if(strchr(requested, '(') && requested[strlen(requested) - 1] == ')')
        return 1;

    vj_ndi_source_info sources[256];
    const int count = vj_ndi_discover(sources, 256, 350);
    int match = -1;
    for(int i = 0; i < count; i++) {
        if(!vj_ndi_source_matches_label(sources[i].name, requested))
            continue;
        if(match >= 0) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "NDI source label '%s' is ambiguous; use the exact discovered source name",
                       requested);
            return 0;
        }
        match = i;
    }
    if(match >= 0) {
        snprintf(resolved_name, name_size, "%s", sources[match].name);
        if(resolved_url && url_size)
            snprintf(resolved_url, url_size, "%s", sources[match].url);
    }
    return 1;
}

vj_ndi_receiver *vj_ndi_receiver_create(const char *source_name,
                                         int width,
                                         int height,
                                         double fps,
                                         int audio_rate,
                                         int audio_channels)
{
    if(!source_name || !*source_name ||
       strlen(source_name) > VJ_NDI_SOURCE_NAME_MAX ||
       width <= 0 || height <= 0 || fps <= 0.0 ||
       audio_rate <= 0 || audio_channels <= 0 ||
       !vj_ndi_runtime_acquire())
        return NULL;

    if(!vj_ndi_api_has_receiver(ndi_runtime.api)) {
        vj_ndi_runtime_release();
        return NULL;
    }

    char resolved_name[256];
    char resolved_url[512];
    if(!vj_ndi_resolve_source(source_name, resolved_name, sizeof(resolved_name),
                              resolved_url, sizeof(resolved_url))) {
        vj_ndi_runtime_release();
        return NULL;
    }

    vj_ndi_receiver *receiver = (vj_ndi_receiver*)calloc(1, sizeof(*receiver));
    if(!receiver) {
        vj_ndi_runtime_release();
        return NULL;
    }
    receiver->api = ndi_runtime.api;
    receiver->source_name = vj_ndi_strdup(resolved_name);
    receiver->width = width;
    receiver->height = height;
    receiver->fps = fps;
    receiver->audio_rate = audio_rate;
    receiver->audio_channels = audio_channels;
    receiver->audio_bits = 16;
    receiver->audio_bytes_per_frame = audio_channels * 2;
    receiver->video_ready_index = -1;
    receiver->video_read_index = -1;
    receiver->active = 1;
    receiver->stats.connected = -1;
    snprintf(receiver->stats.published_name, sizeof(receiver->stats.published_name), "%s", resolved_name);
    snprintf(receiver->stats.published_url, sizeof(receiver->stats.published_url), "%s", resolved_url);

    pthread_mutex_init(&receiver->state_mutex, NULL);
    pthread_cond_init(&receiver->state_cond, NULL);
    pthread_mutex_init(&receiver->video_mutex, NULL);
    pthread_mutex_init(&receiver->audio_mutex, NULL);
    pthread_mutex_init(&receiver->audio_ring_mutex, NULL);
    pthread_mutex_init(&receiver->stats_mutex, NULL);

    const size_t plane = (size_t)width * (size_t)height;
    receiver->video_storage = (uint8_t*)malloc(plane * 3u * 3u);
    receiver->video_map_storage = (int*)malloc(((size_t)width + (size_t)height) * sizeof(int));
    receiver->audio_capacity_frames = (size_t)audio_rate * VJ_NDI_AUDIO_RING_SECONDS;
    receiver->audio_scratch_capacity_frames = (size_t)audio_rate;
    receiver->audio_ring = (int16_t*)calloc(receiver->audio_capacity_frames *
                                            (size_t)audio_channels,
                                            sizeof(int16_t));
    receiver->audio_scratch = (int16_t*)malloc(receiver->audio_scratch_capacity_frames *
                                               (size_t)audio_channels *
                                               sizeof(int16_t));
    receiver->audio_previous = (float*)calloc((size_t)audio_channels, sizeof(float));
    if(!receiver->source_name || !receiver->video_storage ||
       !receiver->video_map_storage || !receiver->audio_ring ||
       !receiver->audio_scratch || !receiver->audio_previous)
        goto fail;

    receiver->video_x_map = receiver->video_map_storage;
    receiver->video_y_map = receiver->video_x_map + width;
    for(int i = 0; i < 3; i++) {
        uint8_t *base = receiver->video_storage + plane * 3u * (size_t)i;
        receiver->video_planes[i][0] = base;
        receiver->video_planes[i][1] = base + plane;
        receiver->video_planes[i][2] = base + plane * 2u;
    }

    NDIlib_recv_create_v3_t create_desc;
    memset(&create_desc, 0, sizeof(create_desc));
    create_desc.source_to_connect_to.p_ndi_name = receiver->source_name;
    create_desc.source_to_connect_to.p_url_address = resolved_url[0] ? resolved_url : NULL;
    create_desc.color_format = NDIlib_recv_color_format_fastest;
    create_desc.bandwidth = NDIlib_recv_bandwidth_highest;
    create_desc.allow_video_fields = false;
    create_desc.p_ndi_recv_name = "VeeJay NDI input";
    receiver->instance = receiver->api->recv_create_v3(&create_desc);
    if(!receiver->instance)
        goto fail;

    if(receiver->api->recv_add_connection_metadata) {
        NDIlib_metadata_frame_t product;
        memset(&product, 0, sizeof(product));
        product.p_data = "<ndi_product long_name=\"Linux VeeJay NDI Receiver\" "
                         "short_name=\"VeeJay\" manufacturer=\"VeeJay\" "
                         "model_name=\"VeeJay\" version=\"" PACKAGE_VERSION "\" "
                         "session_name=\"VeeJay live input\" />";
        receiver->api->recv_add_connection_metadata(receiver->instance, &product);
    }

    if(pthread_create(&receiver->video_thread, NULL,
                      vj_ndi_video_thread, receiver) != 0)
        goto fail;
    receiver->video_thread_started = 1;
    if(pthread_create(&receiver->audio_thread, NULL,
                      vj_ndi_audio_thread, receiver) != 0)
        goto fail;
    receiver->audio_thread_started = 1;

    veejay_msg(VEEJAY_MSG_INFO,
               "NDI receiver created for '%s' (%dx%d target, %.3f fps, %d Hz/%d ch audio)",
               receiver->source_name, width, height, fps, audio_rate, audio_channels);
    return receiver;

fail:
    vj_ndi_receiver_destroy(receiver);
    return NULL;
}

void vj_ndi_receiver_destroy(vj_ndi_receiver *receiver)
{
    if(!receiver)
        return;
    __sync_lock_test_and_set(&receiver->stop, 1);
    pthread_mutex_lock(&receiver->state_mutex);
    pthread_cond_broadcast(&receiver->state_cond);
    pthread_mutex_unlock(&receiver->state_mutex);
    if(receiver->video_thread_started)
        pthread_join(receiver->video_thread, NULL);
    if(receiver->audio_thread_started)
        pthread_join(receiver->audio_thread, NULL);
    if(receiver->instance && receiver->api)
        receiver->api->recv_destroy(receiver->instance);
    pthread_mutex_destroy(&receiver->state_mutex);
    pthread_cond_destroy(&receiver->state_cond);
    pthread_mutex_destroy(&receiver->video_mutex);
    pthread_mutex_destroy(&receiver->audio_mutex);
    pthread_mutex_destroy(&receiver->audio_ring_mutex);
    pthread_mutex_destroy(&receiver->stats_mutex);
    free(receiver->audio_previous);
    free(receiver->audio_scratch);
    free(receiver->audio_ring);
    free(receiver->video_map_storage);
    free(receiver->video_storage);
    free(receiver->source_name);
    free(receiver);
    vj_ndi_runtime_release();
}

int vj_ndi_receiver_set_active(vj_ndi_receiver *receiver, int active)
{
    if(!receiver)
        return 0;

    const int enabled = active ? 1 : 0;
    const int previous = __sync_lock_test_and_set(&receiver->active, enabled);
    if(enabled) {
        pthread_mutex_lock(&receiver->state_mutex);
        pthread_cond_broadcast(&receiver->state_cond);
        pthread_mutex_unlock(&receiver->state_mutex);
    }
    if(previous && !enabled) {
        pthread_mutex_lock(&receiver->audio_mutex);
        receiver->audio_resample_phase = 0.0;
        receiver->audio_previous_valid = 0;
        receiver->audio_source_rate = 0;
        pthread_mutex_unlock(&receiver->audio_mutex);

        pthread_mutex_lock(&receiver->audio_ring_mutex);
        receiver->audio_read_pos = receiver->audio_write_pos;
        receiver->audio_pull_accum = 0.0;
        __sync_lock_test_and_set(&receiver->audio_present, 0);
        pthread_mutex_unlock(&receiver->audio_ring_mutex);

        pthread_mutex_lock(&receiver->video_mutex);
        receiver->video_consumed_sequence = receiver->video_sequence;
        pthread_mutex_unlock(&receiver->video_mutex);

        pthread_mutex_lock(&receiver->stats_mutex);
        receiver->clock_valid = 0;
        receiver->stats.clock_available = 0;
        receiver->stats.clock_age_ms = 0;
        pthread_mutex_unlock(&receiver->stats_mutex);
    }
    return 1;
}

int vj_ndi_receiver_get_video(vj_ndi_receiver *receiver, VJFrame *dst)
{
    if(!receiver || !dst || !__sync_fetch_and_add(&receiver->active, 0))
        return 0;
    if(dst->width != receiver->width || dst->height != receiver->height)
        return 0;

    if(!dst->data[0] || !dst->data[1] || !dst->data[2])
        return 0;

    pthread_mutex_lock(&receiver->video_mutex);
    if(receiver->video_ready_index < 0) {
        pthread_mutex_unlock(&receiver->video_mutex);
        return 0;
    }
    const uint64_t sequence = receiver->video_sequence;
    const uint64_t pending = sequence - receiver->video_consumed_sequence;
    const int index = receiver->video_ready_index;
    receiver->video_read_index = index;
    pthread_mutex_unlock(&receiver->video_mutex);

    const size_t plane = (size_t)receiver->width * (size_t)receiver->height;
    memcpy(dst->data[0], receiver->video_planes[index][0], plane);
    memcpy(dst->data[1], receiver->video_planes[index][1], plane);
    memcpy(dst->data[2], receiver->video_planes[index][2], plane);

    pthread_mutex_lock(&receiver->video_mutex);
    if(receiver->video_read_index == index)
        receiver->video_read_index = -1;
    receiver->video_consumed_sequence = sequence;
    pthread_mutex_unlock(&receiver->video_mutex);
    if(pending > 1) {
        pthread_mutex_lock(&receiver->stats_mutex);
        receiver->stats.dropped_video_frames += pending - 1;
        pthread_mutex_unlock(&receiver->stats_mutex);
    }
    return 1;
}

int vj_ndi_receiver_get_audio(vj_ndi_receiver *receiver, uint8_t *dst)
{
    if(!receiver || !dst || receiver->audio_capacity_frames == 0 ||
       !__sync_fetch_and_add(&receiver->active, 0))
        return 0;

    int16_t *out = (int16_t*)dst;
    pthread_mutex_lock(&receiver->audio_ring_mutex);
    receiver->audio_pull_accum += (double)receiver->audio_rate / receiver->fps;
    int wanted = (int)floor(receiver->audio_pull_accum);
    receiver->audio_pull_accum -= wanted;
    if(wanted <= 0) {
        pthread_mutex_unlock(&receiver->audio_ring_mutex);
        return 0;
    }

    uint64_t available = receiver->audio_write_pos - receiver->audio_read_pos;
    size_t copied = available < (uint64_t)wanted ? (size_t)available : (size_t)wanted;
    const size_t frame_bytes = (size_t)receiver->audio_bytes_per_frame;
    const size_t read_index = (size_t)(receiver->audio_read_pos %
                                       receiver->audio_capacity_frames);
    const size_t first = copied < receiver->audio_capacity_frames - read_index ?
                         copied : receiver->audio_capacity_frames - read_index;
    memcpy(out,
           receiver->audio_ring + read_index * (size_t)receiver->audio_channels,
           first * frame_bytes);
    if(first < copied) {
        memcpy(out + first * (size_t)receiver->audio_channels,
               receiver->audio_ring,
               (copied - first) * frame_bytes);
    }
    receiver->audio_read_pos += copied;
    pthread_mutex_unlock(&receiver->audio_ring_mutex);

    if(copied < (size_t)wanted) {
        memset(out + copied * (size_t)receiver->audio_channels, 0,
               ((size_t)wanted - copied) * frame_bytes);
        pthread_mutex_lock(&receiver->stats_mutex);
        receiver->stats.audio_underruns++;
        pthread_mutex_unlock(&receiver->stats_mutex);
    }
    return wanted;
}

int vj_ndi_receiver_has_audio(vj_ndi_receiver *receiver)
{
    return receiver ? __sync_fetch_and_add(&receiver->audio_present, 0) : 0;
}

int vj_ndi_receiver_get_audio_format(vj_ndi_receiver *receiver,
                                     int *sample_rate,
                                     int *channels,
                                     int *bits,
                                     int *bytes_per_frame)
{
    if(!receiver)
        return 0;
    if(sample_rate) *sample_rate = receiver->audio_rate;
    if(channels) *channels = receiver->audio_channels;
    if(bits) *bits = receiver->audio_bits;
    if(bytes_per_frame) *bytes_per_frame = receiver->audio_bytes_per_frame;
    return 1;
}

int vj_ndi_receiver_set_tally(vj_ndi_receiver *receiver,
                              int program,
                              int preview)
{
    if(!receiver || !receiver->instance || !receiver->api->recv_set_tally)
        return 0;
    NDIlib_tally_t tally;
    tally.on_program = program ? true : false;
    tally.on_preview = preview ? true : false;
    const int result = receiver->api->recv_set_tally(receiver->instance, &tally) ? 1 : 0;
    if(result) {
        pthread_mutex_lock(&receiver->stats_mutex);
        receiver->stats.program_tally = program ? 1 : 0;
        receiver->stats.preview_tally = preview ? 1 : 0;
        pthread_mutex_unlock(&receiver->stats_mutex);
    }
    return result;
}

int vj_ndi_receiver_clock_now(vj_ndi_receiver *receiver,
                              double *clock_seconds,
                              int *age_ms)
{
    if(!receiver || !clock_seconds)
        return 0;

    const double now = vj_ndi_monotonic_seconds();
    pthread_mutex_lock(&receiver->stats_mutex);
    if(!receiver->clock_valid) {
        pthread_mutex_unlock(&receiver->stats_mutex);
        return 0;
    }

    const double age = now - receiver->clock_local_arrival;
    const double maximum_age = fmax(0.25, 4.0 / receiver->fps);
    if(age < 0.0 || age > maximum_age) {
        receiver->stats.clock_available = 0;
        receiver->stats.clock_age_ms = age > 0.0 ? (int)llround(age * 1000.0) : 0;
        pthread_mutex_unlock(&receiver->stats_mutex);
        return 0;
    }

    const double source_elapsed =
        (double)(receiver->clock_source_last - receiver->clock_source_anchor) /
        VJ_NDI_TIMECODE_TICKS_PER_SECOND;
    const double extrapolate = fmin(age, 2.0 / receiver->fps);
    *clock_seconds = receiver->clock_local_anchor + source_elapsed + extrapolate;
    receiver->stats.clock_available = 1;
    receiver->stats.clock_age_ms = (int)llround(age * 1000.0);
    if(age_ms)
        *age_ms = receiver->stats.clock_age_ms;
    pthread_mutex_unlock(&receiver->stats_mutex);
    return 1;
}

void vj_ndi_receiver_get_stats(vj_ndi_receiver *receiver, vj_ndi_stats *stats)
{
    if(!receiver || !stats)
        return;
    double ignored_clock = 0.0;
    vj_ndi_receiver_clock_now(receiver, &ignored_clock, NULL);
    pthread_mutex_lock(&receiver->stats_mutex);
    *stats = receiver->stats;
    pthread_mutex_unlock(&receiver->stats_mutex);
}

static int vj_ndi_gcd(int a, int b)
{
    while(b != 0) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a < 0 ? -a : a;
}

static void vj_ndi_fps_ratio(double fps, int *numerator, int *denominator)
{
    const struct { double fps; int n; int d; } common[] = {
        {23.976, 24000, 1001}, {29.970, 30000, 1001},
        {47.952, 48000, 1001}, {59.940, 60000, 1001},
        {119.880, 120000, 1001}
    };
    for(size_t i = 0; i < sizeof(common) / sizeof(common[0]); i++) {
        if(fabs(fps - common[i].fps) < 0.02) {
            *numerator = common[i].n;
            *denominator = common[i].d;
            return;
        }
    }
    int n = (int)llround(fps * 1000.0);
    int d = 1000;
    int g = vj_ndi_gcd(n, d);
    *numerator = n / g;
    *denominator = d / g;
}

static void vj_ndi_convert_444_to_uyvy(const VJFrame *src,
                                       uint8_t *dst,
                                       int width,
                                       int height,
                                       int stride,
                                       const int *x0_map,
                                       const int *x1_map,
                                       const int *y_map)
{
    const uint8_t *y_plane = src->data[0];
    const uint8_t *u_plane = src->data[1];
    const uint8_t *v_plane = src->data[2];
    const int src_width = src->width;
    const int src_height = src->height;

    if(src_width == width && src_height == height) {
        for(int y = 0; y < height; y++) {
            const size_t row = (size_t)y * (size_t)width;
            uint8_t *out = dst + (size_t)y * (size_t)stride;
            for(int x = 0; x < width; x += 2) {
                const size_t p0 = row + (size_t)x;
                const size_t p1 = row + (size_t)(x + 1 < width ? x + 1 : x);
                out[0] = (uint8_t)(((int)u_plane[p0] + (int)u_plane[p1] + 1) >> 1);
                out[1] = y_plane[p0];
                out[2] = (uint8_t)(((int)v_plane[p0] + (int)v_plane[p1] + 1) >> 1);
                out[3] = y_plane[p1];
                out += 4;
            }
        }
        return;
    }

    for(int y = 0; y < height; y++) {
        const int sy = y_map ? y_map[y] :
                       (int)(((int64_t)y * src_height) / height);
        const size_t row = (size_t)sy * (size_t)src_width;
        uint8_t *out = dst + (size_t)y * (size_t)stride;
        for(int x = 0; x < width; x += 2) {
            int sx0 = x0_map ? x0_map[x >> 1] :
                      (int)(((int64_t)x * src_width) / width);
            int sx1 = x1_map ? x1_map[x >> 1] :
                      (int)(((int64_t)(x + 1 < width ? x + 1 : x) *
                              src_width) / width);
            if(sx0 >= src_width) sx0 = src_width - 1;
            if(sx1 >= src_width) sx1 = src_width - 1;
            const size_t p0 = row + (size_t)sx0;
            const size_t p1 = row + (size_t)sx1;
            out[0] = (uint8_t)(((int)u_plane[p0] + (int)u_plane[p1] + 1) >> 1);
            out[1] = y_plane[p0];
            out[2] = (uint8_t)(((int)v_plane[p0] + (int)v_plane[p1] + 1) >> 1);
            out[3] = y_plane[p1];
            out += 4;
        }
    }
}

static void vj_ndi_sender_prepare_video_map(vj_ndi_sender *sender,
                                             int source_width,
                                             int source_height)
{
    if(sender->video_map_source_width == source_width &&
       sender->video_map_source_height == source_height)
        return;

    const int pairs = (sender->width + 1) >> 1;
    for(int pair = 0; pair < pairs; pair++) {
        const int x = pair << 1;
        int sx0 = (int)(((int64_t)x * source_width) / sender->width);
        int sx1 = (int)(((int64_t)(x + 1 < sender->width ? x + 1 : x) *
                         source_width) / sender->width);
        sender->video_x0_map[pair] = sx0 < source_width ? sx0 : source_width - 1;
        sender->video_x1_map[pair] = sx1 < source_width ? sx1 : source_width - 1;
    }
    vj_ndi_build_scale_map(sender->video_y_map, sender->height, source_height);
    sender->video_map_source_width = source_width;
    sender->video_map_source_height = source_height;
}

vj_ndi_sender *vj_ndi_sender_create(const char *name,
                                    int width,
                                    int height,
                                    double fps,
                                    int audio_rate,
                                    int audio_channels)
{
    if(!name || !*name || strlen(name) > VJ_NDI_SOURCE_NAME_MAX ||
       width <= 0 || height <= 0 || fps <= 0.0 ||
       !vj_ndi_runtime_acquire())
        return NULL;

    if(!vj_ndi_api_has_sender(ndi_runtime.api)) {
        vj_ndi_runtime_release();
        return NULL;
    }

    vj_ndi_sender *sender = (vj_ndi_sender*)calloc(1, sizeof(*sender));
    if(!sender) {
        vj_ndi_runtime_release();
        return NULL;
    }
    sender->api = ndi_runtime.api;
    sender->name = vj_ndi_strdup(name);
    sender->width = width;
    sender->height = height;
    sender->audio_rate = audio_rate;
    sender->audio_channels = audio_channels;
    vj_ndi_fps_ratio(fps, &sender->frame_rate_n, &sender->frame_rate_d);
    pthread_mutex_init(&sender->mutex, NULL);
    pthread_mutex_init(&sender->video_mutex, NULL);

    sender->video_stride = ((width + 1) & ~1) * 2;
    const size_t bytes = (size_t)sender->video_stride * (size_t)height;
    sender->video_buffers[0] = (uint8_t*)malloc(bytes);
    sender->video_buffers[1] = (uint8_t*)malloc(bytes);
    const size_t pairs = ((size_t)width + 1u) >> 1;
    sender->video_map_storage = (int*)malloc((pairs * 2u + (size_t)height) * sizeof(int));
    if(!sender->name || !sender->video_buffers[0] || !sender->video_buffers[1] ||
       !sender->video_map_storage)
        goto fail;
    sender->video_x0_map = sender->video_map_storage;
    sender->video_x1_map = sender->video_x0_map + pairs;
    sender->video_y_map = sender->video_x1_map + pairs;

    NDIlib_send_create_t create_desc;
    memset(&create_desc, 0, sizeof(create_desc));
    create_desc.p_ndi_name = sender->name;
    create_desc.p_groups = NULL;
    create_desc.clock_video = false;
    create_desc.clock_audio = false;
    sender->instance = sender->api->send_create(&create_desc);
    if(!sender->instance)
        goto fail;

    snprintf(sender->stats.published_name, sizeof(sender->stats.published_name), "%s", sender->name);
    if(sender->api->send_get_source_name) {
        const NDIlib_source_t *published = sender->api->send_get_source_name(sender->instance);
        if(published) {
            if(published->p_ndi_name && *published->p_ndi_name)
                snprintf(sender->stats.published_name, sizeof(sender->stats.published_name), "%s", published->p_ndi_name);
            if(published->p_url_address && *published->p_url_address)
                snprintf(sender->stats.published_url, sizeof(sender->stats.published_url), "%s", published->p_url_address);
        }
    }

    if(sender->api->send_add_connection_metadata) {
        NDIlib_metadata_frame_t product;
        memset(&product, 0, sizeof(product));
        product.p_data = "<ndi_product long_name=\"Linux VeeJay NDI Sender\" "
                         "short_name=\"VeeJay\" manufacturer=\"VeeJay\" "
                         "model_name=\"VeeJay\" version=\"" PACKAGE_VERSION "\" "
                         "session_name=\"VeeJay output\" />";
        sender->api->send_add_connection_metadata(sender->instance, &product);
    }

    veejay_msg(VEEJAY_MSG_INFO,
               "Publishing NDI source '%s' as '%s' at %dx%d %d/%d fps",
               sender->name, sender->stats.published_name, width, height,
               sender->frame_rate_n, sender->frame_rate_d);
    return sender;

fail:
    vj_ndi_sender_destroy(sender);
    return NULL;
}

void vj_ndi_sender_destroy(vj_ndi_sender *sender)
{
    if(!sender)
        return;
    pthread_mutex_lock(&sender->video_mutex);
    pthread_mutex_lock(&sender->mutex);
    if(sender->instance && sender->api) {
        sender->api->send_send_video_async_v2(sender->instance, NULL);
        sender->api->send_destroy(sender->instance);
    }
    pthread_mutex_unlock(&sender->mutex);
    pthread_mutex_unlock(&sender->video_mutex);
    pthread_mutex_destroy(&sender->video_mutex);
    pthread_mutex_destroy(&sender->mutex);
    free(sender->video_buffers[0]);
    free(sender->video_buffers[1]);
    free(sender->video_map_storage);
    free(sender->name);
    free(sender);
    vj_ndi_runtime_release();
}

int vj_ndi_sender_send_video(vj_ndi_sender *sender, const VJFrame *frame)
{
    if(!sender || !frame || !sender->instance || frame->width <= 0 ||
       frame->height <= 0 || !frame->data[0] || !frame->data[1] ||
       !frame->data[2])
        return 0;
    pthread_mutex_lock(&sender->video_mutex);
    const int index = sender->video_index;
    vj_ndi_sender_prepare_video_map(sender, frame->width, frame->height);
    vj_ndi_convert_444_to_uyvy(frame, sender->video_buffers[index],
                               sender->width, sender->height, sender->video_stride,
                               sender->video_x0_map, sender->video_x1_map,
                               sender->video_y_map);

    NDIlib_video_frame_v2_t video;
    memset(&video, 0, sizeof(video));
    video.xres = sender->width;
    video.yres = sender->height;
    video.FourCC = NDIlib_FourCC_video_type_UYVY;
    video.frame_rate_N = sender->frame_rate_n;
    video.frame_rate_D = sender->frame_rate_d;
    video.picture_aspect_ratio = (float)sender->width / (float)sender->height;
    video.frame_format_type = NDIlib_frame_format_type_progressive;
    video.timecode = NDIlib_send_timecode_synthesize;
    video.p_data = sender->video_buffers[index];
    video.line_stride_in_bytes = sender->video_stride;
    pthread_mutex_lock(&sender->mutex);
    sender->api->send_send_video_async_v2(sender->instance, &video);
    sender->video_index = 1 - index;
    sender->stats.video_frames++;
    const uint64_t sent_frames = sender->stats.video_frames;

    if(sender->stats.video_frames >= sender->next_telemetry_frame) {
        NDIlib_tally_t tally;
        if(sender->api->send_get_tally &&
           sender->api->send_get_tally(sender->instance, &tally, 0)) {
            sender->stats.program_tally = tally.on_program ? 1 : 0;
            sender->stats.preview_tally = tally.on_preview ? 1 : 0;
        }
        sender->stats.connected = sender->api->send_get_no_connections ?
                                  sender->api->send_get_no_connections(sender->instance, 0) : 0;
        uint64_t interval = (uint64_t)llround((double)sender->frame_rate_n /
                                              (double)sender->frame_rate_d);
        if(interval < 1)
            interval = 1;
        sender->next_telemetry_frame = sender->stats.video_frames + interval;
    }
    pthread_mutex_unlock(&sender->mutex);
    pthread_mutex_unlock(&sender->video_mutex);
    if(sent_frames == 1)
        veejay_msg(VEEJAY_MSG_INFO, "NDI sender '%s' submitted first video frame",
                   sender->stats.published_name[0] ? sender->stats.published_name : sender->name);
    return 1;
}

int vj_ndi_sender_send_audio(vj_ndi_sender *sender,
                             const uint8_t *interleaved_s16,
                             int sample_frames)
{
    return vj_ndi_sender_send_audio_format(sender, interleaved_s16,
                                           sample_frames,
                                           sender ? sender->audio_rate : 0,
                                           sender ? sender->audio_channels : 0);
}

int vj_ndi_sender_send_audio_format(vj_ndi_sender *sender,
                                    const uint8_t *interleaved_s16,
                                    int sample_frames,
                                    int sample_rate,
                                    int channels)
{
    if(!sender || !sender->instance || !interleaved_s16 || sample_frames <= 0 ||
       sample_rate <= 0 || channels <= 0 ||
       !sender->api->util_send_send_audio_interleaved_16s)
        return 0;

    NDIlib_audio_frame_interleaved_16s_t audio;
    memset(&audio, 0, sizeof(audio));
    audio.sample_rate = sample_rate;
    audio.no_channels = channels;
    audio.no_samples = sample_frames;
    audio.timecode = NDIlib_send_timecode_synthesize;
    audio.reference_level = 0;
    audio.p_data = (int16_t*)(uintptr_t)interleaved_s16;

    pthread_mutex_lock(&sender->mutex);
    sender->api->util_send_send_audio_interleaved_16s(sender->instance, &audio);
    sender->audio_rate = sample_rate;
    sender->audio_channels = channels;
    sender->stats.audio_frames += (uint64_t)sample_frames;
    pthread_mutex_unlock(&sender->mutex);
    return 1;
}

int vj_ndi_sender_send_metadata(vj_ndi_sender *sender, const char *xml)
{
    if(!sender || !sender->instance || !xml || !*xml ||
       !sender->api->send_send_metadata)
        return 0;
    NDIlib_metadata_frame_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.length = (int)strlen(xml) + 1;
    metadata.timecode = NDIlib_send_timecode_synthesize;
    metadata.p_data = (char*)(uintptr_t)xml;
    pthread_mutex_lock(&sender->mutex);
    sender->api->send_send_metadata(sender->instance, &metadata);
    pthread_mutex_unlock(&sender->mutex);
    return 1;
}

void vj_ndi_sender_get_stats(vj_ndi_sender *sender, vj_ndi_stats *stats)
{
    if(!sender || !stats)
        return;
    pthread_mutex_lock(&sender->mutex);
    *stats = sender->stats;
    stats->width = sender->width;
    stats->height = sender->height;
    stats->source_fps_n = sender->frame_rate_n;
    stats->source_fps_d = sender->frame_rate_d;
    stats->audio_rate = sender->audio_rate;
    stats->audio_channels = sender->audio_channels;
    pthread_mutex_unlock(&sender->mutex);
}

#else

int vj_ndi_runtime_available(void) { return 0; }
const char *vj_ndi_runtime_version(void) { return "not compiled"; }
void vj_ndi_runtime_shutdown(void) { }
int vj_ndi_discover(vj_ndi_source_info *sources, int max_sources, int timeout_ms)
{
    (void)sources; (void)max_sources; (void)timeout_ms; return 0;
}
char *vj_ndi_discovery_payload(int timeout_ms)
{
    (void)timeout_ms;
    char *payload = (char*)malloc(3);
    if(payload) memcpy(payload, "0\n", 3);
    return payload;
}
vj_ndi_receiver *vj_ndi_receiver_create(const char *source_name, int width,
                                         int height, double fps, int audio_rate,
                                         int audio_channels)
{
    (void)source_name; (void)width; (void)height; (void)fps;
    (void)audio_rate; (void)audio_channels; return NULL;
}
void vj_ndi_receiver_destroy(vj_ndi_receiver *receiver) { (void)receiver; }
int vj_ndi_receiver_set_active(vj_ndi_receiver *receiver, int active)
{ (void)receiver; (void)active; return 0; }
int vj_ndi_receiver_get_video(vj_ndi_receiver *receiver, VJFrame *dst)
{ (void)receiver; (void)dst; return 0; }
int vj_ndi_receiver_get_audio(vj_ndi_receiver *receiver, uint8_t *dst)
{ (void)receiver; (void)dst; return 0; }
int vj_ndi_receiver_has_audio(vj_ndi_receiver *receiver)
{ (void)receiver; return 0; }
int vj_ndi_receiver_get_audio_format(vj_ndi_receiver *receiver, int *sample_rate,
                                     int *channels, int *bits,
                                     int *bytes_per_frame)
{
    (void)receiver; (void)sample_rate; (void)channels; (void)bits;
    (void)bytes_per_frame; return 0;
}
int vj_ndi_receiver_set_tally(vj_ndi_receiver *receiver, int program, int preview)
{ (void)receiver; (void)program; (void)preview; return 0; }
int vj_ndi_receiver_clock_now(vj_ndi_receiver *receiver, double *clock_seconds,
                              int *age_ms)
{
    (void)receiver; (void)clock_seconds; (void)age_ms; return 0;
}
void vj_ndi_receiver_get_stats(vj_ndi_receiver *receiver, vj_ndi_stats *stats)
{ (void)receiver; if(stats) memset(stats, 0, sizeof(*stats)); }
vj_ndi_sender *vj_ndi_sender_create(const char *name, int width, int height,
                                    double fps, int audio_rate,
                                    int audio_channels)
{
    (void)name; (void)width; (void)height; (void)fps;
    (void)audio_rate; (void)audio_channels; return NULL;
}
void vj_ndi_sender_destroy(vj_ndi_sender *sender) { (void)sender; }
int vj_ndi_sender_send_video(vj_ndi_sender *sender, const VJFrame *frame)
{ (void)sender; (void)frame; return 0; }
int vj_ndi_sender_send_audio(vj_ndi_sender *sender,
                             const uint8_t *interleaved_s16,
                             int sample_frames)
{ (void)sender; (void)interleaved_s16; (void)sample_frames; return 0; }
int vj_ndi_sender_send_audio_format(vj_ndi_sender *sender,
                                    const uint8_t *interleaved_s16,
                                    int sample_frames,
                                    int sample_rate,
                                    int channels)
{
    (void)sender; (void)interleaved_s16; (void)sample_frames;
    (void)sample_rate; (void)channels; return 0;
}
int vj_ndi_sender_send_metadata(vj_ndi_sender *sender, const char *xml)
{ (void)sender; (void)xml; return 0; }
void vj_ndi_sender_get_stats(vj_ndi_sender *sender, vj_ndi_stats *stats)
{ (void)sender; if(stats) memset(stats, 0, sizeof(*stats)); }

#endif
