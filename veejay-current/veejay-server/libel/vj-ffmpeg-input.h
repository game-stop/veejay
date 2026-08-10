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

#ifndef VJ_FFMPEG_INPUT_H
#define VJ_FFMPEG_INPUT_H

#include <stdint.h>

typedef struct vj_ffmpeg_input vj_ffmpeg_input;

typedef struct
{
    int width;
    int height;

    int sar_num;
    int sar_den;

    int interlaced;

    double fps;

    int64_t frame_count;
    int frame_count_estimated;

    int codec_id;
    int intra_only;

    int timestamp_index_reliable;
    int64_t indexed_frames;
    int64_t keyframe_count;
    int64_t max_keyframe_gap;

    int has_audio;
    int audio_stream_index;
    int audio_codec_id;
    int audio_rate;
    int audio_channels;

    char codec_name[64];
    char audio_codec_name[64];
    char fourcc[5];
} vj_ffmpeg_input_info;

typedef struct
{
    uint64_t seek_count;

    int64_t last_seek_us;
    int64_t max_seek_us;

    int64_t last_preroll_frames;
    int64_t max_preroll_frames;

    uint64_t decoded_frames;
    uint64_t index_fallbacks;

    uint64_t hw_transfers;
    uint64_t hw_transfer_failures;
    uint64_t hw_fallbacks;
    uint64_t hw_warmup_seeks_ignored;

    int64_t hw_last_transfer_us;
    int64_t hw_max_transfer_us;

    uint64_t audio_seek_count;
    uint64_t audio_sequential_reads;
    uint64_t audio_decoded_frames;
    uint64_t audio_seek_fallbacks;
    uint64_t audio_zero_filled_samples;

    int64_t audio_last_seek_us;
    int64_t audio_max_seek_us;
    int64_t audio_max_preroll_samples;

    int64_t decode_total_us;
    int64_t decode_last_us;
    int64_t decode_max_us;

    int64_t hw_transfer_total_us;

    uint64_t converted_frames;

    int64_t convert_total_us;
    int64_t convert_last_us;
    int64_t convert_max_us;
} vj_ffmpeg_input_stats;

typedef void (*vj_ffmpeg_preroll_store_cb)(void *opaque,
                                           int64_t frame_number,
                                           uint8_t *planes[4]);

vj_ffmpeg_input *vj_ffmpeg_input_open(const char *filename,
                                      int out_pix_fmt,
                                      int out_width,
                                      int out_height);

int vj_ffmpeg_input_prefers_generic(const char *filename);

void vj_ffmpeg_input_close(vj_ffmpeg_input *input);

const vj_ffmpeg_input_info *vj_ffmpeg_input_get_info(const vj_ffmpeg_input *input);

void vj_ffmpeg_input_get_stats(const vj_ffmpeg_input *input,
                               vj_ffmpeg_input_stats *stats);

void vj_ffmpeg_input_check_seek_latency(vj_ffmpeg_input *input,
                                        int64_t frame_budget_us);

int vj_ffmpeg_input_is_hardware(const vj_ffmpeg_input *input);

int vj_ffmpeg_input_configure_audio(vj_ffmpeg_input *input,
                                    int sample_rate,
                                    int channels);

int vj_ffmpeg_input_get_audio_samples(vj_ffmpeg_input *input,
                                      int64_t start_sample,
                                      int sample_count,
                                      uint8_t *dst);

int64_t vj_ffmpeg_input_preroll_requirement(vj_ffmpeg_input *input,
                                            int64_t frame_number);

int vj_ffmpeg_input_get_frame(vj_ffmpeg_input *input,
                              int64_t frame_number,
                              uint8_t *dst[4],
                              long preroll_cache_frames,
                              vj_ffmpeg_preroll_store_cb store_preroll,
                              void *store_opaque);

#endif