/*
 * Copyright (C) 2026 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
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
#ifndef VJ_AUDIO_SYNC_H
#define VJ_AUDIO_SYNC_H

#include <stdint.h>
#include <limits.h>

#ifndef VJ_AUDIO_SYNC_PATH_MAX
#ifdef PATH_MAX
#define VJ_AUDIO_SYNC_PATH_MAX PATH_MAX
#else
#define VJ_AUDIO_SYNC_PATH_MAX 4096
#endif
#endif

#define VJ_AUDIO_SYNC_ALIGN_FEATURES 4096


#define VJ_AUDIO_SYNC_BRIDGE_STATE_IDLE         0
#define VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_SOURCE  1
#define VJ_AUDIO_SYNC_BRIDGE_STATE_WAIT_TARGET  2
#define VJ_AUDIO_SYNC_BRIDGE_STATE_LOCKED       3
#define VJ_AUDIO_SYNC_BRIDGE_STATE_HOLD         4
#define VJ_AUDIO_SYNC_BRIDGE_STATE_FALLBACK     5

#define VJ_AUDIO_SYNC_TRACK_STATE_IDLE          0
#define VJ_AUDIO_SYNC_TRACK_STATE_WAIT_SOURCE   1
#define VJ_AUDIO_SYNC_TRACK_STATE_WAIT_TARGET   2
#define VJ_AUDIO_SYNC_TRACK_STATE_SEARCHING     3
#define VJ_AUDIO_SYNC_TRACK_STATE_LOCKED        4
#define VJ_AUDIO_SYNC_TRACK_STATE_HOLD          5
#define VJ_AUDIO_SYNC_TRACK_STATE_FALLBACK      6

typedef struct
{
    int enabled;
    int open;
    int running;
    int mode;
    int source;
    int target_mode;

    int channels;
    int bytes_per_frame;
    int bits_per_channel;
    int sample_rate;

    float level;
    float envelope;
    float transient;
    float bpm;
    float beat_phase;
    float confidence;

    float target_bpm;
    float target_phase;
    float target_confidence;

    long hits;
    long last_hit_ms;
    long overruns;
    long underruns;
    long reads;

    long target_queue_overruns;
    long target_queue_reads;
    long target_queue_lock_drops;
    long target_queue_overflow_events;
    long target_queue_dropped_frames;
    int  target_queue_pending_ms;
    int  target_queue_retained_ms;
    int  target_queue_ring_ms;

    double bridge_ratio;
    double bridge_correction;
    int max_correction_pct;
    int bridge_state;

    int track_align_locked;
    int track_align_offset_ms;
    int track_align_confidence_pct;
    int track_align_correction_ppm;
    int track_align_state;

    /* Internal wide-search snap suggestion, consumed by the playback loop.
     * Not part of the public status token contract.
     */
    int track_align_snap_pending;
    int track_align_snap_delta_frames;
    int track_align_snap_confidence_pct;
} vj_audio_sync_snapshot_t;


/* Full shared state is intentionally defined here because video_playback_setup
 * embeds vj_audio_sync_shared_t directly.  Do not include vj-lib.h from the
 * audio synchronizer: modules that only need pointers can forward-declare this
 * typedef, while vj-lib.h includes this header to get the complete size.
 */
