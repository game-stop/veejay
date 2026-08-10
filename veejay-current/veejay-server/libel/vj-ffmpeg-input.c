/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <veejaycore/vj-msg.h>
#include "vj-ffmpeg-input.h"

#define VJ_FFMPEG_AUDIO_INITIAL_BLOCK_SAMPLES 8192
#define VJ_FFMPEG_AUDIO_MAX_BLOCK_SAMPLES 262144
#define VJ_FFMPEG_AUDIO_SEEK_PREROLL_US 250000LL
#define VJ_FFMPEG_AUDIO_TIMESTAMP_TOLERANCE 8LL
#define VJ_FFMPEG_HW_DEFAULT_MIN_PIXELS 921600LL
#define VJ_FFMPEG_INDEX_CACHE_MAX_BYTES (16U * 1024U * 1024U)

typedef struct
{
    int64_t pts;
    int keyframe;
} vj_ffmpeg_index_entry;

typedef struct vj_ffmpeg_index_cache_entry
{
    dev_t device;
    ino_t inode;
    off_t file_size;
    time_t mtime_sec;
    int stream_index;
    enum AVCodecID codec_id;
    vj_ffmpeg_index_entry *entries;
    int64_t count;
    int64_t capacity;
    int64_t packet_count;
    int64_t discard_count;
    int reliable;
    int64_t keyframe_count;
    int64_t max_keyframe_gap;
    size_t bytes;
    unsigned int users;
    uint64_t stamp;
    struct vj_ffmpeg_index_cache_entry *next;
} vj_ffmpeg_index_cache_entry;

typedef enum
{
    VJ_FFMPEG_HW_POLICY_AUTO = 0,
    VJ_FFMPEG_HW_POLICY_SOFTWARE,
    VJ_FFMPEG_HW_POLICY_VULKAN,
    VJ_FFMPEG_HW_POLICY_VAAPI
} vj_ffmpeg_hw_policy;

typedef enum
{
    VJ_FFMPEG_HW_BACKEND_SOFTWARE = 0,
    VJ_FFMPEG_HW_BACKEND_VULKAN,
    VJ_FFMPEG_HW_BACKEND_VAAPI
} vj_ffmpeg_hw_backend;

typedef struct
{
    vj_ffmpeg_hw_policy policy;
    int64_t min_pixels;
} vj_ffmpeg_hw_policy_config;

typedef struct
{
    pthread_mutex_t mutex;
    AVBufferRef *device;
    unsigned int users;
    int unavailable;
} vj_ffmpeg_hw_device_slot;

static pthread_once_t vj_ffmpeg_hw_policy_once = PTHREAD_ONCE_INIT;
static vj_ffmpeg_hw_policy_config vj_ffmpeg_hw_policy_config_global = {
    VJ_FFMPEG_HW_POLICY_AUTO,
    VJ_FFMPEG_HW_DEFAULT_MIN_PIXELS
};
static vj_ffmpeg_hw_device_slot vj_ffmpeg_hw_vulkan_device = {
    PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0
};
static vj_ffmpeg_hw_device_slot vj_ffmpeg_hw_vaapi_device = {
    PTHREAD_MUTEX_INITIALIZER, NULL, 0, 0
};
static pthread_mutex_t vj_ffmpeg_index_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static vj_ffmpeg_index_cache_entry *vj_ffmpeg_index_cache = NULL;
static size_t vj_ffmpeg_index_cache_bytes = 0;
static uint64_t vj_ffmpeg_index_cache_clock = 0;
static pthread_once_t vj_ffmpeg_cleanup_once = PTHREAD_ONCE_INIT;

static void vj_ffmpeg_correct_trailing_frame_count(vj_ffmpeg_input *input);

static void vj_ffmpeg_global_cleanup(void)
{
    vj_ffmpeg_hw_device_slot *devices[] = {
        &vj_ffmpeg_hw_vulkan_device,
        &vj_ffmpeg_hw_vaapi_device
    };

    for(size_t i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
        pthread_mutex_lock(&devices[i]->mutex);
        av_buffer_unref(&devices[i]->device);
        pthread_mutex_unlock(&devices[i]->mutex);
    }

    pthread_mutex_lock(&vj_ffmpeg_index_cache_mutex);
    vj_ffmpeg_index_cache_entry **link = &vj_ffmpeg_index_cache;
    while(*link) {
        vj_ffmpeg_index_cache_entry *entry = *link;
        if(entry->users != 0) {
            link = &entry->next;
            continue;
        }
        *link = entry->next;
        vj_ffmpeg_index_cache_bytes -= entry->bytes;
        free(entry->entries);
        free(entry);
    }
    pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);
}

static void vj_ffmpeg_register_cleanup(void)
{
    atexit(vj_ffmpeg_global_cleanup);
}

struct vj_ffmpeg_input
{
    AVFormatContext *format_ctx;
    AVCodecContext *codec_ctx;
    const AVCodec *video_codec;
    AVStream *stream;
    AVFrame *frame;
    AVFrame *hw_transfer_frame;
    AVPacket *packet;
    struct SwsContext *scaler;
    int stream_index;
    enum AVPixelFormat out_pix_fmt;
    enum AVPixelFormat forced_source_pix_fmt;
    int out_width;
    int out_height;
    int dst_linesize[4];
    int64_t next_frame;
    int draining;
    vj_ffmpeg_hw_policy hw_policy;
    vj_ffmpeg_hw_backend hw_backend;
    AVBufferRef *hw_device_ctx;
    enum AVPixelFormat hw_pix_fmt;
    enum AVPixelFormat hw_transfer_pix_fmt;
    const void *hw_transfer_ctx_id;
    int hw_eligible;
    int hw_forced;
    int hw_format_selected;
    int hw_format_rejected;
    int hw_runtime_failed;
    int hw_last_error;
    int hw_vulkan_failed;
    int hw_vaapi_failed;
    int hw_session_warm;
    int hw_request_warmed;
    int64_t hw_source_pixels;
    AVFormatContext *audio_format_ctx;
    AVCodecContext *audio_codec_ctx;
    AVStream *audio_stream;
    AVFrame *audio_frame;
    AVPacket *audio_packet;
    SwrContext *audio_resampler;
    int audio_stream_index;
    int audio_draining;
    int audio_out_rate;
    int audio_out_channels;
    int audio_configured;
    uint8_t *audio_pcm;
    int audio_pcm_capacity;
    int audio_pcm_samples;
    int audio_pcm_offset;
    int64_t audio_pcm_start_sample;
    int64_t audio_resample_next_sample;
    int audio_resample_position_valid;
    int audio_resampler_drained;
    int audio_seek_requires_timestamp;
    int64_t audio_request_next_sample;
    int audio_request_valid;
    vj_ffmpeg_index_entry *index;
    int64_t index_count;
    int64_t index_capacity;
    vj_ffmpeg_index_cache_entry *index_cache_entry;
    vj_ffmpeg_input_stats stats;
    vj_ffmpeg_input_info info;
    char *filename;
    uint64_t slow_seek_warned;

    pthread_mutex_t index_mutex;
    pthread_t index_thread;
    int index_mutex_ready;
    int index_thread_started;
    volatile int index_cancel;
    int64_t index_last_refresh_us;

};

static const char *vj_ffmpeg_hw_policy_name(vj_ffmpeg_hw_policy policy)
{
    switch(policy) {
        case VJ_FFMPEG_HW_POLICY_SOFTWARE: return "software";
        case VJ_FFMPEG_HW_POLICY_VULKAN: return "vulkan";
        case VJ_FFMPEG_HW_POLICY_VAAPI: return "vaapi";
        case VJ_FFMPEG_HW_POLICY_AUTO:
        default: return "auto";
    }
}

static const char *vj_ffmpeg_hw_backend_name(vj_ffmpeg_hw_backend backend)
{
    switch(backend) {
        case VJ_FFMPEG_HW_BACKEND_VULKAN: return "vulkan";
        case VJ_FFMPEG_HW_BACKEND_VAAPI: return "vaapi";
        case VJ_FFMPEG_HW_BACKEND_SOFTWARE:
        default: return "software";
    }
}

static void vj_ffmpeg_hw_mark_failed(vj_ffmpeg_input *input,
                                     vj_ffmpeg_hw_backend backend)
{
    if(backend == VJ_FFMPEG_HW_BACKEND_VULKAN)
        input->hw_vulkan_failed = 1;
    else if(backend == VJ_FFMPEG_HW_BACKEND_VAAPI)
        input->hw_vaapi_failed = 1;
}

static int vj_ffmpeg_hw_is_failed(const vj_ffmpeg_input *input,
                                  vj_ffmpeg_hw_backend backend)
{
    if(backend == VJ_FFMPEG_HW_BACKEND_VULKAN)
        return input->hw_vulkan_failed;
    if(backend == VJ_FFMPEG_HW_BACKEND_VAAPI)
        return input->hw_vaapi_failed;
    return 0;
}

static void vj_ffmpeg_hw_runtime_error(vj_ffmpeg_input *input,
                                       int error,
                                       const char *stage)
{
    if(!input || input->hw_backend == VJ_FFMPEG_HW_BACKEND_SOFTWARE)
        return;

    if(!input->hw_runtime_failed) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' backend=%s runtime failure stage=%s ret=%d",
                   input->filename,
                   vj_ffmpeg_hw_backend_name(input->hw_backend),
                   stage,
                   error);
    }
    input->hw_runtime_failed = 1;
    input->hw_last_error = error;
}

static const char *vj_ffmpeg_hw_policy_order(vj_ffmpeg_hw_policy policy)
{
    switch(policy) {
        case VJ_FFMPEG_HW_POLICY_SOFTWARE: return "software";
        case VJ_FFMPEG_HW_POLICY_VULKAN: return "vulkan,software";
        case VJ_FFMPEG_HW_POLICY_VAAPI: return "vaapi,software";
        case VJ_FFMPEG_HW_POLICY_AUTO:
        default: return "vulkan,vaapi,software";
    }
}

static void vj_ffmpeg_hw_policy_init_once(void)
{
    const char *backend = getenv("VEEJAY_FFMPEG_HW_BACKEND");
    if(backend && *backend) {
        if(strcasecmp(backend, "auto") == 0)
            vj_ffmpeg_hw_policy_config_global.policy = VJ_FFMPEG_HW_POLICY_AUTO;
        else if(strcasecmp(backend, "software") == 0)
            vj_ffmpeg_hw_policy_config_global.policy = VJ_FFMPEG_HW_POLICY_SOFTWARE;
        else if(strcasecmp(backend, "vulkan") == 0)
            vj_ffmpeg_hw_policy_config_global.policy = VJ_FFMPEG_HW_POLICY_VULKAN;
        else if(strcasecmp(backend, "vaapi") == 0)
            vj_ffmpeg_hw_policy_config_global.policy = VJ_FFMPEG_HW_POLICY_VAAPI;
        else
            veejay_msg(VEEJAY_MSG_WARNING,
                       "[FFMPEG-HW] invalid VEEJAY_FFMPEG_HW_BACKEND='%s'; using auto",
                       backend);
    }

    const char *minimum = getenv("VEEJAY_FFMPEG_HW_MIN_PIXELS");
    if(minimum && *minimum) {
        char *end = NULL;
        errno = 0;
        long long value = strtoll(minimum, &end, 10);
        if(errno == 0 && end != minimum && *end == '\0' && value >= 0)
            vj_ffmpeg_hw_policy_config_global.min_pixels = (int64_t)value;
        else
            veejay_msg(VEEJAY_MSG_WARNING,
                       "[FFMPEG-HW] invalid VEEJAY_FFMPEG_HW_MIN_PIXELS='%s'; using %" PRId64,
                       minimum,
                       (int64_t)VJ_FFMPEG_HW_DEFAULT_MIN_PIXELS);
    }

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-HW] policy=%s min_pixels=%" PRId64 " order=%s",
               vj_ffmpeg_hw_policy_name(vj_ffmpeg_hw_policy_config_global.policy),
               vj_ffmpeg_hw_policy_config_global.min_pixels,
               vj_ffmpeg_hw_policy_order(vj_ffmpeg_hw_policy_config_global.policy));
}

static void vj_ffmpeg_hw_policy_for_input(vj_ffmpeg_input *input,
                                          const AVCodecDescriptor *desc)
{
    pthread_once(&vj_ffmpeg_hw_policy_once, vj_ffmpeg_hw_policy_init_once);

    input->hw_policy = vj_ffmpeg_hw_policy_config_global.policy;
    input->hw_backend = VJ_FFMPEG_HW_BACKEND_SOFTWARE;
    input->hw_pix_fmt = AV_PIX_FMT_NONE;
    input->hw_transfer_pix_fmt = AV_PIX_FMT_NONE;
    input->hw_forced = input->hw_policy == VJ_FFMPEG_HW_POLICY_VULKAN ||
                       input->hw_policy == VJ_FFMPEG_HW_POLICY_VAAPI;
    input->hw_source_pixels = input->stream->codecpar->width > 0 && input->stream->codecpar->height > 0 ?
                              (int64_t)input->stream->codecpar->width *
                              (int64_t)input->stream->codecpar->height : 0;

    if(desc && (desc->props & AV_CODEC_PROP_INTRA_ONLY)) {
        input->hw_eligible = 0;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' codec=%s backend=software reason=intra-only",
                   input->filename,
                   desc->name ? desc->name : "unknown");
        return;
    }

    if(input->hw_policy == VJ_FFMPEG_HW_POLICY_SOFTWARE) {
        input->hw_eligible = 0;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' backend=software reason=policy",
                   input->filename);
        return;
    }

    if(input->hw_policy == VJ_FFMPEG_HW_POLICY_AUTO &&
       input->hw_source_pixels < vj_ffmpeg_hw_policy_config_global.min_pixels) {
        input->hw_eligible = 0;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' %dx%d pixels=%" PRId64 " backend=software reason=below-threshold min=%" PRId64,
                   input->filename,
                   input->stream->codecpar->width,
                   input->stream->codecpar->height,
                   input->hw_source_pixels,
                   vj_ffmpeg_hw_policy_config_global.min_pixels);
        return;
    }

    input->hw_eligible = 1;
    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-HW] source='%s' %dx%d pixels=%" PRId64 " hardware-eligible policy=%s%s",
               input->filename,
               input->stream->codecpar->width,
               input->stream->codecpar->height,
               input->hw_source_pixels,
               vj_ffmpeg_hw_policy_name(input->hw_policy),
               input->hw_forced ? " threshold=bypassed" : "");
}

