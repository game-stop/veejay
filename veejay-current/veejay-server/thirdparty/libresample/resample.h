/*
 * samplerate conversion for both audio and video
 * Copyright (c) 2000 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


/* libav only has libavresample
 * ffmpeg has both libavresample and libswresample
 *
 * backport obsolete resampler to veejay
 *
 */

#ifndef AV_RESAMPLER
#define AV_RESAMPLER

void *vj_av_resample_init(int out_rate, int in_rate, int filter_size, int phase_shift, int linear, double cutoff);

void vj_av_resample_close(void *ptr);

int vj_av_resample(void *ptr, short *dst, short *src, int *consumed, int src_size, int dst_size, int update_ctx);

void *vj_av_audio_resample_init(int output_channels, int input_channels,
                                        int output_rate, int input_rate,
                                        enum AVSampleFormat sample_fmt_out,
                                        enum AVSampleFormat sample_fmt_in,
                                        int filter_length, int log2_phase_count,
                                        int linear, double cutoff);

int vj_audio_resample(void *s, short *output, short *input, int nb_samples);


void vj_audio_resample_close(void *s);
#endif