typedef struct vj_audio_sync_shared_t
{
    volatile int initialized;
    volatile int stop_request;
    volatile int running;
    volatile int enabled;
    volatile int open;

    volatile int mode;
    volatile int source;
    volatile int target_mode;
    volatile int input_channels_request;
    volatile int reset_seq;
    volatile int file_generation;

    volatile int channels;
    volatile int bytes_per_frame;
    volatile int bits_per_channel;
    volatile int sample_rate;

    volatile int level_q15;
    volatile int envelope_q15;
    volatile int transient_q15;
    volatile int source_bpm_q8;
    volatile int source_phase_q15;
    volatile int confidence_q15;
    volatile int target_bpm_q8;
    volatile int target_phase_q15;
    volatile int target_confidence_q15;
    volatile int max_correction_pct;
    volatile int bridge_active;

    volatile int track_align_locked;
    volatile int track_align_offset_ms;
    volatile int track_align_confidence_pct;
    volatile int track_align_correction_ppm;
    volatile int track_align_state;
    volatile long track_align_last_update_ms;

    volatile int track_align_snap_pending;
    volatile int track_align_snap_delta_frames;
    volatile int track_align_snap_confidence_pct;
    volatile int track_align_snap_seq;

    volatile long hits;
    volatile long last_hit_ms;
    volatile long overruns;
    volatile long underruns;
    volatile long reads;

    volatile int ring_lock;
    uint8_t *ring;
    int ring_frames;
    int ring_write_frame;
    long long write_frame_abs;

    long long analysis_read_frame;
    long long beat_read_frame;
    long long record_read_frame;

    double bridge_read_pos;
    int bridge_read_valid;
    double monitor_read_pos;
    int monitor_read_valid;
    int monitor_last_l;
    int monitor_last_r;
    int monitor_last_valid;
    long monitor_resyncs;
    long monitor_last_debug_ms;

    double bridge_ratio_smooth;
    double bridge_last_correction;
    long bridge_latch_updated_ms;
    volatile int bridge_state;

    /* Tempo Bridge transport clock.
     * Keep BPM estimates latched and slow-moving so the audible read-head
     * behaves like a stable transport, not like a phase-chasing cassette motor.
     */
    double bridge_source_bpm_latched;
    double bridge_target_bpm_latched;
    int    bridge_bpm_latch_valid;

    /* Track Align / waveform sync feature rings.
     * Source = external master track; target = current clip/original audio.
     * Features are low-rate onset/envelope values, updated by capture/push paths.
     */
    float align_source_feat[VJ_AUDIO_SYNC_ALIGN_FEATURES];
    float align_target_feat[VJ_AUDIO_SYNC_ALIGN_FEATURES];
    int   align_source_pos;
    int   align_target_pos;
    int   align_source_count;
    int   align_target_count;
    double align_source_accum;
    double align_target_accum;
    int   align_source_accum_frames;
    int   align_target_accum_frames;
    double align_source_prev_level;
    double align_target_prev_level;
    int   align_source_rate;
    int   align_target_rate;
    double align_offset_smooth_ms;
    double align_conf_smooth;
    int   align_hold_blocks;
    int   align_last_frame_action;
    long  align_last_snap_ms;
    int   align_snap_cooldown_ms;
    volatile int align_video_fps_x1000;

    /* Worker-thread live snap consensus state. */
    int  align_live_candidate_delta;
    int  align_live_candidate_conf_min;
    int  align_live_candidate_conf_sum;
    int  align_live_candidate_count;
    long align_live_candidate_ms;
    long align_live_last_snap_ms;
    int  align_live_last_snap_delta;

    /* Rough, non-authoritative live basin exported to performer wide search. */
    int  align_rough_hint_offset_ms;
    int  align_rough_hint_conf;
    int  align_rough_hint_count;
    long align_rough_hint_ms;

    /* Current-clip target audio queue.  Producer enqueues PCM cheaply;
     * audio_sync_thread drains it and runs target clock/Track Align analysis.
     */
    uint8_t *target_ring;
    int target_ring_frames;
    int target_ring_write_frame;
    long long target_write_frame_abs;

    /* Oldest retained target frame in the ring.  This is no longer the
     * worker consume cursor: Track Align needs retained target history for
     * stable correlation, while the worker still needs to process each
     * target block exactly once.
     */
    long long target_read_frame_abs;

    /* Next target frame the sync worker must process into clock/features. */
    long long target_process_frame_abs;

    int target_channels;
    int target_bytes_per_frame;
    int target_bits_per_channel;
    int target_sample_rate;
    volatile long target_queue_overruns;
    volatile long target_queue_reads;
    volatile long target_queue_lock_drops;
    volatile long target_queue_overflow_events;
    volatile long target_queue_dropped_frames;

    int record_channels;
    int record_bytes_per_frame;
    int record_bits_per_channel;
    int record_sample_rate;

    char wav_path[VJ_AUDIO_SYNC_PATH_MAX];
    volatile int wav_loop;
    volatile int wav_limit_ms;
    volatile int wav_silence_after_eof;

    /* PLAIN+WAV Track Align cache. Once a strong WAV/video lock is found,
     * replay can snap immediately without re-running wide target analysis.
     * The cache is generation-bound, so changing the WAV path/source clears it.
     */
    volatile int wav_plain_lock_valid;
    volatile int wav_plain_lock_generation;
    volatile int wav_plain_lock_delta_frames;
    volatile int wav_plain_lock_confidence_pct;

    /* internal source detector state; only sync thread / push path writes it */
    double fast_energy;
    double slow_energy;
    double envelope;
    double last_level;
    double beat_period_ms;
    long last_analysis_ms;

    /* internal target-clock detector state for current-clip auto target */
    double target_fast_energy;
    double target_slow_energy;
    double target_envelope;
    double target_last_level;
    double target_beat_period_ms;
    long target_last_analysis_ms;
    long target_last_hit_ms;
} vj_audio_sync_shared_t;