static enum AVHWDeviceType vj_ffmpeg_hw_device_type(vj_ffmpeg_hw_backend backend)
{
    switch(backend) {
        case VJ_FFMPEG_HW_BACKEND_VULKAN: return AV_HWDEVICE_TYPE_VULKAN;
        case VJ_FFMPEG_HW_BACKEND_VAAPI: return AV_HWDEVICE_TYPE_VAAPI;
        case VJ_FFMPEG_HW_BACKEND_SOFTWARE:
        default: return AV_HWDEVICE_TYPE_NONE;
    }
}

static vj_ffmpeg_hw_device_slot *vj_ffmpeg_hw_device_slot_for(vj_ffmpeg_hw_backend backend)
{
    switch(backend) {
        case VJ_FFMPEG_HW_BACKEND_VULKAN: return &vj_ffmpeg_hw_vulkan_device;
        case VJ_FFMPEG_HW_BACKEND_VAAPI: return &vj_ffmpeg_hw_vaapi_device;
        case VJ_FFMPEG_HW_BACKEND_SOFTWARE:
        default: return NULL;
    }
}

static AVBufferRef *vj_ffmpeg_hw_device_acquire(vj_ffmpeg_hw_backend backend)
{
    vj_ffmpeg_hw_device_slot *slot = vj_ffmpeg_hw_device_slot_for(backend);
    enum AVHWDeviceType type = vj_ffmpeg_hw_device_type(backend);
    if(!slot || type == AV_HWDEVICE_TYPE_NONE)
        return NULL;

    pthread_mutex_lock(&slot->mutex);
    if(slot->unavailable) {
        pthread_mutex_unlock(&slot->mutex);
        return NULL;
    }

    if(!slot->device) {
        int64_t started_us = av_gettime_relative();
        int ret = av_hwdevice_ctx_create(&slot->device, type, NULL, NULL, 0);
        if(ret < 0) {
            slot->device = NULL;
            slot->unavailable = 1;
            pthread_mutex_unlock(&slot->mutex);
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[FFMPEG-HW] backend=%s device unavailable ret=%d",
                       vj_ffmpeg_hw_backend_name(backend),
                       ret);
            return NULL;
        }
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] backend=%s device created latency=%" PRId64 "us",
                   vj_ffmpeg_hw_backend_name(backend),
                   av_gettime_relative() - started_us);
        pthread_once(&vj_ffmpeg_cleanup_once, vj_ffmpeg_register_cleanup);
    }
    else {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] backend=%s device reused active_users=%u",
                   vj_ffmpeg_hw_backend_name(backend),
                   slot->users);
    }

    AVBufferRef *ref = av_buffer_ref(slot->device);
    if(ref)
        slot->users++;
    pthread_mutex_unlock(&slot->mutex);
    return ref;
}

static void vj_ffmpeg_hw_device_release(vj_ffmpeg_hw_backend backend,
                                        AVBufferRef **device_ref)
{
    if(!device_ref || !*device_ref)
        return;

    vj_ffmpeg_hw_device_slot *slot = vj_ffmpeg_hw_device_slot_for(backend);
    av_buffer_unref(device_ref);
    if(!slot)
        return;

    pthread_mutex_lock(&slot->mutex);
    if(slot->users > 0)
        slot->users--;
    pthread_mutex_unlock(&slot->mutex);
}

static const AVCodecHWConfig *vj_ffmpeg_hw_codec_config(const AVCodec *codec,
                                                        vj_ffmpeg_hw_backend backend)
{
    enum AVHWDeviceType type = vj_ffmpeg_hw_device_type(backend);
    if(!codec || type == AV_HWDEVICE_TYPE_NONE)
        return NULL;

    for(int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if(!config)
            return NULL;
        if((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
           config->device_type == type &&
           config->pix_fmt != AV_PIX_FMT_NONE)
            return config;
    }
}

static enum AVPixelFormat vj_ffmpeg_hw_get_format(AVCodecContext *codec_ctx,
                                                   const enum AVPixelFormat *formats)
{
    vj_ffmpeg_input *input = codec_ctx ? codec_ctx->opaque : NULL;
    if(!input || !formats)
        return AV_PIX_FMT_NONE;

    for(const enum AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++) {
        if(*p == input->hw_pix_fmt) {
            if(!input->hw_format_selected)
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "[FFMPEG-HW] source='%s' backend=%s hardware pixel format selected hwfmt=%s",
                           input->filename,
                           vj_ffmpeg_hw_backend_name(input->hw_backend),
                           av_get_pix_fmt_name(*p) ? av_get_pix_fmt_name(*p) : "unknown");
            input->hw_format_selected = 1;
            input->hw_format_rejected = 0;
            return *p;
        }
    }

    if(!input->hw_format_rejected)
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' backend=%s hardware pixel format not offered",
                   input->filename,
                   vj_ffmpeg_hw_backend_name(input->hw_backend));
    input->hw_format_rejected = 1;
    return AV_PIX_FMT_NONE;
}

static int vj_ffmpeg_video_decoder_open_backend(vj_ffmpeg_input *input,
                                                 vj_ffmpeg_hw_backend backend)
{
    const AVCodec *codec = input->video_codec;
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if(!codec_ctx)
        return 0;

    int ret = avcodec_parameters_to_context(codec_ctx, input->stream->codecpar);
    if(ret < 0) {
        avcodec_free_context(&codec_ctx);
        return 0;
    }

    AVBufferRef *device_ref = NULL;
    input->hw_pix_fmt = AV_PIX_FMT_NONE;
    input->hw_format_selected = 0;
    input->hw_format_rejected = 0;
    input->hw_session_warm = backend == VJ_FFMPEG_HW_BACKEND_SOFTWARE;
    input->hw_request_warmed = 0;
    input->hw_backend = backend;

    if(backend != VJ_FFMPEG_HW_BACKEND_SOFTWARE) {
        const AVCodecHWConfig *config = vj_ffmpeg_hw_codec_config(codec, backend);
        if(!config) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[FFMPEG-HW] source='%s' codec=%s backend=%s unsupported by decoder",
                       input->filename,
                       codec->name,
                       vj_ffmpeg_hw_backend_name(backend));
            avcodec_free_context(&codec_ctx);
            input->hw_backend = VJ_FFMPEG_HW_BACKEND_SOFTWARE;
            return 0;
        }

        device_ref = vj_ffmpeg_hw_device_acquire(backend);
        if(!device_ref) {
            avcodec_free_context(&codec_ctx);
            input->hw_backend = VJ_FFMPEG_HW_BACKEND_SOFTWARE;
            return 0;
        }

        input->hw_pix_fmt = config->pix_fmt;
        codec_ctx->opaque = input;
        codec_ctx->get_format = vj_ffmpeg_hw_get_format;
        codec_ctx->hw_device_ctx = av_buffer_ref(device_ref);
        if(!codec_ctx->hw_device_ctx) {
            avcodec_free_context(&codec_ctx);
            vj_ffmpeg_hw_device_release(backend, &device_ref);
            input->hw_backend = VJ_FFMPEG_HW_BACKEND_SOFTWARE;
            input->hw_pix_fmt = AV_PIX_FMT_NONE;
            return 0;
        }
    }

    ret = avcodec_open2(codec_ctx, codec, NULL);
    if(ret < 0) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' backend=%s decoder open failed ret=%d",
                   input->filename,
                   vj_ffmpeg_hw_backend_name(backend),
                   ret);
        avcodec_free_context(&codec_ctx);
        if(device_ref)
            vj_ffmpeg_hw_device_release(backend, &device_ref);
        input->hw_backend = VJ_FFMPEG_HW_BACKEND_SOFTWARE;
        input->hw_pix_fmt = AV_PIX_FMT_NONE;
        return 0;
    }

    input->codec_ctx = codec_ctx;
    input->hw_device_ctx = device_ref;
    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-HW] source='%s' decoder configured backend=%s",
               input->filename,
               vj_ffmpeg_hw_backend_name(input->hw_backend));
    return 1;
}

static int vj_ffmpeg_video_decoder_open_selected(vj_ffmpeg_input *input)
{
    if(input->hw_eligible) {
        if(input->hw_policy == VJ_FFMPEG_HW_POLICY_AUTO ||
           input->hw_policy == VJ_FFMPEG_HW_POLICY_VULKAN) {
            if(vj_ffmpeg_video_decoder_open_backend(input,
                                                     VJ_FFMPEG_HW_BACKEND_VULKAN))
                return 1;
        }

        if(input->hw_policy == VJ_FFMPEG_HW_POLICY_AUTO ||
           input->hw_policy == VJ_FFMPEG_HW_POLICY_VAAPI) {
            if(vj_ffmpeg_video_decoder_open_backend(input,
                                                     VJ_FFMPEG_HW_BACKEND_VAAPI))
                return 1;
        }
    }

    return vj_ffmpeg_video_decoder_open_backend(input,
                                                 VJ_FFMPEG_HW_BACKEND_SOFTWARE);
}

static void vj_ffmpeg_video_decoder_close(vj_ffmpeg_input *input)
{
    if(!input)
        return;

    vj_ffmpeg_hw_backend backend = input->hw_backend;
    avcodec_free_context(&input->codec_ctx);
    if(input->hw_device_ctx)
        vj_ffmpeg_hw_device_release(backend, &input->hw_device_ctx);
    av_frame_free(&input->hw_transfer_frame);
    input->hw_backend = VJ_FFMPEG_HW_BACKEND_SOFTWARE;
    input->hw_pix_fmt = AV_PIX_FMT_NONE;
    input->hw_transfer_pix_fmt = AV_PIX_FMT_NONE;
    input->hw_transfer_ctx_id = NULL;
    input->hw_format_selected = 0;
    input->hw_format_rejected = 0;
    input->hw_runtime_failed = 0;
    input->hw_last_error = 0;
    input->hw_session_warm = 0;
    input->hw_request_warmed = 0;
}

static int vj_ffmpeg_audio_channels(const AVCodecContext *codec_ctx)
{
#if LIBAVUTIL_VERSION_MAJOR >= 57
    return codec_ctx ? codec_ctx->ch_layout.nb_channels : 0;
#else
    return codec_ctx ? codec_ctx->channels : 0;
#endif
}

static void vj_ffmpeg_audio_decoder_close(vj_ffmpeg_input *input)
{
    if(!input)
        return;

    if(input->audio_resampler)
        swr_free(&input->audio_resampler);
    free(input->audio_pcm);
    input->audio_pcm = NULL;
    input->audio_pcm_capacity = 0;
    av_packet_free(&input->audio_packet);
    av_frame_free(&input->audio_frame);
    avcodec_free_context(&input->audio_codec_ctx);
    avformat_close_input(&input->audio_format_ctx);
    input->audio_stream = NULL;
    input->audio_stream_index = -1;
    input->audio_draining = 0;
    input->audio_out_rate = 0;
    input->audio_out_channels = 0;
    input->audio_configured = 0;
    input->audio_pcm_samples = 0;
    input->audio_pcm_offset = 0;
    input->audio_resample_position_valid = 0;
    input->audio_resampler_drained = 0;
    input->audio_seek_requires_timestamp = 0;
    input->audio_request_valid = 0;
}

static int vj_ffmpeg_audio_decoder_open(vj_ffmpeg_input *input)
{
    if(!input || !input->filename)
        return 0;

    input->audio_stream_index = -1;

    int ret = avformat_open_input(&input->audio_format_ctx,
                                  input->filename,
                                  NULL,
                                  NULL);
    if(ret < 0)
        return 0;

    ret = avformat_find_stream_info(input->audio_format_ctx, NULL);
    if(ret < 0)
        goto unavailable;

    const AVCodec *codec = NULL;
    ret = av_find_best_stream(input->audio_format_ctx,
                              AVMEDIA_TYPE_AUDIO,
                              -1,
                              -1,
                              &codec,
                              0);
    if(ret < 0 || !codec)
        goto unavailable;

    input->audio_stream_index = ret;
    input->audio_stream = input->audio_format_ctx->streams[input->audio_stream_index];
    input->audio_codec_ctx = avcodec_alloc_context3(codec);
    if(!input->audio_codec_ctx)
        goto unavailable;

    ret = avcodec_parameters_to_context(input->audio_codec_ctx,
                                        input->audio_stream->codecpar);
    if(ret < 0)
        goto unavailable;

    ret = avcodec_open2(input->audio_codec_ctx, codec, NULL);
    if(ret < 0)
        goto unavailable;

    input->audio_frame = av_frame_alloc();
    input->audio_packet = av_packet_alloc();
    if(!input->audio_frame || !input->audio_packet)
        goto unavailable;

    input->info.has_audio = 1;
    input->info.audio_stream_index = input->audio_stream_index;
    input->info.audio_codec_id = input->audio_codec_ctx->codec_id;
    input->info.audio_rate = input->audio_codec_ctx->sample_rate;
    input->info.audio_channels = vj_ffmpeg_audio_channels(input->audio_codec_ctx);

    const AVCodecDescriptor *desc = avcodec_descriptor_get(input->audio_codec_ctx->codec_id);
    snprintf(input->info.audio_codec_name,
             sizeof(input->info.audio_codec_name),
             "%s",
             desc && desc->name ? desc->name : codec->name);

    const char *sample_fmt = av_get_sample_fmt_name(input->audio_codec_ctx->sample_fmt);
    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-AUDIO] opened '%s': stream=%d codec=%s rate=%d channels=%d sample_fmt=%s",
               input->filename,
               input->audio_stream_index,
               input->info.audio_codec_name,
               input->info.audio_rate,
               input->info.audio_channels,
               sample_fmt ? sample_fmt : "unknown");
    return 1;

unavailable:
    vj_ffmpeg_audio_decoder_close(input);
    input->info.has_audio = 0;
    input->info.audio_stream_index = -1;
    input->info.audio_codec_id = AV_CODEC_ID_NONE;
    input->info.audio_rate = 0;
    input->info.audio_channels = 0;
    input->info.audio_codec_name[0] = '\0';
    return 0;
}

