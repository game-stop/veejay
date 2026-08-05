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
#ifndef VJ_NDI_H
#define VJ_NDI_H

#include <stddef.h>
#include <stdint.h>
#include <libvje/vje.h>

#define VJ_NDI_SOURCE_NAME_MAX 253

typedef struct vj_ndi_receiver vj_ndi_receiver;
typedef struct vj_ndi_sender vj_ndi_sender;

typedef struct {
    char name[256];
    char url[512];
} vj_ndi_source_info;

typedef struct {
    uint64_t video_frames;
    uint64_t audio_frames;
    uint64_t dropped_video_frames;
    uint64_t dropped_audio_frames;
    uint64_t audio_underruns;
    uint64_t unsupported_video_frames;
    int connected;
    int width;
    int height;
    int source_fps_n;
    int source_fps_d;
    int audio_rate;
    int audio_channels;
    int program_tally;
    int preview_tally;
    int64_t last_video_timecode;
    int64_t last_video_timestamp;
    int64_t last_audio_timecode;
    int64_t last_audio_timestamp;
    int clock_available;
    int clock_age_ms;
    double clock_drift_ms;
    char last_metadata[256];
    char published_name[256];
    char published_url[512];
} vj_ndi_stats;

int vj_ndi_runtime_available(void);
const char *vj_ndi_runtime_version(void);
void vj_ndi_runtime_shutdown(void);
int vj_ndi_discover(vj_ndi_source_info *sources, int max_sources, int timeout_ms);
char *vj_ndi_discovery_payload(int timeout_ms);

vj_ndi_receiver *vj_ndi_receiver_create(const char *source_name,
                                         int width,
                                         int height,
                                         double fps,
                                         int audio_rate,
                                         int audio_channels);
void vj_ndi_receiver_destroy(vj_ndi_receiver *receiver);
int vj_ndi_receiver_set_active(vj_ndi_receiver *receiver, int active);
int vj_ndi_receiver_get_video(vj_ndi_receiver *receiver, VJFrame *dst);
int vj_ndi_receiver_get_audio(vj_ndi_receiver *receiver, uint8_t *dst);
int vj_ndi_receiver_has_audio(vj_ndi_receiver *receiver);
int vj_ndi_receiver_get_audio_format(vj_ndi_receiver *receiver,
                                     int *sample_rate,
                                     int *channels,
                                     int *bits,
                                     int *bytes_per_frame);
int vj_ndi_receiver_set_tally(vj_ndi_receiver *receiver,
                              int program,
                              int preview);
int vj_ndi_receiver_clock_now(vj_ndi_receiver *receiver,
                              double *clock_seconds,
                              int *age_ms);
void vj_ndi_receiver_get_stats(vj_ndi_receiver *receiver, vj_ndi_stats *stats);

vj_ndi_sender *vj_ndi_sender_create(const char *name,
                                    int width,
                                    int height,
                                    double fps,
                                    int audio_rate,
                                    int audio_channels);
void vj_ndi_sender_destroy(vj_ndi_sender *sender);
int vj_ndi_sender_send_video(vj_ndi_sender *sender, const VJFrame *frame);
int vj_ndi_sender_send_audio(vj_ndi_sender *sender,
                             const uint8_t *interleaved_s16,
                             int sample_frames);
int vj_ndi_sender_send_audio_format(vj_ndi_sender *sender,
                                    const uint8_t *interleaved_s16,
                                    int sample_frames,
                                    int sample_rate,
                                    int channels);
int vj_ndi_sender_send_metadata(vj_ndi_sender *sender, const char *xml);
void vj_ndi_sender_get_stats(vj_ndi_sender *sender, vj_ndi_stats *stats);

#endif