void vj_audio_sync_init(vj_audio_sync_shared_t *s, int input_channels);
void vj_audio_sync_request_stop(vj_audio_sync_shared_t *s);
void vj_audio_sync_free(vj_audio_sync_shared_t *s);
void *vj_audio_sync_thread(void *arg);

int  vj_audio_sync_enable(vj_audio_sync_shared_t *s);
int  vj_audio_sync_disable(vj_audio_sync_shared_t *s);
int  vj_audio_sync_is_enabled(vj_audio_sync_shared_t *s);
int  vj_audio_sync_is_running(vj_audio_sync_shared_t *s);
int  vj_audio_sync_is_open(vj_audio_sync_shared_t *s);
int  vj_audio_sync_get_mode(vj_audio_sync_shared_t *s);

void vj_audio_sync_set_mode(vj_audio_sync_shared_t *s, int mode);
void vj_audio_sync_set_input_channels(vj_audio_sync_shared_t *s, int channels);
void vj_audio_sync_set_source_jack(vj_audio_sync_shared_t *s, int channels);

void vj_audio_sync_set_source_push(vj_audio_sync_shared_t *s,
                                   int channels,
                                   int bits_per_channel,
                                   int sample_rate);
int  vj_audio_sync_push_audio(vj_audio_sync_shared_t *s,
                              const uint8_t *src,
                              int frames,
                              int frame_bytes,
                              int channels,
                              int sample_rate);
int  vj_audio_sync_set_source_wav(vj_audio_sync_shared_t *s, const char *path, int loop);
int  vj_audio_sync_set_source_wav_limited(vj_audio_sync_shared_t *s,
                                          const char *path,
                                          int loop,
                                          int limit_ms);
void vj_audio_sync_wav_restart(vj_audio_sync_shared_t *s);
int  vj_audio_sync_wav_plain_lock_valid(vj_audio_sync_shared_t *s);
int  vj_audio_sync_wav_plain_lock_get(vj_audio_sync_shared_t *s,
                                      int *delta_frames,
                                      int *confidence_pct);
void vj_audio_sync_wav_plain_lock_store(vj_audio_sync_shared_t *s,
                                        int delta_frames,
                                        int confidence_pct);
void vj_audio_sync_wav_plain_lock_clear(vj_audio_sync_shared_t *s);
void vj_audio_sync_set_target_clock(vj_audio_sync_shared_t *s,
                                    float bpm,
                                    float phase,
                                    float confidence);