static int vj_ffmpeg_audio_resampler_init(vj_ffmpeg_input *input,
                                          int sample_rate,
                                          int channels)
{
    if(!input || !input->audio_codec_ctx || sample_rate <= 0 ||
       channels < 1 || channels > 2)
        return 0;

    input->audio_configured = 0;

    int in_rate = input->audio_codec_ctx->sample_rate;
    int in_channels = vj_ffmpeg_audio_channels(input->audio_codec_ctx);
    enum AVSampleFormat in_format = input->audio_codec_ctx->sample_fmt;
    if(in_rate <= 0 || in_channels <= 0 || in_format == AV_SAMPLE_FMT_NONE)
        return 0;

    if(input->audio_resampler)
        swr_free(&input->audio_resampler);

#if LIBAVUTIL_VERSION_MAJOR >= 57
    AVChannelLayout in_layout = {0};
    AVChannelLayout out_layout = {0};
    int layout_ret = 0;

    if(input->audio_codec_ctx->ch_layout.nb_channels > 0 &&
       input->audio_codec_ctx->ch_layout.order != AV_CHANNEL_ORDER_UNSPEC)
        layout_ret = av_channel_layout_copy(&in_layout,
                                            &input->audio_codec_ctx->ch_layout);
    else
        av_channel_layout_default(&in_layout, in_channels);
    if(layout_ret < 0)
        return 0;

    av_channel_layout_default(&out_layout, channels);
    int ret = swr_alloc_set_opts2(&input->audio_resampler,
                                  &out_layout,
                                  AV_SAMPLE_FMT_S16,
                                  sample_rate,
                                  &in_layout,
                                  in_format,
                                  in_rate,
                                  0,
                                  NULL);
    av_channel_layout_uninit(&in_layout);
    av_channel_layout_uninit(&out_layout);
    if(ret < 0) {
        swr_free(&input->audio_resampler);
        return 0;
    }
#else
    int64_t in_layout = input->audio_codec_ctx->channel_layout;
    if(!in_layout)
        in_layout = av_get_default_channel_layout(in_channels);
    int64_t out_layout = av_get_default_channel_layout(channels);

    input->audio_resampler = swr_alloc_set_opts(NULL,
                                                out_layout,
                                                AV_SAMPLE_FMT_S16,
                                                sample_rate,
                                                in_layout,
                                                in_format,
                                                in_rate,
                                                0,
                                                NULL);
    if(!input->audio_resampler)
        return 0;
#endif

    if(!input->audio_resampler || swr_init(input->audio_resampler) < 0) {
        swr_free(&input->audio_resampler);
        return 0;
    }

    size_t initial_bytes = (size_t)VJ_FFMPEG_AUDIO_INITIAL_BLOCK_SAMPLES *
                           (size_t)channels * sizeof(int16_t);
    uint8_t *pcm = realloc(input->audio_pcm, initial_bytes);
    if(!pcm) {
        swr_free(&input->audio_resampler);
        return 0;
    }
    input->audio_pcm = pcm;
    input->audio_pcm_capacity = VJ_FFMPEG_AUDIO_INITIAL_BLOCK_SAMPLES;

    input->audio_out_rate = sample_rate;
    input->audio_out_channels = channels;
    input->audio_pcm_samples = 0;
    input->audio_pcm_offset = 0;
    input->audio_pcm_start_sample = 0;
    input->audio_resample_next_sample = 0;
    input->audio_resample_position_valid = 0;
    input->audio_resampler_drained = 0;
    input->audio_seek_requires_timestamp = 0;
    input->audio_request_next_sample = 0;
    input->audio_request_valid = 0;
    input->audio_configured = 1;
    return 1;
}

static double vj_ffmpeg_rate(AVFormatContext *format_ctx, AVStream *stream)
{
    AVRational rate = av_guess_frame_rate(format_ctx, stream, NULL);
    if(rate.num <= 0 || rate.den <= 0)
        rate = stream->avg_frame_rate;
    if(rate.num <= 0 || rate.den <= 0)
        rate = stream->r_frame_rate;
    if(rate.num <= 0 || rate.den <= 0)
        return 0.0;
    return av_q2d(rate);
}

int vj_ffmpeg_input_prefers_generic(const char *filename)
{
    AVFormatContext *format_ctx = NULL;
    if(!filename || avformat_open_input(&format_ctx, filename, NULL, NULL) < 0)
        return 0;

    int prefer = 0;
    if(avformat_find_stream_info(format_ctx, NULL) >= 0) {
        int stream_index = av_find_best_stream(format_ctx,
                                               AVMEDIA_TYPE_VIDEO,
                                               -1,
                                               -1,
                                               NULL,
                                               0);
        if(stream_index >= 0) {
            AVStream *stream = format_ctx->streams[stream_index];
            const AVCodecDescriptor *desc =
                avcodec_descriptor_get(stream->codecpar->codec_id);
            prefer = desc && !(desc->props & AV_CODEC_PROP_INTRA_ONLY);
        }
    }

    avformat_close_input(&format_ctx);
    return prefer;
}

static int vj_ffmpeg_seek_start(vj_ffmpeg_input *input)
{
    int64_t start_ts = input->stream->start_time;
    if(start_ts == AV_NOPTS_VALUE)
        start_ts = 0;

    int ret = av_seek_frame(input->format_ctx,
                            input->stream_index,
                            start_ts,
                            AVSEEK_FLAG_BACKWARD);
    if(ret < 0) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-IN] seek-to-start failed (%d)", ret);
        return 0;
    }

    avformat_flush(input->format_ctx);
    avcodec_flush_buffers(input->codec_ctx);
    av_packet_unref(input->packet);
    av_frame_unref(input->frame);
    input->next_frame = 0;
    input->draining = 0;
    return 1;
}

static int64_t vj_ffmpeg_estimate_frame_count(vj_ffmpeg_input *input)
{
    AVStream *stream = input->stream;

    if(stream->nb_frames > 0)
        return stream->nb_frames;

    int64_t duration = stream->duration;
    double seconds = 0.0;

    if(duration != AV_NOPTS_VALUE && duration > 0)
        seconds = duration * av_q2d(stream->time_base);
    else if(input->format_ctx->duration != AV_NOPTS_VALUE && input->format_ctx->duration > 0)
        seconds = (double) input->format_ctx->duration / (double) AV_TIME_BASE;

    if(seconds > 0.0 && input->info.fps > 0.0) {
        int64_t estimate = (int64_t) (seconds * input->info.fps + 0.5);
        if(estimate > 0)
            return estimate;
    }

    return 0;
}

static int vj_ffmpeg_index_compare(const void *a, const void *b)
{
    const vj_ffmpeg_index_entry *ia = a;
    const vj_ffmpeg_index_entry *ib = b;
    if(ia->pts < ib->pts)
        return -1;
    if(ia->pts > ib->pts)
        return 1;
    return 0;
}

static int vj_ffmpeg_index_append(vj_ffmpeg_input *input, int64_t pts, int keyframe)
{
    if(input->index_count == input->index_capacity) {
        if(input->index_capacity > INT64_MAX / 2)
            return 0;
        int64_t next_capacity = input->index_capacity ? input->index_capacity * 2 : 4096;
        if(next_capacity < input->index_capacity ||
           (uint64_t)next_capacity > SIZE_MAX / sizeof(*input->index))
            return 0;

        vj_ffmpeg_index_entry *next = realloc(input->index,
                                               (size_t)next_capacity * sizeof(*input->index));
        if(!next)
            return 0;
        input->index = next;
        input->index_capacity = next_capacity;
    }

    input->index[input->index_count].pts = pts;
    input->index[input->index_count].keyframe = keyframe;
    input->index_count++;
    return 1;
}

static int vj_ffmpeg_index_cache_identity(vj_ffmpeg_input *input, struct stat *st)
{
    return input && input->filename && st && stat(input->filename, st) == 0 &&
           S_ISREG(st->st_mode);
}

static int vj_ffmpeg_index_cache_matches(const vj_ffmpeg_index_cache_entry *entry,
                                         const vj_ffmpeg_input *input,
                                         const struct stat *st)
{
    return entry->device == st->st_dev &&
           entry->inode == st->st_ino &&
           entry->file_size == st->st_size &&
           entry->mtime_sec == st->st_mtime &&
           entry->stream_index == input->stream_index &&
           entry->codec_id == input->stream->codecpar->codec_id;
}

static vj_ffmpeg_index_cache_entry *vj_ffmpeg_index_cache_find_locked(
    const vj_ffmpeg_input *input, const struct stat *st)
{
    for(vj_ffmpeg_index_cache_entry *entry = vj_ffmpeg_index_cache;
        entry; entry = entry->next)
        if(vj_ffmpeg_index_cache_matches(entry, input, st))
            return entry;
    return NULL;
}

static void vj_ffmpeg_index_cache_evict_locked(size_t required)
{
    while(vj_ffmpeg_index_cache_bytes + required > VJ_FFMPEG_INDEX_CACHE_MAX_BYTES) {
        vj_ffmpeg_index_cache_entry *candidate = NULL;
        vj_ffmpeg_index_cache_entry *candidate_prev = NULL;
        vj_ffmpeg_index_cache_entry *prev = NULL;

        for(vj_ffmpeg_index_cache_entry *entry = vj_ffmpeg_index_cache;
            entry; entry = entry->next) {
            if(entry->users == 0 && (!candidate || entry->stamp < candidate->stamp)) {
                candidate = entry;
                candidate_prev = prev;
            }
            prev = entry;
        }

        if(!candidate)
            return;
        if(candidate_prev)
            candidate_prev->next = candidate->next;
        else
            vj_ffmpeg_index_cache = candidate->next;
        vj_ffmpeg_index_cache_bytes -= candidate->bytes;
        free(candidate->entries);
        free(candidate);
    }
}

static int64_t vj_ffmpeg_index_cache_attach(vj_ffmpeg_input *input)
{
    struct stat st;
    if(!vj_ffmpeg_index_cache_identity(input, &st))
        return 0;

    pthread_mutex_lock(&vj_ffmpeg_index_cache_mutex);
    vj_ffmpeg_index_cache_entry *entry =
        vj_ffmpeg_index_cache_find_locked(input, &st);
    if(!entry) {
        pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);
        return 0;
    }

    entry->users++;
    entry->stamp = ++vj_ffmpeg_index_cache_clock;
    input->index = entry->entries;
    input->index_count = entry->count;
    input->index_capacity = entry->capacity;
    input->index_cache_entry = entry;
    input->info.timestamp_index_reliable = entry->reliable;
    input->info.indexed_frames = entry->count;
    input->info.keyframe_count = entry->keyframe_count;
    input->info.max_keyframe_gap = entry->max_keyframe_gap;
    int64_t packet_count = entry->packet_count;
    int64_t discard_count = entry->discard_count;
    size_t bytes = entry->bytes;
    pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-SEEK] index cache-hit packets=%" PRId64 " discard=%" PRId64 " timestamps=%" PRId64 " keyframes=%" PRId64 " max_gap=%" PRId64 " reliable=%d bytes=%zu",
               packet_count,
               discard_count,
               input->index_count,
               input->info.keyframe_count,
               input->info.max_keyframe_gap,
               input->info.timestamp_index_reliable,
               bytes);
    return packet_count;
}

static void vj_ffmpeg_index_cache_publish(vj_ffmpeg_input *input,
                                          int64_t packet_count,
                                          int64_t discard_count)
{
    if(!input || !input->index || input->index_count <= 0 ||
       input->index_capacity <= 0)
        return;

    struct stat st;
    if(!vj_ffmpeg_index_cache_identity(input, &st))
        return;

    size_t bytes = (size_t)input->index_capacity * sizeof(*input->index);
    if(bytes > VJ_FFMPEG_INDEX_CACHE_MAX_BYTES)
        return;

    vj_ffmpeg_index_cache_entry *fresh = calloc(1, sizeof(*fresh));
    if(!fresh)
        return;

    fresh->device = st.st_dev;
    fresh->inode = st.st_ino;
    fresh->file_size = st.st_size;
    fresh->mtime_sec = st.st_mtime;
    fresh->stream_index = input->stream_index;
    fresh->codec_id = input->stream->codecpar->codec_id;
    fresh->entries = input->index;
    fresh->count = input->index_count;
    fresh->capacity = input->index_capacity;
    fresh->packet_count = packet_count;
    fresh->discard_count = discard_count;
    fresh->reliable = input->info.timestamp_index_reliable;
    fresh->keyframe_count = input->info.keyframe_count;
    fresh->max_keyframe_gap = input->info.max_keyframe_gap;
    fresh->bytes = bytes;
    fresh->users = 1;

    pthread_mutex_lock(&vj_ffmpeg_index_cache_mutex);
    vj_ffmpeg_index_cache_entry *existing =
        vj_ffmpeg_index_cache_find_locked(input, &st);
    if(existing) {
        existing->users++;
        existing->stamp = ++vj_ffmpeg_index_cache_clock;
        free(input->index);
        input->index = existing->entries;
        input->index_count = existing->count;
        input->index_capacity = existing->capacity;
        input->index_cache_entry = existing;
        input->info.timestamp_index_reliable = existing->reliable;
        input->info.indexed_frames = existing->count;
        input->info.keyframe_count = existing->keyframe_count;
        input->info.max_keyframe_gap = existing->max_keyframe_gap;
        pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);
        free(fresh);
        return;
    }

    vj_ffmpeg_index_cache_evict_locked(bytes);
    if(vj_ffmpeg_index_cache_bytes + bytes > VJ_FFMPEG_INDEX_CACHE_MAX_BYTES) {
        pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);
        free(fresh);
        return;
    }

    fresh->stamp = ++vj_ffmpeg_index_cache_clock;
    fresh->next = vj_ffmpeg_index_cache;
    vj_ffmpeg_index_cache = fresh;
    vj_ffmpeg_index_cache_bytes += bytes;
    input->index_cache_entry = fresh;
    size_t total_bytes = vj_ffmpeg_index_cache_bytes;
    pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);
    pthread_once(&vj_ffmpeg_cleanup_once, vj_ffmpeg_register_cleanup);

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-SEEK] index cached bytes=%zu shared_total=%zu limit=%u",
               bytes,
               total_bytes,
               (unsigned int)VJ_FFMPEG_INDEX_CACHE_MAX_BYTES);
}

static void vj_ffmpeg_index_cache_release(vj_ffmpeg_input *input)
{
    if(!input || !input->index_cache_entry)
        return;

    pthread_mutex_lock(&vj_ffmpeg_index_cache_mutex);
    if(input->index_cache_entry->users > 0)
        input->index_cache_entry->users--;
    input->index_cache_entry->stamp = ++vj_ffmpeg_index_cache_clock;
    pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);

    input->index_cache_entry = NULL;
    input->index = NULL;
    input->index_count = 0;
    input->index_capacity = 0;
}

typedef enum
{
    VJ_FFMPEG_INDEX_MODE_AUTO = 0,
    VJ_FFMPEG_INDEX_MODE_SYNC,
    VJ_FFMPEG_INDEX_MODE_OFF
} vj_ffmpeg_index_mode;

static vj_ffmpeg_index_mode vj_ffmpeg_index_mode_env(void)
{
    const char *setting = getenv("VEEJAY_FFMPEG_INDEX");

    if(!setting || !setting[0] || strcasecmp(setting, "auto") == 0)
        return VJ_FFMPEG_INDEX_MODE_AUTO;

    if(strcasecmp(setting, "sync") == 0)
        return VJ_FFMPEG_INDEX_MODE_SYNC;

    if(strcasecmp(setting, "off") == 0 ||
       strcasecmp(setting, "none") == 0 ||
       strcasecmp(setting, "disable") == 0)
        return VJ_FFMPEG_INDEX_MODE_OFF;

    veejay_msg(VEEJAY_MSG_WARNING,
               "[FFMPEG-SEEK] invalid VEEJAY_FFMPEG_INDEX='%s'; using auto",
               setting);

    return VJ_FFMPEG_INDEX_MODE_AUTO;
}

static int vj_ffmpeg_index_local_append(vj_ffmpeg_index_entry **entries,
                                        int64_t *count,
                                        int64_t *capacity,
                                        int64_t pts,
                                        int keyframe)
{
    if(*count == *capacity) {
        if(*capacity > INT64_MAX / 2)
            return 0;

        int64_t next_capacity = *capacity ? *capacity * 2 : 4096;

        if(next_capacity < *capacity ||
           (uint64_t)next_capacity > SIZE_MAX / sizeof(**entries))
            return 0;

        vj_ffmpeg_index_entry *next =
            realloc(*entries, (size_t)next_capacity * sizeof(**entries));

        if(!next)
            return 0;

        *entries = next;
        *capacity = next_capacity;
    }

    (*entries)[*count].pts = pts;
    (*entries)[*count].keyframe = keyframe;
    (*count)++;

    return 1;
}

static int vj_ffmpeg_index_cache_matches_raw(const vj_ffmpeg_index_cache_entry *entry,
                                             const struct stat *st,
                                             int stream_index,
                                             enum AVCodecID codec_id)
{
    return entry->device == st->st_dev &&
           entry->inode == st->st_ino &&
           entry->file_size == st->st_size &&
           entry->mtime_sec == st->st_mtime &&
           entry->stream_index == stream_index &&
           entry->codec_id == codec_id;
}

static vj_ffmpeg_index_cache_entry *vj_ffmpeg_index_cache_find_raw_locked(
    const struct stat *st,
    int stream_index,
    enum AVCodecID codec_id)
{
    for(vj_ffmpeg_index_cache_entry *entry = vj_ffmpeg_index_cache;
        entry;
        entry = entry->next) {
        if(vj_ffmpeg_index_cache_matches_raw(entry,
                                             st,
                                             stream_index,
                                             codec_id))
            return entry;
    }

    return NULL;
}

static void vj_ffmpeg_index_cache_publish_built(
    const char *filename,
    int stream_index,
    enum AVCodecID codec_id,
    vj_ffmpeg_index_entry *entries,
    int64_t count,
    int64_t capacity,
    int64_t packet_count,
    int64_t discard_count,
    int reliable,
    int64_t keyframe_count,
    int64_t max_keyframe_gap)
{
    if(!filename || !entries || count <= 0 || capacity <= 0) {
        free(entries);
        return;
    }

    struct stat st;

    if(stat(filename, &st) != 0 || !S_ISREG(st.st_mode)) {
        free(entries);
        return;
    }

    size_t bytes = (size_t)capacity * sizeof(*entries);

    if(bytes > VJ_FFMPEG_INDEX_CACHE_MAX_BYTES) {
        free(entries);
        return;
    }

    vj_ffmpeg_index_cache_entry *fresh = calloc(1, sizeof(*fresh));

    if(!fresh) {
        free(entries);
        return;
    }

    fresh->device = st.st_dev;
    fresh->inode = st.st_ino;
    fresh->file_size = st.st_size;
    fresh->mtime_sec = st.st_mtime;
    fresh->stream_index = stream_index;
    fresh->codec_id = codec_id;
    fresh->entries = entries;
    fresh->count = count;
    fresh->capacity = capacity;
    fresh->packet_count = packet_count;
    fresh->discard_count = discard_count;
    fresh->reliable = reliable;
    fresh->keyframe_count = keyframe_count;
    fresh->max_keyframe_gap = max_keyframe_gap;
    fresh->bytes = bytes;
    fresh->users = 0;

    pthread_mutex_lock(&vj_ffmpeg_index_cache_mutex);

    vj_ffmpeg_index_cache_entry *existing =
        vj_ffmpeg_index_cache_find_raw_locked(&st, stream_index, codec_id);

    if(existing) {
        existing->stamp = ++vj_ffmpeg_index_cache_clock;
        pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);
        free(entries);
        free(fresh);
        return;
    }

    vj_ffmpeg_index_cache_evict_locked(bytes);

    if(vj_ffmpeg_index_cache_bytes + bytes > VJ_FFMPEG_INDEX_CACHE_MAX_BYTES) {
        pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);
        free(entries);
        free(fresh);
        return;
    }

    fresh->stamp = ++vj_ffmpeg_index_cache_clock;
    fresh->next = vj_ffmpeg_index_cache;
    vj_ffmpeg_index_cache = fresh;
    vj_ffmpeg_index_cache_bytes += bytes;

    size_t total_bytes = vj_ffmpeg_index_cache_bytes;

    pthread_mutex_unlock(&vj_ffmpeg_index_cache_mutex);

    pthread_once(&vj_ffmpeg_cleanup_once, vj_ffmpeg_register_cleanup);

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-SEEK] async index cached file='%s' timestamps=%" PRId64
               " keyframes=%" PRId64 " bytes=%zu shared_total=%zu",
               filename,
               count,
               keyframe_count,
               bytes,
               total_bytes);
}

typedef struct
{
    char *filename;
    int stream_index;
    enum AVCodecID codec_id;
    AVRational time_base;
    volatile int *cancel;
} vj_ffmpeg_index_job;

static void *vj_ffmpeg_index_async_worker(void *opaque)
{
    vj_ffmpeg_index_job *job = (vj_ffmpeg_index_job *)opaque;

    if(!job)
        return NULL;

    AVFormatContext *format_ctx = NULL;
    AVPacket *packet = NULL;
    vj_ffmpeg_index_entry *entries = NULL;

    int64_t count = 0;
    int64_t capacity = 0;

    int64_t packet_count = 0;
    int64_t display_packet_count = 0;
    int64_t discard_count = 0;
    int64_t missing_pts = 0;
    int64_t dts_fallbacks = 0;

    int stream_index = -1;
    AVStream *stream = NULL;

    int64_t started_us = av_gettime_relative();

    if(avformat_open_input(&format_ctx, job->filename, NULL, NULL) < 0)
        goto done;

    if(job->cancel && *job->cancel)
        goto done;

    if(avformat_find_stream_info(format_ctx, NULL) < 0)
        goto done;

    if(job->cancel && *job->cancel)
        goto done;

    const AVCodec *codec = NULL;

    stream_index = av_find_best_stream(format_ctx,
                                       AVMEDIA_TYPE_VIDEO,
                                       -1,
                                       -1,
                                       &codec,
                                       0);

    if(stream_index < 0 || !codec)
        goto done;

    stream = format_ctx->streams[stream_index];

    packet = av_packet_alloc();

    if(!packet)
        goto done;

    while(!(job->cancel && *job->cancel) &&
          av_read_frame(format_ctx, packet) >= 0) {
        if(packet->stream_index == stream_index) {
            packet_count++;

            if(packet->flags & AV_PKT_FLAG_DISCARD) {
                discard_count++;
            }
            else {
                display_packet_count++;

                int64_t pts = packet->pts;

                if(pts == AV_NOPTS_VALUE) {
                    pts = packet->dts;
                    if(pts != AV_NOPTS_VALUE)
                        dts_fallbacks++;
                }

                if(pts == AV_NOPTS_VALUE) {
                    missing_pts++;
                }
                else {
                    int64_t normalized_pts =
                        av_rescale_q(pts,
                                     stream->time_base,
                                     job->time_base);

                    if(!vj_ffmpeg_index_local_append(&entries,
                                                     &count,
                                                     &capacity,
                                                     normalized_pts,
                                                     (packet->flags & AV_PKT_FLAG_KEY) != 0)) {
                        free(entries);
                        entries = NULL;
                        count = 0;
                        capacity = 0;
                        goto done;
                    }
                }
            }
        }

        av_packet_unref(packet);
    }

    if(job->cancel && *job->cancel)
        goto done;

    if(count > 1) {
        qsort(entries,
              (size_t)count,
              sizeof(*entries),
              vj_ffmpeg_index_compare);
    }

    int reliable = count == display_packet_count &&
                   missing_pts == 0 &&
                   dts_fallbacks == 0;

    int64_t keyframes = 0;
    int64_t previous_key = -1;
    int64_t max_gap = 0;

    for(int64_t i = 0; i < count; i++) {
        if(i > 0 && entries[i].pts <= entries[i - 1].pts)
            reliable = 0;

        if(entries[i].keyframe) {
            if(previous_key >= 0 && i - previous_key > max_gap)
                max_gap = i - previous_key;

            previous_key = i;
            keyframes++;
        }
    }

    if(previous_key >= 0 && count - previous_key > max_gap)
        max_gap = count - previous_key;

    if(keyframes == 0)
        reliable = 0;

    if(stream->nb_frames > 0 &&
       stream->nb_frames != packet_count &&
       stream->nb_frames != display_packet_count)
        reliable = 0;

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-SEEK] async index built file='%s' packets=%" PRId64
               " timestamps=%" PRId64 " keyframes=%" PRId64
               " reliable=%d elapsed=%" PRId64 "us",
               job->filename,
               packet_count,
               count,
               keyframes,
               reliable,
               av_gettime_relative() - started_us);

    if(count > 0) {
        vj_ffmpeg_index_cache_publish_built(job->filename,
                                            job->stream_index,
                                            job->codec_id,
                                            entries,
                                            count,
                                            capacity,
                                            packet_count,
                                            discard_count,
                                            reliable,
                                            keyframes,
                                            max_gap);

        /*
         * Ownership of entries was transferred to the cache if publish
         * succeeded. If publish failed, publish_built frees entries.
         */
        entries = NULL;
    }

done:
    if(entries)
        free(entries);

    if(packet)
        av_packet_free(&packet);

    if(format_ctx)
        avformat_close_input(&format_ctx);

    if(job) {
        if(job->filename)
            free(job->filename);

        free(job);
    }

    return NULL;
}

static void vj_ffmpeg_index_async_start(vj_ffmpeg_input *input)
{
    if(!input || !input->index_mutex_ready || input->index_thread_started)
        return;

    vj_ffmpeg_index_job *job = calloc(1, sizeof(*job));

    if(!job)
        return;

    job->filename = strdup(input->filename);

    if(!job->filename) {
        free(job);
        return;
    }

    job->stream_index = input->stream_index;
    job->codec_id = input->stream->codecpar->codec_id;
    job->time_base = input->stream->time_base;
    job->cancel = &input->index_cancel;

    input->index_cancel = 0;

    int err = pthread_create(&input->index_thread,
                             NULL,
                             vj_ffmpeg_index_async_worker,
                             job);

    if(err != 0) {
        free(job->filename);
        free(job);
        return;
    }

    input->index_thread_started = 1;

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-SEEK] async index started source='%s'",
               input->filename);
}

static void vj_ffmpeg_index_async_refresh(vj_ffmpeg_input *input)
{
    if(!input ||
       input->info.timestamp_index_reliable ||
       input->index_cache_entry ||
       !input->index_thread_started)
        return;

    int64_t now = av_gettime_relative();

    if(input->index_last_refresh_us != 0 &&
       now - input->index_last_refresh_us < 250000LL)
        return;

    input->index_last_refresh_us = now;

    int64_t cached_packet_count = vj_ffmpeg_index_cache_attach(input);

    if(cached_packet_count > 0) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-SEEK] async index attached source='%s' timestamps=%" PRId64
                   " reliable=%d",
                   input->filename,
                   input->index_count,
                   input->info.timestamp_index_reliable);
    }
}