void vj_audio_sync_set_target_mode(vj_audio_sync_shared_t *s, int mode);
int  vj_audio_sync_get_target_mode(vj_audio_sync_shared_t *s);
void vj_audio_sync_set_track_align_video_fps(vj_audio_sync_shared_t *s, double fps);
void vj_audio_sync_reset_target_clock(vj_audio_sync_shared_t *s);
int  vj_audio_sync_push_target_audio(vj_audio_sync_shared_t *s,
                                     const uint8_t *src,
                                     int frames,
                                     int frame_bytes,
                                     int channels,
                                     int sample_rate);
void vj_audio_sync_set_bridge_correction(vj_audio_sync_shared_t *s, int max_pct);
int  vj_audio_sync_track_align_frame_action(vj_audio_sync_shared_t *s,
                                            double video_fps,
                                            int current_speed);
void vj_audio_sync_track_align_reset(vj_audio_sync_shared_t *s);
void vj_audio_sync_track_align_reset_target(vj_audio_sync_shared_t *s);
void vj_audio_sync_track_align_reset_acquisition(vj_audio_sync_shared_t *s);
int vj_audio_sync_track_align_source_ready(vj_audio_sync_shared_t *s, int min_source_features);
int vj_audio_sync_track_align_last_snap(vj_audio_sync_shared_t *s, long *snap_ms, int *delta_frames);

/* Wide waveform snap support.
 * Probe candidate original-audio windows against the recent external master
 * feature window, then offer/consume a one-shot video-frame snap.
 */
int  vj_audio_sync_track_align_probe_target_audio(vj_audio_sync_shared_t *s,
                                                  const uint8_t *src,
                                                  int frames,
                                                  int frame_bytes,
                                                  int channels,
                                                  int bits_per_channel,
                                                  int sample_rate,
                                                  int *confidence_pct);
void vj_audio_sync_track_align_offer_snap(vj_audio_sync_shared_t *s,
                                          int delta_frames,
                                          int confidence_pct);
int  vj_audio_sync_track_align_offer_servo_nudge(vj_audio_sync_shared_t *s,
                                                  int delta_frames,
                                                  int confidence_pct);
int  vj_audio_sync_track_align_consume_snap(vj_audio_sync_shared_t *s,
                                            int *delta_frames,
                                            int *confidence_pct);

int  vj_audio_sync_get_format(vj_audio_sync_shared_t *s,
                              int *channels,
                              int *bytes_per_frame,
                              int *bits_per_channel,
                              int *sample_rate);

int  vj_audio_sync_read_analysis_audio(vj_audio_sync_shared_t *s,
                                       uint8_t *dst,
                                       int max_bytes);

int  vj_audio_sync_read_beat_audio(vj_audio_sync_shared_t *s,
                                   uint8_t *dst,
                                   int max_bytes);

void vj_audio_sync_reset_beat_reader(vj_audio_sync_shared_t *s);

int  vj_audio_sync_copy_record_audio(vj_audio_sync_shared_t *s,
                                     uint8_t *dst,
                                     int dst_frames,
                                     int dst_frame_bytes,
                                     int dst_channels,
                                     int dst_sample_rate);

int  vj_audio_sync_render_bridge_s16(vj_audio_sync_shared_t *s,
                                     uint8_t *dst,
                                     int dst_frames,
                                     int dst_frame_bytes,
                                     int dst_channels,
                                     int dst_sample_rate);

int  vj_audio_sync_render_monitor_s16(vj_audio_sync_shared_t *s,
                                      uint8_t *dst,
                                      int dst_frames,
                                      int dst_frame_bytes,
                                      int dst_channels,
                                      int dst_sample_rate);

int  vj_audio_sync_get_snapshot(vj_audio_sync_shared_t *s,
                                vj_audio_sync_snapshot_t *dst);

#endif /* VJ_AUDIO_SYNC_H */