static int64_t vj_ffmpeg_build_timestamp_index(vj_ffmpeg_input *input)
{
    int64_t cached_packet_count = vj_ffmpeg_index_cache_attach(input);
    if(cached_packet_count > 0)
        return cached_packet_count;

    int64_t packet_count = 0;
    int64_t display_packet_count = 0;
    int64_t discard_count = 0;
    int64_t missing_pts = 0;
    int64_t dts_fallbacks = 0;
    int64_t started_us = av_gettime_relative();
    AVPacket *packet = av_packet_alloc();
    if(!packet)
        return 0;

    if(!vj_ffmpeg_seek_start(input)) {
        av_packet_free(&packet);
        return 0;
    }

    while(av_read_frame(input->format_ctx, packet) >= 0) {
        if(packet->stream_index == input->stream_index) {
            packet_count++;
            if(packet->flags & AV_PKT_FLAG_DISCARD) {
                discard_count++;
            }
            else {
                display_packet_count++;
                int64_t pts = packet->pts;
                if(pts == AV_NOPTS_VALUE) {
                    pts = packet->dts;
                    if(pts != AV_NOPTS_VALUE)
                        dts_fallbacks++;
                }

                if(pts == AV_NOPTS_VALUE)
                    missing_pts++;
                else if(!vj_ffmpeg_index_append(input,
                                                 pts,
                                                 (packet->flags & AV_PKT_FLAG_KEY) != 0)) {
                    av_packet_unref(packet);
                    av_packet_free(&packet);
                    free(input->index);
                    input->index = NULL;
                    input->index_count = 0;
                    input->index_capacity = 0;
                    vj_ffmpeg_seek_start(input);
                    return packet_count;
                }
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    if(input->index_count > 1)
        qsort(input->index,
              (size_t)input->index_count,
              sizeof(*input->index),
              vj_ffmpeg_index_compare);

    int reliable = input->index_count == display_packet_count &&
                   missing_pts == 0 && dts_fallbacks == 0;
    int64_t keyframes = 0;
    int64_t previous_key = -1;
    int64_t max_gap = 0;

    for(int64_t i = 0; i < input->index_count; i++) {
        if(i > 0 && input->index[i].pts <= input->index[i - 1].pts)
            reliable = 0;

        if(input->index[i].keyframe) {
            if(previous_key >= 0 && i - previous_key > max_gap)
                max_gap = i - previous_key;
            previous_key = i;
            keyframes++;
        }
    }

    if(previous_key >= 0 && input->index_count - previous_key > max_gap)
        max_gap = input->index_count - previous_key;
    if(keyframes == 0)
        reliable = 0;
    if(input->stream->nb_frames > 0 &&
       input->stream->nb_frames != packet_count &&
       input->stream->nb_frames != display_packet_count)
        reliable = 0;

    input->info.timestamp_index_reliable = reliable;
    input->info.indexed_frames = input->index_count;
    input->info.keyframe_count = keyframes;
    input->info.max_keyframe_gap = max_gap;

    int64_t elapsed_us = av_gettime_relative() - started_us;
    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-SEEK] index packets=%" PRId64 " discard=%" PRId64 " timestamps=%" PRId64 " keyframes=%" PRId64 " max_gap=%" PRId64 " reliable=%d scan=%" PRId64 "us",
               packet_count,
               discard_count,
               input->index_count,
               keyframes,
               max_gap,
               reliable,
               elapsed_us);

    vj_ffmpeg_index_cache_publish(input, packet_count, discard_count);

    if(!vj_ffmpeg_seek_start(input)) {
        input->info.timestamp_index_reliable = 0;
        return 0;
    }
    return packet_count;
}

static void vj_ffmpeg_fourcc(uint32_t tag, char out[5])
{
    if(tag == 0) {
        memcpy(out, "FFMP", 5);
        return;
    }

    for(int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char) ((tag >> (8 * i)) & 0xffu);
        out[i] = (c >= 32 && c <= 126) ? (char)c : '?';
    }
    out[4] = '\0';
}


static int vj_ffmpeg_index_wait_ms_env(void)
{
    const char *setting = getenv("VEEJAY_FFMPEG_INDEX_WAIT_MS");

    if(!setting || !setting[0])
        return 100;

    char *end = NULL;
    errno = 0;

    long value = strtol(setting, &end, 10);

    if(errno == 0 && end != setting && *end == '\0' &&
       value >= 0 && value <= 2000)
        return (int)value;

    veejay_msg(VEEJAY_MSG_WARNING,
               "[FFMPEG-SEEK] invalid VEEJAY_FFMPEG_INDEX_WAIT_MS='%s'; using 100",
               setting);

    return 100;
}

static void vj_ffmpeg_index_wait_for_attach(vj_ffmpeg_input *input, int timeout_ms)
{
    if(!input || timeout_ms <= 0)
        return;

    if(input->info.timestamp_index_reliable || input->index_cache_entry)
        return;

    if(!input->index_thread_started)
        return;

    int64_t deadline = av_gettime_relative() + (int64_t)timeout_ms * 1000LL;

    while(av_gettime_relative() < deadline) {
        if(vj_ffmpeg_index_cache_attach(input) > 0)
            return;

        av_usleep(1000);
    }
}

static enum AVPixelFormat vj_ffmpeg_range_aware_pix_fmt(enum AVPixelFormat pix_fmt,
                                                        enum AVColorRange range)
{
    if(range != AVCOL_RANGE_JPEG)
        return pix_fmt;
    switch(pix_fmt) {
        case AV_PIX_FMT_YUV420P: return AV_PIX_FMT_YUVJ420P;
        case AV_PIX_FMT_YUV422P: return AV_PIX_FMT_YUVJ422P;
        case AV_PIX_FMT_YUV444P: return AV_PIX_FMT_YUVJ444P;
        default: return pix_fmt;
    }
}

static enum AVPixelFormat vj_ffmpeg_frame_pix_fmt(const AVFrame *frame)
{
    return vj_ffmpeg_range_aware_pix_fmt((enum AVPixelFormat)frame->format,
                                         frame->color_range);
}



vj_ffmpeg_input *vj_ffmpeg_input_open(const char *filename,
                                      int out_pix_fmt,
                                      int out_width,
                                      int out_height)
{
    vj_ffmpeg_input *input = calloc(1, sizeof(*input));
    if(!input)
        return NULL;

    input->filename = strdup(filename);
    if(!input->filename) {
        free(input);
        return NULL;
    }
    
    if(pthread_mutex_init(&input->index_mutex, NULL) == 0)
        input->index_mutex_ready = 1;

    int ret = avformat_open_input(&input->format_ctx, filename, NULL, NULL);
    if(ret < 0) {
        vj_ffmpeg_input_close(input);
        return NULL;
    }

    ret = avformat_find_stream_info(input->format_ctx, NULL);
    if(ret < 0)
        goto fail;

    const AVCodec *codec = NULL;
    ret = av_find_best_stream(input->format_ctx,
                              AVMEDIA_TYPE_VIDEO,
                              -1,
                              -1,
                              &codec,
                              0);
    if(ret < 0 || !codec)
        goto fail;

    input->stream_index = ret;
    input->stream = input->format_ctx->streams[input->stream_index];
    input->video_codec = codec;
    const AVCodecDescriptor *desc = avcodec_descriptor_get(input->stream->codecpar->codec_id);
    vj_ffmpeg_hw_policy_for_input(input, desc);

    if(!vj_ffmpeg_video_decoder_open_selected(input))
        goto fail;

    input->frame = av_frame_alloc();
    input->packet = av_packet_alloc();
    if(!input->frame || !input->packet)
        goto fail;

    input->info.width = input->codec_ctx->width;
    input->info.height = input->codec_ctx->height;
    input->info.fps = vj_ffmpeg_rate(input->format_ctx, input->stream);
    input->info.codec_id = input->codec_ctx->codec_id;

    input->info.intra_only = (desc && (desc->props & AV_CODEC_PROP_INTRA_ONLY)) ? 1 : 0;
    snprintf(input->info.codec_name,
             sizeof(input->info.codec_name),
             "%s",
             desc && desc->name ? desc->name : codec->name);
    vj_ffmpeg_fourcc(input->stream->codecpar->codec_tag, input->info.fourcc);

    AVRational sar = input->stream->sample_aspect_ratio;
    if(sar.num <= 0 || sar.den <= 0)
        sar = input->codec_ctx->sample_aspect_ratio;
    input->info.sar_num = sar.num > 0 ? sar.num : 1;
    input->info.sar_den = sar.den > 0 ? sar.den : 1;
    input->info.interlaced =
        input->stream->codecpar->field_order != AV_FIELD_UNKNOWN &&
        input->stream->codecpar->field_order != AV_FIELD_PROGRESSIVE;

    input->out_pix_fmt = (enum AVPixelFormat) out_pix_fmt;
    input->forced_source_pix_fmt = AV_PIX_FMT_NONE;
    input->out_width = out_width > 0 ? out_width : input->info.width;
    input->out_height = out_height > 0 ? out_height : input->info.height;

    if(av_image_fill_linesizes(input->dst_linesize,
                               input->out_pix_fmt,
                               input->out_width) < 0)
        goto fail;

    vj_ffmpeg_audio_decoder_open(input);

    int64_t packet_count = 0;
    int64_t cached_packet_count = vj_ffmpeg_index_cache_attach(input);

    if(cached_packet_count > 0) {
        packet_count = cached_packet_count;
    }
    else {
        vj_ffmpeg_index_mode index_mode = vj_ffmpeg_index_mode_env();

        int64_t estimated_frames = vj_ffmpeg_estimate_frame_count(input);

        if(index_mode == VJ_FFMPEG_INDEX_MODE_SYNC ||
        (index_mode == VJ_FFMPEG_INDEX_MODE_AUTO && estimated_frames <= 0)) {
            packet_count = vj_ffmpeg_build_timestamp_index(input);
        }
        else if(index_mode == VJ_FFMPEG_INDEX_MODE_AUTO) {
            vj_ffmpeg_index_async_start(input);

            /*
            * Give the async indexer a bounded chance to finish before we
            * choose frame_count.
            *
            * If the index becomes available within the wait window,
            * vj_ffmpeg_index_cache_attach() updates:
            *
            *   input->index
            *   input->index_count
            *   input->index_cache_entry
            *   input->info.timestamp_index_reliable
            *   input->info.indexed_frames
            *
            * Then the frame-count selection below can use the reliable
            * timestamp index count instead of stream->nb_frames.
            */
            vj_ffmpeg_index_wait_for_attach(input,
                                            vj_ffmpeg_index_wait_ms_env());

            /*
            * The wait helper already attached the index if it became ready.
            * Do not call vj_ffmpeg_index_cache_attach() again here, because
            * that would increment the cache user count a second time.
            */
            if(input->index_cache_entry)
                packet_count = input->index_count;
        }
    }
    
    if(input->info.timestamp_index_reliable && input->info.indexed_frames > 0) {
        input->info.frame_count = input->info.indexed_frames;
        input->info.frame_count_estimated = 0;
    }
    else if(input->stream->nb_frames > 0) {
        input->info.frame_count = input->stream->nb_frames;
        input->info.frame_count_estimated = 0;
    }
    else if(packet_count > 0) {
        input->info.frame_count = packet_count;
        input->info.frame_count_estimated = !input->info.timestamp_index_reliable;
    }
    else {
        input->info.frame_count = vj_ffmpeg_estimate_frame_count(input);
        input->info.frame_count_estimated = 1;
    }

    if(input->info.frame_count <= 0)
        goto fail;

    if(!vj_ffmpeg_seek_start(input))
        goto fail;

    vj_ffmpeg_correct_trailing_frame_count(input);


    if(input->hw_backend == VJ_FFMPEG_HW_BACKEND_SOFTWARE)
        veejay_msg(VEEJAY_MSG_INFO,
                   "[VIDEO-DECODE] source='%s' mode=software backend=software codec=%s",
                   input->filename,
                   input->info.codec_name);

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-IN] opened '%s': codec=%s fourcc=%s %dx%d %.6f fps frames=%" PRId64 "%s intra=%d ts_index=%d",
               filename,
               input->info.codec_name,
               input->info.fourcc,
               input->info.width,
               input->info.height,
               input->info.fps,
               input->info.frame_count,
               input->info.frame_count_estimated ? " (estimated)" : "",
               input->info.intra_only,
               input->info.timestamp_index_reliable);

    return input;

fail:
    vj_ffmpeg_input_close(input);
    return NULL;
}

void vj_ffmpeg_input_close(vj_ffmpeg_input *input)
{
    if(!input)
        return;

    if(input->index_mutex_ready) {
        input->index_cancel = 1;

        if(input->index_thread_started)
            pthread_join(input->index_thread, NULL);

        pthread_mutex_destroy(&input->index_mutex);

        input->index_mutex_ready = 0;
        input->index_thread_started = 0;
    }

    if(input->scaler)
        sws_freeContext(input->scaler);

    if(input->index_cache_entry)
        vj_ffmpeg_index_cache_release(input);
    else
        free(input->index);

    vj_ffmpeg_audio_decoder_close(input);

    av_packet_free(&input->packet);
    av_frame_free(&input->frame);

    vj_ffmpeg_video_decoder_close(input);

    avformat_close_input(&input->format_ctx);

    free(input->filename);
    free(input);
}


const vj_ffmpeg_input_info *vj_ffmpeg_input_get_info(const vj_ffmpeg_input *input)
{
    return input ? &input->info : NULL;
}

int vj_ffmpeg_input_configure_audio(vj_ffmpeg_input *input,
                                    int sample_rate,
                                    int channels)
{
    if(!input || !input->info.has_audio || !input->audio_codec_ctx)
        return 0;

    if(input->audio_configured &&
       input->audio_out_rate == sample_rate &&
       input->audio_out_channels == channels)
        return 1;

    if(!vj_ffmpeg_audio_resampler_init(input, sample_rate, channels)) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-AUDIO] unable to configure PCM output source='%s' rate=%d channels=%d",
                   input->filename,
                   sample_rate,
                   channels);
        return 0;
    }

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-AUDIO] PCM output source='%s' s16le rate=%d channels=%d",
               input->filename,
               sample_rate,
               channels);
    return 1;
}

static int64_t vj_ffmpeg_video_origin_us(const vj_ffmpeg_input *input)
{
    if(input->index_count > 0 && input->index[0].pts != AV_NOPTS_VALUE)
        return av_rescale_q(input->index[0].pts,
                            input->stream->time_base,
                            AV_TIME_BASE_Q);

    if(input->stream && input->stream->start_time != AV_NOPTS_VALUE)
        return av_rescale_q(input->stream->start_time,
                            input->stream->time_base,
                            AV_TIME_BASE_Q);

    if(input->format_ctx && input->format_ctx->start_time != AV_NOPTS_VALUE)
        return input->format_ctx->start_time;

    return 0;
}

static int64_t vj_ffmpeg_audio_pts_to_sample(const vj_ffmpeg_input *input,
                                             int64_t pts)
{
    if(!input || !input->audio_stream || input->audio_out_rate <= 0 ||
       pts == AV_NOPTS_VALUE)
        return AV_NOPTS_VALUE;

    int64_t pts_us = av_rescale_q(pts,
                                  input->audio_stream->time_base,
                                  AV_TIME_BASE_Q);
    int64_t origin_us = vj_ffmpeg_video_origin_us(input);
    return av_rescale_q(pts_us - origin_us,
                        AV_TIME_BASE_Q,
                        (AVRational){1, input->audio_out_rate});
}

static int64_t vj_ffmpeg_audio_stream_start_sample(const vj_ffmpeg_input *input)
{
    if(!input || !input->audio_stream ||
       input->audio_stream->start_time == AV_NOPTS_VALUE)
        return 0;
    return vj_ffmpeg_audio_pts_to_sample(input, input->audio_stream->start_time);
}

static int64_t vj_ffmpeg_audio_frame_pts(const AVFrame *frame)
{
    if(frame->best_effort_timestamp != AV_NOPTS_VALUE)
        return frame->best_effort_timestamp;
    if(frame->pts != AV_NOPTS_VALUE)
        return frame->pts;
    return AV_NOPTS_VALUE;
}

static int vj_ffmpeg_audio_resampler_reset(vj_ffmpeg_input *input)
{
    if(!input || !input->audio_resampler)
        return 0;
    swr_close(input->audio_resampler);
    if(swr_init(input->audio_resampler) < 0)
        return 0;
    input->audio_resampler_drained = 0;
    return 1;
}

static int vj_ffmpeg_audio_reset_after_seek(vj_ffmpeg_input *input,
                                            int require_timestamp)
{
    if(!input || !input->audio_format_ctx || !input->audio_codec_ctx ||
       !input->audio_resampler)
        return 0;

    avformat_flush(input->audio_format_ctx);
    avcodec_flush_buffers(input->audio_codec_ctx);
    av_packet_unref(input->audio_packet);
    av_frame_unref(input->audio_frame);
    if(!vj_ffmpeg_audio_resampler_reset(input))
        return 0;

    input->audio_draining = 0;
    input->audio_pcm_samples = 0;
    input->audio_pcm_offset = 0;
    input->audio_pcm_start_sample = 0;
    input->audio_resample_next_sample = 0;
    input->audio_resample_position_valid = 0;
    input->audio_seek_requires_timestamp = require_timestamp;
    input->audio_request_valid = 0;
    return 1;
}

static int vj_ffmpeg_audio_seek(vj_ffmpeg_input *input,
                                int64_t start_sample,
                                int from_stream_start)
{
    if(!input || !input->audio_stream || input->audio_out_rate <= 0)
        return 0;

    int64_t origin_us = vj_ffmpeg_video_origin_us(input);
    int64_t audio_start_us = origin_us;
    if(input->audio_stream->start_time != AV_NOPTS_VALUE)
        audio_start_us = av_rescale_q(input->audio_stream->start_time,
                                      input->audio_stream->time_base,
                                      AV_TIME_BASE_Q);

    int64_t target_us = origin_us +
        av_rescale_q(start_sample,
                     (AVRational){1, input->audio_out_rate},
                     AV_TIME_BASE_Q);
    int64_t seek_us = from_stream_start ? audio_start_us :
                      target_us - VJ_FFMPEG_AUDIO_SEEK_PREROLL_US;
    if(seek_us < audio_start_us)
        seek_us = audio_start_us;

    int64_t seek_ts = av_rescale_q(seek_us,
                                   AV_TIME_BASE_Q,
                                   input->audio_stream->time_base);
    int ret = avformat_seek_file(input->audio_format_ctx,
                                 input->audio_stream_index,
                                 INT64_MIN,
                                 seek_ts,
                                 INT64_MAX,
                                 AVSEEK_FLAG_BACKWARD);
    if(ret < 0)
        ret = av_seek_frame(input->audio_format_ctx,
                            input->audio_stream_index,
                            seek_ts,
                            AVSEEK_FLAG_BACKWARD);
    if(ret < 0) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-AUDIO] seek failed source='%s' sample=%" PRId64 " ret=%d",
                   input->filename,
                   start_sample,
                   ret);
        return 0;
    }

    return vj_ffmpeg_audio_reset_after_seek(input, !from_stream_start);
}

static int vj_ffmpeg_audio_decode_next(vj_ffmpeg_input *input)
{
    for(;;) {
        int ret = avcodec_receive_frame(input->audio_codec_ctx,
                                        input->audio_frame);
        if(ret == 0) {
            input->stats.audio_decoded_frames++;
            return 1;
        }
        if(ret == AVERROR_EOF)
            return 0;
        if(ret != AVERROR(EAGAIN))
            return -1;

        if(input->audio_draining) {
            ret = avcodec_send_packet(input->audio_codec_ctx, NULL);
            if(ret == AVERROR_EOF)
                return 0;
            if(ret < 0 && ret != AVERROR(EAGAIN))
                return -1;
            continue;
        }

        do {
            ret = av_read_frame(input->audio_format_ctx, input->audio_packet);
            if(ret < 0) {
                input->audio_draining = 1;
                ret = avcodec_send_packet(input->audio_codec_ctx, NULL);
                if(ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN))
                    return -1;
                break;
            }

            if(input->audio_packet->stream_index != input->audio_stream_index) {
                av_packet_unref(input->audio_packet);
                continue;
            }

            ret = avcodec_send_packet(input->audio_codec_ctx,
                                      input->audio_packet);
            av_packet_unref(input->audio_packet);
            if(ret < 0 && ret != AVERROR(EAGAIN))
                return -1;
            break;
        } while(1);
    }
}

static int vj_ffmpeg_audio_ensure_pcm(vj_ffmpeg_input *input,
                                      int samples)
{
    if(samples <= input->audio_pcm_capacity)
        return 1;
    if(samples <= 0 || samples > VJ_FFMPEG_AUDIO_MAX_BLOCK_SAMPLES)
        return 0;

    int capacity = input->audio_pcm_capacity;
    if(capacity < VJ_FFMPEG_AUDIO_INITIAL_BLOCK_SAMPLES)
        capacity = VJ_FFMPEG_AUDIO_INITIAL_BLOCK_SAMPLES;
    while(capacity < samples && capacity < VJ_FFMPEG_AUDIO_MAX_BLOCK_SAMPLES) {
        if(capacity > VJ_FFMPEG_AUDIO_MAX_BLOCK_SAMPLES / 2) {
            capacity = VJ_FFMPEG_AUDIO_MAX_BLOCK_SAMPLES;
            break;
        }
        capacity *= 2;
    }
    if(capacity < samples)
        return 0;

    size_t bytes = (size_t)capacity *
                   (size_t)input->audio_out_channels * sizeof(int16_t);
    uint8_t *pcm = realloc(input->audio_pcm, bytes);
    if(!pcm)
        return 0;
    input->audio_pcm = pcm;
    input->audio_pcm_capacity = capacity;
    return 1;
}

static int vj_ffmpeg_audio_convert_frame(vj_ffmpeg_input *input)
{
    AVFrame *frame = input->audio_frame;
    if(!frame || frame->nb_samples <= 0)
        return 0;

    int64_t pts = vj_ffmpeg_audio_frame_pts(frame);
    if(!input->audio_resample_position_valid &&
       pts == AV_NOPTS_VALUE && input->audio_seek_requires_timestamp)
        return -2;

    int64_t frame_start = pts != AV_NOPTS_VALUE ?
                          vj_ffmpeg_audio_pts_to_sample(input, pts) :
                          AV_NOPTS_VALUE;
    int64_t delay = swr_get_delay(input->audio_resampler,
                                  input->audio_out_rate);
    if(delay < 0)
        delay = 0;

    int64_t block_start;
    if(frame_start != AV_NOPTS_VALUE) {
        block_start = frame_start - delay;
        if(input->audio_resample_position_valid) {
            int64_t delta = block_start - input->audio_resample_next_sample;
            if(delta > VJ_FFMPEG_AUDIO_TIMESTAMP_TOLERANCE ||
               delta < -VJ_FFMPEG_AUDIO_TIMESTAMP_TOLERANCE) {
                veejay_msg(VEEJAY_MSG_DEBUG,
                           "[FFMPEG-AUDIO] timestamp discontinuity source='%s' delta=%" PRId64 " samples",
                           input->filename,
                           delta);
                if(!vj_ffmpeg_audio_resampler_reset(input))
                    return -1;
                block_start = frame_start;
            }
            else {
                block_start = input->audio_resample_next_sample;
            }
        }
        input->audio_seek_requires_timestamp = 0;
    }
    else if(input->audio_resample_position_valid) {
        block_start = input->audio_resample_next_sample;
    }
    else {
        block_start = vj_ffmpeg_audio_stream_start_sample(input);
    }

    int out_samples = swr_get_out_samples(input->audio_resampler,
                                          frame->nb_samples);
    if(out_samples <= 0 ||
       !vj_ffmpeg_audio_ensure_pcm(input, out_samples))
        return -1;

    uint8_t *out_data[1] = { input->audio_pcm };
    int produced = swr_convert(input->audio_resampler,
                               out_data,
                               input->audio_pcm_capacity,
                               (const uint8_t **)frame->extended_data,
                               frame->nb_samples);
    if(produced < 0)
        return -1;

    input->audio_pcm_start_sample = block_start;
    input->audio_pcm_samples = produced;
    input->audio_pcm_offset = 0;
    input->audio_resample_next_sample = block_start + produced;
    input->audio_resample_position_valid = 1;
    return produced > 0 ? 1 : 0;
}

static int vj_ffmpeg_audio_flush_resampler(vj_ffmpeg_input *input)
{
    if(input->audio_resampler_drained)
        return 0;

    int out_samples = swr_get_out_samples(input->audio_resampler, 0);
    if(out_samples <= 0) {
        input->audio_resampler_drained = 1;
        return 0;
    }
    if(!vj_ffmpeg_audio_ensure_pcm(input, out_samples))
        return -1;

    uint8_t *out_data[1] = { input->audio_pcm };
    int produced = swr_convert(input->audio_resampler,
                               out_data,
                               input->audio_pcm_capacity,
                               NULL,
                               0);
    if(produced < 0)
        return -1;
    if(produced == 0) {
        input->audio_resampler_drained = 1;
        return 0;
    }

    input->audio_pcm_start_sample = input->audio_resample_next_sample;
    input->audio_pcm_samples = produced;
    input->audio_pcm_offset = 0;
    input->audio_resample_next_sample += produced;
    return 1;
}

static int vj_ffmpeg_audio_load_block(vj_ffmpeg_input *input)
{
    for(;;) {
        int ret = vj_ffmpeg_audio_decode_next(input);
        if(ret < 0)
            return -1;
        if(ret == 0)
            return vj_ffmpeg_audio_flush_resampler(input);

        ret = vj_ffmpeg_audio_convert_frame(input);
        av_frame_unref(input->audio_frame);
        if(ret < 0)
            return ret;
        if(ret > 0)
            return 1;
    }
}

int vj_ffmpeg_input_get_audio_samples(vj_ffmpeg_input *input,
                                      int64_t start_sample,
                                      int sample_count,
                                      uint8_t *dst)
{
    if(!input || !dst || !input->audio_configured ||
       start_sample < 0 || sample_count <= 0)
        return 0;
    if(start_sample > INT64_MAX - sample_count)
        return 0;

    const int frame_bytes = input->audio_out_channels * (int)sizeof(int16_t);
    if(frame_bytes <= 0 ||
       (size_t)sample_count > SIZE_MAX / (size_t)frame_bytes)
        return 0;

    int sequential = input->audio_request_valid &&
                     input->audio_request_next_sample == start_sample;
    int fallback_from_start = 0;
    int seek_verified = sequential;
    int64_t preroll_samples = 0;
    int64_t started_us = sequential ? 0 : av_gettime_relative();

    if(sequential) {
        input->stats.audio_sequential_reads++;
    }
    else {
        if(!vj_ffmpeg_audio_seek(input, start_sample, 0)) {
            if(!vj_ffmpeg_audio_seek(input, start_sample, 1)) {
                memset(dst, 0, (size_t)sample_count * (size_t)frame_bytes);
                input->stats.audio_zero_filled_samples += (uint64_t)sample_count;
                input->audio_request_valid = 0;
                return sample_count;
            }
            fallback_from_start = 1;
            seek_verified = 1;
            input->stats.audio_seek_fallbacks++;
        }
    }

    int written = 0;
    int decoder_error = 0;
    int64_t cursor = start_sample;
    while(written < sample_count) {
        if(input->audio_pcm_offset >= input->audio_pcm_samples) {
            input->audio_pcm_samples = 0;
            input->audio_pcm_offset = 0;

            int ret = vj_ffmpeg_audio_load_block(input);
            if(ret == -2 && !fallback_from_start) {
                if(vj_ffmpeg_audio_seek(input, start_sample, 1)) {
                    fallback_from_start = 1;
                    seek_verified = 1;
                    preroll_samples = 0;
                    input->stats.audio_seek_fallbacks++;
                    continue;
                }
                ret = -1;
            }

            if(ret <= 0) {
                if(ret < 0)
                    decoder_error = 1;
                int remaining = sample_count - written;
                memset(dst + ((size_t)written * (size_t)frame_bytes),
                       0,
                       (size_t)remaining * (size_t)frame_bytes);
                input->stats.audio_zero_filled_samples += (uint64_t)remaining;
                written += remaining;
                cursor += remaining;
                break;
            }
        }

        int available = input->audio_pcm_samples - input->audio_pcm_offset;
        int64_t block_sample = input->audio_pcm_start_sample +
                               input->audio_pcm_offset;

        if(block_sample > cursor) {
            int64_t stream_start = vj_ffmpeg_audio_stream_start_sample(input);
            if(!sequential && !seek_verified && !fallback_from_start &&
               cursor >= stream_start) {
                if(vj_ffmpeg_audio_seek(input, start_sample, 1)) {
                    fallback_from_start = 1;
                    seek_verified = 1;
                    preroll_samples = 0;
                    input->stats.audio_seek_fallbacks++;
                    continue;
                }
            }

            int64_t gap64 = block_sample - cursor;
            int gap = gap64 > (sample_count - written) ?
                      sample_count - written : (int)gap64;
            memset(dst + ((size_t)written * (size_t)frame_bytes),
                   0,
                   (size_t)gap * (size_t)frame_bytes);
            input->stats.audio_zero_filled_samples += (uint64_t)gap;
            written += gap;
            cursor += gap;
            continue;
        }

        if(block_sample < cursor) {
            int64_t skip64 = cursor - block_sample;
            int skip = skip64 > available ? available : (int)skip64;
            input->audio_pcm_offset += skip;
            preroll_samples += skip;
            seek_verified = 1;
            continue;
        }

        seek_verified = 1;
        int copy = available;
        if(copy > sample_count - written)
            copy = sample_count - written;
        memcpy(dst + ((size_t)written * (size_t)frame_bytes),
               input->audio_pcm +
                   ((size_t)input->audio_pcm_offset * (size_t)frame_bytes),
               (size_t)copy * (size_t)frame_bytes);
        input->audio_pcm_offset += copy;
        written += copy;
        cursor += copy;
    }

    input->audio_request_next_sample = start_sample + sample_count;
    input->audio_request_valid = decoder_error ? 0 : 1;

    if(!sequential) {
        int64_t elapsed_us = av_gettime_relative() - started_us;
        input->stats.audio_seek_count++;
        input->stats.audio_last_seek_us = elapsed_us;
        if(elapsed_us > input->stats.audio_max_seek_us)
            input->stats.audio_max_seek_us = elapsed_us;
        if(preroll_samples > input->stats.audio_max_preroll_samples)
            input->stats.audio_max_preroll_samples = preroll_samples;
    }

    return sample_count;
}

void vj_ffmpeg_input_get_stats(const vj_ffmpeg_input *input,
                               vj_ffmpeg_input_stats *stats)
{
    if(!stats)
        return;
    if(!input) {
        memset(stats, 0, sizeof(*stats));
        return;
    }
    *stats = input->stats;
}



static int vj_ffmpeg_decode_next(vj_ffmpeg_input *input)
{
    int64_t started_us = av_gettime_relative();
    for(;;) {
        int ret = avcodec_receive_frame(input->codec_ctx, input->frame);
        if(ret == 0) {
            if(input->hw_backend != VJ_FFMPEG_HW_BACKEND_SOFTWARE &&
               input->frame->format == input->hw_pix_fmt &&
               !input->hw_session_warm) {
                input->hw_session_warm = 1;
                input->hw_request_warmed = 1;
                veejay_msg(VEEJAY_MSG_INFO,
                           "[VIDEO-DECODE] source='%s' mode=hardware backend=%s codec=%s hwfmt=%s",
                           input->filename,
                           vj_ffmpeg_hw_backend_name(input->hw_backend),
                           input->info.codec_name,
                           av_get_pix_fmt_name(input->hw_pix_fmt) ?
                               av_get_pix_fmt_name(input->hw_pix_fmt) : "unknown");
            }
            int64_t elapsed_us = av_gettime_relative() - started_us;
            input->stats.decoded_frames++;
            input->stats.decode_last_us = elapsed_us;
            input->stats.decode_total_us += elapsed_us;
            if(elapsed_us > input->stats.decode_max_us)
                input->stats.decode_max_us = elapsed_us;
            return 1;
        }
        if(ret == AVERROR_EOF)
            return 0;
        if(ret != AVERROR(EAGAIN)) {
            vj_ffmpeg_hw_runtime_error(input, ret, "receive-frame");
            return -1;
        }

        if(input->draining) {
            ret = avcodec_send_packet(input->codec_ctx, NULL);
            if(ret == AVERROR_EOF)
                return 0;
            if(ret < 0 && ret != AVERROR(EAGAIN)) {
                vj_ffmpeg_hw_runtime_error(input, ret, "drain");
                return -1;
            }
            continue;
        }

        do {
            ret = av_read_frame(input->format_ctx, input->packet);
            if(ret < 0) {
                input->draining = 1;
                ret = avcodec_send_packet(input->codec_ctx, NULL);
                if(ret < 0 && ret != AVERROR_EOF && ret != AVERROR(EAGAIN)) {
                    vj_ffmpeg_hw_runtime_error(input, ret, "flush");
                    return -1;
                }
                break;
            }

            if(input->packet->stream_index != input->stream_index) {
                av_packet_unref(input->packet);
                continue;
            }

            ret = avcodec_send_packet(input->codec_ctx, input->packet);
            av_packet_unref(input->packet);
            if(ret < 0 && ret != AVERROR(EAGAIN)) {
                vj_ffmpeg_hw_runtime_error(input, ret, "send-packet");
                return -1;
            }
            break;
        } while(1);
    }
}

static enum AVPixelFormat vj_ffmpeg_hw_transfer_format(const AVFrame *frame)
{
    if(!frame || !frame->hw_frames_ctx)
        return AV_PIX_FMT_NONE;

    enum AVPixelFormat *formats = NULL;
    int ret = av_hwframe_transfer_get_formats(frame->hw_frames_ctx,
                                               AV_HWFRAME_TRANSFER_DIRECTION_FROM,
                                               &formats,
                                               0);
    if(ret < 0 || !formats)
        return AV_PIX_FMT_NONE;

    enum AVPixelFormat selected = AV_PIX_FMT_NONE;
    for(enum AVPixelFormat *p = formats; *p != AV_PIX_FMT_NONE; p++) {
        if(sws_isSupportedInput(*p)) {
            selected = *p;
            break;
        }
    }
    av_free(formats);
    return selected;
}

static AVFrame *vj_ffmpeg_hw_download_frame(vj_ffmpeg_input *input)
{
    AVFrame *source = input->frame;
    if(input->hw_backend == VJ_FFMPEG_HW_BACKEND_SOFTWARE ||
       source->format != input->hw_pix_fmt)
        return source;

    if(!source->hw_frames_ctx) {
        input->hw_runtime_failed = 1;
        input->hw_last_error = AVERROR(EINVAL);
        input->stats.hw_transfer_failures++;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' backend=%s download failed reason=no-hw-frames-context",
                   input->filename,
                   vj_ffmpeg_hw_backend_name(input->hw_backend));
        return NULL;
    }

    const void *transfer_ctx_id = source->hw_frames_ctx->data;
    enum AVPixelFormat transfer_format = input->hw_transfer_pix_fmt;
    if(transfer_format == AV_PIX_FMT_NONE ||
       input->hw_transfer_ctx_id != transfer_ctx_id) {
        transfer_format = vj_ffmpeg_hw_transfer_format(source);
        input->hw_transfer_pix_fmt = transfer_format;
        input->hw_transfer_ctx_id = transfer_ctx_id;
        if(transfer_format != AV_PIX_FMT_NONE)
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[FFMPEG-HW] source='%s' backend=%s transfer-format=%s",
                       input->filename,
                       vj_ffmpeg_hw_backend_name(input->hw_backend),
                       av_get_pix_fmt_name(transfer_format) ?
                           av_get_pix_fmt_name(transfer_format) : "unknown");
    }
    if(transfer_format == AV_PIX_FMT_NONE) {
        input->hw_runtime_failed = 1;
        input->hw_last_error = AVERROR(ENOSYS);
        input->stats.hw_transfer_failures++;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' backend=%s download failed reason=no-sws-compatible-transfer-format",
                   input->filename,
                   vj_ffmpeg_hw_backend_name(input->hw_backend));
        return NULL;
    }

    if(!input->hw_transfer_frame)
        input->hw_transfer_frame = av_frame_alloc();
    if(!input->hw_transfer_frame) {
        input->hw_runtime_failed = 1;
        input->hw_last_error = AVERROR(ENOMEM);
        input->stats.hw_transfer_failures++;
        return NULL;
    }

    AVFrame *transfer = input->hw_transfer_frame;
    if(transfer->format != transfer_format ||
       transfer->width != source->width ||
       transfer->height != source->height ||
       !transfer->buf[0]) {
        av_frame_unref(transfer);
        transfer->format = transfer_format;
        transfer->width = source->width;
        transfer->height = source->height;
        int ret = av_frame_get_buffer(transfer, 32);
        if(ret < 0) {
            input->hw_runtime_failed = 1;
            input->hw_last_error = ret;
            input->stats.hw_transfer_failures++;
            return NULL;
        }
    }
    else {
        int ret = av_frame_make_writable(transfer);
        if(ret < 0) {
            input->hw_runtime_failed = 1;
            input->hw_last_error = ret;
            input->stats.hw_transfer_failures++;
            return NULL;
        }
    }

    int64_t started_us = av_gettime_relative();
    int ret = av_hwframe_transfer_data(transfer, source, 0);
    int64_t elapsed_us = av_gettime_relative() - started_us;
    if(ret < 0) {
        input->hw_runtime_failed = 1;
        input->hw_last_error = ret;
        input->stats.hw_transfer_failures++;
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[FFMPEG-HW] source='%s' backend=%s download failed ret=%d",
                   input->filename,
                   vj_ffmpeg_hw_backend_name(input->hw_backend),
                   ret);
        return NULL;
    }

    transfer->color_range = source->color_range;
    input->stats.hw_transfers++;
    input->stats.hw_last_transfer_us = elapsed_us;
    input->stats.hw_transfer_total_us += elapsed_us;
    if(elapsed_us > input->stats.hw_max_transfer_us)
        input->stats.hw_max_transfer_us = elapsed_us;
    return transfer;
}

static int vj_ffmpeg_copy_frame(vj_ffmpeg_input *input, uint8_t *dst[4])
{
    AVFrame *source = vj_ffmpeg_hw_download_frame(input);

    if(!source)
        return 0;

    int64_t started_us = av_gettime_relative();

    enum AVPixelFormat source_pix_fmt =
        input->forced_source_pix_fmt != AV_PIX_FMT_NONE ?
            input->forced_source_pix_fmt :
            vj_ffmpeg_frame_pix_fmt(source);

    /*
     * Fast path:
     *
     * If the downloaded frame already matches the requested output
     * format and geometry, copy the planes directly and avoid the
     * swscale context entirely.
     */
    if(source_pix_fmt == input->out_pix_fmt &&
       source->width == input->out_width &&
       source->height == input->out_height) {
        av_image_copy(dst,
                      input->dst_linesize,
                      (const uint8_t **)source->data,
                      source->linesize,
                      source_pix_fmt,
                      source->width,
                      source->height);

        int64_t elapsed_us = av_gettime_relative() - started_us;

        input->stats.converted_frames++;
        input->stats.convert_last_us = elapsed_us;
        input->stats.convert_total_us += elapsed_us;

        if(elapsed_us > input->stats.convert_max_us)
            input->stats.convert_max_us = elapsed_us;

        return 1;
    }

    input->scaler = sws_getCachedContext(input->scaler,
                                         source->width,
                                         source->height,
                                         source_pix_fmt,
                                         input->out_width,
                                         input->out_height,
                                         input->out_pix_fmt,
                                         SWS_FAST_BILINEAR,
                                         NULL,
                                         NULL,
                                         NULL);

    if(!input->scaler)
        return 0;

    int rows = sws_scale(input->scaler,
                         (const uint8_t * const *)source->data,
                         source->linesize,
                         0,
                         source->height,
                         dst,
                         input->dst_linesize);

    int ok = rows == input->out_height;

    if(ok) {
        int64_t elapsed_us = av_gettime_relative() - started_us;

        input->stats.converted_frames++;
        input->stats.convert_last_us = elapsed_us;
        input->stats.convert_total_us += elapsed_us;

        if(elapsed_us > input->stats.convert_max_us)
            input->stats.convert_max_us = elapsed_us;
    }

    return ok;
}

static int64_t vj_ffmpeg_frame_pts(const AVFrame *frame)
{
    if(frame->best_effort_timestamp != AV_NOPTS_VALUE)
        return frame->best_effort_timestamp;
    if(frame->pts != AV_NOPTS_VALUE)
        return frame->pts;
    return AV_NOPTS_VALUE;
}

static void vj_ffmpeg_record_seek(vj_ffmpeg_input *input,
                                  int64_t elapsed_us,
                                  int64_t preroll_frames)
{
    if(input->hw_backend != VJ_FFMPEG_HW_BACKEND_SOFTWARE &&
       input->hw_request_warmed) {
        input->stats.hw_warmup_seeks_ignored++;
        return;
    }

    input->stats.seek_count++;
    input->stats.last_seek_us = elapsed_us;
    input->stats.last_preroll_frames = preroll_frames;
    if(elapsed_us > input->stats.max_seek_us)
        input->stats.max_seek_us = elapsed_us;
    if(preroll_frames > input->stats.max_preroll_frames)
        input->stats.max_preroll_frames = preroll_frames;
}

static int vj_ffmpeg_linear_get_frame(vj_ffmpeg_input *input,
                                      int64_t frame_number,
                                      uint8_t *dst[4],
                                      int reset,
                                      long preroll_cache_frames,
                                      vj_ffmpeg_preroll_store_cb store_preroll,
                                      void *store_opaque)
{
    int64_t started_us = 0;
    if(reset) {
        started_us = av_gettime_relative();
        if(!vj_ffmpeg_seek_start(input))
            return 0;
    }

    int64_t skipped = 0;
    while(input->next_frame <= frame_number) {
        int ret = vj_ffmpeg_decode_next(input);
        if(ret <= 0)
            return 0;

        int64_t decoded_number = input->next_frame++;
        if(decoded_number == frame_number) {
            int ok = vj_ffmpeg_copy_frame(input, dst);
            if(!ok)
                input->next_frame = decoded_number;
            if(reset && ok) {
                int64_t elapsed_us = av_gettime_relative() - started_us;
                vj_ffmpeg_record_seek(input, elapsed_us, skipped);
            }
            return ok;
        }

        if(store_preroll && preroll_cache_frames > 0 &&
           decoded_number >= frame_number - preroll_cache_frames) {
            if(vj_ffmpeg_copy_frame(input, dst))
                store_preroll(store_opaque, decoded_number, dst);
            else if(input->hw_runtime_failed)
                return 0;
        }

        skipped++;
        av_frame_unref(input->frame);
    }

    return 0;
}

static int64_t vj_ffmpeg_preceding_keyframe(const vj_ffmpeg_input *input,
                                            int64_t frame_number)
{
    if(frame_number >= input->index_count)
        frame_number = input->index_count - 1;
    for(int64_t i = frame_number; i >= 0; i--)
        if(input->index[i].keyframe)
            return i;
    return -1;
}

static int vj_ffmpeg_tail_frame_decodable(vj_ffmpeg_input *input)
{
    if(!input || input->info.frame_count <= 1)
        return 1;

    if(!input->info.timestamp_index_reliable ||
       input->index_count < input->info.frame_count)
        return 1;

    int64_t target_frame = input->info.frame_count - 1;
    int64_t target_pts = input->index[target_frame].pts;

    int64_t keyframe = vj_ffmpeg_preceding_keyframe(input, target_frame);

    if(keyframe < 0)
        return 1;

    if(av_seek_frame(input->format_ctx,
                     input->stream_index,
                     input->index[keyframe].pts,
                     AVSEEK_FLAG_BACKWARD) < 0)
        return 1;

    avformat_flush(input->format_ctx);
    avcodec_flush_buffers(input->codec_ctx);
    av_packet_unref(input->packet);
    av_frame_unref(input->frame);
    input->draining = 0;

    int found = 0;

    for(;;) {
        int ret = vj_ffmpeg_decode_next(input);

        if(ret <= 0 || input->hw_runtime_failed)
            break;

        int64_t pts = vj_ffmpeg_frame_pts(input->frame);

        if(pts == target_pts) {
            found = 1;
            av_frame_unref(input->frame);
            break;
        }

        if(pts != AV_NOPTS_VALUE && pts > target_pts) {
            av_frame_unref(input->frame);
            break;
        }

        av_frame_unref(input->frame);
    }

    vj_ffmpeg_seek_start(input);

    return found;
}

static void vj_ffmpeg_correct_trailing_frame_count(vj_ffmpeg_input *input)
{
    if(!input || input->info.frame_count <= 1)
        return;

    const char *setting = getenv("VEEJAY_FFMPEG_VALIDATE_FRAME_COUNT");

    int enabled = 1;

    if(setting && setting[0]) {
        if(strcasecmp(setting, "0") == 0 ||
           strcasecmp(setting, "off") == 0 ||
           strcasecmp(setting, "disable") == 0)
            enabled = 0;
    }

    if(!enabled)
        return;

    /*
     * If the container claims one more frame than the timestamp index saw,
     * prefer the smaller timestamp-index count.
     */
    if(input->index_count > 0 &&
       input->info.frame_count == input->index_count + 1) {
        input->info.frame_count = input->index_count;

        if(input->info.indexed_frames > 0)
            input->info.indexed_frames = input->index_count;

        veejay_msg(VEEJAY_MSG_WARNING,
                   "[FFMPEG-IN] trimmed trailing frame based on timestamp index source='%s' frames=%" PRId64,
                   input->filename,
                   input->info.frame_count);
        return;
    }

    /*
     * If the timestamp index and container agree, still verify that the
     * final frame is actually produced by the decoder.
     */
    if(input->info.timestamp_index_reliable &&
       input->index_count == input->info.frame_count) {
        if(!vj_ffmpeg_tail_frame_decodable(input)) {
            input->info.frame_count--;

            if(input->info.indexed_frames > 0)
                input->info.indexed_frames--;

            veejay_msg(VEEJAY_MSG_WARNING,
                       "[FFMPEG-IN] trimmed non-decodable trailing frame source='%s' frames=%" PRId64,
                       input->filename,
                       input->info.frame_count);
        }
    }
}

/*
 * Decide whether an indexed request is cheaper to reach from the decoder's
 * current position or by seeking back to the preceding keyframe.  The cost is
 * expressed in frames that must be decoded before the requested frame; the
 * requested frame itself is common to both paths.
 */
static int64_t vj_ffmpeg_indexed_access_cost(const vj_ffmpeg_input *input,
                                             int64_t frame_number,
                                             int *seek_required,
                                             int64_t *forward_cost_out,
                                             int64_t *seek_cost_out)
{
    int64_t forward_cost = -1;
    int64_t seek_cost = -1;

    *seek_required = frame_number != input->next_frame;

    if(frame_number == input->next_frame) {
        if(forward_cost_out)
            *forward_cost_out = 0;
        if(seek_cost_out)
            *seek_cost_out = 0;
        return 0;
    }

    int64_t keyframe = vj_ffmpeg_preceding_keyframe(input, frame_number);
    if(keyframe >= 0)
        seek_cost = frame_number - keyframe;

    /* A negative cursor means the decoder was reopened and must be sought. */
    if(input->next_frame >= 0 && frame_number > input->next_frame)
        forward_cost = frame_number - input->next_frame;

    if(forward_cost >= 0 &&
       (seek_cost < 0 || forward_cost <= seek_cost)) {
        *seek_required = 0;
        if(forward_cost_out)
            *forward_cost_out = forward_cost;
        if(seek_cost_out)
            *seek_cost_out = seek_cost;
        return forward_cost;
    }

    if(forward_cost_out)
        *forward_cost_out = forward_cost;
    if(seek_cost_out)
        *seek_cost_out = seek_cost;

    return seek_cost >= 0 ? seek_cost : frame_number;
}

void vj_ffmpeg_input_check_seek_latency(vj_ffmpeg_input *input,
                                        int64_t frame_budget_us)
{
    if(!input || frame_budget_us <= 0 || input->stats.seek_count == 0)
        return;

    /*
     * Warn only once per recorded seek.
     */
    if(input->slow_seek_warned == input->stats.seek_count)
        return;

    int64_t severe_budget = frame_budget_us > INT64_MAX / 2 ?
                             INT64_MAX : frame_budget_us * 2;

    if(input->stats.last_seek_us <= severe_budget)
        return;

    input->slow_seek_warned = input->stats.seek_count;

    int64_t critical_budget = frame_budget_us > INT64_MAX / 8 ?
                               INT64_MAX : frame_budget_us * 8;

    int level = VEEJAY_MSG_WARNING;

    if(input->stats.last_seek_us > critical_budget)
        level = VEEJAY_MSG_WARNING;

    veejay_msg(level,
               "[FFMPEG-SEEK] slow seek source='%s' last=%" PRId64 "us worst=%" PRId64 "us frame_budget=%" PRId64 "us seeks=%" PRIu64 " preroll=%" PRId64 " backend=%s",
               input->filename,
               input->stats.last_seek_us,
               input->stats.max_seek_us,
               frame_budget_us,
               input->stats.seek_count,
               input->stats.last_preroll_frames,
               vj_ffmpeg_hw_backend_name(input->hw_backend));
}

int64_t vj_ffmpeg_input_preroll_requirement(vj_ffmpeg_input *input,
                                            int64_t frame_number)
{
    if(!input || frame_number < 0 ||
    frame_number == input->next_frame ||
    input->info.intra_only)
        return 0;

    if(!input->info.timestamp_index_reliable)
        vj_ffmpeg_index_async_refresh(input);

    if(input->info.timestamp_index_reliable && frame_number < input->index_count) {
        int seek_required = 0;

        return vj_ffmpeg_indexed_access_cost(input,
                                             frame_number,
                                             &seek_required,
                                             NULL,
                                             NULL);
    }

    return frame_number;
}

static int64_t vj_ffmpeg_frame_number_for_pts(const vj_ffmpeg_input *input, int64_t pts)
{
    int64_t lo = 0;
    int64_t hi = input->index_count - 1;
    while(lo <= hi) {
        int64_t mid = lo + ((hi - lo) / 2);
        if(input->index[mid].pts < pts)
            lo = mid + 1;
        else if(input->index[mid].pts > pts)
            hi = mid - 1;
        else
            return mid;
    }
    return -1;
}

static int vj_ffmpeg_indexed_get_frame(vj_ffmpeg_input *input,
                                       int64_t frame_number,
                                       uint8_t *dst[4],
                                       int seek_required,
                                       long preroll_cache_frames,
                                       vj_ffmpeg_preroll_store_cb store_preroll,
                                       void *store_opaque)
{
    if(frame_number < 0 || frame_number >= input->index_count)
        return -1;

    const int64_t target_pts = input->index[frame_number].pts;
    int64_t keyframe = -1;
    int64_t started_us = 0;

    if(seek_required) {
        keyframe = vj_ffmpeg_preceding_keyframe(input, frame_number);
        if(keyframe < 0)
            return -1;

        started_us = av_gettime_relative();
        int ret = av_seek_frame(input->format_ctx,
                                input->stream_index,
                                input->index[keyframe].pts,
                                AVSEEK_FLAG_BACKWARD);
        if(ret < 0) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[FFMPEG-SEEK] indexed seek failed target=%" PRId64 " key=%" PRId64 " ret=%d",
                       frame_number, keyframe, ret);
            return -1;
        }

        avformat_flush(input->format_ctx);
        avcodec_flush_buffers(input->codec_ctx);
        av_packet_unref(input->packet);
        av_frame_unref(input->frame);
        input->draining = 0;
    }

    int64_t preroll = 0;
    for(;;) {
        int ret = vj_ffmpeg_decode_next(input);
        if(ret <= 0)
            return -1;

        int64_t pts = vj_ffmpeg_frame_pts(input->frame);
        if(pts == AV_NOPTS_VALUE) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[FFMPEG-SEEK] decoded frame has no presentation timestamp; falling back to linear decode");
            return -1;
        }

        if(pts < target_pts) {
            if(store_preroll && preroll_cache_frames > 0) {
                int64_t decoded_number = vj_ffmpeg_frame_number_for_pts(input, pts);
                if(decoded_number >= 0 &&
                   decoded_number >= frame_number - preroll_cache_frames) {
                    if(vj_ffmpeg_copy_frame(input, dst))
                        store_preroll(store_opaque, decoded_number, dst);
                    else if(input->hw_runtime_failed)
                        return -1;
                }
            }
            preroll++;
            av_frame_unref(input->frame);
            continue;
        }

        if(pts > target_pts) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[FFMPEG-SEEK] target timestamp missed frame=%" PRId64 " wanted=%" PRId64 " got=%" PRId64 "; falling back",
                       frame_number, target_pts, pts);
            return -1;
        }

        int ok = vj_ffmpeg_copy_frame(input, dst);
        if(ok)
            input->next_frame = frame_number + 1;

        if(seek_required && ok) {
            int64_t elapsed_us = av_gettime_relative() - started_us;
            vj_ffmpeg_record_seek(input, elapsed_us, preroll);
        }
        return ok;
    }
}

static int vj_ffmpeg_hw_recover(vj_ffmpeg_input *input,
                                int64_t frame_number)
{
    vj_ffmpeg_hw_backend failed_backend = input->hw_backend;
    int failed_error = input->hw_last_error;
    if(failed_backend == VJ_FFMPEG_HW_BACKEND_SOFTWARE)
        return 0;

    vj_ffmpeg_hw_mark_failed(input, failed_backend);
    input->stats.hw_fallbacks++;

    av_packet_unref(input->packet);
    av_frame_unref(input->frame);
    vj_ffmpeg_video_decoder_close(input);
    input->next_frame = INT64_MIN;
    input->draining = 0;

    if(input->hw_policy == VJ_FFMPEG_HW_POLICY_AUTO &&
       failed_backend == VJ_FFMPEG_HW_BACKEND_VULKAN &&
       !vj_ffmpeg_hw_is_failed(input, VJ_FFMPEG_HW_BACKEND_VAAPI)) {
        if(vj_ffmpeg_video_decoder_open_backend(input,
                                                 VJ_FFMPEG_HW_BACKEND_VAAPI)) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[FFMPEG-HW] fallback source='%s' frame=%" PRId64 " failed=%s ret=%d retry_backend=vaapi",
                       input->filename,
                       frame_number,
                       vj_ffmpeg_hw_backend_name(failed_backend),
                       failed_error);
            return 1;
        }
        vj_ffmpeg_hw_mark_failed(input, VJ_FFMPEG_HW_BACKEND_VAAPI);
    }

    if(vj_ffmpeg_video_decoder_open_backend(input,
                                             VJ_FFMPEG_HW_BACKEND_SOFTWARE)) {
        veejay_msg(VEEJAY_MSG_INFO,
                   "[VIDEO-DECODE] source='%s' mode=software backend=software codec=%s fallback_from=%s frame=%" PRId64 " error=%d",
                   input->filename,
                   input->info.codec_name,
                   vj_ffmpeg_hw_backend_name(failed_backend),
                   frame_number,
                   failed_error);
        return 1;
    }

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[FFMPEG-HW] fallback source='%s' frame=%" PRId64 " failed=%s ret=%d software reopen failed",
               input->filename,
               frame_number,
               vj_ffmpeg_hw_backend_name(failed_backend),
               failed_error);
    return 0;
}

static int vj_ffmpeg_input_get_frame_once(vj_ffmpeg_input *input,
                                          int64_t frame_number,
                                          uint8_t *dst[4],
                                          long preroll_cache_frames,
                                          vj_ffmpeg_preroll_store_cb store_preroll,
                                          void *store_opaque)
{
    if(!input->info.timestamp_index_reliable)
        vj_ffmpeg_index_async_refresh(input);

    if(input->info.timestamp_index_reliable && frame_number < input->index_count) {
        int seek_required = 0;
        vj_ffmpeg_indexed_access_cost(input,
                                      frame_number,
                                      &seek_required,
                                      NULL,
                                      NULL);
        int ret = vj_ffmpeg_indexed_get_frame(input,
                                              frame_number,
                                              dst,
                                              seek_required,
                                              preroll_cache_frames,
                                              store_preroll,
                                              store_opaque);
        if(ret >= 0)
            return ret;
        if(input->hw_runtime_failed)
            return 0;

        input->stats.index_fallbacks++;
        return vj_ffmpeg_linear_get_frame(input,
                                          frame_number,
                                          dst,
                                          1,
                                          preroll_cache_frames,
                                          store_preroll,
                                          store_opaque);
    }

    if(frame_number != input->next_frame) {
        input->stats.index_fallbacks++;
        return vj_ffmpeg_linear_get_frame(input,
                                          frame_number,
                                          dst,
                                          1,
                                          preroll_cache_frames,
                                          store_preroll,
                                          store_opaque);
    }
    return vj_ffmpeg_linear_get_frame(input,
                                      frame_number,
                                      dst,
                                      0,
                                      0,
                                      NULL,
                                      NULL);
}

int vj_ffmpeg_input_get_frame(vj_ffmpeg_input *input,
                              int64_t frame_number,
                              uint8_t *dst[4],
                              long preroll_cache_frames,
                              vj_ffmpeg_preroll_store_cb store_preroll,
                              void *store_opaque)
{
    if(!input || !dst || frame_number < 0 || frame_number >= input->info.frame_count)
        return 0;

    for(int attempt = 0; attempt < 3; attempt++) {
        input->hw_runtime_failed = 0;
        input->hw_last_error = 0;
        input->hw_request_warmed = 0;
        int ret = vj_ffmpeg_input_get_frame_once(input,
                                                 frame_number,
                                                 dst,
                                                 preroll_cache_frames,
                                                 store_preroll,
                                                 store_opaque);
        if(ret == 1 || !input->hw_runtime_failed)
            return ret;

        if(!vj_ffmpeg_hw_recover(input, frame_number))
            return 0;
    }

    return 0;
}

int vj_ffmpeg_input_is_hardware(const vj_ffmpeg_input *input)
{
    return input && input->hw_backend != VJ_FFMPEG_HW_BACKEND_SOFTWARE;
}
