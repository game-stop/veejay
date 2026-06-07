/*
 * veejay
 *
 * Copyright (C) 2000-2008 Niels Elburg <nwelburg@gmail.com>
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
#include <config.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <veejaycore/defs.h>
#include <libsample/sampleadm.h>
#include <libstream/vj-tag.h>
#include <veejaycore/vj-server.h>
#include <libvje/vje.h>
#include <libsubsample/subsample.h>
#include <libveejay/vj-lib.h>
#include <libel/vj-el.h>
#include <math.h>
#include <libel/vj-avcodec.h>
#include <libveejay/vj-event.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/atomic.h>
#include <veejaycore/vj-msg.h>
#include <libveejay/vj-perform.h>
#include <libveejay/libveejay.h>
#include <libveejay/vj-sdl.h>
#include <libsamplerec/samplerecord.h>
#include <libel/pixbuf.h>
#include <veejaycore/avcommon.h>
#include <libveejay/vj-misc.h>
#include <veejaycore/vj-task.h>
#include <veejaycore/lzo.h>
#include <libveejay/vj-viewport.h>
#include <libveejay/vj-composite.h>
#ifdef HAVE_FREETYPE
#include <libveejay/vj-font.h>
#endif
#define RECORDERS 1
#include <libvje/internal.h>
#include <veejaycore/vjmem.h>
#include <libvje/effects/opacity.h>
#include <libvje/effects/masktransition.h>
#include <libvje/effects/shapewipe.h>
#include <libqrwrap/qrwrapper.h>
#include <libveejay/vj-split.h>
#include <libveejay/vjkf.h>
#include <veejaycore/libvevo.h>
#include <libvje/libvje.h>
#include <veejaycore/vims.h>
#include <libveejay/audioscratcher.h>

#ifdef HAVE_JACK
#include <libveejay/vj-jack.h>
#include <libveejay/vj-audio-sync.h>
#include <libveejay/vj-audio-beat.h>
#endif
#define PERFORM_AUDIO_SIZE 16384
#define AUDIO_TURN_HISTORY_BYTES 4096
#ifndef VJ_EXTERNAL_AUDIO_HISTORY_SECONDS
#define VJ_EXTERNAL_AUDIO_HISTORY_SECONDS 240.0
#endif
#ifndef VJ_EXTERNAL_AUDIO_HISTORY_MAX_SECONDS
#define VJ_EXTERNAL_AUDIO_HISTORY_MAX_SECONDS 240.0
#endif
#ifndef TRACK_ALIGN_AUDIO_RENDER_FRAMES
#define TRACK_ALIGN_AUDIO_RENDER_FRAMES 128
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_FAST_CONF
#define VJ_TRACK_ALIGN_WIDE_FAST_CONF 75
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_FAST_MARGIN
#define VJ_TRACK_ALIGN_WIDE_FAST_MARGIN 28
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS
#define VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS 8
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_BUCKET_TOLERANCE_MS
#define VJ_TRACK_ALIGN_WIDE_BUCKET_TOLERANCE_MS 450
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_BUCKET_TTL_MS
#define VJ_TRACK_ALIGN_WIDE_BUCKET_TTL_MS 5500L
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_BUCKET_DOMINANCE_SCORE
#define VJ_TRACK_ALIGN_WIDE_BUCKET_DOMINANCE_SCORE 18
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_BUCKET_LARGE_MS
#define VJ_TRACK_ALIGN_WIDE_BUCKET_LARGE_MS 1500
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_BUCKET_SMALL_ALIAS_MS
#define VJ_TRACK_ALIGN_WIDE_BUCKET_SMALL_ALIAS_MS 900
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_BUCKET_STRONG_ONE_SHOT_CONF
#define VJ_TRACK_ALIGN_WIDE_BUCKET_STRONG_ONE_SHOT_CONF 72
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_BUCKET_STRONG_ONE_SHOT_MARGIN
#define VJ_TRACK_ALIGN_WIDE_BUCKET_STRONG_ONE_SHOT_MARGIN 26
#endif

#ifndef VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_BLOCK_MS
#define VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_BLOCK_MS 1000
#endif
#ifndef VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_FORCE_MS
#define VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_FORCE_MS 1800
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_SEARCH_INTERVAL_MS
#define VJ_TRACK_ALIGN_WIDE_SEARCH_INTERVAL_MS 2500L
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SEARCH_RADIUS_MS
#define VJ_TRACK_ALIGN_WIDE_SEARCH_RADIUS_MS 12000
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SEARCH_STEP_MS
#define VJ_TRACK_ALIGN_WIDE_SEARCH_STEP_MS 500
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_PROBE_MS
#define VJ_TRACK_ALIGN_WIDE_PROBE_MS 6500
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_MIN_CONF 58
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_MIN_MARGIN
#define VJ_TRACK_ALIGN_WIDE_MIN_MARGIN 14
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_STABLE_COUNT
#define VJ_TRACK_ALIGN_WIDE_STABLE_COUNT 3
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_STABLE_TOLERANCE_MS
#define VJ_TRACK_ALIGN_WIDE_STABLE_TOLERANCE_MS 650
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_CANDIDATE_TTL_MS
#define VJ_TRACK_ALIGN_WIDE_CANDIDATE_TTL_MS 9000L
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_POST_SNAP_COOLDOWN_MS
#define VJ_TRACK_ALIGN_WIDE_POST_SNAP_COOLDOWN_MS 10000L
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_REVERSAL_GUARD_MS
#define VJ_TRACK_ALIGN_WIDE_REVERSAL_GUARD_MS 30000L
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_MIN_OFFSET_MS
#define VJ_TRACK_ALIGN_LIVE_SNAP_MIN_OFFSET_MS 900
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_MIN_CONF
#define VJ_TRACK_ALIGN_LIVE_SNAP_MIN_CONF 88
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_LARGE_CONF
#define VJ_TRACK_ALIGN_LIVE_SNAP_LARGE_CONF 70
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_LARGE_OFFSET_MS
#define VJ_TRACK_ALIGN_LIVE_SNAP_LARGE_OFFSET_MS 4000
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_STABLE_COUNT
#define VJ_TRACK_ALIGN_LIVE_SNAP_STABLE_COUNT 3
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_LARGE_STABLE_COUNT
#define VJ_TRACK_ALIGN_LIVE_SNAP_LARGE_STABLE_COUNT 2
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_TOLERANCE_MS
#define VJ_TRACK_ALIGN_LIVE_SNAP_TOLERANCE_MS 450
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_DYNAMIC_FULL_SCALE_MS
#define VJ_TRACK_ALIGN_LIVE_DYNAMIC_FULL_SCALE_MS 8000
#endif

#ifndef VJ_TRACK_ALIGN_CANDIDATE_NONE
#define VJ_TRACK_ALIGN_CANDIDATE_NONE 0
#endif
#ifndef VJ_TRACK_ALIGN_CANDIDATE_WIDE
#define VJ_TRACK_ALIGN_CANDIDATE_WIDE 1
#endif
#ifndef VJ_TRACK_ALIGN_CANDIDATE_LIVE
#define VJ_TRACK_ALIGN_CANDIDATE_LIVE 2
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_CONFLICT_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_CONFLICT_MIN_CONF 60
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_CONFLICT_MIN_MARGIN
#define VJ_TRACK_ALIGN_WIDE_CONFLICT_MIN_MARGIN 10
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_DELTA_MS
#define VJ_TRACK_ALIGN_WIDE_SMALL_DELTA_MS 900
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_STABLE_COUNT
#define VJ_TRACK_ALIGN_WIDE_SMALL_STABLE_COUNT 4
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_STABLE_COUNT
#define VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_STABLE_COUNT 3
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_OFFSET_MS
#define VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_OFFSET_MS 120
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_CONF 74
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_MARGIN
#define VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_MARGIN 28
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_AVG_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_AVG_MIN_CONF 65
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_FRAMES
#define VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_FRAMES 12
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_CONF 88
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_MARGIN
#define VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_MARGIN 25
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_AGE_MS
#define VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_AGE_MS 6000L
#endif

#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_TICK_INTERVAL_MS
#define VJ_TRACK_ALIGN_LIVE_SNAP_TICK_INTERVAL_MS 500L
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_MIN_CONF 88
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_MIN_MARGIN
#define VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_MIN_MARGIN 28
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_AVG_CONF
#define VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_AVG_CONF 88
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_MIN_CONF 55
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_MIN_OFFSET_MS
#define VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_MIN_OFFSET_MS 60
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_LIVE_HINT_AUTHORITY_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_LIVE_HINT_AUTHORITY_MIN_CONF 55
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_LIVE_HINT_AUTHORITY_MIN_OFFSET_MS
#define VJ_TRACK_ALIGN_WIDE_LIVE_HINT_AUTHORITY_MIN_OFFSET_MS 120
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_CONF 25
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_WIDE_CONF
#define VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_WIDE_CONF 80
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_MARGIN
#define VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_MARGIN 30
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_OFFSET_MS
#define VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_OFFSET_MS 350
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MAX_DIFF_FR
#define VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MAX_DIFF_FR 12
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_TOLERANCE_MS
#define VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_TOLERANCE_MS 80
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_LIVE_CONFLICT_SUPPRESS_CONF
#define VJ_TRACK_ALIGN_WIDE_LIVE_CONFLICT_SUPPRESS_CONF 70
#endif


#ifndef VJ_TRACK_ALIGN_WIDE_SOURCE_READY_HOP_MS
#define VJ_TRACK_ALIGN_WIDE_SOURCE_READY_HOP_MS 10
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SOURCE_READY_LATENCY_STEPS
#define VJ_TRACK_ALIGN_WIDE_SOURCE_READY_LATENCY_STEPS 48
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SOURCE_RETRY_MS
#define VJ_TRACK_ALIGN_WIDE_SOURCE_RETRY_MS 400L
#endif


#ifndef VJ_TRACK_ALIGN_WIDE_SHORT_PROBE_MS
#define VJ_TRACK_ALIGN_WIDE_SHORT_PROBE_MS 2200
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SHORT_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_SHORT_MIN_CONF 42
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SHORT_MIN_MARGIN
#define VJ_TRACK_ALIGN_WIDE_SHORT_MIN_MARGIN 8
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_SHORT_AGREE_MS
#define VJ_TRACK_ALIGN_WIDE_SHORT_AGREE_MS 850
#endif


#ifndef VJ_TRACK_ALIGN_WIDE_HINT_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_HINT_MIN_CONF 18
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_HINT_MIN_OFFSET_MS
#define VJ_TRACK_ALIGN_WIDE_HINT_MIN_OFFSET_MS 500
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_HINT_RADIUS_MS
#define VJ_TRACK_ALIGN_WIDE_HINT_RADIUS_MS 2400
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_HINT_STEP_MS
#define VJ_TRACK_ALIGN_WIDE_HINT_STEP_MS 250
#endif

#ifndef VJ_TRACK_ALIGN_WIDE_HINT_ACCEPT_MS
#define VJ_TRACK_ALIGN_WIDE_HINT_ACCEPT_MS 1800
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_HINT_SCORE_BONUS
#define VJ_TRACK_ALIGN_WIDE_HINT_SCORE_BONUS 28
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_HINT_FAR_PENALTY
#define VJ_TRACK_ALIGN_WIDE_HINT_FAR_PENALTY 20
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_QUIET_SUMMARY_CONF
#define VJ_TRACK_ALIGN_WIDE_QUIET_SUMMARY_CONF 35
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_QUIET_SUMMARY_MARGIN
#define VJ_TRACK_ALIGN_WIDE_QUIET_SUMMARY_MARGIN 8
#endif


#ifndef VJ_TRACK_ALIGN_WIDE_ACQUIRE_SEARCH_INTERVAL_MS
#define VJ_TRACK_ALIGN_WIDE_ACQUIRE_SEARCH_INTERVAL_MS 650L
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ACQUIRE_STEP_MS
#define VJ_TRACK_ALIGN_WIDE_ACQUIRE_STEP_MS 250
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ACQUIRE_PROBE_MS
#define VJ_TRACK_ALIGN_WIDE_ACQUIRE_PROBE_MS 2200
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ACQUIRE_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_ACQUIRE_MIN_CONF 26
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ACQUIRE_MIN_MARGIN
#define VJ_TRACK_ALIGN_WIDE_ACQUIRE_MIN_MARGIN 4
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ACQUIRE_AVG_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_ACQUIRE_AVG_MIN_CONF 28
#endif
#ifndef VJ_TRACK_ALIGN_WIDE_ACQUIRE_STABLE_COUNT
#define VJ_TRACK_ALIGN_WIDE_ACQUIRE_STABLE_COUNT 2
#endif


#ifndef VJ_TRACK_ALIGN_WIDE_OFFER_MIN_CONF
#define VJ_TRACK_ALIGN_WIDE_OFFER_MIN_CONF 60
#endif

#define PSLOW_A 3
#define PSLOW_B 4


void vj_audio_declick_observe(const void *owner, const uint8_t *buf, int samples,
                              int frame_bytes, int path, int speed, int dir);
int vj_scratch_process(void *ptr,
                       short *output,
                       int max_out_frames,
                       const short *input,
                       int src_frames,
                       double speed);
int vj_audio_render_slow_stream_bend_s16(uint8_t *dst,
                                           int dst_samples,
                                           const uint8_t *src,
                                           int source_base_sample,
                                           int context_samples,
                                           int slice_count,
                                           int start_stretched_sample,
                                           int phase_offset_start,
                                           int phase_offset_end,
                                           int frame_bytes);
int vj_audio_render_slow_stream_turn_s16(uint8_t *dst,
                                           int dst_samples,
                                           const uint8_t *src,
                                           int source_base_sample,
                                           int context_samples,
                                           int slice_count,
                                           int start_stretched_sample,
                                           int phase_offset_start,
                                           int phase_offset_end,
                                           int frame_bytes);
int vj_audio_render_slow_stream_velocity_turn_s16(uint8_t *dst,
                                                  int dst_samples,
                                                  const uint8_t *src,
                                                  int source_base_sample,
                                                  int context_samples,
                                                  int slice_count,
                                                  int start_stretched_sample,
                                                  int phase_offset_start,
                                                  int phase_offset_end,
                                                  int frame_bytes);
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#include <build.h>
#ifndef SAMPLE_FMT_S16
#define SAMPLE_FMT_S16 AV_SAMPLE_FMT_S16
#endif

#define PRIMARY_FRAMES 6
#define FADE_LUT_SIZE 256

#include <libvje/effects/shapewipe.h>

typedef struct {
    uint8_t *Y;
    uint8_t *Cb;
    uint8_t *Cr;
    uint8_t *alpha;
    uint8_t *P0;
    uint8_t *P1;
    int      ssm;
    int      fx_id;
} ycbcr_frame;

typedef struct {
    int fader_active;
    int fade_method;
    int fade_value;
    int fade_entry;
    int fade_alpha;
    int follow_fade;
    int follow_now[2];
    int follow_run;
    int fx_status;
    int enc_active;
    int type;
    int active;
} varcache_t;

extern uint8_t pixel_Y_lo_;


static long vj_frame_rand(long long frame_num, long start, long end, unsigned long long seed) {
    if (start >= end)
        return start;

    unsigned long long x = ((unsigned long long)frame_num ^ seed);

    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);

    const unsigned long long range = (unsigned long long)((end - start) + 1);
    return start + (long)(x % range);
}

static inline int vj_perform_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

#ifndef VJ_SILENCE_AUDIO_RATE
#define VJ_SILENCE_AUDIO_RATE 48000
#endif
#ifndef VJ_SILENCE_AUDIO_CHANS
#define VJ_SILENCE_AUDIO_CHANS 2
#endif
#ifndef VJ_SILENCE_AUDIO_BITS
#define VJ_SILENCE_AUDIO_BITS 16
#endif

static inline editlist *vj_perform_audio_editlist(veejay_t *info)
{
    if(!info)
        return NULL;
    return info->current_edit_list ? info->current_edit_list : info->edit_list;
}

static inline int vj_perform_audio_params_valid(const editlist *el)
{
    return el &&
           el->video_fps > 0.0 &&
           el->audio_rate > 0 &&
           el->audio_chans > 0 &&
           el->audio_bits > 0 &&
           el->audio_bps > 0;
}

static inline int vj_perform_audio_media_valid(const editlist *el)
{
    return el && el->has_audio && vj_perform_audio_params_valid(el);
}

#ifdef HAVE_JACK
static int vj_perform_prepare_silence_audio_params(editlist *el)
{
    if(!el || el->video_fps <= 0.0)
        return 0;

    if(el->audio_rate <= 0)
        el->audio_rate = VJ_SILENCE_AUDIO_RATE;

    if(el->audio_chans <= 0)
        el->audio_chans = VJ_SILENCE_AUDIO_CHANS;
    else if(el->audio_chans > 2)
        el->audio_chans = 2;

    if(el->audio_bits <= 0 || (el->audio_bits % 8) != 0)
        el->audio_bits = VJ_SILENCE_AUDIO_BITS;

    if(el->audio_bps <= 0)
        el->audio_bps = el->audio_chans * (el->audio_bits / 8);

    return vj_perform_audio_params_valid(el);
}
#endif


#define CACHE_TOP 0
#define CACHE 1
#define CACHE_SIZE (SAMPLE_MAX_EFFECTS+CACHE) * 2


#define AC_STATE_IDLE      0
#define AC_STATE_PRODUCING 1
#define AC_STATE_READY     2
#define AC_STATE_CONSUMING 3

typedef struct {
    volatile long long offset;
    long long start;
    long long end;
    long long last_resampled_frame;
    int consumed_samples;
    int  sample_id;
    int  sample_type;
    int  direction;
    int  max_sfd;
    int  cur_sfd;
    int  direction_changed;
    int  audio_last_stretched_samples;
    int  audio_src_offset;
    int  speed;
    int loopmode;
    int audio_total_samples;
    int last_resampled_dir;
    float last_rms_slope;
   	int		prev_n_samples;
    int audio_diag_valid;
    int audio_diag_frame_bytes;
    uint8_t audio_diag_prev_frame[64];
    uint8_t audio_diag_last_frame[64];
    uint8_t audio_turn_history[AUDIO_TURN_HISTORY_BYTES];
    int audio_turn_history_samples;
    int audio_turn_history_frame_bytes;
    int frame_in_history[8];
    int flip_lock;
    int pending_flip_dir;
    int audio_phase_offset_valid;
    int audio_phase_offset_samples;
    int audio_phase_offset_dir;
    int audio_phase_offset_sfd;
    int audio_phase_offset_start_slice;

    int scratch_initialized;
    double scratch_pos;
    double scratch_vel;
    double scratch_target_vel;
    double scratch_last_sync_pos;
    double scratch_last_sync_error;
    double scratch_sync_bias;
    int scratch_sync_hold_blocks;
    int scratch_stable_blocks;
    int scratch_last_dir;
    int scratch_last_sfd;
    int scratch_ramp_left;
    int scratch_last_reset;
} sample_b_t;

#define SLOW_SCRATCH_MAX_CTX_FRAMES 16

typedef struct
{
    int valid;
    int frames;
    long long first_frame;
    long long last_frame;
    int frame_len[SLOW_SCRATCH_MAX_CTX_FRAMES];
    int frame_off[SLOW_SCRATCH_MAX_CTX_FRAMES];
    double exact_start[SLOW_SCRATCH_MAX_CTX_FRAMES];
    double exact_len[SLOW_SCRATCH_MAX_CTX_FRAMES];
} slow_scratch_ctx_map_t;

typedef struct
{
    varcache_t pvar_;
    VJFrame *rgba_frame[2];
    uint8_t *rgba_buffer[2];

    VJFrame *yuva_frame[2];
    VJFrame *yuv420_frame[2];
    ycbcr_frame **frame_buffer;
    ycbcr_frame **primary_buffer;

    uint8_t *audio_buffer[SAMPLE_MAX_EFFECTS];
    uint8_t *audio_silence_;
    uint8_t *lin_audio_buffer_;
    uint8_t *top_audio_buffer;
    size_t top_audio_buffer_capacity;
    uint8_t *external_audio_history;
    size_t external_audio_history_capacity;
    size_t external_audio_history_write;
    size_t external_audio_history_filled;
    int external_audio_history_frame_bytes;
    long long external_audio_history_abs_write;
    double external_audio_read_pos;
    double external_audio_read_vel;
    uint8_t *external_audio_context;
    size_t external_audio_context_capacity;
    int external_audio_transport_active;
    int external_audio_last_speed;
    int external_audio_last_rate_key;
    int external_audio_last_sync_key;
    uint8_t external_audio_prev_frame[64];
    int external_audio_prev_valid;
    int external_audio_prev_frame_bytes;
    long long clip_target_last_frame;
    int clip_target_last_mode;
    int clip_target_last_id;
    long track_align_last_wide_search_ms;
    int track_align_last_wide_search_mode;
    int track_align_last_wide_search_id;
    long track_align_last_wide_snap_ms;
    int track_align_last_wide_snap_delta;
    long track_align_candidate_ms;
    int track_align_candidate_delta;
    int track_align_candidate_conf;
    int track_align_candidate_margin;
    int track_align_candidate_last_conf;
    int track_align_candidate_last_margin;
    int track_align_candidate_sum_conf;
    int track_align_candidate_sum_margin;
    int track_align_candidate_count;
    int track_align_candidate_kind;
    long track_align_wide_bucket_ms[VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS];
    int track_align_wide_bucket_delta[VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS];
    int track_align_wide_bucket_score[VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS];
    int track_align_wide_bucket_obs[VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS];
    int track_align_wide_bucket_min_conf[VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS];
    int track_align_wide_bucket_min_margin[VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS];
    int track_align_wide_bucket_last_conf[VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS];
    int track_align_wide_bucket_last_margin[VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS];
    int track_align_wide_bucket_large_seen;
    int track_align_seen_reacquire_seq;
    long track_align_last_reacquire_ms;
    long track_align_last_servo_offer_ms;
    long track_align_servo_candidate_ms;
    int track_align_servo_sign;
    int track_align_servo_count;
    int track_align_servo_min_conf;
    int track_align_last_sync_mode;
    int track_align_last_sync_target_mode;
    int track_align_last_sync_reset_seq;
    int play_audio_sample_;
    uint8_t *audio_rec_buffer;
    uint8_t *audio_render_buffer;
    size_t audio_render_buffer_capacity;
    uint8_t *audio_downmix_buffer;
    uint8_t *audio_chain_final_buffer;
    uint8_t *down_sample_buffer;
    uint8_t *down_sample_rec_buffer;
    uint8_t *temp_buffer[4];
    VJFrame temp_frame;
    uint8_t *subrender_buffer[4];
    void *rgba2yuv_scaler;
    void *yuv2rgba_scaler;
    void *yuv420_scaler;
    int fx_rgb_format;
    int fx_yuv_format;
    uint8_t *pribuf_area;
    size_t pribuf_len;
    uint8_t *fx_chain_buffer;
    size_t fx_chain_buflen;

    VJFrame *tmp1;
    VJFrame *tmp2;

    int chain_id;

    audio_edge_t *audio_edge;

    sample_b_t sample_b;
    sample_b_t sample_a;

    void *audio_scratcher;
} performer_t;

static void vj_perform_track_align_clear_candidate(performer_t *p);
static void vj_perform_track_align_clear_wide_buckets(performer_t *p);

typedef struct audio_chain_t {
    int sample_id;
    int sample_type;
    long long start;
    long long end;
    int speed;
    int loopmode;
    uint8_t *buffer;
    editlist *el;
    float opacity;
    long long offset;
    int cur_sfd;
    int max_sfd;
    volatile int ready;
    int buffer_size;
    volatile int state;
} audio_chain_entry_t;

typedef struct {
    audio_chain_entry_t entries[SAMPLE_MAX_EFFECTS];
    int count;
    volatile int state;
} audio_chain_buffer_t;

typedef struct
{
    int sample_id;
    int mode;
    int entry;
    ycbcr_frame *frame;
} performer_cache_t;

typedef struct
{
    performer_cache_t cached_frames[CACHE_SIZE];
    audio_chain_buffer_t audio_chain_buffers[VIDEO_QUEUE_LEN];
    int played_sample_ids[SAMPLE_MAX_EFFECTS];
    long long played_sample_positions[SAMPLE_MAX_EFFECTS];
    float *accum[VIDEO_QUEUE_LEN];
    volatile int audio_chain_index;
    int n_cached_frames;
    void *encoder_;
    ycbcr_frame *preview_buffer;
    int preview_max_w;
    int preview_max_h;
    performer_t *A;
    performer_t *B;
    VJFrame feedback_frame;
    uint8_t *feedback_buffer[4];
    VJFrame *offline_frame;
    float *fade_lut;
    float *gain_lut[2];

} performer_global_t;

static const char *intro =
    "A visual instrument for GNU/Linux\n";

#define MLIMIT(var, low, high) \
    do { (var) = ((var) < (low)) ? (low) : (((var) > (high)) ? (high) : (var)); } while(0)


static const char *vj_playback_mode_label(int pm)
{
    switch (pm) {
        case VJ_PLAYBACK_MODE_PLAIN:  return "plain";
        case VJ_PLAYBACK_MODE_SAMPLE: return "sample";
        case VJ_PLAYBACK_MODE_TAG:    return "stream";
        default:                      return "unknown";
    }
}

static  int vj_perform_preprocess_secundary( veejay_t *info, performer_t *p, int id, int mode,int current_ssm,int chain_entry, VJFrame **frames, VJFrameInfo *frameinfo );
static int vj_perform_get_frame_fx(veejay_t *info, int s1, long long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0plane, uint8_t *p1plane);

static void vj_perform_pre_chain(performer_t *p, VJFrame *frame);
static int vj_perform_post_chain_sample(veejay_t *info,performer_t *p, VJFrame *frame,int sample_id);
static int vj_perform_post_chain_tag(veejay_t *info,performer_t*p, VJFrame *frame, int sample_id);
static void vj_perform_plain_fill_buffer(veejay_t * info,performer_t *p,VJFrame *dst,int sample_id, int mode, long frame_num);
static void vj_perform_tag_fill_buffer(veejay_t * info, performer_t *p, VJFrame *dst, int sample_id);
static int vj_perform_apply_secundary_tag(veejay_t * info, performer_t *p, int sample_id,int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0, uint8_t *p1, int subrender);
static int vj_perform_apply_secundary(veejay_t * info, performer_t *p, int this_sample_id,int sample_id,int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0, uint8_t *p1, int subrender);
static void vj_perform_tag_complete_buffers(veejay_t * info, performer_t *p, vjp_kf *effect_info, int *h, VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup, sample_eff_chain **chain, vj_tag *tag);
static void vj_perform_sample_complete_buffers(veejay_t * info, performer_t *p, vjp_kf *effect_info, int *h, VJFrame *f0, VJFrame *f2, int sample_id, int pm, vjp_kf *setup, sample_eff_chain **chain, sample_info *si);
static void vj_perform_apply_first(veejay_t *info, performer_t *p, vjp_kf *todo_info, VJFrame **frames, sample_eff_chain *entry, int e, int c, long long n_frames, void *ptr, int playmode );
static int vj_perform_render_sample_frame(veejay_t *info, performer_t *p, uint8_t *frame[4], int sample, int type);
static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4]);
static int vj_perform_record_commit_single(veejay_t *info);
static void vj_perform_end_transition(veejay_t *info, int mode, int sample);
static int vj_perform_format_changed_rgb(performer_t *p, VJFrame *frame);
static int vj_perform_format_changed_yuv(performer_t *p, VJFrame *frame);

#ifdef HAVE_JACK
extern void vj_jack_set_input_passthrough(int enabled);
static int vj_perform_queue_audio_frame_impl(veejay_t *info,
                                             void *ptr,
                                             uint8_t *a_buf,
                                             int speed,
                                             long long target_frame,
                                             int sample_id,
                                             int *audio_sample_ptr);


int vj_audio_sync_track_align_source_ready(vj_audio_sync_shared_t *s,
                                           int min_source_features);
int vj_audio_sync_track_align_last_snap(vj_audio_sync_shared_t *s,
                                        long *snap_ms,
                                        int *delta_frames);
#endif

static void vj_perform_set_444__(VJFrame *frame)
{
    frame->ssm = 1;
    frame->shift_h = 0;
    frame->shift_v = 0;
    frame->uv_width = frame->width;
    frame->uv_height = frame->height;
    frame->uv_len = frame->len;
    frame->stride[1] = frame->stride[0];
    frame->stride[2] = frame->stride[0];
    frame->format = (frame->range ? PIX_FMT_YUVJ444P : PIX_FMT_YUV444P);
}

static void vj_perform_set_422__( VJFrame *frame)
{
    frame->shift_h = 1;
    frame->shift_v = 0;
    frame->uv_width = frame->width/2;
    frame->uv_height = frame->height;
    frame->uv_len = frame->uv_width * frame->uv_height;
    frame->stride[1] = frame->uv_width;
    frame->stride[2] = frame->stride[1];
    frame->format = (frame->range ? PIX_FMT_YUV422P : PIX_FMT_YUVJ422P);
    frame->ssm = 0;
}

#define vj_perform_set_444(f)  vj_perform_set_444__( f)
#define vj_perform_set_422(f)  vj_perform_set_422__( f)

static void vj_perform_sample_tick_reset(performer_global_t *g) {
    veejay_memset( g->played_sample_ids, 0 , sizeof(g->played_sample_ids));
    veejay_memset( g->played_sample_positions, 0, sizeof(g->played_sample_positions));
    veejay_memset( g->cached_frames, 0, sizeof(g->cached_frames));
    g->n_cached_frames = 0;
}

static long long vj_perform_sample_already_ticked(performer_global_t *g, int target_id, int chain_id) {
    for( int i = 0; i <= chain_id && i < SAMPLE_MAX_EFFECTS; i ++ ) {
        if(g->played_sample_ids[i]==target_id) {
            return g->played_sample_positions[i];
        }
    }
    return -1;
}

static void vj_perform_sample_ticked(performer_global_t *g, int target_id, int chain_id, long long pos) {
    g->played_sample_ids[chain_id] = target_id;
    g->played_sample_positions[chain_id] = pos;
}


static void vj_perform_supersample(video_playback_setup *settings,performer_t *p, VJFrame *one, VJFrame *two, int sm, int chain_entry)
{
    if( one != NULL ) {
        int no_matter = (sm == -1 ? 1: 0);
        if(!no_matter) {
            if( sm == 1 && one->ssm == 0) {
                chroma_supersample( settings->sample_mode,one,one->data );
                vj_perform_set_444(one);
                p->primary_buffer[0]->ssm = 1;
            }
            else if ( sm == 0 && one->ssm == 1) {
                chroma_subsample( settings->sample_mode,one,one->data);
                vj_perform_set_422(one);
                p->primary_buffer[0]->ssm = 1;
            }
        }
    }

    if( two != NULL ) {
        int no_matter = (sm == -1 ? 1: 0);
        if(!no_matter) {
            if( sm == 1 && two->ssm == 0) {
                chroma_supersample( settings->sample_mode,two,two->data );
                vj_perform_set_444(two);
                p->frame_buffer[ chain_entry ]->ssm = 1;
            }
            else if ( sm == 0 && two->ssm == 1) {
                chroma_subsample( settings->sample_mode,two,two->data);
                vj_perform_set_422(two);
                p->frame_buffer[ chain_entry ]->ssm = 0;
            }
        }
		else if( p->frame_buffer[ chain_entry ]->ssm == 1 && two->ssm == 0 ) {
			chroma_supersample( settings->sample_mode, two, two->data );
			vj_perform_set_444(two);
			p->frame_buffer[ chain_entry ]->ssm = 1;
		}
    }
}

static  void    vj_perform_copy3( uint8_t **input, uint8_t **output, int Y_len, int UV_len, int alpha_len )
{
    int     strides[4] = { Y_len, UV_len, UV_len, alpha_len };
    vj_frame_copy(input,output,strides);
}

static inline void vj_copy_frame_holder(VJFrame *src, ycbcr_frame *data, VJFrame *dst) {
    int i;

    if(data != NULL) {
        dst->data[0] = data->Y;
        dst->data[1] = data->Cb;
        dst->data[2] = data->Cr;
        dst->data[3] = data->alpha;
    }

    dst->uv_len = src->uv_len;
    dst->len = src->len;
    dst->uv_width = src->uv_width;
    dst->uv_height = src->uv_height;
    dst->shift_v = src->shift_v;
    dst->shift_h = src->shift_h;
    dst->format = src->format;
    dst->width = src->width;
    dst->height = src->height;
    dst->ssm = src->ssm;

    for( i = 0; i < 4; i ++ ) {
        dst->stride[i] = src->stride[i];
    }

    dst->stand = src->stand;
    dst->fps = src->fps;
    dst->timecode = src->timecode;
}

#ifdef HAVE_JACK

#ifndef VJ_AUDIO_SOURCE_TRANSITION_GUARD_BLOCKS
#define VJ_AUDIO_SOURCE_TRANSITION_GUARD_BLOCKS 4
#endif

#ifndef VJ_AUDIO_SOURCE_TRANSITION_FADE_BLOCKS
#define VJ_AUDIO_SOURCE_TRANSITION_FADE_BLOCKS 3
#endif

#ifndef VJ_AUDIO_RESET_FADE_BLOCKS
#define VJ_AUDIO_RESET_FADE_BLOCKS 3
#endif

static volatile int vj_audio_source_transition_guard_blocks_ = 0;
static volatile int vj_audio_source_transition_guard_seq_ = 0;
static volatile int vj_audio_source_transition_silence_bytes_ = 0;
static volatile int vj_audio_source_transition_fade_bytes_left_ = 0;
static volatile int vj_audio_source_transition_fade_bytes_total_ = 0;
static volatile int vj_audio_source_transition_frame_bytes_ = 0;


void vj_perform_audio_source_transition_guard_ex(int blocks,
                                                 const char *reason,
                                                 int old_source,
                                                 int new_source,
                                                 int sync_mode,
                                                 int mute)
{
    int seq;

    (void)reason;
    (void)old_source;
    (void)new_source;
    (void)sync_mode;
    (void)mute;

    blocks = vj_perform_clampi(blocks, 0, 8);

    seq = atomic_load_int(&vj_audio_source_transition_guard_seq_) + 1;
    if(seq <= 0)
        seq = 1;

    atomic_store_int(&vj_audio_source_transition_guard_seq_, seq);
    atomic_store_int(&vj_audio_source_transition_guard_blocks_, blocks);
    atomic_store_int(&vj_audio_source_transition_silence_bytes_, 0);
    atomic_store_int(&vj_audio_source_transition_fade_bytes_left_, 0);
    atomic_store_int(&vj_audio_source_transition_fade_bytes_total_, 0);
    atomic_store_int(&vj_audio_source_transition_frame_bytes_, 0);
}

void vj_perform_audio_source_transition_guard(int blocks)
{
    vj_perform_audio_source_transition_guard_ex(blocks,
                                                "legacy-call",
                                                -1,
                                                -1,
                                                -1,
                                                -1);
}

static inline int vj_perform_audio_source_transition_guard_active(void)
{
    return atomic_load_int(&vj_audio_source_transition_guard_blocks_) > 0;
}

static inline int vj_perform_audio_source_transition_guard_left(void)
{
    return atomic_load_int(&vj_audio_source_transition_guard_blocks_);
}

int vj_perform_audio_source_transition_guard_pending(void)
{
    return atomic_load_int(&vj_audio_source_transition_guard_blocks_);
}

int vj_perform_audio_source_transition_silence_pending(void)
{
    return atomic_load_int(&vj_audio_source_transition_silence_bytes_);
}

int vj_perform_audio_source_transition_fade_pending(void)
{
    return atomic_load_int(&vj_audio_source_transition_fade_bytes_left_);
}

int vj_perform_audio_source_transition_guard_seq_debug(void)
{
    return atomic_load_int(&vj_audio_source_transition_guard_seq_);
}

static inline int vj_perform_audio_source_transition_guard_seq(void)
{
    return atomic_load_int(&vj_audio_source_transition_guard_seq_);
}

static inline int vj_perform_audio_source_transition_guard_consume_block(int frames,
                                                                         int frame_bytes,
                                                                         int audio_source,
                                                                         int mute,
                                                                         int *seq_out,
                                                                         int *left_before_out,
                                                                         int *left_after_out,
                                                                         int *silence_bytes_out,
                                                                         int *fade_bytes_out)
{
    int left = atomic_load_int(&vj_audio_source_transition_guard_blocks_);
    int seq = atomic_load_int(&vj_audio_source_transition_guard_seq_);
    int after;
    int silence_bytes;
    int fade_bytes = 0;

    if(seq_out)
        *seq_out = seq;
    if(left_before_out)
        *left_before_out = left;
    if(left_after_out)
        *left_after_out = left;
    if(silence_bytes_out)
        *silence_bytes_out = 0;
    if(fade_bytes_out)
        *fade_bytes_out = 0;

    if(left <= 0 || frames <= 0 || frame_bytes <= 0)
        return 0;

    silence_bytes = frames * frame_bytes;
    if(silence_bytes < 0)
        silence_bytes = 0;

    after = left - 1;
    atomic_store_int(&vj_audio_source_transition_guard_blocks_, after);
    atomic_store_int(&vj_audio_source_transition_frame_bytes_, frame_bytes);
    atomic_store_int(&vj_audio_source_transition_silence_bytes_, silence_bytes);

    if(left_after_out)
        *left_after_out = after;
    if(silence_bytes_out)
        *silence_bytes_out = silence_bytes;

    if(after == 0) {
        if(!mute &&
           (audio_source == VJ_RECORD_AUDIO_SOURCE_ORIGINAL ||
            audio_source == VJ_RECORD_AUDIO_SOURCE_AUTO))
        {
            fade_bytes = silence_bytes * VJ_AUDIO_SOURCE_TRANSITION_FADE_BLOCKS;
            atomic_store_int(&vj_audio_source_transition_fade_bytes_left_, fade_bytes);
            atomic_store_int(&vj_audio_source_transition_fade_bytes_total_, fade_bytes);
            if(fade_bytes_out)
                *fade_bytes_out = fade_bytes;
        } else {
            atomic_store_int(&vj_audio_source_transition_fade_bytes_left_, 0);
            atomic_store_int(&vj_audio_source_transition_fade_bytes_total_, 0);
        }

    }

    return 1;
}



static void vj_perform_audio_source_transition_force_fade_blocks(int frames,
                                                                 int frame_bytes,
                                                                 int blocks,
                                                                 const char *reason,
                                                                 int edge_type,
                                                                 long long target_frame,
                                                                 const char *path)
{
    int fade_bytes;
    
    if(frames <= 0 || frame_bytes <= 0)
        return;

    blocks = vj_perform_clampi(blocks, 1, 8);

    fade_bytes = frames * frame_bytes * blocks;
    if(fade_bytes <= 0)
        return;

    atomic_store_int(&vj_audio_source_transition_frame_bytes_, frame_bytes);
    atomic_store_int(&vj_audio_source_transition_fade_bytes_left_, fade_bytes);
    atomic_store_int(&vj_audio_source_transition_fade_bytes_total_, fade_bytes);

}

static inline int vj_perform_audio_source_transition_take_silence_bytes(int want_bytes)
{
    int left = atomic_load_int(&vj_audio_source_transition_silence_bytes_);
    int take;

    if(left <= 0 || want_bytes <= 0)
        return 0;

    take = (left < want_bytes) ? left : want_bytes;
    atomic_store_int(&vj_audio_source_transition_silence_bytes_, left - take);
    return take;
}

static void vj_perform_audio_source_transition_apply_fade(uint8_t *source, int len)
{
    int frame_bytes;
    int left;
    int total;
    int fade_bytes;
    int start_done;
    int sample_count;

    if(source == NULL || len <= 0)
        return;

    left = atomic_load_int(&vj_audio_source_transition_fade_bytes_left_);
    total = atomic_load_int(&vj_audio_source_transition_fade_bytes_total_);
    frame_bytes = atomic_load_int(&vj_audio_source_transition_frame_bytes_);

    if(left <= 0 || total <= 0 || frame_bytes <= 0)
        return;

    fade_bytes = (left < len) ? left : len;
    if(fade_bytes <= 0)
        return;

    if(frame_bytes > 0)
        fade_bytes -= (fade_bytes % frame_bytes);
    if(fade_bytes <= 0)
        return;

    start_done = total - left;
    sample_count = fade_bytes / 2;


    if((frame_bytes & 1) == 0 && sample_count > 0) {
        int frames = fade_bytes / frame_bytes;
        int start_frame = start_done / frame_bytes;
        int total_frames = total / frame_bytes;
        int samples_per_frame = frame_bytes / 2;

            total_frames = (total_frames < 1) ? 1 : total_frames;

        for(int f = 0; f < frames; f++) {
            int num = start_frame + f + 1;
            num = (num > total_frames) ? total_frames : num;

            for(int s = 0; s < samples_per_frame; s++) {
                int idx = (f * samples_per_frame) + s;
                int off = idx * 2;
                int16_t v = (int16_t)((uint16_t)source[off] |
                                      ((uint16_t)source[off + 1] << 8));
                int scaled = ((int)v * num) / total_frames;
                scaled = vj_perform_clampi(scaled, -32768, 32767);
                source[off] = (uint8_t)(scaled & 0xff);
                source[off + 1] = (uint8_t)((scaled >> 8) & 0xff);
            }
        }
    }

    atomic_store_int(&vj_audio_source_transition_fade_bytes_left_, left - fade_bytes);

}


int vj_perform_play_audio( video_playback_setup *settings, uint8_t *source, int len, uint8_t *silence )
{
    int audio_muted = atomic_load_int(&settings->audio_mute);
    int audio_source = atomic_load_int(&settings->record_audio_source);
    int guard_left_before = atomic_load_int(&vj_audio_source_transition_guard_blocks_);
    int silence_take = 0;
    int output_reason = 0;


    if(audio_muted)
        output_reason = 1;
    else if(audio_source == VJ_RECORD_AUDIO_SOURCE_SILENCE)
        output_reason = 2;
    else {
        silence_take = vj_perform_audio_source_transition_take_silence_bytes(len);
        if(silence_take > 0)
            output_reason = 3;
        else if(guard_left_before > 0)
            output_reason = 4;
        else
            output_reason = 0;
    }

    if(output_reason == 1 || output_reason == 2 || output_reason == 4)
        return vj_jack_play( silence, len );

    if(output_reason == 3) {
        int frame_bytes = atomic_load_int(&vj_audio_source_transition_frame_bytes_);
        int total_frames = 0;

        if(frame_bytes <= 0)
            return vj_jack_play( silence, len );

        silence_take = (silence_take > len) ? len : silence_take;
        silence_take -= (silence_take % frame_bytes);
        if(silence_take <= 0)
            return vj_jack_play( silence, len );

        int written = vj_jack_play( silence, silence_take );
        if(written <= 0)
            return written;

        total_frames += written;


        int written_bytes = written * frame_bytes;
        if(written_bytes < silence_take)
            return total_frames;

        if(silence_take < len) {
            uint8_t *tail = source ? source + silence_take : source;
            int tail_len = len - silence_take;
            vj_perform_audio_source_transition_apply_fade(tail, tail_len);
            written = vj_jack_play(tail, tail_len);
            if(written > 0)
                total_frames += written;
        }

        return total_frames;
    }

    vj_perform_audio_source_transition_apply_fade(source, len);
    return vj_jack_play( source, len );
}

#endif

static ycbcr_frame *vj_perform_cache_get_frame(veejay_t *info,int id, int mode)
{
    int c;
    performer_global_t *g = (performer_global_t*) info->performer;

    for(c=0; c < g->n_cached_frames; c++)
    {
        if(g->cached_frames[c].sample_id == id && g->cached_frames[c].mode == mode)
        {
            if( info->settings->feedback && info->uc->sample_id == id &&
                        info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
                return NULL;
            }
            return g->cached_frames[c].frame;
        }
    }

    return NULL;
}

static void vj_perform_cache_put_frame(performer_global_t *g, int id, int mode, ycbcr_frame *frame) {

    if( mode != VJ_PLAYBACK_MODE_SAMPLE && mode != VJ_PLAYBACK_MODE_TAG )
        return;

    for(int c=0; c < CACHE_SIZE; c++)
    {
        if( g->cached_frames[c].sample_id == 0 || ( g->cached_frames[c].sample_id == id && g->cached_frames[c].mode == mode) ) {
            g->cached_frames[c].sample_id = id;
            g->cached_frames[c].mode = mode;
            g->cached_frames[c].frame = frame;
            g->n_cached_frames ++;
            return;
        }
    }
}

static inline int vj_perform_cache_use_frame(ycbcr_frame *cached_frame, VJFrame *dst) {

    const int cached_in_ssm = cached_frame->ssm;
    const int len = (cached_in_ssm ? dst->len : dst->uv_len );

    veejay_memcpy( dst->data[0], cached_frame->Y, dst->len);
    veejay_memcpy( dst->data[1], cached_frame->Cb, len);
    veejay_memcpy( dst->data[2], cached_frame->Cr, len );


    return cached_frame->ssm;
}


static void vj_perform_initiate_edge_change_ex(
    veejay_t *info,
    int edge_type,
    int prev_dir,
    int cur_dir
)
{
    if (edge_type == AUDIO_EDGE_NONE)
        return;

    if (info == NULL || info->audio == NO_AUDIO || info->performer == NULL)
        return;

    performer_global_t *g = (performer_global_t*)info->performer;
    if (g->A == NULL || g->B == NULL ||
        g->A->audio_edge == NULL || g->B->audio_edge == NULL)
        return;

    const int real_direction_change =
        (prev_dir != 0 && cur_dir != 0 && prev_dir != cur_dir);

    if (edge_type == AUDIO_EDGE_DIRECTION && !real_direction_change)
        edge_type = AUDIO_EDGE_JUMP;

    if(info->settings) {
        const int active_source = atomic_load_int(&info->settings->record_audio_source);
        if(active_source == VJ_RECORD_AUDIO_SOURCE_BEAT_JACK ||
           active_source == VJ_RECORD_AUDIO_SOURCE_SILENCE)
        {
            atomic_store_int(&g->A->audio_edge->pending_edge, AUDIO_EDGE_NONE);
            atomic_store_int(&g->B->audio_edge->pending_edge, AUDIO_EDGE_NONE);
            return;
        }
    }

    atomic_store_int(&g->A->audio_edge->pending_edge, edge_type);
    atomic_store_int(&g->B->audio_edge->pending_edge, edge_type);
}

void vj_perform_initiate_edge_change(
    veejay_t *info,
    int edge_type,
    int prev_dir,
    int cur_dir
)
{
    vj_perform_initiate_edge_change_ex(info, edge_type, prev_dir, cur_dir);
}

static inline int vj_audio_clampi(int v, int lo, int hi)
{
    return vj_perform_clampi(v, lo, hi);
}

#ifdef HAVE_JACK
typedef struct
{
    sample_eff_chain **chain[2];
    int chain_count;
} vj_perform_ab_render_ctx_t;

static int vj_perform_ab_ctx_add_chain(vj_perform_ab_render_ctx_t *ctx, sample_eff_chain **chain)
{
    if(!ctx || !chain || ctx->chain_count >= 2)
        return 0;

    for(int i = 0; i < ctx->chain_count; i++)
    {
        if(ctx->chain[i] == chain)
            return 0;
    }

    ctx->chain[ctx->chain_count++] = chain;
    return 1;
}

static sample_eff_chain *vj_perform_ab_ctx_entry(void *vctx, int chain_pos)
{
    vj_perform_ab_render_ctx_t *ctx = (vj_perform_ab_render_ctx_t *)vctx;
    int lane;
    int entry;

    if(!ctx || chain_pos < 0)
        return NULL;

    lane = chain_pos / SAMPLE_MAX_EFFECTS;
    entry = chain_pos % SAMPLE_MAX_EFFECTS;

    if(lane < 0 || lane >= ctx->chain_count || entry < 0 || entry >= SAMPLE_MAX_EFFECTS)
        return NULL;

    if(!ctx->chain[lane])
        return NULL;

    return ctx->chain[lane][entry];
}

static int vj_perform_ab_chain_get_fx_id(void *ctx, int chain_pos)
{
    sample_eff_chain *entry = vj_perform_ab_ctx_entry(ctx, chain_pos);

    if(!entry)
        return 0;


    if(entry->e_flag == 0)
        return 0;


    if(entry->beat_flag == 0)
        return 0;

    return entry->effect_id;
}

static int vj_perform_ab_chain_get_arg(void *ctx, int chain_pos, int param_nr)
{
    sample_eff_chain *entry = vj_perform_ab_ctx_entry(ctx, chain_pos);

    if(!entry)
        return 0;

    if(param_nr < 0 || param_nr >= SAMPLE_MAX_PARAMETERS)
        return 0;

    return entry->arg[param_nr];
}

static int vj_perform_ab_chain_set_arg(void *ctx, int chain_pos, int param_nr, int value)
{
    sample_eff_chain *entry = vj_perform_ab_ctx_entry(ctx, chain_pos);

    if(!entry)
        return 0;

    if(param_nr < 0 || param_nr >= SAMPLE_MAX_PARAMETERS)
        return 0;

    entry->arg[param_nr] = value;
    return 1;
}

static int vj_perform_ab_ctx_active_fx_count(vj_perform_ab_render_ctx_t *ctx)
{
    int active = 0;

    if(!ctx)
        return 0;

    for(int lane = 0; lane < ctx->chain_count; lane++)
    {
        sample_eff_chain **chain = ctx->chain[lane];

        if(!chain)
            continue;

        for(int entry_nr = 0; entry_nr < SAMPLE_MAX_EFFECTS; entry_nr++)
        {
            sample_eff_chain *entry = chain[entry_nr];

            if(!entry)
                continue;

            if(entry->e_flag == 0 || entry->effect_id <= 0)
                continue;

            active++;
        }
    }

    return active;
}

static void vj_perform_ab_ctx_debug_dump(
    veejay_t *veejay_instance_,
    const char *tag,
    int sample_id,
    int playmode,
    int renderer_id,
    vj_perform_ab_render_ctx_t *ctx,
    int local_enabled,
    int global_enabled,
    int active_fx,
    int changed
)
{
    (void)veejay_instance_;
    (void)tag;
    (void)sample_id;
    (void)playmode;
    (void)renderer_id;
    (void)ctx;
    (void)local_enabled;
    (void)global_enabled;
    (void)active_fx;
    (void)changed;
}
static int vj_perform_audio_beat_playmode_has_fx_chain(int playmode)
{
    return playmode == VJ_PLAYBACK_MODE_SAMPLE ||
           playmode == VJ_PLAYBACK_MODE_TAG;
}
static int vj_perform_audio_beat_global_chain_is_rendered(
    veejay_t *info,
    int sample_id,
    int playmode
)
{
    (void)sample_id;

    if(!vj_perform_audio_beat_playmode_has_fx_chain(playmode))
        return 0;


    if(!info || !info->global_chain || !info->global_chain->enabled)
        return 0;

    return 1;
}

static int vj_perform_ab_ctx_beat_fx_count(vj_perform_ab_render_ctx_t *ctx)
{
    int active = 0;

    if(!ctx)
        return 0;

    for(int lane = 0; lane < ctx->chain_count; lane++)
    {
        sample_eff_chain **chain = ctx->chain[lane];

        if(!chain)
            continue;

        for(int entry_nr = 0; entry_nr < SAMPLE_MAX_EFFECTS; entry_nr++)
        {
            sample_eff_chain *entry = chain[entry_nr];

            if(!entry)
                continue;

            if(entry->e_flag == 0 || entry->effect_id <= 0)
                continue;

            if(entry->beat_flag == 0)
                continue;

            active++;
        }
    }

    return active;
}

static int vj_perform_audio_beat_apply_render_chains(
    veejay_t *info,
    int sample_id,
    int playmode,
    int renderer_id,
    sample_eff_chain **local_chain,
    int local_enabled
)
{
    video_playback_setup *settings;
    vj_perform_ab_render_ctx_t ctx;
    sample_eff_chain **global_chain = NULL;
    int global_enabled = 0;
    int active_fx = 0;
    int beat_fx = 0;
    int changed = 0;
    int action = VJ_AUDIO_BEAT_ACTION_NONE;

    if(!info || !info->settings)
        return 0;

    settings = info->settings;

    if(!vj_audio_beat_is_enabled(&settings->audio_beat))
        return 0;

    if(!vj_audio_beat_is_running(&settings->audio_beat))
        return 0;

    action = atomic_load_int(&settings->audio_beat.action_mode);


    if(action != VJ_AUDIO_BEAT_ACTION_AUTO_FX &&
       action != VJ_AUDIO_BEAT_ACTION_FREEZE_AND_AUTO_FX)
        return 0;

    if(!vj_perform_audio_beat_playmode_has_fx_chain(playmode))
        return 0;

    veejay_memset(&ctx, 0, sizeof(ctx));

    if(vj_perform_audio_beat_global_chain_is_rendered(info, sample_id, playmode))
    {
        global_chain = info->global_chain->fx_chain;
        global_enabled = info->global_chain->enabled;
    }

    if(global_enabled == 2)
        vj_perform_ab_ctx_add_chain(&ctx, global_chain);

    if(local_enabled && local_chain)
        vj_perform_ab_ctx_add_chain(&ctx, local_chain);

    if(global_enabled == 1)
        vj_perform_ab_ctx_add_chain(&ctx, global_chain);

    if(ctx.chain_count <= 0)
    {
        vj_perform_ab_ctx_debug_dump(info, "skip-no-chain",
                                     sample_id,
                                     playmode,
                                     renderer_id,
                                     &ctx,
                                     (local_enabled && local_chain) ? 1 : 0,
                                     global_enabled,
                                     -1,
                                     0);
        return 0;
    }

    active_fx = vj_perform_ab_ctx_active_fx_count(&ctx);

    if(active_fx <= 0)
    {
        vj_perform_ab_ctx_debug_dump(info, "skip-no-active-fx",
                                     sample_id,
                                     playmode,
                                     renderer_id,
                                     &ctx,
                                     (local_enabled && local_chain) ? 1 : 0,
                                     global_enabled,
                                     active_fx,
                                     0);
        return 0;
    }

    beat_fx = vj_perform_ab_ctx_beat_fx_count(&ctx);

    if(beat_fx <= 0)
    {
        vj_perform_ab_ctx_debug_dump(info, "skip-no-beat-fx",
                                     sample_id,
                                     playmode,
                                     renderer_id,
                                     &ctx,
                                     (local_enabled && local_chain) ? 1 : 0,
                                     global_enabled,
                                     active_fx,
                                     0);
        return 0;
    }

#ifdef _OPENMP
#pragma omp critical(vj_perform_audio_beat_auto_apply)
#endif
    {
        changed = vj_audio_beat_auto_apply_chain(
            &settings->audio_beat,
            &ctx,
            ctx.chain_count * SAMPLE_MAX_EFFECTS,
            vj_perform_ab_chain_get_fx_id,
            vj_perform_ab_chain_get_arg,
            vj_perform_ab_chain_set_arg
        );
    }

    vj_perform_ab_ctx_debug_dump(info, "state",
                                 sample_id,
                                 playmode,
                                 renderer_id,
                                 &ctx,
                                 (local_enabled && local_chain) ? 1 : 0,
                                 global_enabled,
                                 active_fx,
                                 changed);

    return changed;
}

#endif

static void vj_perform_clear_audio_edges(
    veejay_t *info,
    audio_edge_t *edge,
    int cur_dir
) {
    if (info == NULL || info->performer == NULL) {
        vj_audio_clear_edge(edge, cur_dir);
        return;
    }

    performer_global_t *g = (performer_global_t*)info->performer;
    audio_edge_t *edge_a = (g != NULL && g->A != NULL) ? g->A->audio_edge : NULL;
    audio_edge_t *edge_b = (g != NULL && g->B != NULL) ? g->B->audio_edge : NULL;

    if (edge_a != NULL)
        vj_audio_clear_edge(edge_a, cur_dir);
    if (edge_b != NULL && edge_b != edge_a)
        vj_audio_clear_edge(edge_b, cur_dir);
    if (edge == NULL && edge_a == NULL && edge_b == NULL)
        vj_audio_clear_edge(edge, cur_dir);
}

static inline int vj_seq_type_to_playback_mode(int type)
{
    return (type == 0 || type == VJ_PLAYBACK_MODE_SAMPLE)
        ? VJ_PLAYBACK_MODE_SAMPLE
        : VJ_PLAYBACK_MODE_TAG;
}

int vj_perform_get_next_sequence_id(veejay_t *info, int *type, int current, int *new_current)
{
    int cur = current;
    int cycle = 0;

    cur = (cur >= MAX_SEQUENCES) ? 0 : cur;

    while( info->seq->samples[ cur ].sample_id == 0 )
    {
        cur ++;
        if( cur >= MAX_SEQUENCES && !cycle) {
            cur = 0;
            cycle = 1;
        }
        else if (cur >= MAX_SEQUENCES && cycle) {
            veejay_msg(VEEJAY_MSG_ERROR, "No valid sequence to play. Sequence Play disabled");
            info->seq->active = 0;
            return 0;
        }
    }

    *type = info->seq->samples[cur].type;
    *new_current = cur;

    return info->seq->samples[cur].sample_id;
}

void vj_perform_setup_transition(veejay_t *info,
                                 int next_sample_id,
                                 int next_type,
                                 int sample_id,
                                 int current_type,
                                 int next_seq_idx)
{
    video_playback_setup *settings = info->settings;

    current_type = vj_seq_type_to_playback_mode(current_type);
    next_type    = vj_seq_type_to_playback_mode(next_type);

    if (next_sample_id <= 0) {
        vj_perform_reset_transition(info);
        return;
    }

    int transition_active = (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_transition_active(sample_id) : vj_tag_get_transition_active(sample_id);

    if (!transition_active) {
        vj_perform_reset_transition(info);
        return;
    }

    int transition_length = (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_transition_length(sample_id) : vj_tag_get_transition_length(sample_id);

    if (transition_length <= 0) {
        vj_perform_reset_transition(info);
        return;
    }

    int transition_shape = (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_transition_shape(sample_id) : vj_tag_get_transition_shape(sample_id);

    if (transition_shape == -1) {
        transition_shape = (int)(((double)shapewipe_get_num_shapes(settings->transition.ptr)) * rand() / RAND_MAX);
    }

    int speed =
        (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_speed(sample_id) : 1;

    int start =
        (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_startFrame(sample_id) : 0;

    int end =
        (current_type == VJ_PLAYBACK_MODE_SAMPLE) ? sample_get_endFrame(sample_id) : vj_tag_get_n_frames(sample_id);

    if (end <= start) {
        vj_perform_reset_transition(info);
        return;
    }

    int span = end - start;
    if (transition_length > span)
        transition_length = span;

    long long start_tx = start;
    long long end_tx   = end;

    if (speed < 0) {
        start_tx = start + transition_length;
        end_tx   = start;
    }
    else {
        start_tx = end - transition_length;
        end_tx   = end;
    }

    settings->transition.shape     = transition_shape;
    settings->transition.next_type = next_type;
    settings->transition.next_id   = next_sample_id;
    settings->transition.ready     = 0;
    settings->transition.seq_index = next_seq_idx;

    atomic_store_long_long(&settings->transition.start, start_tx);
    atomic_store_long_long(&settings->transition.end, end_tx);
    atomic_store_int(&settings->transition.active, transition_active);
}

int vj_perform_next_sequence( veejay_t *info, int *type, int *next_slot )
{
    int new_current = -1;
    int current_type = -1;
    int sample_id = vj_perform_get_next_sequence_id(info,&current_type, info->seq->current, &new_current);

    int next_current = 0;
    int next_sample_id = vj_perform_get_next_sequence_id(info,type, new_current + 1, &next_current );

    *next_slot = next_current;

    if( info->bezerk && current_type == 0 ) {
 	    sample_set_resume_override( sample_id, -1 );
    }

    return next_sample_id;
}

int vj_perform_try_sequence(veejay_t *info)
{
    if (!info->seq->active)
        return 0;

    video_playback_setup *settings = info->settings;

    if (!atomic_load_int(&settings->sequence_boundary))
        return 0;

    atomic_store_int(&settings->sequence_boundary, 0);

    int id = info->uc->sample_id;
    int loops = -1;

    if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        loops = sample_get_loops(id);
    else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
        loops = vj_tag_get_loops(id);
    else
        return 0;

    if (loops != 0) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[SEQ] boundary held mode=%s id=%d loops=%d slot=%d transport=%lld",
                   vj_playback_mode_label(info->uc->playback_mode),
                   id,
                   loops,
                   info->seq->current,
                   atomic_load_long_long(&settings->current_frame_num));
        return 0;
    }

    int type = 0;
    int next_slot = 0;
    int next_id = vj_perform_next_sequence(info, &type, &next_slot);

    if (next_id <= 0) {
        veejay_msg(VEEJAY_MSG_DEBUG,
                   "[SEQ] boundary reached but no next slot from slot=%d id=%d",
                   info->seq->current,
                   id);
        return 0;
    }

    int playback_mode = vj_seq_type_to_playback_mode(type);

    const int global_transition_on =
        atomic_load_int(&settings->transition.global_state);

    const int armed_transition_active =
        atomic_load_int(&settings->transition.active);

    veejay_msg(VEEJAY_MSG_DEBUG,
               "[SEQ] advance slot=%d -> %d %s:%d -> %s:%d transition_global=%d transition_active=%d transport=%lld",
               info->seq->current,
               next_slot,
               vj_playback_mode_label(info->uc->playback_mode),
               id,
               vj_playback_mode_label(playback_mode),
               next_id,
               global_transition_on,
               armed_transition_active,
               atomic_load_long_long(&settings->current_frame_num));

    if (!global_transition_on || !armed_transition_active) {
        info->seq->current = next_slot;
        veejay_change_playback_mode(info, playback_mode, next_id);
    }

    return 0;
}


static int vj_perform_record_buffer_init(veejay_t *info)
{
    performer_global_t *g = (performer_global_t*) info->performer;

    g->offline_frame = (VJFrame*) vj_calloc(sizeof(VJFrame));
    if(!g->offline_frame) return 0;

    veejay_memcpy( g->offline_frame, info->effect_frame1, sizeof(VJFrame) );

    uint8_t *region = (uint8_t*) vj_malloc(sizeof(uint8_t) * g->offline_frame->len * 3);
    if(!region) return 0;

    g->offline_frame->data[0] = region;
    g->offline_frame->data[1] = region + ( g->offline_frame->len );
    g->offline_frame->data[2] = region + ( g->offline_frame->len + g->offline_frame->uv_len );

    veejay_memset( g->offline_frame->data[0] , pixel_Y_lo_, g->offline_frame->len );
    veejay_memset( g->offline_frame->data[1], 128, g->offline_frame->uv_len );
    veejay_memset( g->offline_frame->data[2], 128, g->offline_frame->uv_len );

    g->offline_frame->data[3] = NULL;

    return 1;
}

static void vj_perform_record_buffer_free(performer_global_t *g)
{
    if(g->offline_frame) {
        if(g->offline_frame->data[0]) {
            free(g->offline_frame->data[0]);
            g->offline_frame->data[0] = NULL;
        }
        free(g->offline_frame);
        g->offline_frame = NULL;
    }

}

int vj_init_audio_fader_luts(veejay_t *info) {
    performer_global_t *global = (performer_global_t*) info->performer;
    editlist *el = vj_perform_audio_editlist(info);

    if(!global)
        return 0;

    if(!vj_perform_audio_media_valid(el)) {
        if(el && !el->has_audio)
            veejay_msg(VEEJAY_MSG_INFO, "[AUDIO] No embedded audio stream; audio mixer runs silent");
        else
            veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Invalid embedded audio parameters; audio mixer disabled");
        return 1;
    }

    const int audio_rate = el->audio_rate;
    const double video_fps = el->video_fps;
    const int frame_bytes = el->audio_bps;
    const int max_samples_per_frame = (int)ceil((double)audio_rate / video_fps);

    if(max_samples_per_frame < 2 || frame_bytes <= 0) {
        veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Invalid mixer geometry; audio mixer disabled");
        return 1;
    }

    size_t buffer_size = (size_t)max_samples_per_frame * (size_t)frame_bytes;

    global->fade_lut = (float*) vj_calloc(sizeof(float) * buffer_size);
    if(!global->fade_lut)
        return 0;

    global->gain_lut[0] = (float*) vj_calloc(sizeof(float) * buffer_size);
    if(!global->gain_lut[0])
        return 0;

    global->gain_lut[1] = (float*) vj_calloc(sizeof(float) * buffer_size);
    if(!global->gain_lut[1])
        return 0;

    for (int i = 0; i < max_samples_per_frame; i++) {
        float t = (float)i / (float)(max_samples_per_frame - 1);

        global->gain_lut[0][i] = cosf(t * (M_PI / 2.0f));
        global->gain_lut[1][i] = sinf(t * (M_PI / 2.0f));
        global->fade_lut[i] = t;
    }

    global->gain_lut[0][max_samples_per_frame - 1] = 0.0f;
    global->gain_lut[0][max_samples_per_frame - 2] = 0.0f;

    global->gain_lut[1][max_samples_per_frame - 1] = 1.0f;
    global->gain_lut[1][max_samples_per_frame - 2] = 1.0f;

    int mid = max_samples_per_frame / 2;
    int last = max_samples_per_frame - 1;

    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] Init Fader LUT | rate=%d Hz fps=%.3f samples/frame=%d bytes=%zu", audio_rate, video_fps, max_samples_per_frame, buffer_size);
    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] Fade curve: equal-power (cos/sin)");
    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] LUT[0]   : out=%.3f in=%.3f (start)", global->gain_lut[0][0], global->gain_lut[1][0]);
    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] LUT[%d]  : out=%.3f in=%.3f (mid)", mid, global->gain_lut[0][mid], global->gain_lut[1][mid]);
    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] LUT[%d]  : out=%.3f in=%.3f (end)", last, global->gain_lut[0][last], global->gain_lut[1][last]);

    float energy_mid = global->gain_lut[0][mid] * global->gain_lut[0][mid] +
                    global->gain_lut[1][mid] * global->gain_lut[1][mid];

    veejay_msg(VEEJAY_MSG_DEBUG, "[AudioMix] Mid energy check: %.3f (should be ~1.000 for equal-power)", energy_mid);
    return 1;
}


int vj_perform_allocate(veejay_t *info)
{
    performer_global_t *global = (performer_global_t*) vj_calloc( sizeof(performer_global_t ));

    if(!global) {
        return 1;
    }

    info->performer = global;

    if(info->audio != NO_AUDIO) {
        if( vj_init_audio_fader_luts(info) == 0 ) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to initialize audio mixer");
            return 1;
        }
    }

    global->preview_buffer = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
    if(!global->preview_buffer) {
        return 1;
    }
    global->preview_max_w = info->video_output_width * 2;
    global->preview_max_h = info->video_output_height * 2;
    global->preview_buffer->Y = (uint8_t*) vj_calloc( global->preview_max_w * global->preview_max_h * 2 );
    if(!global->preview_buffer->Y) {
        return 1;
    }

    const int w = info->video_output_width;
    const int h = info->video_output_height;
    const long frame_len = ((w*h)+w+w);
    size_t buf_len = frame_len * 4 * sizeof(uint8_t);

    sample_record_init(frame_len);
    vj_tag_record_init(w,h);


    global->feedback_buffer[0] = (uint8_t*) vj_malloc( buf_len );
    global->feedback_buffer[1] = global->feedback_buffer[0] + frame_len;
    global->feedback_buffer[2] = global->feedback_buffer[1] + frame_len;
    global->feedback_buffer[3] = global->feedback_buffer[2] + frame_len;

    veejay_memset( global->feedback_buffer[0], pixel_Y_lo_,frame_len);
    veejay_memset( global->feedback_buffer[1],128,frame_len);
    veejay_memset( global->feedback_buffer[2],128,frame_len);
    veejay_memset( global->feedback_buffer[3],0,frame_len);

    veejay_memcpy(&(global->feedback_frame), info->effect_frame1, sizeof(VJFrame));

    global->feedback_frame.data[0] = global->feedback_buffer[0];
    global->feedback_frame.data[1] = global->feedback_buffer[1];
    global->feedback_frame.data[2] = global->feedback_buffer[2];
    global->feedback_frame.data[3] = global->feedback_buffer[3];

    info->performer = (void*) global;
    return 0;
}

void vj_perform_destroy(veejay_t *info) {
    performer_global_t *global = (performer_global_t*) info->performer;
    free(global->preview_buffer->Y);
    free(global->preview_buffer);
    free(global);
}

static performer_t *vj_perform_init_performer(veejay_t *info, int chain_id)
{
    const int w = info->video_output_width;
    const int h = info->video_output_height;

    unsigned int c;
    long total_used = 0;

    performer_t *p = (performer_t*) vj_calloc(sizeof(performer_t));
    if(!p) {
        return NULL;
    }

    p->tmp1 = (VJFrame*) vj_calloc(sizeof(VJFrame));
    p->tmp2 = (VJFrame*) vj_calloc(sizeof(VJFrame));


    p->frame_buffer = (ycbcr_frame **) vj_calloc(sizeof(ycbcr_frame*) * SAMPLE_MAX_EFFECTS);
    if(!p->frame_buffer) {
        return NULL;
    }

    p->primary_buffer = (ycbcr_frame **) vj_calloc(sizeof(ycbcr_frame **) * PRIMARY_FRAMES);
    if(!p->primary_buffer) {
        return NULL;
    }

    size_t plane_len = (w*h);
    size_t frame_len = 4 * plane_len;

    size_t performer_frame_size = frame_len * 4;

    p->pribuf_len = PRIMARY_FRAMES * performer_frame_size;
    p->pribuf_area = vj_hmalloc( p->pribuf_len, "in primary buffers" );
    if( !p->pribuf_area ) {
        return NULL;
    }

    for( c = 0; c < PRIMARY_FRAMES; c ++ )
    {
        p->primary_buffer[c] = (ycbcr_frame*) vj_calloc(sizeof(ycbcr_frame));
        p->primary_buffer[c]->Y = p->pribuf_area + (performer_frame_size * c);
        p->primary_buffer[c]->Cb = p->primary_buffer[c]->Y  + frame_len;
        p->primary_buffer[c]->Cr = p->primary_buffer[c]->Cb + frame_len;
        p->primary_buffer[c]->alpha = p->primary_buffer[c]->Cr + frame_len;

        veejay_memset( p->primary_buffer[c]->Y, pixel_Y_lo_,frame_len);
        veejay_memset( p->primary_buffer[c]->Cb,128,frame_len);
        veejay_memset( p->primary_buffer[c]->Cr,128,frame_len);
        veejay_memset( p->primary_buffer[c]->alpha,0,frame_len);
        total_used += performer_frame_size;
    }

    p->temp_buffer[0] = (uint8_t*) vj_malloc( frame_len );
    if(!p->temp_buffer[0]) {
        return NULL;
    }
    p->temp_buffer[1] = p->temp_buffer[0] + plane_len;
    p->temp_buffer[2] = p->temp_buffer[1] + plane_len;
    p->temp_buffer[3] = p->temp_buffer[2] + plane_len;

    veejay_memset(p->temp_buffer[2], 128, plane_len);
    veejay_memset(p->temp_buffer[1], 128, plane_len);
    veejay_memset(p->temp_buffer[3], 0, plane_len);
    veejay_memset(p->temp_buffer[0], 0, plane_len);

    p->rgba_buffer[0] = (uint8_t*) vj_malloc( frame_len * 2 );
    if(!p->rgba_buffer[0] ) {
        return NULL;
    }

    p->rgba_buffer[1] = p->rgba_buffer[0] + frame_len;

    veejay_memset( p->rgba_buffer[0], 0, frame_len * 2 );

    p->subrender_buffer[0] = (uint8_t*) vj_malloc( frame_len );
    if(!p->subrender_buffer[0]) {
        return NULL;
    }
    p->subrender_buffer[1] = p->subrender_buffer[0] + plane_len;
    p->subrender_buffer[2] = p->subrender_buffer[1] + plane_len;
    p->subrender_buffer[3] = p->subrender_buffer[2] + plane_len;

    veejay_memset( p->subrender_buffer[0], pixel_Y_lo_,plane_len);
    veejay_memset( p->subrender_buffer[1],128,plane_len);
    veejay_memset( p->subrender_buffer[2],128,plane_len);
    veejay_memset( p->subrender_buffer[3],0,plane_len);

    total_used += frame_len;
    total_used += frame_len;
    total_used += (frame_len * 2);

    size_t fx_chain_size = (frame_len + frame_len + frame_len) * SAMPLE_MAX_EFFECTS;
    p->fx_chain_buffer = vj_hmalloc( fx_chain_size, "in fx chain buffers" );
    if(p->fx_chain_buffer == NULL ) {
        veejay_msg(VEEJAY_MSG_WARNING,"Unable to allocate sufficient memory to keep all FX chain buffers in RAM");
        return NULL;
    }
    total_used += fx_chain_size;
    p->fx_chain_buflen = fx_chain_size;


    for (c = 0; c < SAMPLE_MAX_EFFECTS; c++) {
        p->frame_buffer[c] = (ycbcr_frame *) vj_calloc(sizeof(ycbcr_frame));
         if(!p->frame_buffer[c]) {
             return NULL;
         }

         const int space = frame_len * 3;
         uint8_t *ptr = p->fx_chain_buffer + (c * space);
         p->frame_buffer[c]->Y = ptr;
         p->frame_buffer[c]->Cb = p->frame_buffer[c]->Y + plane_len;
         p->frame_buffer[c]->Cr = p->frame_buffer[c]->Cb + plane_len;
         p->frame_buffer[c]->alpha = p->frame_buffer[c]->Cr + plane_len;

         p->frame_buffer[c]->P0  = ptr + frame_len;
         p->frame_buffer[c]->P1  = p->frame_buffer[c]->P0 + frame_len;

         veejay_memset( p->frame_buffer[c]->Y, pixel_Y_lo_,plane_len);
         veejay_memset( p->frame_buffer[c]->Cb,128,plane_len);
         veejay_memset( p->frame_buffer[c]->Cr,128,plane_len);
         veejay_memset( p->frame_buffer[c]->alpha,0,plane_len);
         veejay_memset( p->frame_buffer[c]->P0, pixel_Y_lo_, plane_len );
         veejay_memset( p->frame_buffer[c]->P0 + plane_len, 128, plane_len * 2);
         veejay_memset( p->frame_buffer[c]->P1, pixel_Y_lo_, plane_len );
         veejay_memset( p->frame_buffer[c]->P1 + plane_len, 128, plane_len * 2);
     }

    veejay_memset( &(p->pvar_), 0, sizeof( varcache_t));


    veejay_msg(VEEJAY_MSG_INFO,
        "[PRODUCER] Using %.2f MB RAM, %.2f MB RAM pre-allocated for FX",
            ((float)total_used/1048576.0f),
            ((float)fx_chain_size/1048576.0f)
        );

    p->chain_id = chain_id;

    return p;
}

int vj_perform_init(veejay_t * info)
{
    int res = vj_perform_allocate( info );
    if( res != 0 ) {
        veejay_msg(0, "Failed to initialize performer.");
        return 0;
    }

    performer_global_t *global = (performer_global_t*) info->performer;

    chroma_subsample_init();
    chroma_supersample_init();

    global->A = vj_perform_init_performer(info,0);
    global->B = vj_perform_init_performer(info,1);

    if( info->uc->scene_detection ) {
        vj_el_auto_detect_scenes( info->edit_list, global->A->temp_buffer,info->video_output_width,info->video_output_height, info->uc->scene_detection );
    }

    return 1;
}

static void vj_perform_close_audio(performer_t *p) {
    if (!p) return;

    if (p->lin_audio_buffer_)
        free(p->lin_audio_buffer_);
    p->lin_audio_buffer_ = NULL;

    if (p->audio_silence_)
        free(p->audio_silence_);
    p->audio_silence_ = NULL;

    veejay_memset(p->audio_buffer, 0, sizeof(uint8_t*) * SAMPLE_MAX_EFFECTS);

#ifdef HAVE_JACK
    vj_audio_declick_forget_owner(p);

    if (p->top_audio_buffer) {
        free(p->top_audio_buffer);
        p->top_audio_buffer = NULL;
        p->top_audio_buffer_capacity = 0;
    }
    if (p->external_audio_history) {
        free(p->external_audio_history);
        p->external_audio_history = NULL;
        p->external_audio_history_capacity = 0;
        p->external_audio_history_write = 0;
        p->external_audio_history_filled = 0;
        p->external_audio_history_frame_bytes = 0;
        p->external_audio_history_abs_write = 0;
        p->external_audio_read_pos = 0.0;
        p->external_audio_read_vel = 0.0;
        p->external_audio_prev_valid = 0;
        p->external_audio_prev_frame_bytes = 0;
        p->clip_target_last_frame = -1;
        p->clip_target_last_mode = -1;
        p->clip_target_last_id = -1;
        veejay_memset(p->external_audio_prev_frame, 0, sizeof(p->external_audio_prev_frame));
    }
    if (p->external_audio_context) {
        free(p->external_audio_context);
        p->external_audio_context = NULL;
        p->external_audio_context_capacity = 0;
    }
    p->external_audio_transport_active = 0;
    p->external_audio_last_speed = 0;
    p->external_audio_last_rate_key = 0;
    p->external_audio_last_sync_key = -1;
    p->external_audio_prev_valid = 0;
    p->external_audio_prev_frame_bytes = 0;
    p->clip_target_last_frame = -1;
    p->clip_target_last_mode = -1;
    p->clip_target_last_id = -1;
    p->track_align_last_wide_search_ms = 0;
    p->track_align_last_wide_search_mode = -1;
    p->track_align_last_wide_search_id = -1;
    p->track_align_last_sync_mode = -999;
    p->track_align_last_sync_target_mode = -999;
    p->track_align_last_sync_reset_seq = -1;
    p->track_align_last_wide_snap_ms = 0;
    p->track_align_last_wide_snap_delta = 0;
    p->track_align_seen_reacquire_seq = 0;
    p->track_align_last_reacquire_ms = 0;
    p->track_align_last_servo_offer_ms = 0;
    p->track_align_servo_candidate_ms = 0;
    p->track_align_servo_sign = 0;
    p->track_align_servo_count = 0;
    p->track_align_servo_min_conf = 0;
    p->track_align_last_sync_mode = -999;
    p->track_align_last_sync_target_mode = -999;
    p->track_align_last_sync_reset_seq = -1;
    vj_perform_track_align_clear_candidate(p);
    vj_perform_track_align_clear_wide_buckets(p);
    veejay_memset(p->external_audio_prev_frame, 0, sizeof(p->external_audio_prev_frame));
    if (p->audio_rec_buffer) {
        free(p->audio_rec_buffer);
        p->audio_rec_buffer = NULL;
    }
    if (p->audio_render_buffer) {
        free(p->audio_render_buffer);
        p->audio_render_buffer = NULL;
    }

    if (p->audio_chain_final_buffer) {
        free(p->audio_chain_final_buffer);
        p->audio_chain_final_buffer = NULL;
    }

    if (p->down_sample_buffer) {
        free(p->down_sample_buffer);
        p->down_sample_buffer = NULL;
        p->down_sample_rec_buffer = NULL;
    }
    if( p->audio_downmix_buffer) {
        free(p->audio_downmix_buffer);
        p->audio_downmix_buffer = NULL;
    }

    if (p->audio_edge) {
        audio_edge_t *edge = p->audio_edge;
        if (edge->fwdL) free(edge->fwdL);
        if (edge->fwdR) free(edge->fwdR);
        if (edge->silenceL) free(edge->silenceL);
        if (edge->silenceR) free(edge->silenceR);
        if (edge->history) free(edge->history);
        if (edge->fade_lut) free(edge->fade_lut);
        free(edge);
        p->audio_edge = NULL;
    }

#endif

}

int init_audio_resampler(veejay_t *info, performer_t *p) {
#ifdef HAVE_JACK
    editlist *el = vj_perform_audio_editlist(info);

    if(!vj_perform_audio_media_valid(el))
        return 1;

    const int chans = el->audio_chans;
    const int rate  = el->audio_rate;

    p->audio_scratcher = vj_scratch_init(chans, rate, el->video_fps);
#endif
    return 1;
}


int vj_perform_init_audio(veejay_t * info, int AorB)
{
#ifndef HAVE_JACK
    veejay_msg(VEEJAY_MSG_DEBUG, "Jack was not enabled during build, no support for audio");
#else

    performer_global_t *global = (performer_global_t*) info->performer;
    editlist *el = vj_perform_audio_editlist(info);

    if(!vj_perform_audio_params_valid(el)) {
        if(!vj_perform_prepare_silence_audio_params(el)) {
            veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Unable to prepare silent audio runtime parameters");
            return 0;
        }
    }

    performer_t *p = (AorB ? global->A : global->B);

    if(p && p->top_audio_buffer && p->audio_edge && p->audio_silence_)
        return 1;

    double samples_per_frame = (double)el->audio_rate / (double)el->video_fps;
    const uint32_t sample_len = ceil(samples_per_frame) * el->audio_bps;


    const uint32_t runtime_audio_len = (uint32_t)(ceil((double)el->audio_rate * 2.0) * el->audio_bps) + 4096;
    const uint32_t top_audio_len = (runtime_audio_len > (8 * PERFORM_AUDIO_SIZE)) ? runtime_audio_len : (8 * PERFORM_AUDIO_SIZE);

    p->top_audio_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * top_audio_len);
    if(!p->top_audio_buffer)
        return 0;
    p->top_audio_buffer_capacity = top_audio_len;


    double external_history_seconds = VJ_EXTERNAL_AUDIO_HISTORY_SECONDS;
    if(external_history_seconds < 8.0)
        external_history_seconds = 8.0;
    else if(external_history_seconds > VJ_EXTERNAL_AUDIO_HISTORY_MAX_SECONDS)
        external_history_seconds = VJ_EXTERNAL_AUDIO_HISTORY_MAX_SECONDS;

    const uint32_t external_history_len =
        (uint32_t)(ceil((double)el->audio_rate * external_history_seconds) * el->audio_bps) + top_audio_len;

    p->external_audio_history =
        (uint8_t*) vj_calloc(sizeof(uint8_t) * external_history_len);
    if(!p->external_audio_history)
        return 0;
    p->external_audio_history_capacity = external_history_len;
    p->external_audio_history_write = 0;
    p->external_audio_history_filled = 0;
    p->external_audio_history_frame_bytes = 0;
    p->external_audio_history_abs_write = 0;
    p->external_audio_read_pos = 0.0;
    p->external_audio_read_vel = 0.0;

    veejay_msg(VEEJAY_MSG_INFO,
               "[AUDIO-EXT] JACK monitor history %.1f seconds (%u bytes, rate=%d frame_bytes=%d)",
               external_history_seconds,
               external_history_len,
               el->audio_rate,
               el->audio_bps);

    p->external_audio_context =
        (uint8_t*) vj_calloc(sizeof(uint8_t) * top_audio_len);
    if(!p->external_audio_context)
        return 0;
    p->external_audio_context_capacity = top_audio_len;
    p->external_audio_transport_active = 0;
    p->external_audio_last_speed = 0;
    p->external_audio_last_rate_key = 0;
    p->external_audio_last_sync_key = -1;
    p->external_audio_prev_valid = 0;
    p->external_audio_prev_frame_bytes = 0;
    p->clip_target_last_frame = -1;
    p->clip_target_last_mode = -1;
    p->clip_target_last_id = -1;
    p->track_align_last_wide_search_ms = 0;
    p->track_align_last_wide_search_mode = -1;
    p->track_align_last_wide_search_id = -1;
    p->track_align_last_sync_mode = -999;
    p->track_align_last_sync_target_mode = -999;
    p->track_align_last_sync_reset_seq = -1;
    p->track_align_last_wide_snap_ms = 0;
    p->track_align_last_wide_snap_delta = 0;
    p->track_align_seen_reacquire_seq = 0;
    p->track_align_last_reacquire_ms = 0;
    p->track_align_last_servo_offer_ms = 0;
    p->track_align_servo_candidate_ms = 0;
    p->track_align_servo_sign = 0;
    p->track_align_servo_count = 0;
    p->track_align_servo_min_conf = 0;
    p->track_align_last_sync_mode = -999;
    p->track_align_last_sync_target_mode = -999;
    p->track_align_last_sync_reset_seq = -1;
    vj_perform_track_align_clear_candidate(p);
    vj_perform_track_align_clear_wide_buckets(p);
    veejay_memset(p->external_audio_prev_frame, 0, sizeof(p->external_audio_prev_frame));

    p->audio_rec_buffer =
        (uint8_t *) vj_calloc(sizeof(uint8_t) * top_audio_len );
    if(!p->audio_rec_buffer)
        return 0;

    p->down_sample_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * (sample_len * MAX_SPEED_AV) + 1024 + 128 );
    if(!p->down_sample_buffer)
        return 0;
    p->down_sample_rec_buffer = p->down_sample_buffer + (sizeof(uint8_t) * sample_len * MAX_SPEED_AV );

    {
        int audio_render_frames = TRACK_ALIGN_AUDIO_RENDER_FRAMES;
        if(audio_render_frames < SAMPLE_MAX_EFFECTS)
            audio_render_frames = SAMPLE_MAX_EFFECTS;
        p->audio_render_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * sample_len * audio_render_frames );
        if(!p->audio_render_buffer)
            return 0;
        p->audio_render_buffer_capacity = (size_t)sample_len * (size_t)audio_render_frames;
    }

    p->audio_chain_final_buffer = (uint8_t*) vj_calloc(sizeof(uint8_t) * sample_len );
    if(!p->audio_chain_final_buffer)
        return 0;

    p->audio_downmix_buffer =  (uint8_t *) vj_calloc(sizeof(uint8_t) * sample_len * SAMPLE_MAX_EFFECTS );
    if(!p->audio_downmix_buffer)
        return 0;


    p->lin_audio_buffer_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * sample_len * SAMPLE_MAX_EFFECTS );
    if(!p->lin_audio_buffer_)
        return 0;

    for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        p->audio_buffer[i] = p->lin_audio_buffer_ + (sample_len * i);
    }

    p->audio_silence_ = (uint8_t*) vj_calloc( sizeof(uint8_t) * sample_len );
    if(!p->audio_silence_)
        return 0;

    audio_edge_t *edge = (audio_edge_t*) vj_calloc( sizeof(audio_edge_t) );
    if(!edge) {
        return 0;
    }

    edge->buflen = (sample_len * MAX_SPEED_AV * sizeof(int16_t));

    edge->fwdL = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen);
    edge->fwdR = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen);
    edge->silenceL = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen);
    edge->silenceR = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen);

    edge->history = (int16_t*) vj_calloc( sizeof(int16_t) * edge->buflen * el->audio_chans);

    edge->fade_lut = (float*) vj_calloc( sizeof(float) * 257);
    for (int i = 0; i <= FADE_LUT_SIZE; i++) {

        edge->fade_lut[i] = sinf(((float)i / FADE_LUT_SIZE) * (1.57079632679f));
    }

    p->audio_edge = edge;


    return init_audio_resampler(info, p );

#endif
    return 0;
}

static void vj_perform_free_performer(performer_t *p)
{
    int c;
    if(p->frame_buffer) {
        for(c = 0; c < SAMPLE_MAX_EFFECTS; c ++ )
        {
            if(p->frame_buffer[c]) {
                free(p->frame_buffer[c]);
                p->frame_buffer[c] = NULL;
            }
        }
        free(p->frame_buffer);
        p->frame_buffer = NULL;
    }

    if(p->primary_buffer){
        for( c = 0;c < PRIMARY_FRAMES; c++ )
        {
            free(p->primary_buffer[c] );
            p->primary_buffer[c] = NULL;
        }
        free(p->primary_buffer);
        p->primary_buffer = NULL;
    }


   if(p->temp_buffer[0]) {
       free(p->temp_buffer[0]);
       p->temp_buffer[0] = NULL;
   }
   if(p->subrender_buffer[0]) {
       free(p->subrender_buffer[0]);
       p->subrender_buffer[0] = NULL;
   }

   if(p->rgba_buffer[0]) {
       free(p->rgba_buffer[0]);
       p->rgba_buffer[0] = NULL;
   }

   if(p->fx_chain_buffer)
   {
        munlock(p->fx_chain_buffer, p->fx_chain_buflen);
        free(p->fx_chain_buffer);
   }

   if(p->pribuf_area)
   {
       munlock(p->pribuf_area, p->pribuf_len);
       free(p->pribuf_area);
   }

   yuv_free_swscaler( p->rgba2yuv_scaler );
   yuv_free_swscaler( p->yuv2rgba_scaler );
   yuv_free_swscaler( p->yuv420_scaler );

   free(p->rgba_frame[0]);
   free(p->rgba_frame[1]);

}

void vj_perform_free(veejay_t * info)
{
    performer_global_t *global = (performer_global_t*)info->performer;
    if( global == NULL ) {
        return;
    }

    munlockall();

    sample_record_free();

    if (global->preview_buffer){
        if(global->preview_buffer->Y)
            free(global->preview_buffer->Y);
        free(global->preview_buffer);
    }
    if(global->feedback_buffer[0]) {
       free(global->feedback_buffer[0]);
       global->feedback_buffer[0] = NULL;
    }

    if( global->A )
        vj_perform_free_performer( global->A );
    if( global->B )
        vj_perform_free_performer( global->B );

    if(info->edit_list && info->edit_list->has_audio) {
        vj_perform_close_audio(global->A);
        vj_perform_close_audio(global->B);
    }

    vj_perform_record_buffer_free(global);

    vj_avcodec_stop(global->encoder_,0);

    free(global);
}

int vj_perform_preview_max_width(veejay_t *info) {
    performer_global_t *global = (performer_global_t*)info->performer;
    return global->preview_max_w;
}

int vj_perform_preview_max_height(veejay_t *info) {
    performer_global_t *global = (performer_global_t*)info->performer;
    return global->preview_max_h;
}

int vj_perform_audio_start(veejay_t * info)
{
    editlist *el = info ? info->edit_list : NULL;

    if(!info || !el)
        return 0;

#ifdef HAVE_JACK
    if(info->settings && atomic_load_int(&info->settings->audio_threads_disabled))
        return 0;

    if(el->has_audio && !vj_perform_audio_params_valid(el)) {
        veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Embedded audio stream has invalid parameters; audio playback disabled");
        return 0;
    }

    const int silent_output = !el->has_audio;
    int saved_has_audio = el->has_audio;

    if(silent_output) {
        if(!vj_perform_prepare_silence_audio_params(el)) {
            veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Unable to prepare silent playback parameters");
            return 0;
        }

        if(info->current_edit_list && info->current_edit_list != el)
            vj_perform_prepare_silence_audio_params(info->current_edit_list);

        veejay_msg(VEEJAY_MSG_INFO,
                   "[AUDIO] No embedded audio stream; starting JACK silence output (%ld Hz, %d channels, %d bit)",
                   (long)el->audio_rate,
                   el->audio_chans,
                   el->audio_bits);
        el->has_audio = 1;
    }

    vj_jack_initialize();

    int res = vj_jack_init_duplex(el);

    if( res <= 0 ) {
        veejay_msg(VEEJAY_MSG_WARNING,
                   "[AUDIO] Jack duplex start failed; falling back to playback-only mode");

        vj_jack_initialize();
        res = vj_jack_init(el);
    }

    if(silent_output)
        el->has_audio = saved_has_audio;

    if( res <= 0 ) {
        veejay_msg(0, "[AUDIO] Audio playback disabled");
        info->audio = NO_AUDIO;
        return 0;
    }

    if ( res == 2 )
    {
        vj_jack_stop();
        info->audio = NO_AUDIO;
        veejay_msg(VEEJAY_MSG_ERROR,"Please run jackd with a sample rate of %ld",(long)el->audio_rate );
        return 0;
    }

    if(vj_jack_has_input())
        veejay_msg(VEEJAY_MSG_DEBUG,"[AUDIO] Jack audio playback started with capture input ports");
    else
        veejay_msg(VEEJAY_MSG_WARNING,"[AUDIO] Jack audio playback started without capture input ports");

    return 1;
#else
    if(el->has_audio)
        veejay_msg(VEEJAY_MSG_WARNING, "[AUDIO] Jack support not compiled in (no audio)");
    return 0;
#endif
}

void vj_perform_audio_stop(veejay_t * info)
{
    if(!info)
        return;

    if (info->audio == AUDIO_PLAY) {
#ifdef HAVE_JACK
        vj_jack_stop();
#endif
        info->audio = NO_AUDIO;
    }
}

void vj_perform_get_primary_frame(veejay_t * info, uint8_t **frame)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    frame[0] = p->primary_buffer[info->out_buf]->Y;
    frame[1] = p->primary_buffer[info->out_buf]->Cb;
    frame[2] = p->primary_buffer[info->out_buf]->Cr;
    frame[3] = p->primary_buffer[info->out_buf]->alpha;
}

uint8_t *vj_perform_get_preview_buffer(veejay_t *info)
{
    performer_global_t *global = (performer_global_t*)info->performer;
    return global->preview_buffer->Y;
}

void    vj_perform_get_crop_dimensions(veejay_t *info, int *w, int *h)
{
    *w = info->video_output_width - info->settings->viewport.left - info->settings->viewport.right;
    *h = info->video_output_height - info->settings->viewport.top - info->settings->viewport.bottom;
}

static long stream_pts_ = 0;
static int vj_perform_compress_primary_frame_s2(veejay_t *info,VJFrame *frame )
{
    performer_global_t *g = (performer_global_t*) info->performer;
    if( g->encoder_ == NULL ) {
        g->encoder_ = vj_avcodec_start(info->effect_frame1, ENCODER_MJPEG, NULL);
        if(g->encoder_ == NULL) {
            return 0;
        }
    }

    return vj_avcodec_encode_frame(g->encoder_,
            stream_pts_ ++,
            ENCODER_MJPEG,
            frame->data,
            vj_avcodec_get_buf(g->encoder_),
            vj_avcodec_get_buf_size(g->encoder_),
            frame->format);
}

void    vj_perform_send_primary_frame_s2(veejay_t *info, int mcast, int to_mcast_link_id, VJFrame *display_frame)
{
    int i;
    performer_global_t *g = (performer_global_t*) info->performer;

    if( info->splitter ) {
        for ( i = 0; i < VJ_MAX_CONNECTIONS ; i ++ ) {
            if( info->rlinks[i] >= 0 ) {
                int link_id = info->rlinks[i];
                if( link_id == -1 )
                    continue;

                int screen_id = info->splitted_screens[ i ];
                if( screen_id < 0 )
                    continue;

                VJFrame *frame = vj_split_get_screen( info->splitter, screen_id );
                int data_len = vj_perform_compress_primary_frame_s2( info, frame );
                if(data_len <= 0)
                    continue;

                if( vj_server_send_frame( info->vjs[3], link_id, vj_avcodec_get_buf(g->encoder_),data_len, frame ) <= 0 ) {
                    _vj_server_del_client( info->vjs[3], link_id );
                }

                info->rlinks[i] = -1;
                info->splitted_screens[i] = -1;
            }
        }

        atomic_store_int(&info->settings->unicast_frame_sender, 0);
    }
    else {

        int data_len = vj_perform_compress_primary_frame_s2( info, display_frame );
        if( data_len <= 0) {
            return;
        }

        int id = (mcast ? 2: 3);

        if(!mcast)
        {
            for( i = 0; i < VJ_MAX_CONNECTIONS; i++ ) {
                if( info->rlinks[i] != -1 ) {
                    if(vj_server_send_frame( info->vjs[id], info->rlinks[i], vj_avcodec_get_buf(g->encoder_), data_len, display_frame )<=0)
                    {
                            _vj_server_del_client( info->vjs[id], info->rlinks[i] );
                    }
                    info->rlinks[i] = -1;
                }
            }

            atomic_store_int(&info->settings->unicast_frame_sender ,0);
        }
        else
        {
            if(vj_server_send_frame( info->vjs[id], to_mcast_link_id, vj_avcodec_get_buf(g->encoder_), data_len, display_frame )<=0)
            {
                veejay_msg(VEEJAY_MSG_DEBUG,  "Error sending multicast frame");
            }
        }
    }
}

void vj_perform_get_primary_frame_420p(veejay_t *info, uint8_t **frame )
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    p->yuv420_frame[0]->data[0] = frame[0] = p->temp_buffer[0];
    p->yuv420_frame[0]->data[1] = frame[1] = p->temp_buffer[1];
    p->yuv420_frame[0]->data[2] = frame[2] = p->temp_buffer[2];
    p->yuv420_frame[0]->data[3] = frame[3] = p->temp_buffer[3];

    VJFrame pframe;
    memcpy(&pframe, info->effect_frame1, sizeof(VJFrame));
    pframe.data[0] = p->primary_buffer[info->out_buf]->Y;
    pframe.data[1] = p->primary_buffer[info->out_buf]->Cb;
    pframe.data[2] = p->primary_buffer[info->out_buf]->Cr;
    pframe.stride[0] = p->yuv420_frame[0]->stride[0];
    pframe.stride[1] = p->yuv420_frame[0]->stride[0] >> 1;
    pframe.stride[2] = p->yuv420_frame[0]->stride[1];
    pframe.stride[3] = 0;

    vj_perform_format_changed_yuv(p, &pframe);
    yuv_convert_and_scale( p->yuv420_scaler, &pframe, p->yuv420_frame[0] );
}

static void *vj_perform_init_scaler(VJFrame *src, VJFrame *dst) {
    sws_template templ;
    veejay_memset(&templ,0,sizeof(sws_template));
    templ.flags = yuv_which_scaler();

    return yuv_init_swscaler( src, dst, &templ, yuv_sws_get_cpu_flags() );
}

static int vj_perform_format_changed_yuv(performer_t *p, VJFrame *frame) {

    if( p->yuv420_scaler == NULL || p->yuv420_frame[0] == NULL || p->fx_yuv_format != frame->format) {
        if(p->yuv420_frame[0]) {
           free(p->yuv420_frame[0]);
        }
        p->yuv420_frame[0] = yuv_yuv_template(NULL, NULL,NULL, frame->width,frame->height,
            frame->range ? PIX_FMT_YUVJ420P : PIX_FMT_YUV420P);
        if(!p->yuv420_frame[0]) {
            return 0;
        }
        p->yuv420_scaler =  vj_perform_init_scaler( frame, p->yuv420_frame[0]);
        if(p->yuv420_scaler == NULL )
            return 0;
    }

    p->fx_yuv_format = frame->format;

    return 1;

}

static int vj_perform_format_changed_rgb(performer_t *p, VJFrame *frame) {


    if( p->yuv2rgba_scaler == NULL || p->rgba_frame[0] == NULL || p->fx_rgb_format != frame->format ) {
        if(p->rgba_frame[0]) {
            free(p->rgba_frame[0]);
        }

        p->rgba_frame[0] = yuv_rgb_template( p->rgba_buffer[0], frame->width, frame->height, PIX_FMT_RGBA );
        if(!p->rgba_frame[0])
            return 0;

        if(p->yuv2rgba_scaler) {
            yuv_free_swscaler(p->yuv2rgba_scaler);
        }

        p->yuv2rgba_scaler = vj_perform_init_scaler(frame, p->rgba_frame[0]);
        if(p->yuv2rgba_scaler == NULL )
            return 0;
    }


    if( p->rgba2yuv_scaler == NULL || p->rgba_frame[1] == NULL || p->fx_rgb_format != frame->format ) {
        if(p->rgba_frame[1]) {
            free(p->rgba_frame[1]);
        }

        p->rgba_frame[1] = yuv_rgb_template( p->rgba_buffer[0], frame->width, frame->height, PIX_FMT_RGBA );
        if(!p->rgba_frame[1])
            return 0;

        if(p->rgba2yuv_scaler) {
            yuv_free_swscaler(p->rgba2yuv_scaler);
        }
        p->rgba2yuv_scaler = vj_perform_init_scaler(p->rgba_frame[1], frame);
        if(p->rgba2yuv_scaler == NULL )
            return 0;
    }


    p->rgba_frame[0]->data[0] = p->rgba_buffer[0];
    p->rgba_frame[1]->data[0] = p->rgba_buffer[1];
    p->fx_rgb_format = frame->format;

    return 1;
}

static void vj_perform_apply_first(veejay_t *info,performer_t *p, vjp_kf *todo_info,
    VJFrame **frames, sample_eff_chain *entry, int e , int c, long long n_frame, void *ptr, int playmode)
{
    int n_params = 0;
    int is_mixer = 0;
    int rgb = 0;

    if(entry == NULL || !vje_get_info( e, &is_mixer, &n_params, &rgb)) {
        return;
    }

    if(n_params > SAMPLE_MAX_PARAMETERS) {
        veejay_msg(VEEJAY_MSG_WARNING, "FX %d has more than %d parameters", SAMPLE_MAX_PARAMETERS);
        n_params = SAMPLE_MAX_PARAMETERS;
    }

    int args[SAMPLE_MAX_PARAMETERS];
    veejay_memset(args,0,sizeof(args));

    if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG )
    {
        if(!vj_tag_get_all_effect_args(entry, args, n_params, (int) n_frame))
            return;
    }
    else
    {
        if(!sample_get_all_effect_arg(entry, args, n_params, (int) n_frame))
            return;
    }

    if( rgb ) {

        if(!vj_perform_format_changed_rgb(p, frames[0])) {
            return;
        }

        yuv_convert_and_scale_rgb( p->yuv2rgba_scaler, frames[0], p->rgba_frame[0] );
        if(is_mixer) {
            yuv_convert_and_scale_rgb( p->yuv2rgba_scaler, frames[1], p->rgba_frame[1] );
        }

        vjert_apply( entry, p->rgba_frame, p->chain_id,c, args );

        yuv_convert_and_scale_from_rgb( p->rgba2yuv_scaler, p->rgba_frame[0],frames[0] );
    }
    else {
        vjert_apply( entry, frames, p->chain_id, c, args );
    }

}

static long vj_calc_next_sample_offset(
    sample_b_t *sb,
    int *advance_out
) {
    long start = sb->start;
    long end = sb->end;
    int speed = sb->speed;
    int loop_mode = sb->loopmode;

    const long len = (end - start) + 1;

    if (len <= 0) {
        *advance_out = 0;
        sb->offset = 0;
        return 0;
    }

    const long max_off = len - 1;
    int advance = 1;

    if(speed == 0) {
        long off = sb->offset;

        if(off < 0)
            off = 0;
        else if(off > max_off)
            off = max_off;

        *advance_out = 0;
        sb->offset = off;
        return off;
    }

    if (sb->direction_changed) {
        sb->cur_sfd = 0;
        sb->direction_changed = 0;
    }

    if (sb->max_sfd > 0) {
        sb->cur_sfd++;

        if (sb->cur_sfd < sb->max_sfd) {
            advance = 0;
        } else {
            sb->cur_sfd = 0;
            advance = 1;
        }
    }

    *advance_out = advance;

    long off = sb->offset;

    if (off < 0)
        off = 0;
    else if (off > max_off)
        off = max_off;

    if (advance && loop_mode == 3) {
        off = (long)((double)len * (double)rand() / ((double)RAND_MAX + 1.0));
        if (off > max_off)
            off = max_off;

        sb->offset = off;
        return off;
    }

    if (advance) {
        const long step = llabs((long long)speed);

        off += step * sb->direction;

        if (off > max_off) {
            if (loop_mode == 2) {
                off = max_off;
                sb->direction = -1;
            } else if (loop_mode == 1) {
                off = 0;
            } else {
                off = max_off;
            }
        } else if (off < 0) {
            if (loop_mode == 2) {
                off = 0;
                sb->direction = +1;
            } else if (loop_mode == 1) {
                off = max_off;
            } else {
                off = 0;
            }
        }

        sb->offset = off;
    }

    return off;
}

long vj_calc_next_sub_audioframe(veejay_t *info, int b, audio_chain_entry_t *audio_entry) {
    sample_b_t sb;

    sb.start = audio_entry->start;
    sb.end = audio_entry->end;
    sb.speed = audio_entry->speed;
    sb.offset = audio_entry->offset;
    sb.direction = (sb.speed < 0 ? -1: 1);
    sb.loopmode = audio_entry->loopmode;
    sb.cur_sfd = audio_entry->cur_sfd;
    sb.max_sfd = audio_entry->max_sfd;

    int advance = 0;
    long off = vj_calc_next_sample_offset(
        &sb,
        &advance
    );

    return sb.start + off;
}

long vj_calc_next_subframe(veejay_t *info, int b)
{
    performer_global_t *g = info->performer;
    performer_t *perf = g->A;
    sample_b_t *sb = &perf->sample_b;

    int sample_i[6];

    if(sample_get_long_info(b,&sample_i[0],&sample_i[1],&sample_i[2],&sample_i[3],&sample_i[4], &sample_i[5])!=0) return -1;

    const long start = sample_i[0];
    const long end   = sample_i[1];

    sb->offset  = atomic_load_long_long(&sb->offset);
    sb->cur_sfd = sample_i[4];
    sb->max_sfd = sample_i[5];
    sb->speed   = sample_i[3];
    sb->start   = start;
    sb->end     = end;
    sb->loopmode= sample_i[2];

    if (sb->direction == 0) {
        int dir = (sb->speed < 0) ? -1 : +1;
        sb->direction_changed = (dir != sb->direction);
        sb->direction = dir;
    }

    int advance = 0;

    long off = vj_calc_next_sample_offset(
        sb,
        &advance
    );

    atomic_store_long_long(&sb->offset, off);
    atomic_store_long_long(&sb->start, start);

    sample_set_framedups(b, sb->cur_sfd);

    return start + off;
}

#ifdef HAVE_JACK

static void slow_motion_update_turn_history(sample_b_t *posdata,
                                            const uint8_t *buf,
                                            int samples,
                                            int frame_bytes);


static int get_audio_frame_safe(
    veejay_t *info,
    editlist *el,
    long long frame,
    uint8_t *audio_buf,
    int pred_len,
    int frame_bytes,
    int speed
) {
    (void)speed;

    if (el == NULL || audio_buf == NULL || pred_len <= 0 || frame_bytes <= 0)
        return 0;

    int n_samples = vj_el_get_audio_frame(el, frame, audio_buf);

    if (n_samples <= 0) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);
        if(info && info->settings) {
            atomic_add_fetch_old_long_long(&info->settings->audio_osd.prod_anomalies, 1);
            atomic_add_fetch_old_long_long(&info->settings->audio_osd.prod_write_zero, 1);
        }
        return pred_len;
    }

    if (n_samples < pred_len) {
        veejay_memset(audio_buf + ((size_t)n_samples * (size_t)frame_bytes), 0,
                      (pred_len - n_samples) * frame_bytes);
        if(info && info->settings) {
            atomic_add_fetch_old_long_long(&info->settings->audio_osd.prod_anomalies, 1);
            atomic_add_fetch_old_long_long(&info->settings->audio_osd.prod_write_short, 1);
        }
        return pred_len;
    }

    return n_samples;
}


static inline double slow_motion_sync_abs_pos(const editlist *el,
                                       const sample_b_t *posdata,
                                       long long source_frame,
                                       int pred_len,
                                       int slice_count,
                                       int cur_slice,
                                       int direction);
static inline int slow_motion_frame_len_exact(const editlist *el,
                                              long long frame,
                                              int pred_len);
static int slow_motion_fetch_scratch_context(veejay_t *info,
                                             editlist *el,
                                             sample_b_t *posdata,
                                             uint8_t *ctx,
                                             slow_scratch_ctx_map_t *map,
                                             int pred_len,
                                             int frame_bytes,
                                             int speed_int,
                                             double pos_a,
                                             double pos_b,
                                             double *ctx_abs_start_out,
                                             int *ctx_samples_out,
                                             long long *ctx_first_frame_out,
                                             long long *ctx_last_frame_out);
static double slow_motion_ctx_exact_to_actual_rel(const slow_scratch_ctx_map_t *map,
                                                  double abs_pos,
                                                  int ctx_samples);
static inline int16_t slow_motion_cubic_interp_s16(int p0, int p1, int p2, int p3, double t);

static inline void normal_ctx_sample_frame_s16(const uint8_t *ctx,
                                               int ctx_samples,
                                               double ctx_abs_start,
                                               const slow_scratch_ctx_map_t *ctx_map,
                                               double abs_pos,
                                               int frame_bytes,
                                               int16_t *dst,
                                               int dst_words)
{
    if (dst == NULL || dst_words <= 0)
        return;

    for (int c = 0; c < dst_words; c++)
        dst[c] = 0;

    if (ctx == NULL || ctx_samples <= 0 || frame_bytes <= 0 || (frame_bytes & 1))
        return;

    const int words = frame_bytes / 2;
    const int out_words = (dst_words < words) ? dst_words : words;
    const int max_index = ctx_samples - 1;
    const int16_t *in = (const int16_t*)ctx;

    double rel = (ctx_map != NULL && ctx_map->valid)
        ? slow_motion_ctx_exact_to_actual_rel(ctx_map, abs_pos, ctx_samples)
        : (abs_pos - ctx_abs_start);

    if (rel < 0.0)
        rel = 0.0;
    else if (rel > (double)max_index)
        rel = (double)max_index;

    int idx = (int)floor(rel);
    double frac = rel - (double)idx;
    if (idx < 0) {
        idx = 0;
        frac = 0.0;
    } else if (idx > max_index) {
        idx = max_index;
        frac = 0.0;
    }

    int i0 = idx - 1;
    int i1 = idx;
    int i2 = idx + 1;
    int i3 = idx + 2;
    if (i0 < 0) i0 = 0;
    if (i2 > max_index) i2 = max_index;
    if (i3 > max_index) i3 = max_index;

    const int b0 = i0 * words;
    const int b1 = i1 * words;
    const int b2 = i2 * words;
    const int b3 = i3 * words;

    for (int c = 0; c < out_words; c++) {
        dst[c] = slow_motion_cubic_interp_s16(
            in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac);
    }
}

static int normal_ctx_turn_candidate_cost_s16(const uint8_t *ctx,
                                              int ctx_samples,
                                              double ctx_abs_start,
                                              const slow_scratch_ctx_map_t *ctx_map,
                                              double base_pos,
                                              double start_pos,
                                              double v0,
                                              double v1,
                                              int turn_samples,
                                              int frame_bytes,
                                              const uint8_t *prev_prev_frame,
                                              const uint8_t *prev_frame,
                                              int *value_delta_out,
                                              int *slope_delta_out,
                                              int *step_max_out)
{
    if (value_delta_out != NULL)
        *value_delta_out = 0x3fffffff;
    if (slope_delta_out != NULL)
        *slope_delta_out = 0x3fffffff;
    if (step_max_out != NULL)
        *step_max_out = 0x3fffffff;

    if (ctx == NULL || prev_frame == NULL || ctx_samples <= 0 ||
        frame_bytes <= 0 || (frame_bytes & 1))
        return 0x3fffffff;

    const int words = frame_bytes / 2;
    const int local_words = (words > 8) ? 8 : words;
    const int preview = (turn_samples < 32) ? turn_samples : 32;
    const int16_t *prev = (const int16_t*)prev_frame;
    const int16_t *prev2 = (const int16_t*)prev_prev_frame;
    int16_t cur[8];
    int16_t oldv[8];
    int16_t newv[8];
    int16_t last[8];
    int have_last = 0;
    int value_delta = 0;
    int slope_delta = 0;
    int step_max = 0;
    int cost;

    normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                start_pos, frame_bytes, cur, local_words);

    for (int c = 0; c < local_words; c++) {
        int d = (int)cur[c] - (int)prev[c];
        d = (d < 0) ? -d : d;
        if (d > value_delta)
            value_delta = d;
        last[c] = cur[c];
    }
    have_last = 1;

    for (int i = 1; i < preview; i++) {
        const double u = (turn_samples > 1) ? ((double)i / (double)(turn_samples - 1)) : 1.0;
        const double w = u * u * (3.0 - (2.0 * u));
        normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                    start_pos + (v0 * (double)i),
                                    frame_bytes, oldv, local_words);
        normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                    base_pos + (v1 * (double)i),
                                    frame_bytes, newv, local_words);
        for (int c = 0; c < local_words; c++) {
            const double mixed = ((1.0 - w) * (double)oldv[c]) + (w * (double)newv[c]);
            int sample = (int)((mixed >= 0.0) ? (mixed + 0.5) : (mixed - 0.5));
            if (sample < -32768) sample = -32768;
            if (sample >  32767) sample =  32767;
            if (have_last) {
                int st = sample - (int)last[c];
                st = (st < 0) ? -st : st;
                if (st > step_max)
                    step_max = st;
            }
            last[c] = (int16_t)sample;
        }
    }

    if (prev_prev_frame != NULL) {
        normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                    start_pos + v0, frame_bytes, cur, local_words);
        for (int c = 0; c < local_words; c++) {
            int old_slope = (int)prev[c] - (int)prev2[c];
            int new_slope = (int)cur[c] - (int)prev[c];
            int sd = new_slope - old_slope;
            sd = (sd < 0) ? -sd : sd;
            if (sd > slope_delta)
                slope_delta = sd;
        }
    } else {
        slope_delta = step_max;
    }

    cost = (value_delta * 8) + (slope_delta * 12) + (step_max * 18);
    if (value_delta > 4096)
        cost += (value_delta - 4096) * 48;
    if (slope_delta > 8192)
        cost += (slope_delta - 8192) * 24;
    if (step_max > 768)
        cost += (step_max - 768) * 64;

    if (value_delta_out != NULL)
        *value_delta_out = value_delta;
    if (slope_delta_out != NULL)
        *slope_delta_out = slope_delta;
    if (step_max_out != NULL)
        *step_max_out = step_max;

    return cost;
}

static int perform_normal_turn_render_s16(uint8_t *dst,
                                          int dst_samples,
                                          const uint8_t *ctx,
                                          int ctx_samples,
                                          double ctx_abs_start,
                                          const slow_scratch_ctx_map_t *ctx_map,
                                          double base_pos,
                                          double start_pos,
                                          double v0,
                                          double v1,
                                          int turn_samples,
                                          int frame_bytes,
                                          int *step_max_out,
                                          int *step_avg_out)
{
    if (step_max_out != NULL)
        *step_max_out = 0;
    if (step_avg_out != NULL)
        *step_avg_out = 0;

    if (dst == NULL || ctx == NULL || dst_samples <= 0 || ctx_samples <= 0 ||
        frame_bytes <= 0 || (frame_bytes & 1))
        return 0;

    if (turn_samples < 8)
        turn_samples = 8;
    if (turn_samples > dst_samples)
        turn_samples = dst_samples;

    const int words = frame_bytes / 2;
    const int local_words = (words > 8) ? 8 : words;
    int16_t *out = (int16_t*)dst;
    int16_t oldv[8];
    int16_t newv[8];
    int16_t prev_words[8];
    int prev_valid = 0;
    int step_peak = 0;
    int64_t step_sum = 0;
    int step_n = 0;

    for (int i = 0; i < dst_samples; i++) {
        const int bo = i * words;
        int frame_step = 0;

        if (i < turn_samples) {
            const double u = (turn_samples > 1) ? ((double)i / (double)(turn_samples - 1)) : 1.0;
            const double w = u * u * (3.0 - (2.0 * u));
            normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                        start_pos + (v0 * (double)i),
                                        frame_bytes, oldv, local_words);
            normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                        base_pos + (v1 * (double)i),
                                        frame_bytes, newv, local_words);

            if (words <= 8) {
                for (int c = 0; c < words; c++) {
                    const double mixed = ((1.0 - w) * (double)oldv[c]) + (w * (double)newv[c]);
                    int sample = (int)((mixed >= 0.0) ? (mixed + 0.5) : (mixed - 0.5));
                    if (sample < -32768) sample = -32768;
                    if (sample >  32767) sample =  32767;
                    out[bo + c] = (int16_t)sample;
                    if (prev_valid) {
                        int d = sample - (int)prev_words[c];
                        d = (d < 0) ? -d : d;
                        if (d > frame_step)
                            frame_step = d;
                    }
                    prev_words[c] = (int16_t)sample;
                }
            } else {
                for (int c = 0; c < words; c++) {
                    int16_t a[1];
                    int16_t b[1];
                    normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                                start_pos + (v0 * (double)i),
                                                frame_bytes, a, 1);
                    normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                                base_pos + (v1 * (double)i),
                                                frame_bytes, b, 1);
                    const double mixed = ((1.0 - w) * (double)a[0]) + (w * (double)b[0]);
                    int sample = (int)((mixed >= 0.0) ? (mixed + 0.5) : (mixed - 0.5));
                    if (sample < -32768) sample = -32768;
                    if (sample >  32767) sample =  32767;
                    out[bo + c] = (int16_t)sample;
                }
            }
        } else {
            int16_t sample_words[8];
            normal_ctx_sample_frame_s16(ctx, ctx_samples, ctx_abs_start, ctx_map,
                                        base_pos + (v1 * (double)i),
                                        frame_bytes, sample_words, local_words);
            for (int c = 0; c < local_words; c++) {
                out[bo + c] = sample_words[c];
                if (prev_valid) {
                    int d = (int)sample_words[c] - (int)prev_words[c];
                    d = (d < 0) ? -d : d;
                    if (d > frame_step)
                        frame_step = d;
                }
                prev_words[c] = sample_words[c];
            }
        }

        if (prev_valid) {
            if (frame_step > step_peak)
                step_peak = frame_step;
            step_sum += frame_step;
            step_n++;
        }
        prev_valid = 1;
    }

    if (step_max_out != NULL)
        *step_max_out = step_peak;
    if (step_avg_out != NULL)
        *step_avg_out = (step_n > 0) ? (int)(step_sum / step_n) : 0;

    return dst_samples;
}

static int perform_normal_direction_turn(veejay_t *info,
                                         editlist *el,
                                         performer_t *p,
                                         uint8_t *audio_buf,
                                         uint8_t *ctx_buf,
                                         int pred_len,
                                         int frame_bytes,
                                         long long cur_frame,
                                         sample_b_t *sample_ptr,
                                         int cur_dir,
                                         int last_dir,
                                         int pending_edge)
{
    if (info == NULL || el == NULL || p == NULL || audio_buf == NULL ||
        ctx_buf == NULL || sample_ptr == NULL || pred_len <= 0 || frame_bytes <= 0 ||
        cur_dir == 0 || last_dir == 0 || cur_dir == last_dir)
        return 0;

    int out_samples = slow_motion_frame_len_exact(el, cur_frame, pred_len);
    if (out_samples <= 0)
        out_samples = pred_len;
    if (out_samples > pred_len + 64)
        out_samples = pred_len + 64;

    const double base_pos = slow_motion_sync_abs_pos(el, sample_ptr, cur_frame,
                                                     pred_len, 1, 0, cur_dir);
    int turn_speed = abs(sample_ptr->speed);
    if (turn_speed < 1)
        turn_speed = 1;
    if (turn_speed > 4)
        turn_speed = 4;
    const double v0 = (double)(last_dir * turn_speed);
    const double v1 = (double)(cur_dir * turn_speed);
    int phase_radius = 768 * turn_speed;
    if (phase_radius > 3072)
        phase_radius = 3072;

    int turn_samples = 96;
    if (turn_samples > out_samples)
        turn_samples = out_samples;
    if (turn_samples < 16)
        turn_samples = (out_samples < 16) ? out_samples : 16;

    const double direct_end = base_pos + (v1 * (double)((out_samples > 1) ? (out_samples - 1) : 0));
    const double turn_end = base_pos + (v1 * (double)((turn_samples > 1) ? (turn_samples - 1) : 0));
    const double turn_overshoot = base_pos + (v0 * (double)turn_samples);

    double minp = base_pos - (double)phase_radius;
    double maxp = base_pos + (double)phase_radius;
    if (direct_end < minp) minp = direct_end;
    if (direct_end > maxp) maxp = direct_end;
    if (turn_end < minp) minp = turn_end;
    if (turn_end > maxp) maxp = turn_end;
    if (turn_overshoot < minp) minp = turn_overshoot;
    if (turn_overshoot > maxp) maxp = turn_overshoot;

    double ctx_abs_start = 0.0;
    int ctx_samples = 0;
    long long ctx_first = 0;
    long long ctx_last = 0;
    slow_scratch_ctx_map_t ctx_map;
    veejay_memset(&ctx_map, 0, sizeof(ctx_map));

    int got_ctx = slow_motion_fetch_scratch_context(info, el, sample_ptr,
                                                    ctx_buf, &ctx_map,
                                                    pred_len, frame_bytes,
                                                    cur_dir, minp, maxp,
                                                    &ctx_abs_start,
                                                    &ctx_samples,
                                                    &ctx_first,
                                                    &ctx_last);
    if (got_ctx <= 0)
        return 0;

    int phase_shift = 0;
    int phase_delta = -1;
    int phase_slope = -1;
    int phase_step = -1;
    if (sample_ptr->audio_diag_valid &&
        sample_ptr->audio_diag_frame_bytes == frame_bytes) {
        int best_cost = 0x3fffffff;
        int best_off = 0;
        int best_delta = 0x3fffffff;
        int best_slope = 0x3fffffff;
        int best_step = 0x3fffffff;

        for (int off = -phase_radius; off <= phase_radius; off += 16) {
            int d = 0, sd = 0, st = 0;
            int cost = normal_ctx_turn_candidate_cost_s16(ctx_buf, ctx_samples,
                                                          ctx_abs_start, &ctx_map,
                                                          base_pos, base_pos + (double)off,
                                                          v0, v1, turn_samples,
                                                          frame_bytes,
                                                          sample_ptr->audio_diag_prev_frame,
                                                          sample_ptr->audio_diag_last_frame,
                                                          &d, &sd, &st);
            cost += ((off < 0 ? -off : off) * 1);
            if (cost < best_cost) {
                best_cost = cost;
                best_off = off;
                best_delta = d;
                best_slope = sd;
                best_step = st;
            }
        }

        int fine_lo = best_off - 8;
        int fine_hi = best_off + 8;
        if (fine_lo < -phase_radius) fine_lo = -phase_radius;
        if (fine_hi >  phase_radius) fine_hi =  phase_radius;

        for (int off = fine_lo; off <= fine_hi; off++) {
            int d = 0, sd = 0, st = 0;
            int cost = normal_ctx_turn_candidate_cost_s16(ctx_buf, ctx_samples,
                                                          ctx_abs_start, &ctx_map,
                                                          base_pos, base_pos + (double)off,
                                                          v0, v1, turn_samples,
                                                          frame_bytes,
                                                          sample_ptr->audio_diag_prev_frame,
                                                          sample_ptr->audio_diag_last_frame,
                                                          &d, &sd, &st);
            cost += ((off < 0 ? -off : off) * 1);
            if (cost < best_cost) {
                best_cost = cost;
                best_off = off;
                best_delta = d;
                best_slope = sd;
                best_step = st;
            }
        }

        phase_shift = best_off;
        phase_delta = best_delta;
        phase_slope = best_slope;
        phase_step = best_step;
    }

    const int unsafe_step_limit = (turn_speed > 1) ? 1024 : 768;
    const int unsafe_slope_limit = (turn_speed > 1) ? 14000 : 12000;
    if (phase_delta > 4096 || phase_slope > unsafe_slope_limit || phase_step > unsafe_step_limit) {


        return 0;
    }

    int abs_shift = (phase_shift < 0) ? -phase_shift : phase_shift;
    if (phase_delta > 2500 || phase_slope > 8000 || phase_step > 512)
        turn_samples = 128;
    else if (abs_shift > 384)
        turn_samples = 160;
    else if (abs_shift > 192)
        turn_samples = 144;
    else if (abs_shift > 64)
        turn_samples = 112;

    if (turn_samples > out_samples)
        turn_samples = out_samples;

    const double start_pos = base_pos + (double)phase_shift;

    int step_max = 0;
    int step_avg = 0;
    int copied = perform_normal_turn_render_s16(audio_buf, out_samples,
                                                ctx_buf, ctx_samples,
                                                ctx_abs_start, &ctx_map,
                                                base_pos, start_pos, v0, v1,
                                                turn_samples,
                                                frame_bytes,
                                                &step_max, &step_avg);
    if (copied <= 0)
        return 0;

    int edge_delta = -1;
    if (sample_ptr->audio_diag_valid && sample_ptr->audio_diag_frame_bytes == frame_bytes) {
        edge_delta = vj_audio_frame_delta_s16(audio_buf,
                                              sample_ptr->audio_diag_last_frame,
                                              frame_bytes);
    }

    const int peak = vj_audio_peak_s16(audio_buf, copied, frame_bytes);
    const int declick_path = (turn_speed > 1) ? AUDIO_PATH_FAST : AUDIO_PATH_DIRECT;
    const int hard_turn_boundary =
        (edge_delta >= 3072 ||
         phase_slope >= 8192 ||
         phase_step >= 512 ||
         step_max >= 1024 ||
         peak >= 32760);

    if (hard_turn_boundary) {
        vj_audio_declick_apply(p, audio_buf, copied, frame_bytes,
                               declick_path, sample_ptr->speed, cur_dir,
                               AUDIO_EDGE_DIRECTION, 1);
    } else {
        vj_audio_declick_observe(p, audio_buf, copied, frame_bytes,
                                 declick_path, sample_ptr->speed, cur_dir);
    }


    sample_ptr->scratch_initialized = 0;
    sample_ptr->scratch_pos = 0.0;
    sample_ptr->scratch_vel = 0.0;
    sample_ptr->scratch_target_vel = 0.0;
    sample_ptr->scratch_last_dir = 0;
    sample_ptr->scratch_last_sfd = 0;
    sample_ptr->scratch_ramp_left = 0;
    sample_ptr->scratch_sync_bias = 0.0;
    sample_ptr->scratch_sync_hold_blocks = 0;
    sample_ptr->scratch_stable_blocks = 0;

    return copied;
}

static int perform_normal_playback(
    veejay_t *info,
    editlist *el,
    performer_t *p,
    uint8_t *audio_buf,
    uint8_t *temporary_buffer,
    int pred_len,
    int sample_size,
    long long cur_frame,
    sample_b_t *sample_ptr
) {
    if (el == NULL || p == NULL || audio_buf == NULL || temporary_buffer == NULL ||
        sample_ptr == NULL || pred_len <= 0 || sample_size <= 0)
        return 0;

    const int speed = sample_ptr->speed;
    const int frame_bytes = sample_size;
    const int abs_speed = abs(speed);
    const int cur_dir = (speed > 0) ? 1 : ((speed < 0) ? -1 : 0);
    audio_edge_t *edge = p->audio_edge;

    int pending_edge = AUDIO_EDGE_NONE;
    int last_dir = 0;

    if (edge != NULL) {
        pending_edge = atomic_load_int(&edge->pending_edge);
        last_dir = atomic_load_int(&edge->last_direction);
    }

    if (pending_edge == AUDIO_EDGE_SILENCE && cur_dir != 0)
        pending_edge = AUDIO_EDGE_JUMP;

    const int direction_flipped =
        (last_dir != 0 && cur_dir != 0 && last_dir != cur_dir);


#ifdef HAVE_JACK
    if (pending_edge == AUDIO_EDGE_RESET && cur_dir != 0) {
        vj_perform_audio_source_transition_force_fade_blocks(pred_len,
                                                             frame_bytes,
                                                             VJ_AUDIO_RESET_FADE_BLOCKS,
                                                             "media-reset-edge",
                                                             pending_edge,
                                                             cur_frame,
                                                             "normal");
    }
#endif

    if (speed == 0 || pending_edge == AUDIO_EDGE_SILENCE) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);

        vj_audio_declick_apply(p, audio_buf, pred_len, frame_bytes,
                            AUDIO_PATH_SILENCE, 0, 0,
                            pending_edge, direction_flipped);

        vj_perform_clear_audio_edges(info, edge, 0);
        sample_ptr->direction_changed = 0;
        sample_ptr->prev_n_samples = pred_len;
        return pred_len;
    }

    if (abs_speed <= 1) {
        if (vj_audio_edge_is_hard(pending_edge) && p->audio_scratcher != NULL)
            vj_scratch_reset(p->audio_scratcher);

        const int stale_direction_edge =
            (pending_edge == AUDIO_EDGE_DIRECTION && !direction_flipped);
        const int effective_edge = stale_direction_edge ? AUDIO_EDGE_NONE : pending_edge;
        const int normal_turn = direction_flipped &&
            (pending_edge == AUDIO_EDGE_NONE || pending_edge == AUDIO_EDGE_DIRECTION);

        int n_samples = 0;
        int guarded_turn_skipped = 0;
        if (normal_turn) {
            n_samples = perform_normal_direction_turn(info, el, p,
                                                      audio_buf,
                                                      temporary_buffer,
                                                      pred_len,
                                                      frame_bytes,
                                                      cur_frame,
                                                      sample_ptr,
                                                      cur_dir,
                                                      last_dir,
                                                      pending_edge);
            guarded_turn_skipped = (n_samples <= 0);
        }

        if (n_samples <= 0) {
            n_samples = get_audio_frame_safe(info, el, cur_frame,
                                             audio_buf, pred_len,
                                             frame_bytes, speed);

            if (speed < 0)
                vj_audio_reverse_buffer(audio_buf, n_samples, frame_bytes);

            if (guarded_turn_skipped) {
                vj_audio_declick_apply(p, audio_buf, n_samples, frame_bytes,
                                       AUDIO_PATH_DIRECT, speed, cur_dir,
                                       AUDIO_EDGE_DIRECTION, 1);


            } else {
                vj_audio_declick_apply(p, audio_buf, n_samples, frame_bytes,
                                    AUDIO_PATH_DIRECT, speed, cur_dir,
                                    effective_edge, 0);
            }
        }

        slow_motion_update_turn_history(sample_ptr, audio_buf, n_samples, frame_bytes);

        if (sample_ptr->audio_diag_valid &&
            sample_ptr->audio_diag_frame_bytes == frame_bytes) {
            vj_audio_copy_last_frame(sample_ptr->audio_diag_prev_frame,
                                     (int)sizeof(sample_ptr->audio_diag_prev_frame),
                                     sample_ptr->audio_diag_last_frame, 1, frame_bytes);
        } else {
            veejay_memset(sample_ptr->audio_diag_prev_frame, 0,
                          sizeof(sample_ptr->audio_diag_prev_frame));
        }

        vj_audio_copy_last_frame(sample_ptr->audio_diag_last_frame,
                                 (int)sizeof(sample_ptr->audio_diag_last_frame),
                                 audio_buf, n_samples, frame_bytes);
        sample_ptr->audio_diag_valid = 1;
        sample_ptr->audio_diag_frame_bytes = frame_bytes;

        vj_perform_clear_audio_edges(info, edge, cur_dir);

        sample_ptr->direction_changed = 0;
        sample_ptr->prev_n_samples = n_samples;
        return n_samples;
    }

    if (vj_audio_edge_is_hard(pending_edge) && p->audio_scratcher != NULL)
        vj_scratch_reset(p->audio_scratcher);

    int n_frames = abs_speed;
    n_frames = (n_frames >= MAX_SPEED) ? (MAX_SPEED - 1) : n_frames;
    n_frames = (n_frames < 1) ? 1 : n_frames;

    const int fast_turn = direction_flipped &&
        (pending_edge == AUDIO_EDGE_NONE || pending_edge == AUDIO_EDGE_DIRECTION);

    if (fast_turn) {
        int turn_out = perform_normal_direction_turn(info, el, p,
                                                     audio_buf,
                                                     temporary_buffer,
                                                     pred_len,
                                                     frame_bytes,
                                                     cur_frame,
                                                     sample_ptr,
                                                     cur_dir,
                                                     last_dir,
                                                     pending_edge);
        if (turn_out > 0) {
            slow_motion_update_turn_history(sample_ptr, audio_buf, turn_out, frame_bytes);
            if (sample_ptr->audio_diag_valid &&
                sample_ptr->audio_diag_frame_bytes == frame_bytes) {
                vj_audio_copy_last_frame(sample_ptr->audio_diag_prev_frame,
                                         (int)sizeof(sample_ptr->audio_diag_prev_frame),
                                         sample_ptr->audio_diag_last_frame, 1, frame_bytes);
            } else {
                veejay_memset(sample_ptr->audio_diag_prev_frame, 0,
                              sizeof(sample_ptr->audio_diag_prev_frame));
            }
            vj_audio_copy_last_frame(sample_ptr->audio_diag_last_frame,
                                     (int)sizeof(sample_ptr->audio_diag_last_frame),
                                     audio_buf, turn_out, frame_bytes);
            sample_ptr->audio_diag_valid = 1;
            sample_ptr->audio_diag_frame_bytes = frame_bytes;
            vj_perform_clear_audio_edges(info, edge, cur_dir);
            sample_ptr->direction_changed = 0;
            sample_ptr->prev_n_samples = turn_out;
            return turn_out;
        }


    }

    uint8_t *tmp_ptr = temporary_buffer;
    int total_input_samples = 0;

    for (int i = 0; i < n_frames; i++) {
        long long f = (speed < 0)
            ? (cur_frame - (long long)(n_frames - 1 - i))
            : (cur_frame + (long long)i);

        int got = get_audio_frame_safe(info, el, f, tmp_ptr,
                                       pred_len, frame_bytes, speed);

        total_input_samples += got;
        tmp_ptr += (size_t)got * (size_t)frame_bytes;
    }

    int out = vj_audio_resample_block_s16(
        audio_buf,
        pred_len,
        temporary_buffer,
        total_input_samples,
        (double)speed,
        frame_bytes
    );

    if (fast_turn) {
        vj_audio_declick_apply(p, audio_buf, out, frame_bytes,
                               AUDIO_PATH_FAST, speed, cur_dir,
                               AUDIO_EDGE_DIRECTION, 1);
    } else {
        vj_audio_declick_apply(p, audio_buf, out, frame_bytes,
                               AUDIO_PATH_FAST, speed, cur_dir,
                               pending_edge, direction_flipped);
    }

    slow_motion_update_turn_history(sample_ptr, audio_buf, out, frame_bytes);
    if (sample_ptr->audio_diag_valid &&
        sample_ptr->audio_diag_frame_bytes == frame_bytes) {
        vj_audio_copy_last_frame(sample_ptr->audio_diag_prev_frame,
                                 (int)sizeof(sample_ptr->audio_diag_prev_frame),
                                 sample_ptr->audio_diag_last_frame, 1, frame_bytes);
    } else {
        veejay_memset(sample_ptr->audio_diag_prev_frame, 0,
                      sizeof(sample_ptr->audio_diag_prev_frame));
    }
    vj_audio_copy_last_frame(sample_ptr->audio_diag_last_frame,
                             (int)sizeof(sample_ptr->audio_diag_last_frame),
                             audio_buf, out, frame_bytes);
    sample_ptr->audio_diag_valid = 1;
    sample_ptr->audio_diag_frame_bytes = frame_bytes;

    vj_perform_clear_audio_edges(info, edge, cur_dir);

    sample_ptr->direction_changed = 0;
    sample_ptr->prev_n_samples = out;
    return out;
}

static int slow_motion_turn_history_capacity(int frame_bytes)
{
    if (frame_bytes <= 0)
        return 0;

    int cap = AUDIO_TURN_HISTORY_BYTES / frame_bytes;
    if (cap < 0)
        cap = 0;
    if (cap > 1024)
        cap = 1024;
    return cap;
}

static void slow_motion_clear_turn_history(sample_b_t *posdata)
{
    if (posdata == NULL)
        return;

    posdata->audio_turn_history_samples = 0;
    posdata->audio_turn_history_frame_bytes = 0;
}

static void slow_motion_update_turn_history(sample_b_t *posdata,
                                            const uint8_t *buf,
                                            int samples,
                                            int frame_bytes)
{
    if (posdata == NULL || buf == NULL || samples <= 0 || frame_bytes <= 0)
        return;

    const int cap = slow_motion_turn_history_capacity(frame_bytes);
    if (cap <= 0)
        return;

    if (posdata->audio_turn_history_frame_bytes != frame_bytes) {
        posdata->audio_turn_history_samples = 0;
        posdata->audio_turn_history_frame_bytes = frame_bytes;
    }

    int keep_from_new = samples;
    if (keep_from_new > cap)
        keep_from_new = cap;

    const uint8_t *src = buf + ((size_t)(samples - keep_from_new) * (size_t)frame_bytes);

    if (keep_from_new >= cap) {
        veejay_memcpy(posdata->audio_turn_history, src,
                      (size_t)cap * (size_t)frame_bytes);
        posdata->audio_turn_history_samples = cap;
        return;
    }

    int old_keep = posdata->audio_turn_history_samples;
    if (old_keep < 0)
        old_keep = 0;
    if (old_keep > cap)
        old_keep = cap;

    if (old_keep + keep_from_new > cap)
        old_keep = cap - keep_from_new;

    if (old_keep > 0) {
        memmove(posdata->audio_turn_history,
                posdata->audio_turn_history +
                    ((size_t)(posdata->audio_turn_history_samples - old_keep) * (size_t)frame_bytes),
                (size_t)old_keep * (size_t)frame_bytes);
    }

    veejay_memcpy(posdata->audio_turn_history + ((size_t)old_keep * (size_t)frame_bytes),
                  src,
                  (size_t)keep_from_new * (size_t)frame_bytes);
    posdata->audio_turn_history_samples = old_keep + keep_from_new;
    posdata->audio_turn_history_frame_bytes = frame_bytes;
}

static inline double slow_motion_clampd(double v, double lo, double hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline double slow_motion_exact_samples_per_frame(const editlist *el, int pred_len)
{
    if (el == NULL || el->audio_rate <= 0 || el->video_fps <= 0.0)
        return (double)((pred_len > 0) ? pred_len : 1);

    return (double)el->audio_rate / (double)el->video_fps;
}

static inline double slow_motion_frame_abs_start_exact(const editlist *el,
                                                       long long frame,
                                                       int pred_len)
{
    if (frame <= 0)
        return 0.0;

    const double spf = slow_motion_exact_samples_per_frame(el, pred_len);
    return floor(((double)frame * spf) + 1.0e-9);
}

static inline int slow_motion_frame_len_exact(const editlist *el,
                                              long long frame,
                                              int pred_len)
{
    const double a = slow_motion_frame_abs_start_exact(el, frame, pred_len);
    const double b = slow_motion_frame_abs_start_exact(el, frame + 1, pred_len);
    int n = (int)(b - a);

    if (n <= 0)
        n = (pred_len > 0) ? pred_len : 1;
    return n;
}

static inline long long slow_motion_frame_for_abs_pos_exact(const editlist *el,
                                                            double pos,
                                                            int pred_len)
{
    if (pos <= 0.0)
        return 0;

    const double spf = slow_motion_exact_samples_per_frame(el, pred_len);
    if (spf <= 0.0)
        return (long long)floor(pos / (double)((pred_len > 0) ? pred_len : 1));

    long long f = (long long)floor(pos / spf);
    if (f < 0)
        f = 0;
    return f;
}

static inline int slow_motion_has_valid_frame_bounds(const sample_b_t *posdata)
{
    return (posdata != NULL && posdata->end > posdata->start);
}

static inline int16_t slow_motion_cubic_interp_s16(int p0, int p1, int p2, int p3, double t)
{
    double t2 = t * t;
    double t3 = t2 * t;
    double y = 0.5 * ((2.0 * (double)p1) +
        ((double)(-p0 + p2) * t) +
        ((double)(2 * p0 - 5 * p1 + 4 * p2 - p3) * t2) +
        ((double)(-p0 + 3 * p1 - 3 * p2 + p3) * t3));

    int yi = (int)((y >= 0.0) ? (y + 0.5) : (y - 0.5));
    if (yi > 32767)
        yi = 32767;
    else if (yi < -32768)
        yi = -32768;
    return (int16_t)yi;
}

static void slow_motion_clear_scratch_head(sample_b_t *posdata)
{
    if (posdata == NULL)
        return;

    posdata->scratch_initialized = 0;
    posdata->scratch_pos = 0.0;
    posdata->scratch_vel = 0.0;
    posdata->scratch_target_vel = 0.0;
    posdata->scratch_last_sync_pos = 0.0;
    posdata->scratch_last_sync_error = 0.0;
    posdata->scratch_sync_bias = 0.0;
    posdata->scratch_sync_hold_blocks = 0;
    posdata->scratch_stable_blocks = 0;
    posdata->scratch_last_dir = 0;
    posdata->scratch_last_sfd = 0;
    posdata->scratch_ramp_left = 0;
    posdata->scratch_last_reset = 0;
}

static double slow_motion_sync_abs_pos(const editlist *el,
                                       const sample_b_t *posdata,
                                       long long source_frame,
                                       int pred_len,
                                       int slice_count,
                                       int cur_slice,
                                       int direction)
{
    if (pred_len <= 0)
        return 0.0;

    if (slice_count <= 1)
        slice_count = 1;
    cur_slice = vj_audio_clampi(cur_slice, 0, slice_count - 1);

    const double frame_start = slow_motion_frame_abs_start_exact(el, source_frame, pred_len);
    const int frame_samples = slow_motion_frame_len_exact(el, source_frame, pred_len);
    const double slice_phase = ((double)cur_slice * (double)frame_samples) / (double)slice_count;

    double p = (direction >= 0)
        ? (frame_start + slice_phase)
        : (frame_start + (double)(frame_samples - 1) - slice_phase);

    if (slow_motion_has_valid_frame_bounds(posdata)) {
        const double lo = slow_motion_frame_abs_start_exact(el, posdata->start, pred_len);
        const double hi = slow_motion_frame_abs_start_exact(el, posdata->end + 1, pred_len) - 1.0;
        p = slow_motion_clampd(p, lo, hi);
    }

    return p;
}

static int slow_motion_fetch_scratch_context(veejay_t *info,
                                             editlist *el,
                                             sample_b_t *posdata,
                                             uint8_t *ctx,
                                             slow_scratch_ctx_map_t *map,
                                             int pred_len,
                                             int frame_bytes,
                                             int speed_int,
                                             double pos_a,
                                             double pos_b,
                                             double *ctx_abs_start_out,
                                             int *ctx_samples_out,
                                             long long *ctx_first_frame_out,
                                             long long *ctx_last_frame_out)
{
    if (ctx_abs_start_out != NULL)
        *ctx_abs_start_out = 0.0;
    if (ctx_samples_out != NULL)
        *ctx_samples_out = 0;
    if (ctx_first_frame_out != NULL)
        *ctx_first_frame_out = 0;
    if (ctx_last_frame_out != NULL)
        *ctx_last_frame_out = 0;

    if (map != NULL)
        veejay_memset(map, 0, sizeof(*map));

    if (info == NULL || el == NULL || posdata == NULL || ctx == NULL ||
        pred_len <= 0 || frame_bytes <= 0)
        return 0;

    double minp = (pos_a < pos_b) ? pos_a : pos_b;
    double maxp = (pos_a > pos_b) ? pos_a : pos_b;

    long long first = slow_motion_frame_for_abs_pos_exact(el, minp, pred_len) - 3;
    long long last  = slow_motion_frame_for_abs_pos_exact(el, maxp, pred_len) + 3;

    if (slow_motion_has_valid_frame_bounds(posdata)) {
        if (first < posdata->start)
            first = posdata->start;
        if (last > posdata->end)
            last = posdata->end;
    }

    if (first < 0)
        first = 0;
    if (last < first)
        last = first;

    const int max_frames = SLOW_SCRATCH_MAX_CTX_FRAMES;
    if ((last - first + 1) > max_frames) {
        long long center = slow_motion_frame_for_abs_pos_exact(el, (pos_a + pos_b) * 0.5, pred_len);
        first = center - (max_frames / 2);
        last = first + max_frames - 1;
        if (first < 0) {
            first = 0;
            last = first + max_frames - 1;
        }
        if (slow_motion_has_valid_frame_bounds(posdata)) {
            if (first < posdata->start) {
                first = posdata->start;
                last = first + max_frames - 1;
            }
            if (last > posdata->end) {
                last = posdata->end;
                first = last - max_frames + 1;
                if (first < posdata->start)
                    first = posdata->start;
            }
        }
    }

    const int frames = (int)(last - first + 1);
    if (frames <= 0)
        return 0;

    const int scratch_capacity_samples = (512 * 1024) / frame_bytes;
    const int slot_capacity = pred_len + 64;
    const int context_capacity_samples = frames * slot_capacity;

    if (context_capacity_samples > scratch_capacity_samples) {


        return 0;
    }

    uint8_t *dst = ctx;
    int total_samples = 0;
    int min_got = 0;
    int max_got = 0;
    int over_frames = 0;

    for (int i = 0; i < frames; i++) {
        long long f = first + (long long)i;

        veejay_memset(dst, 0, (size_t)slot_capacity * (size_t)frame_bytes);

        int got = get_audio_frame_safe(info, el, f, dst, pred_len, frame_bytes, speed_int);
        if (got < 0)
            got = 0;

        if (got > slot_capacity) {
            got = slot_capacity;
            over_frames++;
        }

        if (map != NULL && i < SLOW_SCRATCH_MAX_CTX_FRAMES) {
            map->frame_len[i] = got;
            map->frame_off[i] = total_samples;
            map->exact_start[i] = slow_motion_frame_abs_start_exact(el, f, pred_len);
            map->exact_len[i] = (double)slow_motion_frame_len_exact(el, f, pred_len);
            if (map->exact_len[i] <= 0.0)
                map->exact_len[i] = (double)((got > 0) ? got : pred_len);
        }

        if (i == 0 || got < min_got)
            min_got = got;
        if (i == 0 || got > max_got)
            max_got = got;

        dst += (size_t)got * (size_t)frame_bytes;
        total_samples += got;
    }

    const double exact_start = slow_motion_frame_abs_start_exact(el, first, pred_len);

    if (map != NULL) {
        map->valid = 1;
        map->frames = frames;
        map->first_frame = first;
        map->last_frame = last;
    }


    if (ctx_abs_start_out != NULL)
        *ctx_abs_start_out = exact_start;
    if (ctx_samples_out != NULL)
        *ctx_samples_out = total_samples;
    if (ctx_first_frame_out != NULL)
        *ctx_first_frame_out = first;
    if (ctx_last_frame_out != NULL)
        *ctx_last_frame_out = last;

    return total_samples;
}

static double slow_motion_ctx_exact_to_actual_rel(const slow_scratch_ctx_map_t *map,
                                                  double abs_pos,
                                                  int ctx_samples)
{
    if (map == NULL || !map->valid || map->frames <= 0 || ctx_samples <= 0)
        return abs_pos;

    int best = -1;
    for (int i = 0; i < map->frames && i < SLOW_SCRATCH_MAX_CTX_FRAMES; i++) {
        const double a = map->exact_start[i];
        const double b = a + map->exact_len[i];
        if (abs_pos >= a && abs_pos < b) {
            best = i;
            break;
        }
    }

    if (best < 0) {
        if (abs_pos < map->exact_start[0])
            return 0.0;
        best = map->frames - 1;
        if (best >= SLOW_SCRATCH_MAX_CTX_FRAMES)
            best = SLOW_SCRATCH_MAX_CTX_FRAMES - 1;
    }

    const double exact_len = (map->exact_len[best] > 0.0) ? map->exact_len[best] : 1.0;
    double phase = (abs_pos - map->exact_start[best]) / exact_len;
    phase = slow_motion_clampd(phase, 0.0, 0.999999);

    int got = map->frame_len[best];
    if (got <= 0)
        got = 1;

    double rel = (double)map->frame_off[best] + phase * (double)got;
    if (rel < 0.0)
        rel = 0.0;
    else if (rel > (double)(ctx_samples - 1))
        rel = (double)(ctx_samples - 1);
    return rel;
}

static int slow_motion_render_scratch_head_s16(uint8_t *dst,
                                               int dst_samples,
                                               const uint8_t *ctx,
                                               int ctx_samples,
                                               double ctx_abs_start,
                                               const slow_scratch_ctx_map_t *ctx_map,
                                               sample_b_t *posdata,
                                               double target_vel,
                                               double sync_start_pos,
                                               int frame_bytes,
                                               int pred_len,
                                               int edge_transition,
                                               int *step_max_out,
                                               int *step_avg_out,
                                               double *pos_start_out,
                                               double *pos_end_out,
                                               double *vel_start_out,
                                               double *vel_end_out)
{
    if (step_max_out != NULL)
        *step_max_out = 0;
    if (step_avg_out != NULL)
        *step_avg_out = 0;
    if (pos_start_out != NULL)
        *pos_start_out = (posdata != NULL) ? posdata->scratch_pos : 0.0;
    if (pos_end_out != NULL)
        *pos_end_out = (posdata != NULL) ? posdata->scratch_pos : 0.0;
    if (vel_start_out != NULL)
        *vel_start_out = (posdata != NULL) ? posdata->scratch_vel : 0.0;
    if (vel_end_out != NULL)
        *vel_end_out = (posdata != NULL) ? posdata->scratch_vel : 0.0;

    if (dst == NULL || ctx == NULL || posdata == NULL || dst_samples <= 0 ||
        ctx_samples <= 0 || frame_bytes <= 0 || (frame_bytes & 1))
        return 0;

    const int words = frame_bytes / 2;
    const int16_t *in = (const int16_t*)ctx;
    int16_t *out = (int16_t*)dst;
    const int max_index = ctx_samples - 1;

    double head = posdata->scratch_pos;
    double vel = posdata->scratch_vel;
    const double start_head = head;
    const double start_vel = vel;

    const int turn_time = edge_transition ? 1536 : 384;
    const double vel_alpha_edge = 1.0 / (double)turn_time;
    const double vel_alpha_normal = 1.0 / 256.0;

    const double block_sync_error = sync_start_pos - head;
    (void)block_sync_error;

    int step_peak = 0;
    int64_t step_sum = 0;
    int step_n = 0;
    int16_t prev_words[8];
    int prev_valid = 0;
    const int local_words = (words > 8) ? 8 : words;

    for (int i = 0; i < dst_samples; i++) {
        const double wanted = target_vel;
        const double a = (posdata->scratch_ramp_left > 0) ? vel_alpha_edge : vel_alpha_normal;

        vel += (wanted - vel) * a;
        if (posdata->scratch_ramp_left > 0)
            posdata->scratch_ramp_left--;

        double rel = (ctx_map != NULL && ctx_map->valid)
            ? slow_motion_ctx_exact_to_actual_rel(ctx_map, head, ctx_samples)
            : (head - ctx_abs_start);
        if (rel < 0.0)
            rel = 0.0;
        else if (rel > (double)max_index)
            rel = (double)max_index;

        int idx = (int)floor(rel);
        double frac = rel - (double)idx;
        if (idx < 0) {
            idx = 0;
            frac = 0.0;
        } else if (idx > max_index) {
            idx = max_index;
            frac = 0.0;
        }

        int i0 = idx - 1;
        int i1 = idx;
        int i2 = idx + 1;
        int i3 = idx + 2;
        if (i0 < 0) i0 = 0;
        if (i2 > max_index) i2 = max_index;
        if (i3 > max_index) i3 = max_index;

        const int b0 = i0 * words;
        const int b1 = i1 * words;
        const int b2 = i2 * words;
        const int b3 = i3 * words;
        const int bo = i * words;

        int frame_step = 0;
        for (int c = 0; c < words; c++) {
            int16_t s = slow_motion_cubic_interp_s16(
                in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac);
            out[bo + c] = s;
            if (c < local_words) {
                if (prev_valid) {
                    int d = (int)s - (int)prev_words[c];
                    d = (d < 0) ? -d : d;
                    if (d > frame_step)
                        frame_step = d;
                }
                prev_words[c] = s;
            }
        }
        if (prev_valid) {
            if (frame_step > step_peak)
                step_peak = frame_step;
            step_sum += frame_step;
            step_n++;
        }
        prev_valid = 1;

        head += vel;
    }

    posdata->scratch_pos = head;
    posdata->scratch_vel = vel;
    posdata->scratch_target_vel = target_vel;
    posdata->scratch_last_sync_pos = sync_start_pos + target_vel * (double)dst_samples;
    posdata->scratch_last_sync_error = posdata->scratch_last_sync_pos - head;

    if (step_max_out != NULL)
        *step_max_out = step_peak;
    if (step_avg_out != NULL)
        *step_avg_out = (step_n > 0) ? (int)(step_sum / step_n) : 0;
    if (pos_start_out != NULL)
        *pos_start_out = start_head;
    if (pos_end_out != NULL)
        *pos_end_out = head;
    if (vel_start_out != NULL)
        *vel_start_out = start_vel;
    if (vel_end_out != NULL)
        *vel_end_out = vel;

    return dst_samples;
}

int perform_slow_motion(
    veejay_t *info,
    editlist *el,
    performer_t *p,
    uint8_t *audio_buf,
    uint8_t *downsample_buffer,
    int *sampled_down,
    long long target_frame,
    sample_b_t *posdata
) {
    (void)sampled_down;

    if (el == NULL || p == NULL || audio_buf == NULL || downsample_buffer == NULL ||
        posdata == NULL)
        return 0;

    const int frame_bytes = el->audio_bps;
    const int pred_len = el->audio_rate / el->video_fps;
    audio_edge_t *edge = p->audio_edge;

    if (frame_bytes <= 0 || pred_len <= 0)
        return 0;

    int cur_dir = (posdata->speed > 0) ? 1 : ((posdata->speed < 0) ? -1 : 0);

    int pending_edge = AUDIO_EDGE_NONE;
    int last_dir = 0;
    int direction_flipped = 0;

    if (edge != NULL) {
        last_dir = atomic_load_int(&edge->last_direction);
        pending_edge = atomic_load_int(&edge->pending_edge);
        direction_flipped = (last_dir != 0 && cur_dir != 0 && cur_dir != last_dir);
    }

    if (pending_edge == AUDIO_EDGE_SILENCE && cur_dir != 0)
        pending_edge = AUDIO_EDGE_JUMP;

#ifdef HAVE_JACK
    if (pending_edge == AUDIO_EDGE_RESET && cur_dir != 0) {
        vj_perform_audio_source_transition_force_fade_blocks(pred_len,
                                                             frame_bytes,
                                                             VJ_AUDIO_RESET_FADE_BLOCKS,
                                                             "media-reset-edge",
                                                             pending_edge,
                                                             target_frame,
                                                             "slow-motion");
    }
#endif

    if (cur_dir == 0 || pending_edge == AUDIO_EDGE_SILENCE) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);
        vj_audio_declick_apply(p, audio_buf, pred_len, frame_bytes,
                               AUDIO_PATH_SILENCE, 0, 0,
                               pending_edge, direction_flipped);
        vj_perform_clear_audio_edges(info, edge, 0);
        slow_motion_clear_scratch_head(posdata);
        posdata->prev_n_samples = pred_len;
        return pred_len;
    }

    int slice_count = posdata->max_sfd;
    slice_count = (slice_count <= 1) ? 2 : slice_count;
    if (slice_count > MAX_SPEED_AV)
        slice_count = MAX_SPEED_AV;

    int cur_slice = posdata->cur_sfd;
    cur_slice = (cur_slice < 0) ? 0 : cur_slice;
    cur_slice = (cur_slice >= slice_count) ? (slice_count - 1) : cur_slice;

    const int hard_edge = (pending_edge != AUDIO_EDGE_NONE &&
                           pending_edge != AUDIO_EDGE_DIRECTION);
    const int edge_transition = (pending_edge == AUDIO_EDGE_DIRECTION || direction_flipped);

    int speed_mag = abs(posdata->speed);
    if (speed_mag < 1)
        speed_mag = 1;
    if (speed_mag > MAX_SPEED_AV)
        speed_mag = MAX_SPEED_AV;

    const int target_frame_samples = slow_motion_frame_len_exact(el, target_frame, pred_len);
    const double nominal_target_vel = ((double)cur_dir * (double)speed_mag * (double)target_frame_samples) /
                                      ((double)slice_count * (double)pred_len);
    const double sync_pos = slow_motion_sync_abs_pos(el, posdata, target_frame, pred_len,
                                                     slice_count, cur_slice, cur_dir);

    double target_vel = nominal_target_vel;
    double sync_bias = 0.0;
    double sync_bias_raw = 0.0;

    int reset_head = 0;
    if (!posdata->scratch_initialized) {
        reset_head = 1;
    }
    if (hard_edge)
        reset_head = 1;


    if (reset_head) {
        posdata->scratch_initialized = 1;
        posdata->scratch_pos = sync_pos;
        posdata->scratch_vel = target_vel;
        posdata->scratch_target_vel = target_vel;
        posdata->scratch_last_dir = cur_dir;
        posdata->scratch_last_sfd = slice_count;
        posdata->scratch_ramp_left = 0;
        posdata->scratch_last_reset = 1;
        posdata->scratch_sync_bias = 0.0;
        posdata->scratch_sync_hold_blocks = 0;
        posdata->scratch_stable_blocks = 0;
        slow_motion_clear_turn_history(posdata);
    } else {
        posdata->scratch_last_reset = 0;
        if (edge_transition || posdata->scratch_last_dir != cur_dir ||
            posdata->scratch_last_sfd != slice_count) {
            posdata->scratch_ramp_left = pred_len;
            if (posdata->scratch_ramp_left < 512)
                posdata->scratch_ramp_left = 512;
            if (posdata->scratch_ramp_left > 2048)
                posdata->scratch_ramp_left = 2048;

            if (edge_transition) {
                posdata->scratch_sync_hold_blocks = (slice_count >= 8) ? 12 : 8;
                posdata->scratch_stable_blocks = 0;
                posdata->scratch_sync_bias = 0.0;
            }
        }
        posdata->scratch_target_vel = target_vel;
        posdata->scratch_last_dir = cur_dir;
        posdata->scratch_last_sfd = slice_count;
    }

    if (!reset_head && !hard_edge) {
        const double sync_err_now = sync_pos - posdata->scratch_pos;
        const double abs_nominal = fabs(nominal_target_vel);
        const int hold_active = (edge_transition || posdata->scratch_sync_hold_blocks > 0);
        const int stable_needed = (slice_count >= 8) ? 10 : 6;

        if (hold_active) {
            if (!edge_transition && posdata->scratch_sync_hold_blocks > 0)
                posdata->scratch_sync_hold_blocks--;

            posdata->scratch_stable_blocks = 0;
            posdata->scratch_sync_bias *= 0.50;
            if (fabs(posdata->scratch_sync_bias) < 0.000001)
                posdata->scratch_sync_bias = 0.0;

            sync_bias = 0.0;
            sync_bias_raw = 0.0;
            target_vel = nominal_target_vel;
        } else {
            if (posdata->scratch_stable_blocks < 1000000)
                posdata->scratch_stable_blocks++;

            if (posdata->scratch_stable_blocks < stable_needed) {
                posdata->scratch_sync_bias *= 0.75;
                if (fabs(posdata->scratch_sync_bias) < 0.000001)
                    posdata->scratch_sync_bias = 0.0;

                sync_bias = 0.0;
                sync_bias_raw = 0.0;
                target_vel = nominal_target_vel;
            } else {
                const double abs_err = fabs(sync_err_now);
                double leash_den = (double)pred_len * 640.0;
                double leash_smooth = 0.03125;
                double max_bias = abs_nominal * 0.006;

                if (abs_err > (double)(pred_len * 2)) {
                    leash_den = (double)pred_len * 384.0;
                    leash_smooth = 0.0625;
                    max_bias = abs_nominal * 0.014;
                    if (max_bias < 0.00035)
                        max_bias = 0.00035;
                    if (max_bias > 0.0070)
                        max_bias = 0.0070;
                } else if (abs_err > (double)((pred_len * 3) / 4)) {
                    leash_den = (double)pred_len * 512.0;
                    leash_smooth = 0.046875;
                    max_bias = abs_nominal * 0.010;
                    if (max_bias < 0.00025)
                        max_bias = 0.00025;
                    if (max_bias > 0.0050)
                        max_bias = 0.0050;
                } else {
                    if (max_bias < 0.00015)
                        max_bias = 0.00015;
                    if (max_bias > 0.0030)
                        max_bias = 0.0030;
                }

                sync_bias_raw = sync_err_now / leash_den;
                sync_bias_raw = slow_motion_clampd(sync_bias_raw, -max_bias, max_bias);

                posdata->scratch_sync_bias +=
                    (sync_bias_raw - posdata->scratch_sync_bias) * leash_smooth;
                sync_bias = posdata->scratch_sync_bias;
                target_vel = nominal_target_vel + sync_bias;
            }
        }
    } else {
        posdata->scratch_sync_bias = 0.0;
        posdata->scratch_sync_hold_blocks = 0;
        posdata->scratch_stable_blocks = 0;
        sync_bias = 0.0;
        sync_bias_raw = 0.0;
        target_vel = nominal_target_vel;
    }

    const double predicted_end = posdata->scratch_pos +
        (posdata->scratch_vel * (double)pred_len) +
        (target_vel * (double)pred_len);

    double ctx_abs_start = 0.0;
    int ctx_samples = 0;
    long long ctx_first = 0;
    long long ctx_last = 0;
    slow_scratch_ctx_map_t ctx_map;
    veejay_memset(&ctx_map, 0, sizeof(ctx_map));

    int got_ctx = slow_motion_fetch_scratch_context(info, el, posdata,
                                                    downsample_buffer,
                                                    &ctx_map,
                                                    pred_len, frame_bytes,
                                                    posdata->speed,
                                                    posdata->scratch_pos,
                                                    predicted_end,
                                                    &ctx_abs_start,
                                                    &ctx_samples,
                                                    &ctx_first,
                                                    &ctx_last);

    if (got_ctx <= 0) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);
        vj_audio_declick_apply(p, audio_buf, pred_len, frame_bytes,
                               AUDIO_PATH_SILENCE, 0, 0,
                               pending_edge, direction_flipped);
        vj_perform_clear_audio_edges(info, edge, cur_dir);
        posdata->prev_n_samples = pred_len;
        return pred_len;
    }

    double pos0 = posdata->scratch_pos;
    double pos1 = posdata->scratch_pos;
    double vel0 = posdata->scratch_vel;
    double vel1 = posdata->scratch_vel;
    int step_max = 0;
    int step_avg = 0;

    int copied = slow_motion_render_scratch_head_s16(audio_buf,
                                                     pred_len,
                                                     downsample_buffer,
                                                     ctx_samples,
                                                     ctx_abs_start,
                                                     &ctx_map,
                                                     posdata,
                                                     target_vel,
                                                     sync_pos,
                                                     frame_bytes,
                                                     pred_len,
                                                     edge_transition,
                                                     &step_max,
                                                     &step_avg,
                                                     &pos0,
                                                     &pos1,
                                                     &vel0,
                                                     &vel1);
    if (copied <= 0) {
        veejay_memset(audio_buf, 0, pred_len * frame_bytes);
        copied = pred_len;
    }


    if (hard_edge) {
        vj_audio_declick_apply(p, audio_buf, copied, frame_bytes,
                               AUDIO_PATH_SLOW, posdata->speed, cur_dir,
                               pending_edge, direction_flipped);
    } else {
        vj_audio_declick_observe(p, audio_buf, copied, frame_bytes,
                                 AUDIO_PATH_SLOW, posdata->speed, cur_dir);


    }

    if (posdata->audio_diag_valid && posdata->audio_diag_frame_bytes == frame_bytes) {
        vj_audio_copy_last_frame(posdata->audio_diag_prev_frame,
                                 (int)sizeof(posdata->audio_diag_prev_frame),
                                 posdata->audio_diag_last_frame, 1, frame_bytes);
    } else {
        veejay_memset(posdata->audio_diag_prev_frame, 0,
                      sizeof(posdata->audio_diag_prev_frame));
    }

    vj_audio_copy_last_frame(posdata->audio_diag_last_frame,
                             (int)sizeof(posdata->audio_diag_last_frame),
                             audio_buf, copied, frame_bytes);
    posdata->audio_diag_valid = 1;
    posdata->audio_diag_frame_bytes = frame_bytes;

    slow_motion_update_turn_history(posdata, audio_buf, copied, frame_bytes);

    posdata->audio_last_stretched_samples = pred_len * slice_count;
    posdata->audio_total_samples = ctx_samples;
    posdata->audio_src_offset = (int)((ctx_map.valid)
        ? slow_motion_ctx_exact_to_actual_rel(&ctx_map, posdata->scratch_pos, ctx_samples)
        : (posdata->scratch_pos - ctx_abs_start));
    posdata->last_resampled_frame = target_frame;
    posdata->last_resampled_dir = cur_dir;
    posdata->consumed_samples = cur_slice * pred_len;

    if (edge != NULL)
        edge->ticks_since_last_flip++;

    vj_perform_clear_audio_edges(info, edge, cur_dir);

    posdata->direction_changed = 0;
    posdata->prev_n_samples = copied;
    return copied;
}

int vj_perform_fill_audio_buffers(
    veejay_t *info,
    editlist *el,
    uint8_t *audio_buf,
    performer_t *p,
    int *sampled_down,
    long long target_frame
) {
    video_playback_setup *settings = info->settings;
    uint8_t *temporary_buffer = p->audio_render_buffer;
    uint8_t *downsample_buffer = p->down_sample_buffer;
    performer_global_t *g = (performer_global_t*) info->performer;

    sample_b_t *sample_ptr = (p == g->A) ? &(g->A->sample_a) : &(g->A->sample_b);

    sample_ptr->audio_last_stretched_samples = settings->audio_last_stretched_samples;
    sample_ptr->direction_changed = atomic_load_int(&settings->audio_direction_changed);
    sample_ptr->max_sfd = atomic_load_int(&settings->audio_slice_len);
    sample_ptr->cur_sfd = atomic_load_int(&settings->audio_slice);
    sample_ptr->speed = settings->current_playback_speed;

    int num_samples = (el->audio_rate / el->video_fps);
    int result = 0;

    if (sample_ptr->max_sfd > 1) {
        long long slow_audio_frame = atomic_load_long_long(&settings->current_frame_num);
        if (slow_audio_frame < 0)
            slow_audio_frame = target_frame;


        result = perform_slow_motion(info, el, p, audio_buf, downsample_buffer,
                                     sampled_down, slow_audio_frame, sample_ptr);

        settings->audio_last_stretched_samples = sample_ptr->audio_last_stretched_samples;
        atomic_store_int(&settings->audio_direction_changed, 0);

        sample_ptr->prev_n_samples = result;
        return result;
    }

    result = perform_normal_playback(info, el, p, audio_buf, temporary_buffer,
                                     num_samples, el->audio_bps, target_frame,
                                     sample_ptr);

    atomic_store_int(&settings->audio_direction_changed, 0);
    atomic_store_int(&settings->audio_slice, 0);

    sample_ptr->direction_changed = 0;
    sample_ptr->prev_n_samples = result;

    return result;
}

#endif


static int vj_perform_apply_secundary_tag(veejay_t * info, performer_t *p, int sample_id, int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0_ref, uint8_t *p1_ref, int subrender )
{
    long long nframe;
    int len = 0;
    int ssm = 0;
    performer_global_t *global = (performer_global_t*) info->performer;

    ycbcr_frame *cached_frame = NULL;

    switch (type)
    {
        case VJ_TAG_TYPE_YUV4MPEG:
        case VJ_TAG_TYPE_V4L:
        case VJ_TAG_TYPE_VLOOPBACK:
        case VJ_TAG_TYPE_AVFORMAT:
        case VJ_TAG_TYPE_NET:
        case VJ_TAG_TYPE_MCAST:
        case VJ_TAG_TYPE_PICTURE:
        case VJ_TAG_TYPE_COLOR:

        cached_frame = vj_perform_cache_get_frame(info,sample_id, VJ_PLAYBACK_MODE_TAG);

        if (cached_frame == NULL)
        {
            if(! vj_tag_get_active( sample_id ) )
            {
                vj_tag_set_active(sample_id, 1 );
            }

            int res = vj_tag_get_frame(sample_id, dst,p->audio_buffer[chain_entry]);
            if(res==1)  {
                vj_perform_cache_put_frame( global, sample_id, VJ_PLAYBACK_MODE_TAG, p->frame_buffer[ chain_entry ]);
            }
        }
        else
        {
            ssm = vj_perform_cache_use_frame(cached_frame, dst);
        }

        break;

   case VJ_TAG_TYPE_NONE:

        nframe = vj_perform_sample_already_ticked(info->performer, sample_id, chain_entry );
        if( nframe == -1 ) {
            nframe = vj_calc_next_subframe(info, sample_id);
            vj_perform_sample_ticked(info->performer, sample_id, chain_entry, nframe );
        }

        cached_frame = vj_perform_cache_get_frame(info,sample_id, VJ_PLAYBACK_MODE_SAMPLE);

        if(cached_frame == NULL)
        {
            len = vj_perform_get_frame_fx( info, sample_id, nframe, src,dst,p0_ref,p1_ref );
            if(len > 0 ) {
               vj_perform_cache_put_frame(global,sample_id, VJ_PLAYBACK_MODE_SAMPLE, p->frame_buffer[ chain_entry]);
            }
        }
        else
        {
            ssm = vj_perform_cache_use_frame(cached_frame, dst );
        }

        break;
    }

    return ssm;
}

static  int vj_perform_get_feedback_frame(veejay_t *info, VJFrame *src, VJFrame *dst, int check_sample, int s1)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    if(check_sample && info->settings->feedback == 0) {
        if(info->uc->sample_id == s1 && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ) {
            int max_sfd = (s1 ? sample_get_framedup( s1 ) : info->sfd );
            int strides[4] = {
                src->len,
                src->uv_len,
                src->uv_len,
                src->stride[3] * src->height
            };

            if( max_sfd <= 1 ) {
                vj_frame_copy( src->data, dst->data, strides );
                return 1;
            }

            uint8_t *pri7[4] = {
                p->primary_buffer[4]->Y,
                p->primary_buffer[4]->Cb,
                p->primary_buffer[4]->Cr,
                p->primary_buffer[4]->alpha
            };

            vj_frame_copy( pri7, dst->data, strides );

            return 1;
        }
    }

    return 0;
}

static  int vj_perform_get_frame_( veejay_t *info, int s1, long long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0_buffer[4], uint8_t *p1_buffer[4], int check_sample )
{
    if( vj_perform_get_feedback_frame(info, src,dst, check_sample, s1) )
        return 1;

    int max_sfd = ( s1 ? sample_get_framedup(s1) : info->sfd );
    editlist *el = ( s1 ? sample_get_editlist(s1) : info->edit_list);
    if( el == NULL ) {
        veejay_msg(VEEJAY_MSG_WARNING, "Selected mixing source and ID does not exist, Use / to toggle mixing type" );
        if( info->edit_list == NULL ) {
            veejay_msg(VEEJAY_MSG_WARNING, "No plain source playing");
            return 0;
        } else {
            veejay_msg(VEEJAY_MSG_WARNING, "Fetching frame %d from plain source", nframe );
            el = info->edit_list;
        }
    }

    if( max_sfd <= 1 ) {
        int res = vj_el_get_video_frame( el, (long) nframe, dst->data );
        if(res) {
            dst->ssm = 0;
        }
        return res;
    }

    int cur_sfd = (s1 ? sample_get_framedups(s1 ) : 0);
    int speed = (s1 ? sample_get_speed(s1) : info->settings->current_playback_speed);
    if(speed == 0) {
        int res = vj_el_get_video_frame(el, (long)nframe, dst->data);

        if(res)
            dst->ssm = 0;

        return res;
    }

    int uv_len = dst->uv_len;

    long p0_frame = 0;
    long p1_frame = 0;

    long    start = ( s1 ? sample_get_startFrame(s1) : info->settings->min_frame_num);
    long    end   = ( s1 ? sample_get_endFrame(s1) : info->settings->max_frame_num );

    if( cur_sfd == 0 ) {
        p0_frame = nframe;
        vj_el_get_video_frame( el, p0_frame, p0_buffer );
        p1_frame = nframe + speed;

        if(p1_frame > end )
            p1_frame = end;
        else if ( p1_frame < start )
            p1_frame = start;

        if( p1_frame != p0_frame )
            vj_el_get_video_frame( el, p1_frame, p1_buffer );

        vj_perform_copy3( p0_buffer, dst->data, dst->len, uv_len,0 );
    } else {
        const uint32_t N = max_sfd;
        const uint32_t n1 = cur_sfd;

        const float frac = (float)n1 / (float)(N - 1);

        vj_frame_slow_single( p0_buffer, p1_buffer, dst->data, dst->len, uv_len, frac );

        if( (n1 + 1 ) == N ) {
            vj_perform_copy3( dst->data, p0_buffer, dst->len,uv_len,0);
        }
    }

    cur_sfd ++;
    if( cur_sfd == max_sfd)
        cur_sfd = 0;

    sample_set_framedups(s1, cur_sfd);

    return 1;
}

static int vj_perform_get_frame_fx(veejay_t *info, int s1, long long nframe, VJFrame *src, VJFrame *dst, uint8_t *p0plane, uint8_t *p1plane)
{
    uint8_t *p0_buffer[4] = {
        p0plane,
        p0plane + dst->len,
        p0plane + dst->len + dst->len,
        p0plane + dst->len + dst->len + dst->len
    };
    uint8_t *p1_buffer[4] = {
        p1plane,
        p1plane + dst->len,
        p1plane + dst->len + dst->len,
        p1plane + dst->len + dst->len + dst->len
    };

    return vj_perform_get_frame_(info, s1, nframe,src,dst,p0_buffer,p1_buffer,1 );
}

static int vj_perform_apply_secundary(veejay_t * info,performer_t *p, int this_sample_id, int sample_id, int type, int chain_entry, VJFrame *src, VJFrame *dst,uint8_t *p0_ref, uint8_t *p1_ref, int subrender)
{
    int nframe;
    int len;
    int res = 1;
    int ssm = 0;
    performer_global_t *g = (performer_global_t*) info->performer;


    ycbcr_frame *cached_frame = NULL;

    switch (type)
    {
        case VJ_TAG_TYPE_YUV4MPEG:
        case VJ_TAG_TYPE_V4L:
        case VJ_TAG_TYPE_VLOOPBACK:
        case VJ_TAG_TYPE_AVFORMAT:
        case VJ_TAG_TYPE_NET:
        case VJ_TAG_TYPE_MCAST:
        case VJ_TAG_TYPE_COLOR:
        case VJ_TAG_TYPE_PICTURE:
        case VJ_TAG_TYPE_GENERATOR:

            cached_frame = vj_perform_cache_get_frame(info,sample_id, VJ_PLAYBACK_MODE_TAG);
            if (cached_frame == NULL)
            {
                if(! vj_tag_get_active( sample_id ) )
                {
                    vj_tag_set_active(sample_id, 1 );
                }

                res = vj_tag_get_frame(sample_id, dst,p->audio_buffer[chain_entry]);
                if(res) {
                    vj_perform_cache_put_frame(g, sample_id, VJ_PLAYBACK_MODE_TAG, p->frame_buffer[ chain_entry ]);
                }
            }
            else
            {
                ssm = vj_perform_cache_use_frame(cached_frame, dst);
            }

            break;

        case VJ_TAG_TYPE_NONE:

            nframe = vj_perform_sample_already_ticked(info->performer, sample_id, chain_entry );
            if( nframe == -1 ) {
                nframe = vj_calc_next_subframe(info, sample_id);
                vj_perform_sample_ticked(info->performer, sample_id, chain_entry, nframe );
            }

            cached_frame = vj_perform_cache_get_frame(info,sample_id, VJ_PLAYBACK_MODE_SAMPLE);

            if(cached_frame == NULL) {
                len = vj_perform_get_frame_fx( info, sample_id, nframe, src, dst, p0_ref, p1_ref );

                if(len > 0 ) {
                    vj_perform_cache_put_frame(g, sample_id, VJ_PLAYBACK_MODE_SAMPLE, p->frame_buffer[ chain_entry]);
                }
            }
            else
            {
                ssm = vj_perform_cache_use_frame(cached_frame,dst);
            }

            break;
    }

    return ssm;
}

static void vj_perform_tag_render_chain_entry(veejay_t *info,performer_t *p,vjp_kf *setup, int sample_id, int pm, sample_eff_chain *fx_entry,int chain_entry, VJFrame *frames[2], int subrender)
{
    VJFrameInfo *frameinfo;
    video_playback_setup *settings = info->settings;

    frameinfo = info->effect_frame_info;

    frames[1]->data[0] = p->frame_buffer[chain_entry]->Y;
    frames[1]->data[1] = p->frame_buffer[chain_entry]->Cb;
    frames[1]->data[2] = p->frame_buffer[chain_entry]->Cr;
    frames[1]->data[3] = p->frame_buffer[chain_entry]->alpha;

    setup->ref = sample_id;

    int effect_id = fx_entry->effect_id;
    int sub_mode = vje_get_subformat(effect_id);
    int ef = vje_get_extra_frame(effect_id);

    vj_perform_supersample(settings,p, frames[0], (ef ? frames[1] : NULL), sub_mode, chain_entry );

    if(ef)
    {
        frames[1]->ssm = vj_perform_apply_secundary_tag(info,p,fx_entry->channel,fx_entry->source_type,chain_entry,frames[0],frames[1],p->frame_buffer[chain_entry]->P0, p->frame_buffer[chain_entry]->P1, 0 );

        if( subrender && settings->fxdepth ) {
            frames[1]->ssm = vj_perform_preprocess_secundary( info,p, fx_entry->channel,fx_entry->source_type,sub_mode,chain_entry, frames, frameinfo );
        }

        vj_perform_supersample(settings,p, NULL, frames[1], sub_mode, chain_entry);
    }

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 1) {
        vj_perform_pre_chain( p, frames[0] );
    }

    vj_perform_apply_first(info,p,setup,frames,fx_entry,effect_id,chain_entry,atomic_load_long_long(&settings->current_frame_num),fx_entry->fx_instance,pm);

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 2) {
        vj_perform_pre_chain( p, frames[0] );
    }
}

static  int vj_perform_preprocess_secundary( veejay_t *info,performer_t *p, int id, int mode,int expected_ssm,int chain_entry, VJFrame **F, VJFrameInfo *frameinfo )
{
    if( mode == 0 ) {
       if( info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE && id == info->uc->sample_id ) {
           return F[0]->ssm;
       }
    }
    else {
        if( info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG && id == info->uc->sample_id )
            return F[0]->ssm;
    }

    int n  = 0;
    video_playback_setup *settings = info->settings;

    VJFrame top,sub;
    veejay_memcpy(&top, F[1], sizeof(VJFrame));
    top.data[0] = F[1]->data[0];
    top.data[1] = F[1]->data[1];
    top.data[2] = F[1]->data[2];
    top.data[3] = F[1]->data[3];
    top.ssm     = F[1]->ssm;

    veejay_memcpy(&sub, F[0], sizeof(VJFrame));
    sub.data[0] = p->subrender_buffer[0];
    sub.data[1] = p->subrender_buffer[1];
    sub.data[2] = p->subrender_buffer[2];
    sub.data[3] = p->subrender_buffer[3];
    sub.ssm     = 0;

    VJFrame *subframes[2];
    subframes[0] = &top;
    subframes[1] = &sub;


    uint8_t *p0_ref = p->subrender_buffer[0] + ( F[0]->len * 4 );
    uint8_t *p1_ref = p0_ref + (F[0]->len * 4);


    vjp_kf setup;
    veejay_memset(&setup,0,sizeof(vjp_kf));
    setup.ref = id;

    sample_eff_chain **chain = NULL;

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 3 ) {
        vj_perform_pre_chain( p, &top );
    }

    switch( mode ) {
        case VJ_PLAYBACK_MODE_SAMPLE:
            chain = sample_get_effect_chain( id );
            for( n=0; n < SAMPLE_MAX_EFFECTS; n ++ ) {
                sample_eff_chain *fx_entry = chain[n];
                if( fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
                    continue;

                int fx_id = fx_entry->effect_id;
                int sm = vje_get_subformat(fx_id);
                int ef = vje_get_extra_frame(fx_id);

                if( ef ) {
                    subframes[1]->ssm = vj_perform_apply_secundary(info,p,id,fx_entry->channel,fx_entry->source_type,n,subframes[0],subframes[1],p0_ref, p1_ref, 1);
                }

                vj_perform_supersample( settings,p, subframes[0], subframes[1], sm, chain_entry );

                vj_perform_apply_first(info,p,&setup,subframes,fx_entry,fx_id,n,atomic_load_long_long(&info->settings->current_frame_num),fx_entry->fx_instance, mode );
            }
            break;
        case VJ_PLAYBACK_MODE_TAG:
            chain = vj_tag_get_effect_chain( id );
            for( n=0; n < SAMPLE_MAX_EFFECTS; n ++ ) {
                sample_eff_chain *fx_entry = chain[n];
                if( fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
                    continue;

                int fx_id = fx_entry->effect_id;
                int sm = vje_get_subformat(fx_id);
                int ef = vje_get_extra_frame(fx_id);

                if( ef ) {
                    subframes[1]->ssm = vj_perform_apply_secundary_tag(info,p,fx_entry->channel,fx_entry->source_type,n,subframes[0],subframes[1],p0_ref,p1_ref,1);
                }

                vj_perform_supersample(settings,p, subframes[0],subframes[1], sm, chain_entry);

                vj_perform_apply_first(info,p,&setup,subframes,fx_entry,fx_id,n,atomic_load_long_long(&info->settings->current_frame_num),fx_entry->fx_instance, mode );
            }
            break;
    }

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 4 ) {
        vj_perform_pre_chain( p, &top );
    }

    return top.ssm;
}

static void vj_perform_render_chain_entry(veejay_t *info,performer_t *p, vjp_kf *setup, int sample_id, int pm, sample_eff_chain *fx_entry,
     int chain_entry, VJFrame *frames[2], int subrender)
{
    VJFrameInfo *frameinfo;
    video_playback_setup *settings = info->settings;

    frameinfo = info->effect_frame_info;

    frames[1]->data[0] = p->frame_buffer[chain_entry]->Y;
    frames[1]->data[1] = p->frame_buffer[chain_entry]->Cb;
    frames[1]->data[2] = p->frame_buffer[chain_entry]->Cr;
    frames[1]->data[3] = p->frame_buffer[chain_entry]->alpha;

    setup->ref = sample_id;

    int effect_id = fx_entry->effect_id;
    int sub_mode = vje_get_subformat(effect_id);
    int ef = vje_get_extra_frame(effect_id);

    vj_perform_supersample(settings,p, frames[0], NULL, sub_mode, chain_entry);

    if(ef)
    {
        frames[1]->ssm = vj_perform_apply_secundary(info,p,sample_id,fx_entry->channel,fx_entry->source_type,chain_entry,frames[0],frames[1],p->frame_buffer[chain_entry]->P0, p->frame_buffer[chain_entry]->P1, 0);

        if( subrender && settings->fxdepth) {
            frames[1]->ssm = vj_perform_preprocess_secundary(info,p,fx_entry->channel,fx_entry->source_type,sub_mode,chain_entry,frames,frameinfo );
        }

        vj_perform_supersample(settings,p, NULL, frames[1], sub_mode, chain_entry);
    }

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 1) {
        vj_perform_pre_chain( p,frames[0] );
    }

    vj_perform_apply_first(info,p,setup,frames,fx_entry,effect_id,chain_entry,
            atomic_load_long_long(&settings->current_frame_num), fx_entry->fx_instance,pm  );

    if( p->pvar_.fade_entry == chain_entry && p->pvar_.fade_method == 2) {
        vj_perform_pre_chain( p, frames[0] );
    }

}

void vj_perform_global_chain_reset(veejay_t *info) {
    global_chain_t *g = info->global_chain;
    sample_eff_chain **gfx = g->fx_chain;

    if(gfx == NULL)
        return;

    for(int chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        if(gfx[chain_entry]->kf) {
            vpf(gfx[chain_entry]->kf);
            gfx[chain_entry]->kf = NULL;
            gfx[chain_entry]->kf_type = 0;
            gfx[chain_entry]->kf_status = 0;
            veejay_msg(VEEJAY_MSG_DEBUG, "Global KF reset");
        }
    }
}

static void vj_perform_global_chain_sync(veejay_t *info, global_chain_t *g_chain, int id, int pm) {
    video_playback_setup *settings = info->settings;

    if(!g_chain->enabled)
        return;

    sample_eff_chain **origin = (g_chain->origin_mode == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_effect_chain(g_chain->origin_id) :
                                (g_chain->origin_mode == VJ_PLAYBACK_MODE_TAG ? vj_tag_get_effect_chain(g_chain->origin_id) : NULL ));
    if(origin == NULL) {
        return;
    }

    if(g_chain->origin_id == id && g_chain->origin_mode == pm )
        return;

    sample_eff_chain **gfx = g_chain->fx_chain;

    for(int chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        if(gfx[chain_entry]->effect_id <= 0) {
            continue;
        }

        if(gfx[chain_entry]->kf) {
            continue;
        }

        if(origin[chain_entry]->kf && origin[chain_entry]->kf_status) {
            int frame_len = settings->max_frame_num - settings->min_frame_num;
            void *new_kf = keyframe_port_clone_and_resize( origin[chain_entry]->kf, frame_len);
            if(new_kf) {
                gfx[chain_entry]->kf = new_kf;
                gfx[chain_entry]->kf_status = 1;
                gfx[chain_entry]->kf_type = origin[chain_entry]->kf_type;
                veejay_msg(VEEJAY_MSG_DEBUG, "Resampled KF to new length of %d frames", frame_len);
            }
        }
    }
}


static void vj_perform_sample_complete_buffers(veejay_t * info,performer_t *p, vjp_kf *effect_info, int *hint444,
    VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup, sample_eff_chain **chain, sample_info *si)
{
    int chain_entry;
    VJFrame *frames[2];
    frames[0] = f0;
    frames[1] = f1;
    setup->ref = sample_id;


    if(p->pvar_.fader_active || p->pvar_.fade_value > 0 || p->pvar_.fade_alpha ) {
        if( p->pvar_.fade_entry == -1 ) {
            vj_perform_pre_chain( p, frames[0] );
        }
    }

    for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        sample_eff_chain *fx_entry = chain[chain_entry];
        if(fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
            continue;

        frames[1]->data[0] = p->frame_buffer[chain_entry]->Y;
        frames[1]->data[1] = p->frame_buffer[chain_entry]->Cb;
        frames[1]->data[2] = p->frame_buffer[chain_entry]->Cr;
        frames[1]->data[3] = p->frame_buffer[chain_entry]->alpha;

        vj_perform_render_chain_entry(info,p,effect_info,sample_id,pm,fx_entry,chain_entry,frames,(si->subrender? fx_entry->is_rendering: si->subrender));
    }
    *hint444 = frames[0]->ssm;
}

static void vj_perform_tag_complete_buffers(veejay_t * info, performer_t *p,vjp_kf *effect_info, int *hint444, VJFrame *f0, VJFrame *f1, int sample_id, int pm, vjp_kf *setup, sample_eff_chain **chain, vj_tag *tag  )
{
    int chain_entry;
    VJFrame *frames[2];
    frames[0] = f0;
    frames[1] = f1;
    setup->ref = sample_id;


    if( p->pvar_.fader_active || p->pvar_.fade_value >0 || p->pvar_.fade_alpha) {
        if( p->pvar_.fade_entry == -1 ) {
            vj_perform_pre_chain(p, frames[0] );
        }
    }

    for(chain_entry = 0; chain_entry < SAMPLE_MAX_EFFECTS; chain_entry++)
    {
        sample_eff_chain *fx_entry = chain[chain_entry];
        if(fx_entry->e_flag == 0 || fx_entry->effect_id <= 0)
            continue;
        vj_perform_tag_render_chain_entry(info,p,effect_info,sample_id,pm,fx_entry,chain_entry,frames,(tag->subrender ? fx_entry->is_rendering : tag->subrender));
    }

    *hint444 = frames[0]->ssm;
}

static void vj_perform_plain_fill_buffer(veejay_t * info, performer_t *p,VJFrame *dst, int sample_id, int mode, long frame_num)
{
    performer_global_t *g = (performer_global_t*) info->performer;

    VJFrame frame;

    if(info->settings->feedback && info->settings->feedback_stage > 1 ) {
        vj_copy_frame_holder(dst, NULL, &frame);
        frame.data[0] = g->feedback_frame.data[0];
        frame.data[1] = g->feedback_frame.data[1];
        frame.data[2] = g->feedback_frame.data[2];
        frame.data[3] = g->feedback_frame.data[3];
    } else {
        vj_copy_frame_holder(dst, NULL, &frame);
        frame.data[0] = dst->data[0];
        frame.data[1] = dst->data[1];
        frame.data[2] = dst->data[2];
        frame.data[3] = dst->data[3];
    }

    uint8_t *p0_buffer[PSLOW_B] = {
        p->primary_buffer[PSLOW_B]->Y,
        p->primary_buffer[PSLOW_B]->Cb,
        p->primary_buffer[PSLOW_B]->Cr,
        p->primary_buffer[PSLOW_B]->alpha };

    uint8_t *p1_buffer[4]= {
        p->primary_buffer[PSLOW_A]->Y,
        p->primary_buffer[PSLOW_A]->Cb,
        p->primary_buffer[PSLOW_A]->Cr,
        p->primary_buffer[PSLOW_A]->alpha };

    if(mode == VJ_PLAYBACK_MODE_SAMPLE)
    {
        vj_perform_get_frame_(info, sample_id, frame_num,&frame,&frame, p0_buffer,p1_buffer,0 );
    }
    else if ( mode == VJ_PLAYBACK_MODE_PLAIN ) {
        vj_perform_get_frame_(info, 0, frame_num,&frame,&frame, p0_buffer, p1_buffer,0 );
    }

}

static int rec_audio_sample_ = 0;

static editlist *vj_perform_record_audio_editlist(veejay_t *info)
{
    editlist *el = NULL;

    if(!info)
        return NULL;

    if(info->uc && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        el = sample_get_editlist(info->uc->sample_id);

    if(!el)
        el = info->current_edit_list;
    if(!el)
        el = info->edit_list;

    return el;
}

static int vj_perform_record_audio_expected_frames(editlist *el)
{
    if(!el || el->audio_rate <= 0 || el->video_fps <= 0.0)
        return 0;

    return (int)ceil((double)el->audio_rate / (double)el->video_fps);
}

#ifdef HAVE_JACK
static int vj_perform_record_audio_beat_active(video_playback_setup *settings)
{
    if(!settings)
        return 0;

    return vj_audio_sync_is_enabled(&settings->audio_sync) &&
           vj_audio_sync_is_running(&settings->audio_sync) &&
           vj_audio_sync_is_open(&settings->audio_sync);
}

static int vj_perform_record_audio_from_beat(
    veejay_t *info,
    performer_t *p,
    editlist *el
) {
    video_playback_setup *settings;
    int wanted_frames;
    int frame_bytes;
    int copied_frames;

    if(!info || !info->settings || !p || !p->audio_rec_buffer || !el)
        return 0;

    settings = info->settings;

    wanted_frames = vj_perform_record_audio_expected_frames(el);
    if(wanted_frames <= 0)
        return 0;

    frame_bytes = el->audio_bps;
    if(frame_bytes <= 0)
        return 0;

    copied_frames = vj_audio_sync_copy_record_audio(
        &settings->audio_sync,
        p->audio_rec_buffer,
        wanted_frames,
        frame_bytes,
        el->audio_chans,
        el->audio_rate
    );

    if(copied_frames < 0)
        copied_frames = 0;

    if(copied_frames > wanted_frames)
        copied_frames = wanted_frames;


    if(copied_frames < wanted_frames) {
        veejay_memset(
            p->audio_rec_buffer + ((size_t)copied_frames * (size_t)frame_bytes),
            0,
            (size_t)(wanted_frames - copied_frames) * (size_t)frame_bytes
        );
    }

    return wanted_frames;
}
#endif

static int vj_perform_record_audio_from_original(
    veejay_t *info,
    performer_t *p
) {
#ifdef HAVE_JACK
    video_playback_setup *settings;
    editlist *el;
    long long target_frame;
    int wanted_frames;
    int frame_bytes;
    int sample_id;
    int got;

    if(!info || !info->settings || !p || !p->audio_rec_buffer)
        return 0;

    settings = info->settings;
    el = vj_perform_record_audio_editlist(info);
    if(!el || !el->has_audio)
        return 0;

    wanted_frames = vj_perform_record_audio_expected_frames(el);
    frame_bytes = el->audio_bps;
    if(wanted_frames <= 0 || frame_bytes <= 0)
        return 0;

    target_frame = atomic_load_long_long(&settings->current_frame_num);
    sample_id = (info->uc ? info->uc->sample_id : 0);

    got = vj_perform_queue_audio_frame_impl(info,
                                            (void*)p,
                                            p->audio_rec_buffer,
                                            settings->current_playback_speed,
                                            target_frame,
                                            sample_id,
                                            &rec_audio_sample_);

    if(got < 0)
        got = 0;
    if(got > wanted_frames)
        got = wanted_frames;

    if(got < wanted_frames) {
        veejay_memset(p->audio_rec_buffer + ((size_t)got * (size_t)frame_bytes),
                      0,
                      (size_t)(wanted_frames - got) * (size_t)frame_bytes);
        got = wanted_frames;
    }

    return got;
#else
    (void)info;
    (void)p;
    return 0;
#endif
}

static int vj_perform_record_audio_frame(
    veejay_t *info,
    performer_t *p
) {
    video_playback_setup *settings;
    editlist *el;
    int source = VJ_RECORD_AUDIO_SOURCE_AUTO;

    if(!info || !info->settings || !p || !p->audio_rec_buffer)
        return 0;

    settings = info->settings;
    el = vj_perform_record_audio_editlist(info);

    source = atomic_load_int(&settings->record_audio_source);

    if(source == VJ_RECORD_AUDIO_SOURCE_SILENCE) {
        if(el) {
            int wanted_frames = vj_perform_record_audio_expected_frames(el);
            int frame_bytes = el->audio_bps;

            if(wanted_frames > 0 && frame_bytes > 0) {
                veejay_memset(p->audio_rec_buffer,
                              0,
                              (size_t)wanted_frames * (size_t)frame_bytes);
                return wanted_frames;
            }
        }
        return 0;
    }

#ifdef HAVE_JACK
    if(source == VJ_RECORD_AUDIO_SOURCE_BEAT_JACK) {
        if(!vj_perform_record_audio_beat_active(settings) &&
           vj_audio_sync_is_running(&settings->audio_sync))
        {
            vj_audio_sync_set_mode(&settings->audio_sync, VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL);
            vj_audio_sync_set_source_jack(&settings->audio_sync, 2);
            vj_audio_sync_enable(&settings->audio_sync);
        }

        if(vj_perform_record_audio_beat_active(settings))
            return vj_perform_record_audio_from_beat(info, p, el);

        if(el) {
            int wanted_frames = vj_perform_record_audio_expected_frames(el);
            int frame_bytes = el->audio_bps;

            if(wanted_frames > 0 && frame_bytes > 0) {
                veejay_memset(p->audio_rec_buffer,
                              0,
                              (size_t)wanted_frames * (size_t)frame_bytes);
                return wanted_frames;
            }
        }

        return 0;
    }

    if(source == VJ_RECORD_AUDIO_SOURCE_AUTO &&
       vj_perform_record_audio_beat_active(settings))
    {
        int sync_mode = atomic_load_int(&settings->audio_sync.mode);
        if(sync_mode == VJ_AUDIO_SYNC_MODE_LIVE_EXTERNAL ||
           sync_mode == VJ_AUDIO_SYNC_MODE_MONITOR ||
           sync_mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE)
            return vj_perform_record_audio_from_beat(info, p, el);
    }
#endif

    return vj_perform_record_audio_from_original(info, p);
}

void vj_perform_record_audio_source_reset(veejay_t *info)
{
    rec_audio_sample_ = 0;

    if(!info || !info->settings)
        return;

    video_playback_setup *settings = info->settings;
    const int source = atomic_load_int(&settings->record_audio_source);
    const int speed = settings->current_playback_speed;
    const int cur_dir = (speed > 0) ? 1 : ((speed < 0) ? -1 : 0);
    const int edge = (cur_dir == 0) ? AUDIO_EDGE_SILENCE : AUDIO_EDGE_JUMP;

#ifdef HAVE_JACK
#endif

    atomic_store_int(&settings->audio_slice, 0);
    atomic_store_int(&settings->audio_flush_request, 1);
    settings->audio_last_stretched_samples = 0;


    if(source == VJ_RECORD_AUDIO_SOURCE_BEAT_JACK ||
       source == VJ_RECORD_AUDIO_SOURCE_SILENCE)
    {
#ifdef HAVE_JACK
#endif
        vj_perform_clear_audio_edges(info, NULL, cur_dir);
        return;
    }


    vj_perform_initiate_edge_change_ex(info, edge, 0, cur_dir);
}

static int vj_perform_render_sample_frame(
    veejay_t *info,
    performer_t *p,
    uint8_t *frame[4],
    int sample,
    int type
) {
    int audio_len = 0;

    if(type == 0 && info->audio == AUDIO_PLAY) {
        audio_len = vj_perform_record_audio_frame(info, p);
    }

    return sample_record_frame(
        sample,
        frame,
        p->audio_rec_buffer,
        audio_len,
        info->pixel_format
    );
}

static int vj_perform_render_offline_tag_frame(veejay_t *info)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g ? g->A : NULL;
    int audio_len = 0;

    if(vj_tag_get_active( info->settings->offline_tag_id ) == 0 ) {
        vj_tag_enable( info->settings->offline_tag_id );
    }

    vj_tag_get_frame( info->settings->offline_tag_id, g->offline_frame, NULL );

    if(p && info->audio == AUDIO_PLAY)
        audio_len = vj_perform_record_audio_frame(info, p);

    return vj_tag_record_frame(info->settings->offline_tag_id,
                               g->offline_frame->data,
                               (audio_len > 0 && p) ? p->audio_rec_buffer : NULL,
                               audio_len,
                               info->pixel_format);
}

static int vj_perform_render_tag_frame(veejay_t *info, uint8_t *frame[4])
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g ? g->A : NULL;
    int audio_len = 0;

    if(p && info->audio == AUDIO_PLAY)
        audio_len = vj_perform_record_audio_frame(info, p);

    return vj_tag_record_frame(info->uc->sample_id,
                               frame,
                               (audio_len > 0 && p) ? p->audio_rec_buffer : NULL,
                               audio_len,
                               info->pixel_format);
}

int vj_perform_commit_offline_recording(veejay_t *info, int id, char *recording)
{
    sample_info *sample = NULL;
    if( id > 0 ) {
        sample = sample_get(id);
        if(!sample) {
            veejay_msg(VEEJAY_MSG_ERROR, "Sample %d no longer exists, creating new sample for recording", id);
            id = 0;
        }
    }

    int new_id = -1;

    if( id > 0 && sample != NULL ) {
        long end_pos = sample->last_frame;
        new_id = veejay_edit_addmovie_sample( info, recording, id );
        if(new_id != -1) {
            if(end_pos < sample->last_frame) {
                sample->first_frame = end_pos;
            }
            veejay_msg(VEEJAY_MSG_DEBUG, "Sample position set to %ld - %ld to loop newly recorded video %s",
                    sample->first_frame, sample->last_frame, recording );
        }
        else {
            veejay_msg(VEEJAY_MSG_ERROR,"Failed to add recording %s to EditList", recording);
        }
    }

    if( id == 0 ) {
        new_id = veejay_edit_addmovie_sample(info, recording, 0 );
        if(new_id != -1) {
            veejay_msg(VEEJAY_MSG_DEBUG, "Added recording %s to new sample", recording);
        }
        else {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to add recording %s to EditList", recording );
        }
    }

    return new_id;
}

static int vj_perform_record_offline_commit_single(veejay_t *info)
{
    char filename[2048];
    int stream_id = info->settings->offline_tag_id;
    int n_id = 0;
    if(vj_tag_get_encoded_file(stream_id, filename))
    {
        int df = vj_event_get_video_format();
        int id = 0;

        if(info->settings->offline_linked_sample_id == -1 ) {
            id = 0;
            veejay_msg(VEEJAY_MSG_INFO, "Adding recorded video file %s to a new sample", filename);
        }
        else {
            id = info->settings->offline_linked_sample_id;
            veejay_msg(VEEJAY_MSG_INFO, "Appending recorded video file %s to sample %d", filename, id );
        }

        if( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
            if(info->settings->offline_linked_sample_id > 0) {
                veejay_msg(VEEJAY_MSG_WARNING, "Cannot append to a yuv4mpeg stream (change recording format)");
            }
            n_id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
        } else {

            n_id = vj_perform_commit_offline_recording( info, id, filename );

            veejay_msg(VEEJAY_MSG_INFO, "Sample %d has new video data", n_id );
        }

        return n_id;
    }

    return 0;
}

static int vj_perform_record_commit_single(veejay_t *info)
{
    char filename[1024];

    if( info->seq->active && info->seq->rec_id ) {
            int id = 0;
            if( sample_get_encoded_file( info->seq->rec_id, filename ) ) {
                int df = vj_event_get_video_format();
                if ( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
                    id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
                }
                else {
                    id = veejay_edit_addmovie_sample(info,filename,0);
                }

                if( id <= 0 ) {
                    veejay_msg(VEEJAY_MSG_ERROR, "Error trying to add %s as a sample", filename);
                }

            }
            return id;
    }
    else {
        if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        {
            if(sample_get_encoded_file(info->uc->sample_id, filename))
            {
                int df = vj_event_get_video_format();
                int id = 0;
                if ( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
                    id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
                }
                else
                {
                    id = veejay_edit_addmovie_sample(info,filename, 0 );
                }
                if(id <= 0)
                {
                    veejay_msg(VEEJAY_MSG_ERROR, "Error trying to add %s as sample or stream", filename);
                    return 0;
                }
                return id;
            }
        }

        if(info->uc->playback_mode==VJ_PLAYBACK_MODE_TAG)
        {
            int stream_id = info->uc->sample_id;
            if(vj_tag_get_encoded_file(stream_id, filename))
            {
                int df = vj_event_get_video_format();
                int id = 0;
                if( df == ENCODER_YUV4MPEG || df == ENCODER_YUV4MPEG420 ) {
                    id = veejay_create_tag( info, VJ_TAG_TYPE_YUV4MPEG,filename,info->nstreams,0,0 );
                } else {
                    id = veejay_edit_addmovie_sample(info, filename, 0);
                }
                if( id <= 0 )
                {
                    veejay_msg(VEEJAY_MSG_ERROR, "Adding file %s to new sample", filename);
                    return 0;
                }
                return id;
            }
        }
    }
    return 0;
}

void vj_perform_start_offline_recorder(veejay_t *v, int rec_format, int stream_id, int duration, int autoplay, int sample_id)
{
    char tmp[2048];
    char prefix[40];

    if(rec_format==-1)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "No video recording format selected");
        return;
    }

    snprintf(prefix,sizeof(prefix),"stream-%02d", stream_id);

    if(!veejay_create_temp_file(prefix, tmp ))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Error creating temporary file %s. Unable to start offline recorder", tmp);
        return;
    }

    veejay_msg(VEEJAY_MSG_DEBUG, "Created temporary file %s", tmp );

    if( vj_tag_init_encoder(stream_id, tmp, rec_format,duration) )
    {
        video_playback_setup *s = v->settings;
        s->offline_record = 1;
        s->offline_tag_id = stream_id;
        s->offline_created_sample = autoplay;
        s->offline_linked_sample_id = ( sample_exists(sample_id) ? sample_id: -1 );

        if(v->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE && s->offline_linked_sample_id > 0 ) {
            sample_set_offline_recorder( v->uc->sample_id, duration, stream_id, rec_format );
        }
    }
    else
    {
       veejay_msg(VEEJAY_MSG_ERROR, "Error starting offline recorder stream %d",stream_id);
    }
}

void vj_perform_record_offline_stop(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    int df = vj_event_get_video_format();

    int stream_id = settings->offline_tag_id;
    int play = settings->offline_created_sample;

    vj_tag_reset_encoder(stream_id);
    vj_tag_reset_autosplit(stream_id);

    if( play ) {
        if(df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420)
        {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
            int id = sample_highest_valid_id();
            veejay_set_sample(info, id );
            if( id > 0 ) {
                veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
            }
        }
        else {

            veejay_msg(VEEJAY_MSG_INFO, "Completed offline recording");
        }
    }

    vj_perform_record_buffer_free(info->performer);

    if( settings->offline_linked_sample_id > 0 ) {
        int n_frames = 0;
        int linked_id = 0;
        int rec_format = 0;
        sample_get_offline_recorder( settings->offline_linked_sample_id, &n_frames, &linked_id, &rec_format );
        vj_perform_start_offline_recorder(info, rec_format, linked_id, n_frames, 0, settings->offline_linked_sample_id );
    }
    else {
        settings->offline_record = 0;
        settings->offline_created_sample = 0;
        settings->offline_tag_id = 0;
    }

}

void vj_perform_record_stop(veejay_t *info)
{
 video_playback_setup *settings = info->settings;
 int df = vj_event_get_video_format();

 if(info->uc->playback_mode==VJ_PLAYBACK_MODE_SAMPLE || ( info->seq->active && info->seq->rec_id > 0 ))
 {
     sample_reset_encoder(info->uc->sample_id);
     sample_reset_autosplit(info->uc->sample_id);
     if( settings->sample_record && settings->sample_record_switch)
     {
        settings->sample_record_switch = 0;
        if( df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420 ) {
            int id = sample_highest_valid_id();
            veejay_set_sample( info,id);
            if(id > 0 ) {
                veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
            }
        } else {
            veejay_msg(VEEJAY_MSG_WARNING, "Not autoplaying new streams");
        }
     }
     settings->sample_record = 0;
     settings->sample_record_switch =0;
     settings->render_list = 0;

     info->seq->rec_id = 0;

 }
 else if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG)
 {
    int stream_id = info->uc->sample_id;
    int play = settings->tag_record_switch;
    vj_tag_reset_encoder(stream_id);
    vj_tag_reset_autosplit(stream_id);

    settings->tag_record = 0;
    settings->tag_record_switch = 0;

    if(play)
    {
        if(df != ENCODER_YUV4MPEG && df != ENCODER_YUV4MPEG420)
        {
            info->uc->playback_mode = VJ_PLAYBACK_MODE_SAMPLE;
            int id = sample_highest_valid_id();
            if( id > 0 ) {
                veejay_set_sample(info, id );
                veejay_msg(VEEJAY_MSG_INFO, "Autoplaying new sample %d",id);
            }
        }
        else {

            veejay_msg(VEEJAY_MSG_WARNING, "Not autoplaying new streams");
        }
    }
  }
}

void vj_perform_record_sample_frame(veejay_t *info, int sample, int type) {
    uint8_t *frame[4];
    int res = 1;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    frame[0] = p->primary_buffer[0]->Y;
    frame[1] = p->primary_buffer[0]->Cb;
    frame[2] = p->primary_buffer[0]->Cr;
    frame[3] = NULL;

    res = vj_perform_render_sample_frame(info, p, frame, sample,type);

    if( res == 2 || res == 1)
    {
        sample_stop_encoder( sample );

        vj_perform_record_commit_single( info );
        vj_perform_record_stop(info);
     }

     if( res == -1)
     {
        sample_stop_encoder(sample);
        vj_perform_record_stop(info);
     }
}

void vj_perform_record_offline_tag_frame(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    int res = 1;
    int stream_id = settings->offline_tag_id;
    performer_global_t *g = (performer_global_t*) info->performer;

    if( g->offline_frame == NULL ) {
        if(vj_perform_record_buffer_init(info) == 0) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to allocate buffer for recorder");
            vj_tag_stop_encoder(stream_id);
            return;
        }
    }

    res = vj_perform_render_offline_tag_frame(info);

    if( res == 2)
    {
        int df = vj_event_get_video_format();
        long frames_left = vj_tag_get_frames_left(stream_id) ;

        vj_tag_stop_encoder( stream_id );
        int n = vj_perform_record_offline_commit_single( info );
        vj_tag_reset_encoder( stream_id );

        if(frames_left > 0 )
        {
            if( vj_tag_init_encoder( stream_id, NULL,
                df, frames_left)==-1)
            {
                veejay_msg(VEEJAY_MSG_WARNING,"Error while auto splitting");
            }
        }
        else
        {
            long len = vj_tag_get_total_frames(stream_id);

            veejay_msg(VEEJAY_MSG_DEBUG, "Added new sample %d of %ld frames",n,len);
            vj_tag_reset_encoder( stream_id );
            vj_tag_reset_autosplit( stream_id );
        }
     }

     if( res == 1)
     {
        vj_tag_stop_encoder(stream_id);
        vj_perform_record_offline_commit_single( info );
        vj_perform_record_offline_stop(info);
     }

     if( res == -1)
     {
        vj_tag_stop_encoder(stream_id);
        vj_perform_record_offline_stop(info);
     }
}

void vj_perform_record_tag_frame(veejay_t *info) {
    uint8_t *frame[4];
    int res = 1;
    int stream_id = info->uc->sample_id;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    frame[0] = p->primary_buffer[0]->Y;
    frame[1] = p->primary_buffer[0]->Cb;
    frame[2] = p->primary_buffer[0]->Cr;
    frame[3] = NULL;

    info->effect_frame1->data[0] = frame[0];
    info->effect_frame1->data[1] = frame[1];
    info->effect_frame1->data[2] = frame[2];
    info->effect_frame1->data[3] = frame[3];

    res = vj_perform_render_tag_frame(info, frame);

    if( res == 2)
    {
        int df = vj_event_get_video_format();
        long frames_left = vj_tag_get_frames_left(stream_id) ;
        vj_tag_stop_encoder( stream_id );
        int n = vj_perform_record_commit_single( info );
        vj_tag_reset_encoder( stream_id );
        if(frames_left > 0 )
        {
            if( vj_tag_init_encoder( stream_id, NULL, df, frames_left)==-1)
            {
                veejay_msg(VEEJAY_MSG_WARNING,
                    "Error while auto splitting");
            }
        }
        else
        {
            long len = vj_tag_get_total_frames(stream_id);

            veejay_msg(VEEJAY_MSG_DEBUG, "Added new sample %d of %ld frames",n,len);
            vj_tag_reset_encoder( stream_id );
            vj_tag_reset_autosplit( stream_id );
        }
     }

     if( res == 1)
     {
        vj_tag_stop_encoder(stream_id);
        vj_perform_record_commit_single( info );
        vj_perform_record_stop(info);
     }

     if( res == -1)
     {
        vj_tag_stop_encoder(stream_id);
        vj_perform_record_stop(info);
     }
}

static void vj_perform_tag_fill_buffer(veejay_t *info, performer_t *p, VJFrame *dst, int sample_id)
{
    int error = 1;
    performer_global_t *g = (performer_global_t*) info->performer;
    int active = p->pvar_.active;
    video_playback_setup *settings = info->settings;

    if(info->settings->feedback && info->settings->feedback_stage > 1) {
        dst->data[0] = g->feedback_frame.data[0];
        dst->data[1] = g->feedback_frame.data[1];
        dst->data[2] = g->feedback_frame.data[2];
        dst->data[3] = g->feedback_frame.data[3];
        dst->ssm = g->feedback_frame.ssm;
    } else {
        dst->data[0] = p->primary_buffer[0]->Y;
        dst->data[1] = p->primary_buffer[0]->Cb;
        dst->data[2] = p->primary_buffer[0]->Cr;
        dst->data[3] = p->primary_buffer[0]->alpha;
    }

    if(settings && settings->current_playback_speed == 0)
        return;

    if(!active)
    {
        vj_tag_enable(sample_id);
    }
    else
    {
        if(vj_tag_get_frame(sample_id, dst, NULL))
            error = 0;
    }

    if(error == 1)
        dummy_apply(dst, VJ_EFFECT_COLOR_BLACK);
}

static void vj_perform_pre_chain(performer_t *p, VJFrame *frame)
{
    vj_perform_copy3( frame->data, p->temp_buffer, frame->len, (frame->ssm ? frame->len : frame->uv_len), frame->stride[3] * frame->height  );

    veejay_memcpy( &(p->temp_frame), frame, sizeof(VJFrame) );
}

static  inline  void    vj_perform_supersample_chain( performer_t *p, subsample_mode_t sample_mode, VJFrame *frame )
{
    if( frame->ssm == p->temp_frame.ssm ) {
        return;
    }

    if( p->temp_frame.ssm == 0 && frame->ssm == 1 ) {
        chroma_supersample(sample_mode,&(p->temp_frame), p->temp_buffer );
        p->temp_frame.ssm = 1;
    }

    if( p->temp_frame.ssm == 1 && frame->ssm == 0 ) {
        chroma_subsample(sample_mode,&(p->temp_frame),p->temp_frame.data);
        p->temp_frame.ssm = 0;
    }
}

void    vj_perform_follow_fade(veejay_t *info, int status) {
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    p->pvar_.follow_fade = status;
}

static int vj_perform_post_chain_sample(veejay_t *info,performer_t *p, VJFrame *frame, int sample_id)
{
    int opacity;
    int mode   = p->pvar_.fader_active;
    int follow = 0;
    int fade_alpha_method = p->pvar_.fade_alpha;

    if(mode == 2 )
        opacity = (int) sample_get_fader_val(sample_id);
    else
        opacity = (int) sample_apply_fader_inc(sample_id);

    p->pvar_.fade_value = opacity;

    switch( fade_alpha_method ) {
        case 0:
            if( opacity > 0 ) {
                vj_perform_supersample_chain(p, info->settings->sample_mode,frame );
                opacity_blend_apply( frame->data ,p->temp_buffer,frame->len,(frame->ssm ? frame->len: frame->uv_len), opacity );
            }
            break;
        default:
            vj_perform_supersample_chain( p, info->settings->sample_mode, frame );
            alpha_transition_apply( frame, p->temp_buffer,(0xff - opacity) );
            break;
    }

    if(mode != 2)
    {
        int dir =sample_get_fader_direction(sample_id);
        if((dir<0) &&(opacity == 0))
        {
            int fade_method = sample_get_fade_method(sample_id );
            if( fade_method == 0 )
                sample_set_effect_status(sample_id, 1);
            sample_reset_fader(sample_id);
            if( p->pvar_.follow_fade ) {
              follow = 1;
            }
        }
        if((dir>0) && (opacity==255))
        {
            sample_reset_fader(sample_id);
            if( p->pvar_.follow_fade ) {
              follow = 1;
            }
        }
        } else if(mode) {
            if( p->pvar_.follow_fade ) {
              follow = 1;
        }
    }

    if( follow ) {
        if( p->pvar_.fade_entry == -1 ) {

            int i,k;
            int tmp = 0;
            for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++) {
                k = sample_get_chain_channel(sample_id, i );
                tmp = sample_get_chain_source(sample_id, i );
                if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k) )) {
                    p->pvar_.follow_now[1] = tmp;
                    p->pvar_.follow_now[0] = k;
                    break;
                }
            }
        }
        else {

            int tmp = 0;
            int k = sample_get_chain_channel(sample_id, p->pvar_.fade_entry );
            tmp = sample_get_chain_source(sample_id, p->pvar_.fade_entry );
            if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k))) {
                p->pvar_.follow_now[1] = tmp;
                p->pvar_.follow_now[0] = k;
            }
        }
    }

    return follow;
}

static int vj_perform_post_chain_tag(veejay_t *info,performer_t *p, VJFrame *frame, int sample_id)
{
    int opacity = 0;
    int mode = p->pvar_.fader_active;
    int follow = 0;
    int fade_alpha_method = p->pvar_.fade_alpha;

    if(mode == 2)
        opacity = (int) vj_tag_get_fader_val(sample_id);
    else if( mode )
        opacity = (int) vj_tag_apply_fader_inc(sample_id);

    p->pvar_.fade_value = opacity;

    if( opacity == 0 ) {
        if( p->pvar_.follow_fade ) {
           follow = 1;
        }
    }

    switch( fade_alpha_method ) {
        case 0:
            if( opacity > 0 ) {
                vj_perform_supersample_chain( p, info->settings->sample_mode, frame );
                opacity_blend_apply( frame->data ,p->temp_buffer,frame->len, (frame->ssm ? frame->len : frame->uv_len), opacity );
            }
            break;
        default:
            vj_perform_supersample_chain( p, info->settings->sample_mode, frame );
            alpha_transition_apply( frame, p->temp_buffer,0xff - opacity );
            break;
    }

    if(mode != 2)
    {
        int dir = vj_tag_get_fader_direction(sample_id);

        if((dir < 0) && (opacity == 0))
        {
            int fade_method = vj_tag_get_fade_method(sample_id);
            if( fade_method == 0 )
                vj_tag_set_effect_status(sample_id,1);
            vj_tag_reset_fader(sample_id);
            if( p->pvar_.follow_fade ) {
               follow = 1;
            }
        }
        if((dir > 0) && (opacity == 255))
        {
            vj_tag_reset_fader(sample_id);
            if( p->pvar_.follow_fade ) {
               follow = 1;
            }
        }
        } else if(mode){
            if( p->pvar_.follow_fade ) {
             follow = 1;
        }
    }

    if( follow ) {
        int i;
        int tmp=0,k;
        for( i = 0; i < SAMPLE_MAX_EFFECTS - 1; i ++ ) {
            k = vj_tag_get_chain_channel(sample_id, i );
            tmp = vj_tag_get_chain_source(sample_id, i );
            if( (tmp == 0 && sample_exists(k)) || (tmp > 0 && vj_tag_exists(k)) ) {
                p->pvar_.follow_now[1] = tmp;
                p->pvar_.follow_now[0] = k;
                break;
            }
        }
    }
    return follow;
}

#ifdef HAVE_JACK
#ifndef VJ_TRACK_ALIGN_CANDIDATE_NONE
#define VJ_TRACK_ALIGN_CANDIDATE_NONE 0
#endif
#ifndef VJ_TRACK_ALIGN_CANDIDATE_WIDE
#define VJ_TRACK_ALIGN_CANDIDATE_WIDE 1
#endif
#ifndef VJ_TRACK_ALIGN_CANDIDATE_LIVE
#define VJ_TRACK_ALIGN_CANDIDATE_LIVE 2
#endif
#ifndef VJ_TRACK_ALIGN_LIVE_SNAP_TICK_INTERVAL_MS
#define VJ_TRACK_ALIGN_LIVE_SNAP_TICK_INTERVAL_MS 500L
#endif


#ifndef VJ_TRACK_ALIGN_REACQUIRE_TRIM_WINDOW_MS
#define VJ_TRACK_ALIGN_REACQUIRE_TRIM_WINDOW_MS 6000L
#endif
#ifndef VJ_TRACK_ALIGN_REACQUIRE_TRIM_MIN_OFFSET_MS
#define VJ_TRACK_ALIGN_REACQUIRE_TRIM_MIN_OFFSET_MS 45
#endif
#ifndef VJ_TRACK_ALIGN_REACQUIRE_TRIM_MAX_OFFSET_MS
#define VJ_TRACK_ALIGN_REACQUIRE_TRIM_MAX_OFFSET_MS 700
#endif
#ifndef VJ_TRACK_ALIGN_REACQUIRE_TRIM_MIN_CONF
#define VJ_TRACK_ALIGN_REACQUIRE_TRIM_MIN_CONF 70
#endif
#ifndef VJ_TRACK_ALIGN_REACQUIRE_TRIM_STABLE_COUNT
#define VJ_TRACK_ALIGN_REACQUIRE_TRIM_STABLE_COUNT 2
#endif
#ifndef VJ_TRACK_ALIGN_REACQUIRE_SETTLED_MAX_OFFSET_MS
#define VJ_TRACK_ALIGN_REACQUIRE_SETTLED_MAX_OFFSET_MS 35
#endif
#ifndef VJ_TRACK_ALIGN_REACQUIRE_SETTLED_MIN_CONF
#define VJ_TRACK_ALIGN_REACQUIRE_SETTLED_MIN_CONF 72
#endif


#ifndef VJ_TRACK_ALIGN_SETTLED_SERVO_MIN_OFFSET_MS
#define VJ_TRACK_ALIGN_SETTLED_SERVO_MIN_OFFSET_MS 55
#endif
#ifndef VJ_TRACK_ALIGN_SETTLED_SERVO_MAX_OFFSET_MS
#define VJ_TRACK_ALIGN_SETTLED_SERVO_MAX_OFFSET_MS 900
#endif
#ifndef VJ_TRACK_ALIGN_SETTLED_SERVO_MIN_CONF
#define VJ_TRACK_ALIGN_SETTLED_SERVO_MIN_CONF 58
#endif
#ifndef VJ_TRACK_ALIGN_SETTLED_SERVO_STABLE_COUNT
#define VJ_TRACK_ALIGN_SETTLED_SERVO_STABLE_COUNT 3
#endif
#ifndef VJ_TRACK_ALIGN_SETTLED_SERVO_INTERVAL_MS
#define VJ_TRACK_ALIGN_SETTLED_SERVO_INTERVAL_MS 650L
#endif
#ifndef VJ_TRACK_ALIGN_SETTLED_SERVO_CANDIDATE_TTL_MS
#define VJ_TRACK_ALIGN_SETTLED_SERVO_CANDIDATE_TTL_MS 2200L
#endif
#ifndef VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_CONF
#define VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_CONF 12
#endif
#ifndef VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_OFFSET_MS
#define VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_OFFSET_MS 250
#endif
#ifndef VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_DIFF_MS
#define VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_DIFF_MS 150
#endif

static double vj_perform_runtime_audio_rate(veejay_t *info, editlist *el);
static int vj_perform_runtime_sfd(veejay_t *info);
static double vj_perform_runtime_effective_audio_rate(veejay_t *info, editlist *el);
static void vj_audio_pad_exact_tail(uint8_t *dst, int produced, int expected, int frame_bytes);
static int vj_audio_retime_slow_cubic_s16(uint8_t *dst, int dst_samples, const uint8_t *src, int src_samples, int frame_bytes, double rate);
static int vj_perform_retime_audio_chunk(veejay_t *info, performer_t *p, editlist *el, uint8_t *dst, int dst_samples, const uint8_t *src, int src_samples, int frame_bytes);
static int vj_perform_runtime_slow_audio_chunk(veejay_t *info, performer_t *p, editlist *el, uint8_t *dst, int dst_samples, long long target_frame, int frame_bytes, double rate);
static void vj_external_audio_history_reset(performer_t *p, int frame_bytes);
static void vj_external_audio_history_append(performer_t *p, const uint8_t *src, int frames, int frame_bytes);
static int vj_external_audio_history_read_latest(performer_t *p, uint8_t *dst, int wanted_frames, int frame_bytes);
static int vj_external_audio_output_latest_or_silence(performer_t *p, uint8_t *dst, int wanted_frames, int frame_bytes);
static int vj_external_audio_history_ready(performer_t *p, int needed_frames, int frame_bytes);
static int vj_external_audio_min_ready_frames(int dst_frames, double transport_rate, int sample_rate);
static void vj_external_audio_store_tail(performer_t *p, const uint8_t *buf, int frames, int frame_bytes);
static void vj_external_audio_smooth_block_start(performer_t *p, uint8_t *buf, int frames, int frame_bytes, int force_edge);
static int vj_external_audio_render_transport(performer_t *p, uint8_t *dst, int dst_frames, int frame_bytes, double transport_rate, int sample_rate);
static int vj_external_audio_render_tape_deck(performer_t *p, uint8_t *dst, int dst_frames, int frame_bytes, double deck_rate, int reverse, int sample_rate);
static int vj_external_audio_render_reverse_deck(performer_t *p, uint8_t *dst, int dst_frames, int frame_bytes, double deck_rate, int sample_rate);
static void vj_perform_feed_beat_analysis(veejay_t *info, const uint8_t *buf, int frames, int frame_bytes, editlist *el);
static double vj_perform_track_align_transport_fps(veejay_t *info, editlist *el)
{
    double fps = 0.0;

    if(info && info->settings)
        fps = (double)info->settings->output_fps;

    if(fps <= 0.0 && el && el->video_fps > 0.0)
        fps = (double)el->video_fps;

    if(fps < 1.0)
        fps = 25.0;
    else if(fps > 240.0)
        fps = 240.0;

    return fps;
}

static double vj_perform_track_align_source_fps(editlist *el)
{
    double fps = 0.0;

    if(el && el->video_fps > 0.0)
        fps = (double)el->video_fps;

    if(fps < 1.0)
        fps = 25.0;
    else if(fps > 240.0)
        fps = 240.0;

    return fps;
}

#ifndef VJ_TRACK_ALIGN_NORMAL_FPS_ABS_TOL
#define VJ_TRACK_ALIGN_NORMAL_FPS_ABS_TOL 0.075
#endif
#ifndef VJ_TRACK_ALIGN_NORMAL_FPS_REL_TOL
#define VJ_TRACK_ALIGN_NORMAL_FPS_REL_TOL 0.035
#endif
#ifndef VJ_TRACK_ALIGN_NORMAL_FPS_MAX_TOL
#define VJ_TRACK_ALIGN_NORMAL_FPS_MAX_TOL 1.25
#endif

static int vj_perform_track_align_fps_close(double fps, double source_fps)
{
    double tol;
    double diff;

    if(source_fps <= 0.0)
        source_fps = 25.0;
    if(fps <= 0.0)
        return 0;

    diff = fabs(fps - source_fps);
    tol = source_fps * VJ_TRACK_ALIGN_NORMAL_FPS_REL_TOL;
    if(tol < VJ_TRACK_ALIGN_NORMAL_FPS_ABS_TOL)
        tol = VJ_TRACK_ALIGN_NORMAL_FPS_ABS_TOL;
    if(tol > VJ_TRACK_ALIGN_NORMAL_FPS_MAX_TOL)
        tol = VJ_TRACK_ALIGN_NORMAL_FPS_MAX_TOL;

    return diff <= tol;
}

static int vj_perform_track_align_current_sfd(veejay_t *info)
{
    int sfd = 1;

    if(!info || !info->settings)
        return 1;

    if(info->uc && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
        int id = info->uc->sample_id;
        if(id >= 0)
            sfd = sample_get_framedup(id);
    }
    else {
        sfd = info->settings->sfd > 0 ? info->settings->sfd : info->sfd;
    }

    if(sfd <= 0)
        sfd = 1;

    return sfd;
}

static int vj_perform_track_align_normal_transport(veejay_t *info, editlist *el)
{
    double fps;
    double source_fps;

    if(!info || !info->settings || !el)
        return 0;

    if(info->settings->current_playback_speed != 1)
        return 0;

    if(vj_perform_track_align_current_sfd(info) > 1)
        return 0;

    fps = vj_perform_track_align_transport_fps(info, el);
    source_fps = vj_perform_track_align_source_fps(el);

    return vj_perform_track_align_fps_close(fps, source_fps);
}

static void vj_perform_track_align_observe_sync_epoch(veejay_t *info,
                                                        performer_t *p,
                                                        const char *where)
{
    int sync_mode;
    int target_mode;
    int reset_seq;

    if(!info || !info->settings || !p)
        return;

    sync_mode = atomic_load_int(&info->settings->audio_sync.mode);
    target_mode = vj_audio_sync_get_target_mode(&info->settings->audio_sync);
    reset_seq = atomic_load_int(&info->settings->audio_sync.reset_seq);

    if(p->track_align_last_sync_mode == sync_mode &&
       p->track_align_last_sync_target_mode == target_mode &&
       p->track_align_last_sync_reset_seq == reset_seq)
        return;

    if(p->track_align_last_sync_mode != -999 ||
       p->track_align_last_sync_reset_seq != -1)
    {
    }

    p->track_align_last_sync_mode = sync_mode;
    p->track_align_last_sync_target_mode = target_mode;
    p->track_align_last_sync_reset_seq = reset_seq;

    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        p->track_align_last_reacquire_ms = ((long)ts.tv_sec * 1000L) + ((long)ts.tv_nsec / 1000000L);
    }

    p->track_align_last_wide_search_ms = 0;
    p->track_align_last_wide_snap_ms = 0;
    p->track_align_last_wide_snap_delta = 0;
    p->track_align_last_servo_offer_ms = 0;
    p->track_align_servo_candidate_ms = 0;
    p->track_align_servo_sign = 0;
    p->track_align_servo_count = 0;
    p->track_align_servo_min_conf = 0;
    p->clip_target_last_frame = -1;
    p->clip_target_last_mode = -1;
    p->clip_target_last_id = -1;
    vj_perform_track_align_clear_candidate(p);
    vj_perform_track_align_clear_wide_buckets(p);
}

static int vj_perform_track_align_try_live_snap(veejay_t *info, performer_t *p, editlist *el, const vj_audio_sync_snapshot_t *snap, long now_ms, double fps, const char *reason);

static void vj_perform_track_align_observe_reacquire_seq(veejay_t *info,
                                                         performer_t *p,
                                                         const char *where)
{
    int seq;

    if(!info || !info->settings || !p)
        return;

    seq = atomic_load_int(&info->settings->track_align_reacquire_seq);
    if(p->track_align_seen_reacquire_seq == seq)
        return;


    p->track_align_seen_reacquire_seq = seq;
    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        p->track_align_last_reacquire_ms = ((long)ts.tv_sec * 1000L) + ((long)ts.tv_nsec / 1000000L);
    }
    p->clip_target_last_frame = -1;
    p->clip_target_last_mode = -1;
    p->clip_target_last_id = -1;
    p->track_align_last_wide_search_ms = 0;
    p->track_align_last_wide_snap_ms = 0;
    p->track_align_last_wide_snap_delta = 0;
    p->track_align_last_servo_offer_ms = 0;
    p->track_align_servo_candidate_ms = 0;
    p->track_align_servo_sign = 0;
    p->track_align_servo_count = 0;
    p->track_align_servo_min_conf = 0;
    vj_perform_track_align_clear_candidate(p);
    vj_perform_track_align_clear_wide_buckets(p);
}


static inline int vj_perform_wav_plain_lock_bypasses_target(veejay_t *info)
{
    if(!info || !info->settings || !info->uc)
        return 0;

    if(info->uc->playback_mode != VJ_PLAYBACK_MODE_PLAIN)
        return 0;

    if(atomic_load_int(&info->settings->audio_sync.source) != VJ_AUDIO_SYNC_SOURCE_WAV_FILE)
        return 0;

    return vj_audio_sync_wav_plain_lock_valid(&info->settings->audio_sync);
}

static void vj_perform_feed_clip_target_clock(veejay_t *info, performer_t *p, editlist *el, long long target_frame, int dst_frames, int frame_bytes);
static void vj_perform_enqueue_output_target_clock(veejay_t *info, performer_t *p, editlist *el, long long target_frame, const uint8_t *buf, int frames, int frame_bytes);
static void vj_perform_track_align_observe_reacquire_seq(veejay_t *info, performer_t *p, const char *where);
static void vj_perform_track_align_wide_search(veejay_t *info, performer_t *p, editlist *el, long long target_frame, int frame_bytes);
int vj_perform_queue_audio_frame(veejay_t *info, void *ptr, uint8_t *a_buf, int speed, long long target_frame,int sample_id);

static void vj_perform_feed_beat_analysis(veejay_t *info,
                                           const uint8_t *buf,
                                           int frames,
                                           int frame_bytes,
                                           editlist *el)
{
    if(!info || !info->settings || !buf || frames <= 0 ||
       frame_bytes <= 0 || !el)
        return;

    video_playback_setup *settings = info->settings;

    if(!vj_audio_beat_is_enabled(&settings->audio_beat))
        return;

    if(!vj_audio_sync_is_enabled(&settings->audio_sync))
        return;

    if(atomic_load_int(&settings->audio_sync.source) != VJ_AUDIO_SYNC_SOURCE_PUSH)
        return;

    vj_audio_sync_push_audio(&settings->audio_sync,
                             buf,
                             frames,
                             frame_bytes,
                             el->audio_chans,
                             el->audio_rate);
}

static void vj_perform_feed_clip_target_clock(veejay_t *info,
                                               performer_t *p,
                                               editlist *el,
                                               long long target_frame,
                                               int dst_frames,
                                               int frame_bytes)
{
    int got;
    int wanted_frames;
    uint8_t *tmp;
    int cur_mode;
    int cur_id;

    if(!info || !info->settings || !p || !el ||
       target_frame < 0 || dst_frames <= 0 || frame_bytes <= 0)
        return;

    if(!vj_audio_sync_is_enabled(&info->settings->audio_sync))
        return;

    if(!el->has_audio || !vj_perform_audio_params_valid(el)) {
        if(vj_audio_sync_get_target_mode(&info->settings->audio_sync) ==
           VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        {
            vj_audio_sync_reset_target_clock(&info->settings->audio_sync);
            p->clip_target_last_frame = -1;
            p->clip_target_last_mode = -1;
            p->clip_target_last_id = -1;
        }
        return;
    }

    if(vj_audio_sync_get_target_mode(&info->settings->audio_sync) !=
       VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        return;

    {
        int sync_mode = atomic_load_int(&info->settings->audio_sync.mode);
        if(sync_mode != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE &&
           sync_mode != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
            return;
    }

    if(vj_perform_wav_plain_lock_bypasses_target(info))
        return;

    cur_mode = info->uc ? info->uc->playback_mode : -1;
    cur_id   = info->uc ? info->uc->sample_id : -1;


    if((p->clip_target_last_mode != -1 || p->clip_target_last_id != -1) &&
       (p->clip_target_last_mode != cur_mode ||
        p->clip_target_last_id   != cur_id))
    {
        vj_audio_sync_reset_target_clock(&info->settings->audio_sync);
        p->clip_target_last_frame = -1;
        p->clip_target_last_mode = -1;
        p->clip_target_last_id = -1;
    }


    if(p->clip_target_last_frame == target_frame &&
       p->clip_target_last_mode == cur_mode &&
       p->clip_target_last_id == cur_id)
    {
        return;
    }

    wanted_frames = dst_frames;
    if(el->audio_rate > 0 && el->video_fps > 0.0) {
        int media_frames = (int)ceil((double)el->audio_rate /
                                     (double)el->video_fps);
        if(media_frames > wanted_frames)
            wanted_frames = media_frames;
    }

    if(wanted_frames < dst_frames + 64)
        wanted_frames = dst_frames + 64;

    if(!p->audio_render_buffer || p->audio_render_buffer_capacity <
       (size_t)wanted_frames * (size_t)frame_bytes)
        return;

    tmp = p->audio_render_buffer;
    got = vj_el_get_audio_frame(el, target_frame, tmp);
    if(got <= 0)
        return;

    if(got > wanted_frames)
        got = wanted_frames;

    if(vj_audio_sync_push_target_audio(&info->settings->audio_sync,
                                       tmp,
                                       got,
                                       frame_bytes,
                                       el->audio_chans,
                                       el->audio_rate) > 0)
    {
        p->clip_target_last_frame = target_frame;
        p->clip_target_last_mode = cur_mode;
        p->clip_target_last_id = cur_id;

        if(atomic_load_int(&info->settings->audio_sync.mode) ==
           VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        {
            double fps = vj_perform_track_align_source_fps(el);
            vj_audio_sync_set_track_align_video_fps(&info->settings->audio_sync, fps);
        }
    }
}


static void vj_perform_enqueue_output_target_clock(veejay_t *info,
                                                   performer_t *p,
                                                   editlist *el,
                                                   long long target_frame,
                                                   const uint8_t *buf,
                                                   int frames,
                                                   int frame_bytes)
{
    int cur_mode;
    int cur_id;
    int sync_mode;

    if(!info || !info->settings || !p || !el || !el->has_audio ||
       !buf || frames <= 0 || frame_bytes <= 0 || target_frame < 0)
        return;

    if(!vj_audio_sync_is_enabled(&info->settings->audio_sync))
        return;

    sync_mode = atomic_load_int(&info->settings->audio_sync.mode);
    if(sync_mode != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        return;

    if(vj_audio_sync_get_target_mode(&info->settings->audio_sync) !=
       VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        return;

    if(vj_perform_wav_plain_lock_bypasses_target(info))
        return;

    vj_perform_track_align_observe_sync_epoch(info, p, "target-clock");

    if(!vj_perform_track_align_normal_transport(info, el)) {
        return;
    }

    vj_perform_track_align_observe_reacquire_seq(info, p, "enqueue");

    cur_mode = info->uc ? info->uc->playback_mode : -1;
    cur_id   = info->uc ? info->uc->sample_id : -1;

    if((p->clip_target_last_mode != -1 || p->clip_target_last_id != -1) &&
       (p->clip_target_last_mode != cur_mode ||
        p->clip_target_last_id   != cur_id))
    {
        vj_audio_sync_reset_target_clock(&info->settings->audio_sync);
        p->clip_target_last_frame = -1;
        p->clip_target_last_mode = -1;
        p->clip_target_last_id = -1;
    }

    if(p->clip_target_last_frame == target_frame &&
       p->clip_target_last_mode == cur_mode &&
       p->clip_target_last_id == cur_id)
    {
        return;
    }

    vj_audio_sync_set_track_align_video_fps(
        &info->settings->audio_sync,
        vj_perform_track_align_source_fps(el)
    );

    if(vj_audio_sync_push_target_audio(&info->settings->audio_sync,
                                       buf,
                                       frames,
                                       frame_bytes,
                                       el->audio_chans,
                                       el->audio_rate) > 0)
    {
        p->clip_target_last_frame = target_frame;
        p->clip_target_last_mode = cur_mode;
        p->clip_target_last_id = cur_id;


        vj_perform_track_align_wide_search(info,
                                           p,
                                           el,
                                           target_frame,
                                           frame_bytes);
    }
}


#ifndef VJ_EXTERNAL_JACK_NEUTRAL_RATE_LOW
#define VJ_EXTERNAL_JACK_NEUTRAL_RATE_LOW 0.995
#endif

#ifndef VJ_EXTERNAL_JACK_NEUTRAL_RATE_HIGH
#define VJ_EXTERNAL_JACK_NEUTRAL_RATE_HIGH 1.005
#endif

static inline int vj_perform_external_jack_rate_is_neutral(double rate)
{
    return rate >= VJ_EXTERNAL_JACK_NEUTRAL_RATE_LOW &&
           rate <= VJ_EXTERNAL_JACK_NEUTRAL_RATE_HIGH;
}


int vj_perform_queue_audio_chunk_ext(
    veejay_t *info,
    int client_frames_to_write,
    long long target_frame,
    int fade_in,
    uint8_t *audio_payload_chunk
) {
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    video_playback_setup *settings = info->settings;

    (void)fade_in;

    if (client_frames_to_write <= 0 || audio_payload_chunk == NULL)
        return 0;

    editlist *el = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
        ? sample_get_editlist(info->uc->sample_id)
        : info->edit_list;

    if (el == NULL)
        el = info->current_edit_list;

    if (el == NULL)
        return 0;

#ifdef HAVE_JACK
    if(!vj_perform_audio_params_valid(el)) {
        if(!vj_perform_prepare_silence_audio_params(el))
            return 0;
    }
#endif

    if (el->audio_bps <= 0)
        return 0;

    const double rate = vj_perform_runtime_audio_rate(info, el);
    const double effective_rate = vj_perform_runtime_effective_audio_rate(info, el);
    const int frame_bytes = el->audio_bps;
    const int speed = settings->current_playback_speed;
    const int speed_abs = (speed < 0) ? -speed : speed;
    const int route_sfd = vj_perform_runtime_sfd(info);
    const int external_transport_requested =
        (speed != 1) ||
        (route_sfd > 1) ||
        !vj_perform_external_jack_rate_is_neutral(effective_rate);

    int audio_source = atomic_load_int(&settings->record_audio_source);
    if (audio_source < VJ_RECORD_AUDIO_SOURCE_AUTO ||
        audio_source > VJ_RECORD_AUDIO_SOURCE_SILENCE)
    {
        audio_source = VJ_RECORD_AUDIO_SOURCE_AUTO;
    }

    if (frame_bytes <= 0)
        return 0;

#ifdef HAVE_JACK
    {
        int guard_seq = 0;
        int guard_left_before = 0;
        int guard_left_after = 0;
        int guard_silence_bytes = 0;
        int guard_fade_bytes = 0;
        int mute_dbg = atomic_load_int(&settings->audio_mute);


        if(vj_perform_audio_source_transition_guard_consume_block(
                client_frames_to_write,
                frame_bytes,
                audio_source,
                mute_dbg,
                &guard_seq,
                &guard_left_before,
                &guard_left_after,
                &guard_silence_bytes,
                &guard_fade_bytes))
        {
            vj_jack_set_input_passthrough(0);
            if(p) {
                p->external_audio_transport_active = 0;
                p->external_audio_prev_valid = 0;
            }
            return client_frames_to_write;
        }
    }
#endif

    if (audio_source == VJ_RECORD_AUDIO_SOURCE_SILENCE) {


#ifdef HAVE_JACK
        vj_jack_set_input_passthrough(0);
#endif
        if(p) {
            p->external_audio_transport_active = 0;
            p->external_audio_prev_valid = 0;
        }
        return client_frames_to_write;
    }

#ifdef HAVE_JACK


    if (frame_bytes > 0 && !(frame_bytes & 1)) {
        int sync_enabled = vj_audio_sync_is_enabled(&settings->audio_sync);
        int sync_mode = atomic_load_int(&settings->audio_sync.mode);


        const int external_playback_mode =
            (sync_mode == VJ_AUDIO_SYNC_MODE_MONITOR ||
             sync_mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE ||
             sync_mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN);

        const int sync_source_now = atomic_load_int(&settings->audio_sync.source);

        const int source_wants_jack_playback =
            (audio_source == VJ_RECORD_AUDIO_SOURCE_BEAT_JACK &&
             external_playback_mode &&
             sync_source_now != VJ_AUDIO_SYNC_SOURCE_WAV_FILE);

        const int auto_wants_external =
            (audio_source == VJ_RECORD_AUDIO_SOURCE_AUTO &&
             sync_enabled &&
             external_playback_mode);

        const int mode_wants_external_audio =
            (sync_enabled && external_playback_mode);


        const int monitor_neutral_forward =
            (speed == 1 &&
             route_sfd <= 1 &&
             vj_perform_external_jack_rate_is_neutral(effective_rate));

        const int monitor_neutral_passthrough =
            (sync_mode == VJ_AUDIO_SYNC_MODE_MONITOR &&
             monitor_neutral_forward);

        const int track_align_neutral_passthrough =
            (sync_mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN &&
             monitor_neutral_forward);

        const int direct_jack_passthrough =
            (sync_enabled &&
             sync_source_now == VJ_AUDIO_SYNC_SOURCE_JACK &&
             (monitor_neutral_passthrough || track_align_neutral_passthrough) &&
             !(p && p->external_audio_transport_active));


        vj_jack_set_input_passthrough(direct_jack_passthrough &&
                                      !atomic_load_int(&settings->audio_mute));

        if (sync_enabled &&
            sync_mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN &&
            vj_audio_sync_get_target_mode(&settings->audio_sync) == VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        {


            vj_audio_sync_set_track_align_video_fps(
                &settings->audio_sync,
                vj_perform_track_align_source_fps(el)
            );
        }

        if (source_wants_jack_playback || auto_wants_external || mode_wants_external_audio) {
            const size_t out_bytes =
                (size_t)client_frames_to_write * (size_t)frame_bytes;
            int client_rate = vj_jack_get_client_samplerate();
            int external_frames = 0;
            int source = atomic_load_int(&settings->audio_sync.source);
            int channels = atomic_load_int(&settings->audio_sync.input_channels_request);
            uint8_t *fresh = (p && p->top_audio_buffer &&
                              p->top_audio_buffer_capacity >= out_bytes)
                ? p->top_audio_buffer
                : audio_payload_chunk;

            if (client_rate <= 0)
                client_rate = el->audio_rate;

            if (p) {
                int sync_key = ((sync_mode & 0xff) << 8) | (source & 0xff);
                if (p->external_audio_last_sync_key != sync_key ||
                    p->external_audio_history_frame_bytes != frame_bytes)
                {
                    vj_external_audio_history_reset(p, frame_bytes);
                    p->external_audio_last_sync_key = sync_key;
                }
            }

            if (source_wants_jack_playback) {


                if (source != VJ_AUDIO_SYNC_SOURCE_JACK || channels != 2)
                    vj_audio_sync_set_source_jack(&settings->audio_sync, 2);

                vj_audio_sync_enable(&settings->audio_sync);
            }

            sync_mode = atomic_load_int(&settings->audio_sync.mode);
            if (sync_mode != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE &&
                sync_mode != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN &&
                sync_mode != VJ_AUDIO_SYNC_MODE_MONITOR)
            {


                if (p && p->external_audio_transport_active) {
                    vj_external_audio_output_latest_or_silence(p,
                                                               audio_payload_chunk,
                                                               client_frames_to_write,
                                                               frame_bytes);
                    vj_audio_declick_apply(p,
                                           audio_payload_chunk,
                                           client_frames_to_write,
                                           frame_bytes,
                                           AUDIO_PATH_DIRECT,
                                           speed,
                                           (speed < 0) ? -1 : 1,
                                           AUDIO_EDGE_JUMP,
                                           1);
                    vj_external_audio_smooth_block_start(p,
                                                         audio_payload_chunk,
                                                         client_frames_to_write,
                                                         frame_bytes,
                                                         1);
                    p->external_audio_transport_active = 0;
                } else {
                    vj_external_audio_output_latest_or_silence(p,
                                                               audio_payload_chunk,
                                                               client_frames_to_write,
                                                               frame_bytes);
                    vj_external_audio_store_tail(p,
                                                 audio_payload_chunk,
                                                 client_frames_to_write,
                                                 frame_bytes);
                }
                return client_frames_to_write;
            }

            if (sync_mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE) {


                vj_perform_feed_clip_target_clock(info,
                                                  p,
                                                  el,
                                                  target_frame,
                                                  client_frames_to_write,
                                                  frame_bytes);

                if(speed < 0) {
                    external_frames = vj_audio_sync_render_monitor_s16(
                        &settings->audio_sync,
                        fresh,
                        client_frames_to_write,
                        frame_bytes,
                        el->audio_chans,
                        client_rate
                    );
                } else {
                    external_frames = vj_audio_sync_render_bridge_s16(
                        &settings->audio_sync,
                        fresh,
                        client_frames_to_write,
                        frame_bytes,
                        el->audio_chans,
                        client_rate
                    );
                }
            } else if (sync_mode == VJ_AUDIO_SYNC_MODE_TRACK_ALIGN) {


                vj_audio_sync_set_track_align_video_fps(
                    &settings->audio_sync,
                    vj_perform_track_align_source_fps(el)
                );

                vj_perform_feed_clip_target_clock(info,
                                                  p,
                                                  el,
                                                  target_frame,
                                                  client_frames_to_write,
                                                  frame_bytes);

                if (direct_jack_passthrough) {
                    external_frames = vj_audio_sync_render_monitor_s16(
                        &settings->audio_sync,
                        fresh,
                        client_frames_to_write,
                        frame_bytes,
                        el->audio_chans,
                        client_rate
                    );

                    if (external_frames > 0) {
                        if (external_frames < client_frames_to_write)
                            vj_audio_pad_exact_tail(fresh,
                                                    external_frames,
                                                    client_frames_to_write,
                                                    frame_bytes);

                        if (p)
                            vj_external_audio_history_append(p,
                                                             fresh,
                                                             client_frames_to_write,
                                                             frame_bytes);

                        vj_perform_feed_beat_analysis(info,
                                                      fresh,
                                                      client_frames_to_write,
                                                      frame_bytes,
                                                      el);
                    }

                    veejay_memset(audio_payload_chunk, 0, out_bytes);
                    return client_frames_to_write;
                }

                external_frames = vj_audio_sync_render_monitor_s16(
                    &settings->audio_sync,
                    fresh,
                    client_frames_to_write,
                    frame_bytes,
                    el->audio_chans,
                    client_rate
                );
            } else {
                if (direct_jack_passthrough) {
                    external_frames = vj_audio_sync_render_monitor_s16(
                        &settings->audio_sync,
                        fresh,
                        client_frames_to_write,
                        frame_bytes,
                        el->audio_chans,
                        client_rate
                    );

                    if (external_frames > 0) {
                        if (external_frames < client_frames_to_write)
                            vj_audio_pad_exact_tail(fresh,
                                                    external_frames,
                                                    client_frames_to_write,
                                                    frame_bytes);

                        if (p)
                            vj_external_audio_history_append(p,
                                                             fresh,
                                                             client_frames_to_write,
                                                             frame_bytes);

                        vj_perform_feed_beat_analysis(info,
                                                      fresh,
                                                      client_frames_to_write,
                                                      frame_bytes,
                                                      el);
                    }

                    veejay_memset(audio_payload_chunk, 0, out_bytes);
                    return client_frames_to_write;
                }

                external_frames = vj_audio_sync_render_monitor_s16(
                    &settings->audio_sync,
                    fresh,
                    client_frames_to_write,
                    frame_bytes,
                    el->audio_chans,
                    client_rate
                );
            }

            if (external_frames <= 0) {

                vj_external_audio_output_latest_or_silence(p,
                                                           audio_payload_chunk,
                                                           client_frames_to_write,
                                                           frame_bytes);
                if (p && p->external_audio_transport_active) {
                    vj_audio_declick_apply(p,
                                           audio_payload_chunk,
                                           client_frames_to_write,
                                           frame_bytes,
                                           AUDIO_PATH_DIRECT,
                                           speed,
                                           (speed < 0) ? -1 : 1,
                                           AUDIO_EDGE_JUMP,
                                           1);
                    vj_external_audio_smooth_block_start(p,
                                                         audio_payload_chunk,
                                                         client_frames_to_write,
                                                         frame_bytes,
                                                         1);
                    p->external_audio_transport_active = 0;
                } else {
                    vj_external_audio_store_tail(p,
                                                 audio_payload_chunk,
                                                 client_frames_to_write,
                                                 frame_bytes);
                }
                return client_frames_to_write;
            }

            if (external_frames < client_frames_to_write)
                vj_audio_pad_exact_tail(fresh,
                                        external_frames,
                                        client_frames_to_write,
                                        frame_bytes);

            if (p && fresh != NULL)
                vj_external_audio_history_append(p,
                                                 fresh,
                                                 client_frames_to_write,
                                                 frame_bytes);


            const double speed_dir = (speed < 0) ? -1.0 : ((speed > 0) ? 1.0 : 0.0);
            const int speed_mag = (speed_abs < 1) ? 1 : speed_abs;
            double transport_rate = speed_dir * (double)speed_mag * effective_rate;
            const int wants_transport = direct_jack_passthrough ? 0 : external_transport_requested;

            if (!wants_transport || !p) {
                const int was_transporting = (p && p->external_audio_transport_active);


                if (fresh != audio_payload_chunk)
                    veejay_memcpy(audio_payload_chunk, fresh, out_bytes);

                if(p)
                    p->external_audio_transport_active = 0;

                if(was_transporting) {
                    vj_audio_declick_apply(p,
                                           audio_payload_chunk,
                                           client_frames_to_write,
                                           frame_bytes,
                                           AUDIO_PATH_DIRECT,
                                           speed,
                                           (speed < 0) ? -1 : 1,
                                           AUDIO_EDGE_JUMP,
                                           1);
                    vj_external_audio_smooth_block_start(p,
                                                         audio_payload_chunk,
                                                         client_frames_to_write,
                                                         frame_bytes,
                                                         1);
                } else {
                    vj_external_audio_store_tail(p,
                                                 audio_payload_chunk,
                                                 client_frames_to_write,
                                                 frame_bytes);
                }

                return client_frames_to_write;
            }


            if (speed == 0 || fabs(transport_rate) < 0.0001) {
                veejay_memset(audio_payload_chunk, 0, out_bytes);
                p->external_audio_transport_active = 0;
                p->external_audio_prev_valid = 0;
                return client_frames_to_write;
            }


            if(sync_mode == VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE && speed < 0) {
                double reverse_rate = fabs(transport_rate);
                int rendered = 0;


                if(reverse_rate < 1.0)
                    reverse_rate = 1.0;
                if(reverse_rate > (double)MAX_SPEED)
                    reverse_rate = (double)MAX_SPEED;

                rendered = vj_external_audio_render_reverse_deck(p,
                                                                 audio_payload_chunk,
                                                                 client_frames_to_write,
                                                                 frame_bytes,
                                                                 reverse_rate,
                                                                 client_rate);

                if(rendered > 0) {
                    vj_external_audio_smooth_block_start(p,
                                                         audio_payload_chunk,
                                                         client_frames_to_write,
                                                         frame_bytes,
                                                         0);
                    vj_audio_declick_observe(p,
                                             audio_payload_chunk,
                                             client_frames_to_write,
                                             frame_bytes,
                                             AUDIO_PATH_DIRECT,
                                             speed,
                                             -1);
                    return client_frames_to_write;
                }
            }

            const int external_pitch_route =
                (speed > 1) ||
                (speed <= -2 && sync_mode != VJ_AUDIO_SYNC_MODE_TEMPO_BRIDGE) ||
                (speed == 1 && effective_rate > 1.0005);

            if (external_pitch_route) {
                double pitch_rate = fabs(transport_rate);
                int pitch_ready;
                int rendered;

                if (pitch_rate < 1.0)
                    pitch_rate = 1.0;
                if (pitch_rate > (double)MAX_SPEED)
                    pitch_rate = (double)MAX_SPEED;

                pitch_ready = (int)ceil((double)client_frames_to_write * pitch_rate) + 8;

                if (vj_external_audio_history_ready(p, pitch_ready, frame_bytes)) {


                    rendered = vj_external_audio_render_tape_deck(p,
                                                               audio_payload_chunk,
                                                               client_frames_to_write,
                                                               frame_bytes,
                                                               pitch_rate,
                                                               (speed < 0),
                                                               client_rate);
                    if (rendered > 0) {


                        p->external_audio_transport_active = 1;
                        vj_external_audio_smooth_block_start(p,
                                                             audio_payload_chunk,
                                                             client_frames_to_write,
                                                             frame_bytes,
                                                             0);
                        vj_audio_declick_observe(p,
                                                 audio_payload_chunk,
                                                 client_frames_to_write,
                                                 frame_bytes,
                                                 AUDIO_PATH_DIRECT,
                                                 speed,
                                                 (speed < 0) ? -1 : 1);
                        return client_frames_to_write;
                    }
                }
            }

            int min_ready = vj_external_audio_min_ready_frames(
                client_frames_to_write,
                transport_rate,
                client_rate
            );

            if (!vj_external_audio_history_ready(p, min_ready, frame_bytes)) {
                const int was_transporting = p->external_audio_transport_active;

                if (fresh != audio_payload_chunk)
                    veejay_memcpy(audio_payload_chunk, fresh, out_bytes);

                p->external_audio_transport_active = 0;

                if(was_transporting) {
                    vj_audio_declick_apply(p,
                                           audio_payload_chunk,
                                           client_frames_to_write,
                                           frame_bytes,
                                           AUDIO_PATH_DIRECT,
                                           speed,
                                           (speed < 0) ? -1 : 1,
                                           AUDIO_EDGE_JUMP,
                                           1);
                    vj_external_audio_smooth_block_start(p,
                                                         audio_payload_chunk,
                                                         client_frames_to_write,
                                                         frame_bytes,
                                                         1);
                } else {
                    vj_external_audio_store_tail(p,
                                                 audio_payload_chunk,
                                                 client_frames_to_write,
                                                 frame_bytes);
                }

                return client_frames_to_write;
            }

            int rendered = vj_external_audio_render_transport(p,
                                                              audio_payload_chunk,
                                                              client_frames_to_write,
                                                              frame_bytes,
                                                              transport_rate,
                                                              client_rate);
            if (rendered > 0) {
                vj_external_audio_smooth_block_start(p,
                                                     audio_payload_chunk,
                                                     client_frames_to_write,
                                                     frame_bytes,
                                                     0);
                vj_audio_declick_observe(p,
                                         audio_payload_chunk,
                                         client_frames_to_write,
                                         frame_bytes,
                                         AUDIO_PATH_DIRECT,
                                         speed,
                                         (speed < 0) ? -1 : 1);
                return client_frames_to_write;
            }

            if (fresh != audio_payload_chunk)
                veejay_memcpy(audio_payload_chunk, fresh, out_bytes);


            vj_audio_declick_apply(p,
                                   audio_payload_chunk,
                                   client_frames_to_write,
                                   frame_bytes,
                                   AUDIO_PATH_DIRECT,
                                   speed,
                                   (speed < 0) ? -1 : 1,
                                   AUDIO_EDGE_JUMP,
                                   1);
            vj_external_audio_smooth_block_start(p,
                                                 audio_payload_chunk,
                                                 client_frames_to_write,
                                                 frame_bytes,
                                                 1);
            p->external_audio_transport_active = 0;
            return client_frames_to_write;
        }
    }
#endif

    if ((audio_source == VJ_RECORD_AUDIO_SOURCE_AUTO ||
         audio_source == VJ_RECORD_AUDIO_SOURCE_ORIGINAL) &&
        speed_abs == 1 &&
        rate > 0.9995 && rate < 1.0005 &&
        effective_rate > 0.9995 && effective_rate < 1.0005)
    {
        if (!p || !p->top_audio_buffer) {
            veejay_memset(audio_payload_chunk,
                          0,
                          (size_t)client_frames_to_write * (size_t)frame_bytes);
            return client_frames_to_write;
        }

        int got = vj_perform_queue_audio_frame(info,
                                               (void*)p,
                                               p->top_audio_buffer,
                                               speed,
                                               target_frame,
                                               info->uc->sample_id);

        if (got <= 0) {
            veejay_memset(audio_payload_chunk,
                          0,
                          (size_t)client_frames_to_write * (size_t)frame_bytes);
            return client_frames_to_write;
        }

        vj_audio_consume_chain(info, p->top_audio_buffer, got);

        if (got == client_frames_to_write) {
            veejay_memcpy(audio_payload_chunk,
                          p->top_audio_buffer,
                          (size_t)client_frames_to_write * (size_t)frame_bytes);
            vj_perform_feed_beat_analysis(info,
                                          audio_payload_chunk,
                                          client_frames_to_write,
                                          frame_bytes,
                                          el);
            vj_perform_enqueue_output_target_clock(info,
                                                   p,
                                                   el,
                                                   target_frame,
                                                   audio_payload_chunk,
                                                   client_frames_to_write,
                                                   frame_bytes);
            return client_frames_to_write;
        }

        if (!(frame_bytes & 1)) {
            int out = vj_audio_retime_slow_cubic_s16(audio_payload_chunk,
                                                     client_frames_to_write,
                                                     p->top_audio_buffer,
                                                     got,
                                                     frame_bytes,
                                                     1.0);
            vj_perform_feed_beat_analysis(info,
                                          audio_payload_chunk,
                                          client_frames_to_write,
                                          frame_bytes,
                                          el);
            vj_perform_enqueue_output_target_clock(info,
                                                   p,
                                                   el,
                                                   target_frame,
                                                   audio_payload_chunk,
                                                   client_frames_to_write,
                                                   frame_bytes);
            return out;
        }

        int n = (got < client_frames_to_write) ? got : client_frames_to_write;
        if (n > 0)
            veejay_memcpy(audio_payload_chunk,
                          p->top_audio_buffer,
                          (size_t)n * (size_t)frame_bytes);
        vj_audio_pad_exact_tail(audio_payload_chunk,
                                n,
                                client_frames_to_write,
                                frame_bytes);
        vj_perform_feed_beat_analysis(info,
                                      audio_payload_chunk,
                                      client_frames_to_write,
                                      frame_bytes,
                                      el);
        vj_perform_enqueue_output_target_clock(info,
                                               p,
                                               el,
                                               target_frame,
                                               audio_payload_chunk,
                                               client_frames_to_write,
                                               frame_bytes);
        return client_frames_to_write;
    }

    if (effective_rate < 0.9995 && frame_bytes > 0 && !(frame_bytes & 1)) {
        int out = vj_perform_runtime_slow_audio_chunk(info,
                                                      p,
                                                      el,
                                                      audio_payload_chunk,
                                                      client_frames_to_write,
                                                      target_frame,
                                                      frame_bytes,
                                                      effective_rate);
        if(out > 0) {
            vj_perform_feed_beat_analysis(info,
                                          audio_payload_chunk,
                                          client_frames_to_write,
                                          frame_bytes,
                                          el);
            vj_perform_enqueue_output_target_clock(info,
                                                   p,
                                                   el,
                                                   target_frame,
                                                   audio_payload_chunk,
                                                   client_frames_to_write,
                                                   frame_bytes);
        }
        return out;
    }

    int num_samples = vj_perform_queue_audio_frame(info,
                                                   (void*)p,
                                                   p->top_audio_buffer,
                                                   speed,
                                                   target_frame,
                                                   info->uc->sample_id);

    if (num_samples > 0)
        vj_audio_consume_chain(info, p->top_audio_buffer, num_samples);

    int out = vj_perform_retime_audio_chunk(info,
                                            p,
                                            el,
                                            audio_payload_chunk,
                                            client_frames_to_write,
                                            p->top_audio_buffer,
                                            num_samples,
                                            frame_bytes);

    if(out > 0) {
        vj_perform_feed_beat_analysis(info,
                                      audio_payload_chunk,
                                      client_frames_to_write,
                                      frame_bytes,
                                      el);
        vj_perform_enqueue_output_target_clock(info,
                                               p,
                                               el,
                                               target_frame,
                                               audio_payload_chunk,
                                               client_frames_to_write,
                                               frame_bytes);
    }

    return out;
}

static int32_t read_sample(const uint8_t *buf, int bytes_per_sample) {
    int32_t s = 0;

    switch (bytes_per_sample) {
        case 1: s = (int8_t)buf[0]; break;
        case 2: s = (int16_t)(buf[0] | (buf[1] << 8)); break;
        case 3:
            s = buf[0] | (buf[1] << 8) | (buf[2] << 16);
            if (s & 0x800000) s |= ~0xFFFFFF;
            break;
        case 4:
            s = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
            break;
        default: s = 0; break;
    }

    return s;
}

static void write_sample(uint8_t *buf, int bytes_per_sample, int32_t s) {
    int32_t max_val = (1 << (bytes_per_sample*8 - 1)) - 1;
    int32_t min_val = -(1 << (bytes_per_sample*8 - 1));
    if (s > max_val) s = max_val;
    if (s < min_val) s = min_val;

    switch (bytes_per_sample) {
        case 1: buf[0] = (uint8_t)s; break;
        case 2:
            buf[0] = s & 0xFF;
            buf[1] = (s >> 8) & 0xFF;
            break;
        case 3:
            buf[0] = s & 0xFF;
            buf[1] = (s >> 8) & 0xFF;
            buf[2] = (s >> 16) & 0xFF;
            break;
        case 4:
            {
                uint32_t u = (uint32_t)s;
                buf[0] = u & 0xFF;
                buf[1] = (u >> 8) & 0xFF;
                buf[2] = (u >> 16) & 0xFF;
                buf[3] = (u >> 24) & 0xFF;
            }
            break;
        default: break;
    }
}

#define POST_MIX_TRIM 0.70710678f

int vj_audio_crossfade_buffers(
    performer_global_t *g,
    const uint8_t *buf_a,
    const uint8_t *buf_b,
    uint8_t *out,
    int num_samples,
    int num_channels,
    int bytesperframe,
    float t,
    float opacity_a,
    float opacity_b
) {

    if (num_samples <= 0)
        return 0;

    t = fminf(fmaxf(t, 0.0f), 1.0f);

    int bps = bytesperframe / num_channels;

    const int lut_max = (int)ceil(num_samples) - 1;
    const float fidx = t * (float)lut_max;
    const int idx0 = (int)fidx;
    const int idx1 = (idx0 < lut_max) ? idx0 + 1 : idx0;
    const float frac = fidx - (float)idx0;

    const float g_out =
        g->gain_lut[0][idx0] +
        frac * (g->gain_lut[0][idx1] - g->gain_lut[0][idx0]);

    const float g_in  =
        g->gain_lut[1][idx0] +
        frac * (g->gain_lut[1][idx1] - g->gain_lut[1][idx0]);

    const float gain_a = g_out * opacity_a;
    const float gain_b = g_in  * opacity_b;

    const float max_val =
        (bps == 1) ? 127.0f :
        (bps == 2) ? 32767.0f :
        (bps == 3) ? 8388607.0f :
                     2147483647.0f;

    for (int i = 0; i < num_samples; i++) {
        for (int ch = 0; ch < num_channels; ch++) {
            const int off = (i * num_channels + ch) * bps;

            float sa = buf_a ? (float)read_sample(&buf_a[off], bps) : 0.0f;
            float sb = buf_b ? (float)read_sample(&buf_b[off], bps) : 0.0f;

            if (bps == 1) {
                sa -= 128.0f;
                sb -= 128.0f;
            }

            float mixed = (sa * gain_a + sb * gain_b) * POST_MIX_TRIM;

            if (mixed > max_val) mixed = max_val;
            else if (mixed < -max_val) mixed = -max_val;

            int32_t out_val =
                (bps == 1) ? (int32_t)(mixed + 128.0f)
                           : (int32_t)mixed;

            write_sample(&out[off], bps, out_val);
        }
    }

    return num_samples;
}


static void vj_external_audio_history_reset(performer_t *p, int frame_bytes)
{
    if(!p)
        return;

    p->external_audio_history_write = 0;
    p->external_audio_history_filled = 0;
    p->external_audio_history_frame_bytes = frame_bytes;
    p->external_audio_history_abs_write = 0;
    p->external_audio_read_pos = 0.0;
    p->external_audio_read_vel = 0.0;
    p->external_audio_transport_active = 0;
    p->external_audio_last_speed = 0;
    p->external_audio_last_rate_key = 0;
    p->external_audio_last_sync_key = -1;
    p->external_audio_prev_valid = 0;
    p->external_audio_prev_frame_bytes = 0;
    veejay_memset(p->external_audio_prev_frame, 0, sizeof(p->external_audio_prev_frame));

    if(p->audio_scratcher)
        vj_scratch_reset(p->audio_scratcher);
}

static void vj_external_audio_history_append(performer_t *p,
                                             const uint8_t *src,
                                             int frames,
                                             int frame_bytes)
{
    if(!p || !p->external_audio_history || !src || frames <= 0 ||
       frame_bytes <= 0 || p->external_audio_history_capacity <= 0)
        return;

    if(p->external_audio_history_frame_bytes != frame_bytes)
        vj_external_audio_history_reset(p, frame_bytes);

    const size_t cap = p->external_audio_history_capacity;
    size_t bytes = (size_t)frames * (size_t)frame_bytes;

    if(bytes >= cap) {
        const uint8_t *tail = src + (bytes - cap);
        veejay_memcpy(p->external_audio_history, tail, cap);
        p->external_audio_history_write = 0;
        p->external_audio_history_filled = cap;
        p->external_audio_history_abs_write += frames;
        return;
    }

    size_t wr = p->external_audio_history_write;
    size_t first = cap - wr;
    if(first > bytes)
        first = bytes;

    veejay_memcpy(p->external_audio_history + wr, src, first);
    if(bytes > first)
        veejay_memcpy(p->external_audio_history, src + first, bytes - first);

    wr += bytes;
    if(wr >= cap)
        wr -= cap;

    p->external_audio_history_write = wr;
    p->external_audio_history_filled =
        (p->external_audio_history_filled + bytes > cap)
            ? cap
            : (p->external_audio_history_filled + bytes);
    p->external_audio_history_abs_write += frames;
}

static int vj_external_audio_history_read_latest(performer_t *p,
                                                 uint8_t *dst,
                                                 int wanted_frames,
                                                 int frame_bytes)
{
    if(!p || !p->external_audio_history || !dst || wanted_frames <= 0 ||
       frame_bytes <= 0 || p->external_audio_history_frame_bytes != frame_bytes)
        return 0;

    size_t wanted_bytes = (size_t)wanted_frames * (size_t)frame_bytes;
    size_t avail = p->external_audio_history_filled;

    if(avail <= 0)
        return 0;

    if(wanted_bytes > avail)
        wanted_bytes = avail - (avail % (size_t)frame_bytes);

    if(wanted_bytes <= 0)
        return 0;

    if(p->external_audio_context_capacity > 0 && wanted_bytes > p->external_audio_context_capacity)
        wanted_bytes = p->external_audio_context_capacity -
            (p->external_audio_context_capacity % (size_t)frame_bytes);

    if(wanted_bytes <= 0)
        return 0;

    const size_t cap = p->external_audio_history_capacity;
    size_t start = (p->external_audio_history_write + cap - wanted_bytes) % cap;
    size_t first = cap - start;
    if(first > wanted_bytes)
        first = wanted_bytes;

    veejay_memcpy(dst, p->external_audio_history + start, first);
    if(wanted_bytes > first)
        veejay_memcpy(dst + first, p->external_audio_history, wanted_bytes - first);

    return (int)(wanted_bytes / (size_t)frame_bytes);
}

static int vj_external_audio_output_latest_or_silence(performer_t *p,
                                                      uint8_t *dst,
                                                      int wanted_frames,
                                                      int frame_bytes)
{
    if(!dst || wanted_frames <= 0 || frame_bytes <= 0)
        return 0;

    int got = vj_external_audio_history_read_latest(p,
                                                    dst,
                                                    wanted_frames,
                                                    frame_bytes);

    if(got <= 0) {
        veejay_memset(dst, 0, (size_t)wanted_frames * (size_t)frame_bytes);
        return wanted_frames;
    }

    if(got < wanted_frames)
        vj_audio_pad_exact_tail(dst, got, wanted_frames, frame_bytes);

    return wanted_frames;
}

static int vj_external_audio_history_ready(performer_t *p,
                                           int needed_frames,
                                           int frame_bytes)
{
    if(!p || frame_bytes <= 0 || needed_frames <= 0)
        return 0;

    if(p->external_audio_history_frame_bytes != frame_bytes)
        return 0;

    size_t need = (size_t)needed_frames * (size_t)frame_bytes;
    return p->external_audio_history_filled >= need;
}


static int vj_external_audio_min_ready_frames(int dst_frames,
                                              double transport_rate,
                                              int sample_rate)
{
    double av = fabs(transport_rate);
    int ready = dst_frames * 4;

    if(sample_rate <= 0)
        sample_rate = 44100;

    if(av < 0.01)
        av = 0.01;

    if(transport_rate < 0.0) {
        int rev = sample_rate / 2;
        if(rev > ready)
            ready = rev;
    }
    else if(av > 1.0005) {
        int burst = (int)((double)dst_frames * (av + 4.0));
        int half_sec = sample_rate / 2;
        ready = burst;
        if(half_sec > ready)
            ready = half_sec;
    }
    else if(av < 0.9995) {
        int slow = dst_frames * 8;
        if(slow > ready)
            ready = slow;
    }

    if(ready < dst_frames * 3)
        ready = dst_frames * 3;

    return ready;
}

static inline int16_t vj_external_audio_lerp_s16(int a, int b, double f)
{
    double v = (double)a + ((double)b - (double)a) * f;
    int iv = (int)((v >= 0.0) ? (v + 0.5) : (v - 0.5));

    if(iv < -32768)
        iv = -32768;
    else if(iv > 32767)
        iv = 32767;

    return (int16_t)iv;
}

static inline int16_t vj_external_audio_cubic_s16(int p0, int p1, int p2, int p3, double t)
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    double y = 0.5 * ((2.0 * (double)p1) +
        ((double)(-p0 + p2) * t) +
        ((double)(2 * p0 - 5 * p1 + 4 * p2 - p3) * t2) +
        ((double)(-p0 + 3 * p1 - 3 * p2 + p3) * t3));

    int iv = (int)((y >= 0.0) ? (y + 0.5) : (y - 0.5));
    if(iv < -32768)
        iv = -32768;
    else if(iv > 32767)
        iv = 32767;

    return (int16_t)iv;
}



static inline int16_t vj_external_audio_sample_history_s16(const int16_t *hist,
                                                          int cap_frames,
                                                          int words,
                                                          long long oldest,
                                                          long long newest,
                                                          double pos,
                                                          int channel)
{
    if(pos < (double)oldest)
        pos = (double)oldest;
    else if(pos > (double)newest)
        pos = (double)newest;

    long long ip = (long long)floor(pos);
    double frac = pos - (double)ip;

    long long ip0 = ip - 1;
    long long ip1 = ip;
    long long ip2 = ip + 1;
    long long ip3 = ip + 2;

    if(ip0 < oldest) ip0 = oldest;
    if(ip1 < oldest) ip1 = oldest;
    if(ip2 > newest) ip2 = newest;
    if(ip3 > newest) ip3 = newest;

    int idx0 = (int)(ip0 % (long long)cap_frames);
    int idx1 = (int)(ip1 % (long long)cap_frames);
    int idx2 = (int)(ip2 % (long long)cap_frames);
    int idx3 = (int)(ip3 % (long long)cap_frames);
    if(idx0 < 0) idx0 += cap_frames;
    if(idx1 < 0) idx1 += cap_frames;
    if(idx2 < 0) idx2 += cap_frames;
    if(idx3 < 0) idx3 += cap_frames;

    return vj_external_audio_cubic_s16(
        hist[(idx0 * words) + channel],
        hist[(idx1 * words) + channel],
        hist[(idx2 * words) + channel],
        hist[(idx3 * words) + channel],
        frac
    );
}

static int vj_external_audio_copy_history_window(performer_t *p,
                                                 uint8_t *dst,
                                                 int frames,
                                                 int frame_bytes,
                                                 long long first_abs,
                                                 int reverse)
{
    if(!p || !p->external_audio_history || !dst || frames <= 0 ||
       frame_bytes <= 0 || p->external_audio_history_frame_bytes != frame_bytes)
        return 0;

    const int cap_frames = (int)(p->external_audio_history_capacity / (size_t)frame_bytes);
    const int avail_frames = (int)(p->external_audio_history_filled / (size_t)frame_bytes);

    if(cap_frames <= 0 || avail_frames <= 0)
        return 0;

    const long long newest = p->external_audio_history_abs_write - 1;
    const long long oldest = p->external_audio_history_abs_write - (long long)avail_frames;
    long long last_abs = reverse ? (first_abs - (long long)(frames - 1))
                                 : (first_abs + (long long)(frames - 1));

    if(first_abs < oldest || first_abs > newest || last_abs < oldest || last_abs > newest)
        return 0;

    if(!reverse) {
        int idx = (int)(first_abs % (long long)cap_frames);
        if(idx < 0)
            idx += cap_frames;

        size_t bytes = (size_t)frames * (size_t)frame_bytes;
        size_t off = (size_t)idx * (size_t)frame_bytes;
        size_t cap_bytes = (size_t)cap_frames * (size_t)frame_bytes;
        size_t first = cap_bytes - off;
        if(first > bytes)
            first = bytes;

        veejay_memcpy(dst, p->external_audio_history + off, first);
        if(bytes > first)
            veejay_memcpy(dst + first, p->external_audio_history, bytes - first);
        return frames;
    }

    uint8_t *out = dst;
    for(int i = 0; i < frames; i++) {
        long long abs_pos = first_abs - (long long)i;
        int idx = (int)(abs_pos % (long long)cap_frames);
        if(idx < 0)
            idx += cap_frames;

        veejay_memcpy(out,
                      p->external_audio_history + ((size_t)idx * (size_t)frame_bytes),
                      frame_bytes);
        out += frame_bytes;
    }

    return frames;
}

static int vj_external_audio_render_reverse_deck(performer_t *p,
                                                    uint8_t *dst,
                                                    int dst_frames,
                                                    int frame_bytes,
                                                    double deck_rate,
                                                    int sample_rate)
{
    if(!p || !dst || !p->external_audio_history || dst_frames <= 0 ||
       frame_bytes <= 0 || (frame_bytes & 1) ||
       p->external_audio_history_frame_bytes != frame_bytes)
        return 0;

    const int words = frame_bytes / 2;
    const int cap_frames = (int)(p->external_audio_history_capacity / (size_t)frame_bytes);
    const int avail_frames = (int)(p->external_audio_history_filled / (size_t)frame_bytes);

    if(cap_frames <= 8 || avail_frames <= dst_frames + 8)
        return 0;

    if(sample_rate <= 0)
        sample_rate = 44100;

    if(deck_rate < 1.0)
        deck_rate = 1.0;
    if(deck_rate > (double)MAX_SPEED)
        deck_rate = (double)MAX_SPEED;

    const long long newest = p->external_audio_history_abs_write - 1;
    const long long oldest = p->external_audio_history_abs_write - (long long)avail_frames;

    if(newest <= oldest + (long long)(dst_frames * 4))
        return 0;

    double guard_old = (double)oldest + 8.0;
    int latency = sample_rate / 2;
    if(latency < dst_frames * 8)
        latency = dst_frames * 8;

    double safe_new = (double)newest - (double)latency;
    if(safe_new <= guard_old + (double)(dst_frames * 4)) {
        latency = dst_frames * 4;
        safe_new = (double)newest - (double)latency;
    }

    if(safe_new <= guard_old + (double)(dst_frames * 2))
        return 0;

    const int rate_key = (int)(deck_rate * 1000.0 + 0.5);
    const int mode_key = -3000;

    if(!p->external_audio_transport_active ||
       p->external_audio_last_speed != mode_key)
    {
        p->external_audio_read_pos = safe_new;
        p->external_audio_read_vel = -deck_rate;
        p->external_audio_transport_active = 1;
        p->external_audio_last_speed = mode_key;
        p->external_audio_last_rate_key = rate_key;
    }
    else {
        const double target_vel = -deck_rate;
        double alpha = 0.22;
        if(fabs(target_vel - p->external_audio_read_vel) > 1.0)
            alpha = 0.40;
        p->external_audio_read_vel += (target_vel - p->external_audio_read_vel) * alpha;
        p->external_audio_last_rate_key = rate_key;
    }

    if(p->external_audio_read_vel > -0.01)
        p->external_audio_read_vel = -deck_rate;

    double start = p->external_audio_read_pos;
    double end = start + p->external_audio_read_vel * (double)(dst_frames - 1);

    if(start > safe_new)
        start = safe_new;

    if(end < guard_old) {


        start = safe_new;
        end = start + p->external_audio_read_vel * (double)(dst_frames - 1);
    }

    if(end < guard_old || start > (double)newest - 4.0)
        return 0;

    p->external_audio_read_pos = start;

    const int16_t *hist = (const int16_t*)p->external_audio_history;
    int16_t *out = (int16_t*)dst;

    for(int i = 0; i < dst_frames; i++) {
        double pos = p->external_audio_read_pos + p->external_audio_read_vel * (double)i;

        if(pos < guard_old)
            pos = guard_old;
        else if(pos > (double)newest - 4.0)
            pos = (double)newest - 4.0;

        long long ip = (long long)floor(pos);
        double frac = pos - (double)ip;

        long long ip0 = ip - 1;
        long long ip1 = ip;
        long long ip2 = ip + 1;
        long long ip3 = ip + 2;

        if(ip0 < oldest) ip0 = oldest;
        if(ip1 < oldest) ip1 = oldest;
        if(ip2 > newest) ip2 = newest;
        if(ip3 > newest) ip3 = newest;

        int idx0 = (int)(ip0 % (long long)cap_frames);
        int idx1 = (int)(ip1 % (long long)cap_frames);
        int idx2 = (int)(ip2 % (long long)cap_frames);
        int idx3 = (int)(ip3 % (long long)cap_frames);
        if(idx0 < 0) idx0 += cap_frames;
        if(idx1 < 0) idx1 += cap_frames;
        if(idx2 < 0) idx2 += cap_frames;
        if(idx3 < 0) idx3 += cap_frames;

        const int b0 = idx0 * words;
        const int b1 = idx1 * words;
        const int b2 = idx2 * words;
        const int b3 = idx3 * words;
        const int bo = i * words;

        for(int c = 0; c < words; c++) {
            out[bo + c] = vj_external_audio_cubic_s16(
                hist[b0 + c],
                hist[b1 + c],
                hist[b2 + c],
                hist[b3 + c],
                frac
            );
        }
    }

    p->external_audio_read_pos += p->external_audio_read_vel * (double)dst_frames;
    return dst_frames;
}


static int vj_external_audio_render_tape_deck(performer_t *p,
                                              uint8_t *dst,
                                              int dst_frames,
                                              int frame_bytes,
                                              double deck_rate,
                                              int reverse,
                                              int sample_rate)
{
    if(!p || !dst || !p->external_audio_history || !p->external_audio_context ||
       !p->audio_scratcher || dst_frames <= 0 || frame_bytes <= 0 ||
       (frame_bytes & 1) || p->external_audio_history_frame_bytes != frame_bytes)
        return 0;

    const int cap_frames = (int)(p->external_audio_history_capacity / (size_t)frame_bytes);
    const int avail_frames = (int)(p->external_audio_history_filled / (size_t)frame_bytes);
    int context_cap_frames = (int)(p->external_audio_context_capacity / (size_t)frame_bytes);

    if(cap_frames <= 8 || avail_frames <= dst_frames + 8 || context_cap_frames <= dst_frames + 8)
        return 0;

    if(sample_rate <= 0)
        sample_rate = 44100;

    if(deck_rate < 1.0)
        deck_rate = 1.0;
    if(deck_rate > (double)MAX_SPEED)
        deck_rate = (double)MAX_SPEED;

    const long long newest = p->external_audio_history_abs_write - 1;
    const long long oldest = p->external_audio_history_abs_write - (long long)avail_frames;

    if(newest <= oldest + 32)
        return 0;

    int latency = (sample_rate * 3) / 2;
    int min_latency = dst_frames * 8;
    if(latency < min_latency)
        latency = min_latency;

    long long safe_newest = newest - (long long)latency;
    long long safe_oldest = oldest + 16;

    if(safe_newest <= safe_oldest + (long long)(dst_frames * 4)) {
        latency = sample_rate / 2;
        if(latency < dst_frames * 4)
            latency = dst_frames * 4;
        safe_newest = newest - (long long)latency;
    }

    if(safe_newest <= safe_oldest + (long long)(dst_frames * 4))
        return 0;

    int safe_frames = (int)(safe_newest - safe_oldest + 1);
    int min_context = (int)ceil((double)dst_frames * deck_rate) + 64;
    int context_frames = (sample_rate * 9) / 5;

    if(context_frames < sample_rate)
        context_frames = sample_rate;
    if(context_frames < min_context * 3)
        context_frames = min_context * 3;
    if(context_frames > context_cap_frames)
        context_frames = context_cap_frames;
    if(context_frames > safe_frames)
        context_frames = safe_frames;

    if(context_frames < min_context)
        return 0;


    if(context_frames > 8)
        context_frames -= 4;

    long long first_abs;
    if(reverse)
        first_abs = safe_newest;
    else
        first_abs = safe_newest - (long long)context_frames + 1;

    if(vj_external_audio_copy_history_window(p,
                                             p->external_audio_context,
                                             context_frames,
                                             frame_bytes,
                                             first_abs,
                                             reverse) != context_frames)
        return 0;

    const int mode_key = reverse ? -2 : 2;
    const int rate_key = (int)(deck_rate * 1000.0 + 0.5);

    if(!p->external_audio_transport_active ||
       p->external_audio_last_speed != mode_key)
    {
        vj_scratch_reset(p->audio_scratcher);
        p->external_audio_read_pos = 0.0;
        p->external_audio_read_vel = reverse ? -deck_rate : deck_rate;
        p->external_audio_transport_active = 1;
        p->external_audio_last_speed = mode_key;
        p->external_audio_last_rate_key = rate_key;
    }
    else
    {
        const double target_vel = reverse ? -deck_rate : deck_rate;
        double alpha = 0.18;
        if(fabs(target_vel - p->external_audio_read_vel) > 1.0)
            alpha = 0.35;
        p->external_audio_read_vel += (target_vel - p->external_audio_read_vel) * alpha;
        p->external_audio_last_rate_key = rate_key;
    }

    double scratch_rate = fabs(p->external_audio_read_vel);
    if(scratch_rate < 1.0)
        scratch_rate = 1.0;
    if(scratch_rate > (double)MAX_SPEED)
        scratch_rate = (double)MAX_SPEED;

    int produced = vj_scratch_process(p->audio_scratcher,
                                      (short*)dst,
                                      dst_frames,
                                      (const short*)p->external_audio_context,
                                      context_frames,
                                      scratch_rate);

    if(produced <= 0) {


        vj_scratch_reset(p->audio_scratcher);
        produced = vj_scratch_process(p->audio_scratcher,
                                      (short*)dst,
                                      dst_frames,
                                      (const short*)p->external_audio_context,
                                      context_frames,
                                      scratch_rate);
    }

    if(produced <= 0)
        return 0;

    if(produced > dst_frames)
        produced = dst_frames;

    if(produced < dst_frames)
        vj_audio_pad_exact_tail(dst, produced, dst_frames, frame_bytes);

    p->external_audio_read_pos += scratch_rate * (double)dst_frames;


    const double phase_limit = (double)sample_rate * 60.0;
    if(p->external_audio_read_pos > phase_limit)
        p->external_audio_read_pos = fmod(p->external_audio_read_pos, (double)sample_rate);

    return dst_frames;
}


static void vj_external_audio_store_tail(performer_t *p,
                                         const uint8_t *buf,
                                         int frames,
                                         int frame_bytes)
{
    if(!p || !buf || frames <= 0 || frame_bytes <= 0)
        return;

    if(frame_bytes > (int)sizeof(p->external_audio_prev_frame)) {
        p->external_audio_prev_valid = 0;
        p->external_audio_prev_frame_bytes = 0;
        return;
    }

    const uint8_t *last = buf + ((size_t)(frames - 1) * (size_t)frame_bytes);
    veejay_memcpy(p->external_audio_prev_frame, last, frame_bytes);
    p->external_audio_prev_valid = 1;
    p->external_audio_prev_frame_bytes = frame_bytes;
}

static void vj_external_audio_smooth_block_start(performer_t *p,
                                                 uint8_t *buf,
                                                 int frames,
                                                 int frame_bytes,
                                                 int force_edge)
{
    if(!p || !buf || frames <= 0 || frame_bytes <= 0 || (frame_bytes & 1))
        return;

    if(!p->external_audio_prev_valid ||
       p->external_audio_prev_frame_bytes != frame_bytes ||
       frame_bytes > (int)sizeof(p->external_audio_prev_frame))
    {
        vj_external_audio_store_tail(p, buf, frames, frame_bytes);
        return;
    }

    const int words = frame_bytes / 2;
    const int16_t *prev = (const int16_t*)p->external_audio_prev_frame;
    int16_t *out = (int16_t*)buf;


    int peak_delta = 0;
    for(int c = 0; c < words; c++) {
        int d = (int)out[c] - (int)prev[c];
        if(d < 0)
            d = -d;
        if(d > peak_delta)
            peak_delta = d;
    }

    if(!force_edge && peak_delta < 512) {
        vj_external_audio_store_tail(p, buf, frames, frame_bytes);
        return;
    }

    int fade;
    if(force_edge)
        fade = (peak_delta > 12000) ? 160 : ((peak_delta > 6000) ? 128 : 96);
    else
        fade = (peak_delta > 12000) ? 96 : ((peak_delta > 6000) ? 64 : 32);

    if(fade > frames)
        fade = frames;
    if(fade <= 0) {
        vj_external_audio_store_tail(p, buf, frames, frame_bytes);
        return;
    }


    const double denom = (fade > 1) ? (double)(fade - 1) : 1.0;
    for(int i = 0; i < fade; i++) {
        double t = (double)i / denom;
        double w = t * t * (3.0 - (2.0 * t));
        const int bo = i * words;

        for(int c = 0; c < words; c++) {
            double mixed = ((1.0 - w) * (double)prev[c]) +
                           (w * (double)out[bo + c]);
            int v = (int)((mixed >= 0.0) ? (mixed + 0.5) : (mixed - 0.5));
            if(v < -32768) v = -32768;
            else if(v > 32767) v = 32767;
            out[bo + c] = (int16_t)v;
        }
    }

    vj_external_audio_store_tail(p, buf, frames, frame_bytes);
}

static int vj_external_audio_render_transport(performer_t *p,
                                              uint8_t *dst,
                                              int dst_frames,
                                              int frame_bytes,
                                              double transport_rate,
                                              int sample_rate)
{
    if(!p || !dst || !p->external_audio_history || dst_frames <= 0 ||
       frame_bytes <= 0 || (frame_bytes & 1) ||
       p->external_audio_history_frame_bytes != frame_bytes)
        return 0;

    const int words = frame_bytes / 2;
    const int cap_frames = (int)(p->external_audio_history_capacity / (size_t)frame_bytes);
    const int avail_frames = (int)(p->external_audio_history_filled / (size_t)frame_bytes);

    if(cap_frames <= 8 || avail_frames <= dst_frames + 8)
        return 0;

    if(sample_rate <= 0)
        sample_rate = 44100;

    const long long newest = p->external_audio_history_abs_write - 1;
    const long long oldest = p->external_audio_history_abs_write - (long long)avail_frames;

    if(newest <= oldest + 8)
        return 0;

    const double av = fabs(transport_rate);
    int latency = dst_frames * 8;

    if(transport_rate < 0.0) {
        latency = sample_rate / 2;
    } else if(av > 1.0005) {
        latency = sample_rate / 2;
    } else if(av < 0.9995) {
        latency = sample_rate / 4;
    }

    if(latency < dst_frames * 4)
        latency = dst_frames * 4;

    int max_latency = avail_frames - (dst_frames * 3) - 16;
    if(max_latency < dst_frames * 2)
        return 0;
    if(latency > max_latency)
        latency = max_latency;

    const int transport_sign = (transport_rate < 0.0) ? -1 : 1;


    if(!p->external_audio_transport_active ||
       p->external_audio_last_speed != transport_sign)
    {
        p->external_audio_read_pos = (double)newest - (double)latency;
        p->external_audio_read_vel = transport_rate;
        p->external_audio_transport_active = 1;
        p->external_audio_last_speed = transport_sign;
        p->external_audio_last_rate_key = 0;
    }


    double alpha = 0.08;
    if(fabs(transport_rate - p->external_audio_read_vel) > 1.0)
        alpha = 0.14;
    p->external_audio_read_vel += (transport_rate - p->external_audio_read_vel) * alpha;


    const double guard_old = (double)oldest + 4.0;
    const double guard_new = (double)newest - 4.0;
    double start = p->external_audio_read_pos;
    double end = start + p->external_audio_read_vel * (double)(dst_frames - 1);
    double lo = (start < end ? start : end) - 2.0;
    double hi = (start > end ? start : end) + 3.0;

    if(lo < guard_old) {
        if(p->external_audio_read_vel < 0.0) {


            p->external_audio_read_vel *= 0.35;
            p->external_audio_read_pos = guard_old + (double)(dst_frames * 4);
        } else {


            p->external_audio_read_pos = (double)newest - (double)latency;
            if(p->external_audio_read_pos < guard_old + (double)(dst_frames * 2))
                p->external_audio_read_pos = guard_old + (double)(dst_frames * 2);
        }

        start = p->external_audio_read_pos;
        end = start + p->external_audio_read_vel * (double)(dst_frames - 1);
        lo = (start < end ? start : end) - 2.0;
        hi = (start > end ? start : end) + 3.0;

        if(lo < guard_old)
            return 0;
    }

    if(hi > guard_new) {
        if(p->external_audio_read_vel > 1.0) {


            p->external_audio_read_vel += (1.0 - p->external_audio_read_vel) * 0.55;
            end = start + p->external_audio_read_vel * (double)(dst_frames - 1);
            hi = (start > end ? start : end) + 3.0;

            if(hi > guard_new) {
                p->external_audio_read_vel = 1.0;
                p->external_audio_read_pos = guard_new - (double)(dst_frames + 8);
                if(p->external_audio_read_pos < guard_old + (double)(dst_frames * 2))
                    p->external_audio_read_pos = guard_old + (double)(dst_frames * 2);
            }
        } else {
            p->external_audio_read_pos = guard_new - (double)(dst_frames + 8);
        }

        start = p->external_audio_read_pos;
        end = start + p->external_audio_read_vel * (double)(dst_frames - 1);
        lo = (start < end ? start : end) - 2.0;
        hi = (start > end ? start : end) + 3.0;

        if(hi > guard_new || lo < guard_old)
            return 0;
    }

    const int16_t *hist = (const int16_t*)p->external_audio_history;
    int16_t *out = (int16_t*)dst;

    for(int i = 0; i < dst_frames; i++) {
        double pos = p->external_audio_read_pos + p->external_audio_read_vel * (double)i;

        if(pos < guard_old)
            pos = guard_old;
        else if(pos > guard_new)
            pos = guard_new;

        long long ip = (long long)floor(pos);
        double frac = pos - (double)ip;

        long long ip0 = ip - 1;
        long long ip1 = ip;
        long long ip2 = ip + 1;
        long long ip3 = ip + 2;

        if(ip0 < oldest) ip0 = oldest;
        if(ip1 < oldest) ip1 = oldest;
        if(ip2 > newest) ip2 = newest;
        if(ip3 > newest) ip3 = newest;

        int idx0 = (int)(ip0 % (long long)cap_frames);
        int idx1 = (int)(ip1 % (long long)cap_frames);
        int idx2 = (int)(ip2 % (long long)cap_frames);
        int idx3 = (int)(ip3 % (long long)cap_frames);
        if(idx0 < 0) idx0 += cap_frames;
        if(idx1 < 0) idx1 += cap_frames;
        if(idx2 < 0) idx2 += cap_frames;
        if(idx3 < 0) idx3 += cap_frames;

        const int b0 = idx0 * words;
        const int b1 = idx1 * words;
        const int b2 = idx2 * words;
        const int b3 = idx3 * words;
        const int bo = i * words;

        for(int c = 0; c < words; c++) {
            out[bo + c] = vj_external_audio_cubic_s16(
                hist[b0 + c],
                hist[b1 + c],
                hist[b2 + c],
                hist[b3 + c],
                frac
            );
        }
    }

    p->external_audio_read_pos += p->external_audio_read_vel * (double)dst_frames;
    return dst_frames;
}


static double vj_perform_runtime_audio_rate(veejay_t *info, editlist *el)
{
    if (info == NULL || info->settings == NULL || el == NULL || el->video_fps <= 0.0)
        return 1.0;

    video_playback_setup *settings = info->settings;
    double rate = settings->runtime_playback_rate;

    if (rate <= 0.0) {
        double fps = settings->output_fps;
        if (fps <= 0.0)
            fps = el->video_fps;
        rate = fps / (double)el->video_fps;
    }

    if (rate < 0.01)
        rate = 0.01;
    else if (rate > 16.0)
        rate = 16.0;

    return rate;
}

static int vj_perform_runtime_sfd(veejay_t *info)
{
    if (info == NULL || info->settings == NULL)
        return 1;

    video_playback_setup *settings = info->settings;
    int sfd = atomic_load_int(&settings->audio_slice_len);

    if (sfd < 1)
        sfd = settings->sfd;

    if (sfd < 1) {
        if (info->uc != NULL && info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE)
            sfd = sample_get_framedup(info->uc->sample_id);
        else
            sfd = info->sfd;
    }

    if (sfd < 1)
        sfd = 1;
    else if (sfd > MAX_SPEED_AV)
        sfd = MAX_SPEED_AV;

    return sfd;
}

static double vj_perform_runtime_effective_audio_rate(veejay_t *info, editlist *el)
{
    double rate = vj_perform_runtime_audio_rate(info, el);
    const int sfd = vj_perform_runtime_sfd(info);

    if (sfd > 1)
        rate /= (double)sfd;

    if (rate < 0.01)
        rate = 0.01;
    else if (rate > 16.0)
        rate = 16.0;

    return rate;
}


static int vj_perform_runtime_slow_audio_chunk(veejay_t *info,
                                               performer_t *p,
                                               editlist *el,
                                               uint8_t *dst,
                                               int dst_samples,
                                               long long target_frame,
                                               int frame_bytes,
                                               double rate)
{
    if (info == NULL || p == NULL || el == NULL || dst == NULL ||
        dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if ((frame_bytes & 1) || p->down_sample_buffer == NULL) {
        int pred_len = (el->audio_rate > 0 && el->video_fps > 0.0)
            ? (int)((double)el->audio_rate / (double)el->video_fps)
            : dst_samples;
        if (pred_len < 1)
            pred_len = 1;
        int got = vj_perform_queue_audio_frame(info, (void*)p, p->top_audio_buffer,
                                               info->settings->current_playback_speed,
                                               target_frame, info->uc->sample_id);
        return vj_audio_retime_slow_cubic_s16(dst, dst_samples, p->top_audio_buffer,
                                              got > 0 ? got : pred_len,
                                              frame_bytes, rate);
    }

    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*)info->performer;
    sample_b_t *posdata = (g != NULL && g->A != NULL && p == g->A)
        ? &(g->A->sample_a)
        : ((g != NULL && g->A != NULL) ? &(g->A->sample_b) : &(p->sample_b));
    audio_edge_t *edge = p->audio_edge;

    int pred_len = (el->audio_rate > 0 && el->video_fps > 0.0)
        ? (int)((double)el->audio_rate / (double)el->video_fps)
        : dst_samples;
    if (pred_len < 1)
        pred_len = 1;

    int speed = settings->current_playback_speed;
    int cur_dir = (speed > 0) ? 1 : ((speed < 0) ? -1 : 0);
    int speed_mag = abs(speed);
    if (speed_mag < 1)
        speed_mag = 1;
    if (speed_mag > MAX_SPEED_AV)
        speed_mag = MAX_SPEED_AV;

    int pending_edge = AUDIO_EDGE_NONE;
    int last_dir = 0;
    int direction_flipped = 0;
    if (edge != NULL) {
        pending_edge = atomic_load_int(&edge->pending_edge);
        last_dir = atomic_load_int(&edge->last_direction);
        direction_flipped = (last_dir != 0 && cur_dir != 0 && cur_dir != last_dir);
    }

    if (pending_edge == AUDIO_EDGE_SILENCE && cur_dir != 0)
        pending_edge = AUDIO_EDGE_JUMP;

#ifdef HAVE_JACK
    if (pending_edge == AUDIO_EDGE_RESET && cur_dir != 0 &&
        el->has_audio && target_frame != -1) {
        vj_perform_audio_source_transition_force_fade_blocks(dst_samples,
                                                             frame_bytes,
                                                             VJ_AUDIO_RESET_FADE_BLOCKS,
                                                             "media-reset-edge",
                                                             pending_edge,
                                                             target_frame,
                                                             "retime");
    }
#endif

    if (cur_dir == 0 || pending_edge == AUDIO_EDGE_SILENCE ||
        !el->has_audio || target_frame == -1) {
        veejay_memset(dst, 0, (size_t)dst_samples * (size_t)frame_bytes);
        vj_audio_declick_apply(p, dst, dst_samples, frame_bytes,
                               AUDIO_PATH_SILENCE, 0, 0,
                               pending_edge, direction_flipped);
        vj_perform_clear_audio_edges(info, edge, 0);
        slow_motion_clear_scratch_head(posdata);
        posdata->prev_n_samples = dst_samples;
        return dst_samples;
    }

    int target_frame_samples = slow_motion_frame_len_exact(el, target_frame, pred_len);
    if (target_frame_samples < 1)
        target_frame_samples = pred_len;

    int slice_count = vj_perform_runtime_sfd(info);
    int cur_slice = atomic_load_int(&settings->audio_slice);
    cur_slice = (cur_slice < 0) ? 0 : cur_slice;
    cur_slice = (cur_slice >= slice_count) ? (slice_count - 1) : cur_slice;
    const int last_slice = posdata->cur_sfd;
    const int last_slice_count = posdata->scratch_last_sfd;

    double sync_pos = slow_motion_sync_abs_pos(el, posdata, target_frame, pred_len,
                                               slice_count, cur_slice, cur_dir);

    double target_vel = (double)cur_dir * (double)speed_mag * rate;

    const int hard_edge = (pending_edge != AUDIO_EDGE_NONE &&
                           pending_edge != AUDIO_EDGE_DIRECTION);
    const int edge_transition = (pending_edge == AUDIO_EDGE_DIRECTION || direction_flipped);
    const int same_target_frame = (posdata->scratch_initialized &&
                                   posdata->last_resampled_frame == target_frame &&
                                   last_slice == cur_slice &&
                                   last_slice_count == slice_count &&
                                   !hard_edge && !edge_transition);
    if (same_target_frame)
        sync_pos = posdata->scratch_pos;
    const int reset_head = (!posdata->scratch_initialized || hard_edge);

    if (reset_head) {
        posdata->scratch_initialized = 1;
        posdata->scratch_pos = sync_pos;
        posdata->scratch_vel = target_vel;
        posdata->scratch_target_vel = target_vel;
        posdata->scratch_last_dir = cur_dir;
        posdata->scratch_last_sfd = slice_count;
        posdata->cur_sfd = cur_slice;
        posdata->max_sfd = slice_count;
        posdata->scratch_ramp_left = 0;
        posdata->scratch_last_reset = 1;
        posdata->scratch_sync_bias = 0.0;
        posdata->scratch_sync_hold_blocks = 0;
        posdata->scratch_stable_blocks = 0;
        slow_motion_clear_turn_history(posdata);
    } else {
        posdata->scratch_last_reset = 0;
        double dv = fabs(posdata->scratch_target_vel - target_vel);
        if (edge_transition || posdata->scratch_last_dir != cur_dir ||
            posdata->scratch_last_sfd != slice_count || posdata->cur_sfd != cur_slice ||
            dv > 0.0005) {
            int ramp = dst_samples / 2;
            if (ramp < 512)
                ramp = 512;
            if (ramp > 4096)
                ramp = 4096;
            posdata->scratch_ramp_left = ramp;
            posdata->scratch_sync_hold_blocks = 4;
            posdata->scratch_stable_blocks = 0;
        }
        posdata->scratch_target_vel = target_vel;
        posdata->scratch_last_dir = cur_dir;
        posdata->scratch_last_sfd = slice_count;
        posdata->cur_sfd = cur_slice;
        posdata->max_sfd = slice_count;
    }

    if (!reset_head && !edge_transition) {
        double err = sync_pos - posdata->scratch_pos;
        double max_snap = (double)pred_len * 8.0;
        if (fabs(err) > max_snap) {
            posdata->scratch_pos = sync_pos;
            posdata->scratch_vel = target_vel;
            posdata->scratch_ramp_left = 0;
        } else {
            double max_bias = fabs(target_vel) * 0.0125;
            if (max_bias < 0.0001)
                max_bias = 0.0001;
            if (max_bias > 0.006)
                max_bias = 0.006;
            double bias = err / ((double)dst_samples * 64.0);
            bias = slow_motion_clampd(bias, -max_bias, max_bias);
            target_vel += bias;
        }
    }

    double predicted_end = posdata->scratch_pos + target_vel * (double)dst_samples;
    double ctx_abs_start = 0.0;
    int ctx_samples = 0;
    long long ctx_first = 0;
    long long ctx_last = 0;
    slow_scratch_ctx_map_t ctx_map;
    veejay_memset(&ctx_map, 0, sizeof(ctx_map));

    int got_ctx = slow_motion_fetch_scratch_context(info, el, posdata,
                                                    p->down_sample_buffer,
                                                    &ctx_map,
                                                    pred_len,
                                                    frame_bytes,
                                                    speed,
                                                    posdata->scratch_pos,
                                                    predicted_end,
                                                    &ctx_abs_start,
                                                    &ctx_samples,
                                                    &ctx_first,
                                                    &ctx_last);

    if (got_ctx <= 0 || ctx_samples <= 0) {
        veejay_memset(dst, 0, (size_t)dst_samples * (size_t)frame_bytes);
        vj_audio_declick_apply(p, dst, dst_samples, frame_bytes,
                               AUDIO_PATH_SILENCE, 0, 0,
                               pending_edge, direction_flipped);
        vj_perform_clear_audio_edges(info, edge, cur_dir);
        posdata->prev_n_samples = dst_samples;
        return dst_samples;
    }

    double pos0 = posdata->scratch_pos;
    double pos1 = posdata->scratch_pos;
    double vel0 = posdata->scratch_vel;
    double vel1 = posdata->scratch_vel;
    int step_max = 0;
    int step_avg = 0;

    int copied = slow_motion_render_scratch_head_s16(dst,
                                                     dst_samples,
                                                     p->down_sample_buffer,
                                                     ctx_samples,
                                                     ctx_abs_start,
                                                     &ctx_map,
                                                     posdata,
                                                     target_vel,
                                                     sync_pos,
                                                     frame_bytes,
                                                     pred_len,
                                                     edge_transition,
                                                     &step_max,
                                                     &step_avg,
                                                     &pos0,
                                                     &pos1,
                                                     &vel0,
                                                     &vel1);
    if (copied <= 0) {
        veejay_memset(dst, 0, (size_t)dst_samples * (size_t)frame_bytes);
        copied = dst_samples;
    }

    int edge_delta = -1;
    if (posdata->audio_diag_valid && posdata->audio_diag_frame_bytes == frame_bytes)
        edge_delta = vj_audio_frame_delta_s16(dst, posdata->audio_diag_last_frame, frame_bytes);

    if (hard_edge || edge_delta >= 1800 || step_max >= 3200) {
        vj_audio_declick_apply(p, dst, copied, frame_bytes,
                               AUDIO_PATH_SLOW, speed, cur_dir,
                               hard_edge ? pending_edge : AUDIO_EDGE_DIRECTION,
                               direction_flipped || edge_delta >= 1800 || step_max >= 3200);
    } else {
        vj_audio_declick_observe(p, dst, copied, frame_bytes,
                                 AUDIO_PATH_SLOW, speed, cur_dir);
    }

    if (posdata->audio_diag_valid && posdata->audio_diag_frame_bytes == frame_bytes) {
        vj_audio_copy_last_frame(posdata->audio_diag_prev_frame,
                                 (int)sizeof(posdata->audio_diag_prev_frame),
                                 posdata->audio_diag_last_frame, 1, frame_bytes);
    } else {
        veejay_memset(posdata->audio_diag_prev_frame, 0,
                      sizeof(posdata->audio_diag_prev_frame));
    }

    vj_audio_copy_last_frame(posdata->audio_diag_last_frame,
                             (int)sizeof(posdata->audio_diag_last_frame),
                             dst, copied, frame_bytes);
    posdata->audio_diag_valid = 1;
    posdata->audio_diag_frame_bytes = frame_bytes;
    slow_motion_update_turn_history(posdata, dst, copied, frame_bytes);

    posdata->audio_last_stretched_samples = dst_samples;
    posdata->audio_total_samples = ctx_samples;
    posdata->audio_src_offset = (int)((ctx_map.valid)
        ? slow_motion_ctx_exact_to_actual_rel(&ctx_map, posdata->scratch_pos, ctx_samples)
        : (posdata->scratch_pos - ctx_abs_start));
    posdata->last_resampled_frame = target_frame;
    posdata->last_resampled_dir = cur_dir;
    posdata->cur_sfd = cur_slice;
    posdata->max_sfd = slice_count;
    posdata->scratch_last_sfd = slice_count;
    posdata->consumed_samples = 0;
    posdata->prev_n_samples = copied;

    if (edge != NULL)
        edge->ticks_since_last_flip++;

    vj_perform_clear_audio_edges(info, edge, cur_dir);
    return copied;
}

static int vj_audio_retime_slow_cubic_s16(uint8_t *dst, int dst_samples, const uint8_t *src, int src_samples, int frame_bytes, double rate)
{
    if (dst == NULL || dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || src_samples <= 0) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    if ((frame_bytes & 1) || rate <= 0.0) {
        int n = (src_samples < dst_samples) ? src_samples : dst_samples;
        if (src != dst && n > 0)
            veejay_memcpy(dst, src, n * frame_bytes);
        vj_audio_pad_exact_tail(dst, n, dst_samples, frame_bytes);
        return dst_samples;
    }

    const int words = frame_bytes / 2;
    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;
    const int max_index = src_samples - 1;

    if (src_samples == 1) {
        for (int i = 0; i < dst_samples; i++)
            for (int c = 0; c < words; c++)
                out[(i * words) + c] = in[c];
        return dst_samples;
    }

    const double step = ((double)src_samples / (double)dst_samples);

    for (int i = 0; i < dst_samples; i++) {
        double pos = (double)i * step;
        if (pos < 0.0)
            pos = 0.0;
        else if (pos > (double)max_index)
            pos = (double)max_index;

        int idx = (int)floor(pos);
        double frac = pos - (double)idx;

        if (idx < 0) {
            idx = 0;
            frac = 0.0;
        } else if (idx > max_index) {
            idx = max_index;
            frac = 0.0;
        }

        int i0 = idx - 1;
        int i1 = idx;
        int i2 = idx + 1;
        int i3 = idx + 2;

        if (i0 < 0) i0 = 0;
        if (i2 > max_index) i2 = max_index;
        if (i3 > max_index) i3 = max_index;

        const int b0 = i0 * words;
        const int b1 = i1 * words;
        const int b2 = i2 * words;
        const int b3 = i3 * words;
        const int bo = i * words;

        for (int c = 0; c < words; c++) {
            out[bo + c] = slow_motion_cubic_interp_s16(
                in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac
            );
        }
    }

    return dst_samples;
}

static void vj_audio_pad_exact_tail(uint8_t *dst, int produced, int expected, int frame_bytes)
{
    if (dst == NULL || expected <= 0 || frame_bytes <= 0)
        return;

    if (produced <= 0) {
        veejay_memset(dst, 0, expected * frame_bytes);
        return;
    }

    if (produced >= expected)
        return;

    uint8_t *last = dst + ((size_t)(produced - 1) * (size_t)frame_bytes);
    uint8_t *out = dst + ((size_t)produced * (size_t)frame_bytes);

    for (int i = produced; i < expected; i++) {
        veejay_memcpy(out, last, frame_bytes);
        out += frame_bytes;
    }
}

static int vj_perform_retime_audio_chunk(veejay_t *info,
                                         performer_t *p,
                                         editlist *el,
                                         uint8_t *dst,
                                         int dst_samples,
                                         const uint8_t *src,
                                         int src_samples,
                                         int frame_bytes)
{
    if (dst == NULL || dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || src_samples <= 0) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    const double rate = vj_perform_runtime_effective_audio_rate(info, el);

    if (rate > 0.9995 && rate < 1.0005) {
        int n = (src_samples < dst_samples) ? src_samples : dst_samples;

        if (src != dst && n > 0)
            veejay_memcpy(dst, src, n * frame_bytes);

        vj_audio_pad_exact_tail(dst, n, dst_samples, frame_bytes);
        return dst_samples;
    }

    if (rate < 0.9995 && !(frame_bytes & 1))
        return vj_audio_retime_slow_cubic_s16(dst, dst_samples, src, src_samples, frame_bytes, rate);

    if ((frame_bytes & 1) || p == NULL || p->audio_scratcher == NULL) {
        int n = (src_samples < dst_samples) ? src_samples : dst_samples;

        if (src != dst && n > 0)
            veejay_memcpy(dst, src, n * frame_bytes);

        vj_audio_pad_exact_tail(dst, n, dst_samples, frame_bytes);
        return dst_samples;
    }

    int produced = vj_scratch_process(p->audio_scratcher,
                                      (short*)dst,
                                      dst_samples,
                                      (const short*)src,
                                      src_samples,
                                      rate);

    if (produced < 0)
        produced = 0;
    if (produced > dst_samples)
        produced = dst_samples;

    vj_audio_pad_exact_tail(dst, produced, dst_samples, frame_bytes);
    return dst_samples;
}


int vj_perform_queue_audio_chunk_crossfade(
    veejay_t *info,
    int client_frames_to_write,
    long long target_frame_a,
    long long target_frame_b,
    uint8_t *audio_payload_chunk,
    int sample_b,
    long long trans_start_frame,
    long long trans_end_frame
) {
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    performer_t *q = g->B;
    editlist *el = info->current_edit_list;

    if (client_frames_to_write <= 0 || audio_payload_chunk == NULL || el == NULL)
        return 0;

    const int bps = el->audio_bps;
    const int num_channels = el->audio_chans;
    const int speed_a = info->settings->current_playback_speed;
    const int speed_b = sample_get_speed(sample_b);

    int num_samples_a =
        vj_perform_queue_audio_frame(info, (void*)p,
                                     p->top_audio_buffer,
                                     speed_a,
                                     target_frame_a,
                                     info->uc->sample_id);

    if (num_samples_a > 0)
        vj_audio_consume_chain(info, p->top_audio_buffer, num_samples_a);

    int num_samples_b =
        vj_perform_queue_audio_frame(info, (void*)q,
                                     q->top_audio_buffer,
                                     speed_b,
                                     target_frame_b,
                                     sample_b);

    int num_samples = (num_samples_a < num_samples_b)
                        ? num_samples_a
                        : num_samples_b;

    if (num_samples <= 0) {
        veejay_memset(audio_payload_chunk, 0, client_frames_to_write * bps);
        return client_frames_to_write;
    }

    double trans_len = (double)(trans_end_frame - trans_start_frame);
    if (trans_len <= 0.0)
        trans_len = 1.0;

    const double absolute_frame = (double)target_frame_a;
    float t = (float)((absolute_frame - trans_start_frame) / trans_len);
    t = fminf(fmaxf(t, 0.0f), 1.0f);

    if (absolute_frame >= (double)trans_end_frame) {
        vj_audio_crossfade_buffers(
            g,
            NULL,
            (speed_b != 0) ? q->top_audio_buffer : NULL,
            p->audio_render_buffer,
            num_samples,
            num_channels,
            bps,
            1.0f,
            0.0f,
            1.0f
        );
    } else {
        vj_audio_crossfade_buffers(
            g,
            (speed_a != 0) ? p->top_audio_buffer : NULL,
            (speed_b != 0) ? q->top_audio_buffer : NULL,
            p->audio_render_buffer,
            num_samples,
            num_channels,
            bps,
            t,
            1.0f,
            1.0f
        );
    }

    const double rate = vj_perform_runtime_audio_rate(info, el);
    if (rate > 0.9995 && rate < 1.0005) {
        if (num_samples == client_frames_to_write) {
            veejay_memcpy(audio_payload_chunk,
                          p->audio_render_buffer,
                          (size_t)client_frames_to_write * (size_t)bps);
            return client_frames_to_write;
        }

        if (!(bps & 1)) {
            return vj_audio_retime_slow_cubic_s16(audio_payload_chunk,
                                                  client_frames_to_write,
                                                  p->audio_render_buffer,
                                                  num_samples,
                                                  bps,
                                                  1.0);
        }

        int n = (num_samples < client_frames_to_write)
            ? num_samples
            : client_frames_to_write;

        if (n > 0)
            veejay_memcpy(audio_payload_chunk,
                          p->audio_render_buffer,
                          (size_t)n * (size_t)bps);

        vj_audio_pad_exact_tail(audio_payload_chunk,
                                n,
                                client_frames_to_write,
                                bps);
        return client_frames_to_write;
    }

    return vj_perform_retime_audio_chunk(info,
                                         p,
                                         el,
                                         audio_payload_chunk,
                                         client_frames_to_write,
                                         p->audio_render_buffer,
                                         num_samples,
                                         bps);
}

static int vj_perform_queue_audio_frame_buf(veejay_t *info, performer_t *p, uint8_t *a_buf, editlist *el,int speed, long long target_frame )
{
    int num_samples = 0;

    if(!info || !p || !a_buf || !el)
        return 0;

    if(!el->has_audio || speed == 0 || target_frame == -1) {
        num_samples = (el->audio_rate / el->video_fps);
        int bps = el->audio_bps;
        veejay_memset( a_buf, 0, num_samples * bps);
        return num_samples;
    }

    if(info->audio != AUDIO_PLAY)
        return num_samples;

    if( el->has_audio )
        num_samples = vj_perform_fill_audio_buffers(info,el, a_buf, p, &(p->play_audio_sample_), target_frame);

    return num_samples;
}

static void vj_perform_track_align_pcm_stats(const uint8_t *data,
                                             int frames,
                                             int frame_bytes,
                                             int channels,
                                             double *rms,
                                             double *peak,
                                             int *nonzero)
{
    int sample_bytes;
    double sum2 = 0.0;
    double pk = 0.0;
    int nz = 0;

    if(rms)
        *rms = 0.0;
    if(peak)
        *peak = 0.0;
    if(nonzero)
        *nonzero = 0;

    if(!data || frames <= 0 || frame_bytes <= 0 || channels <= 0)
        return;

    sample_bytes = frame_bytes / channels;
    if(sample_bytes != 1 && sample_bytes != 2)
        return;

    for(int i = 0; i < frames; i++) {
        const uint8_t *f = data + ((size_t)i * (size_t)frame_bytes);
        double mono = 0.0;

        for(int c = 0; c < channels; c++) {
            int v;
            const uint8_t *p = f + (c * sample_bytes);
            if(sample_bytes == 1)
                v = (((int)p[0] - 128) << 8);
            else
                v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            mono += (double)v * (1.0 / 32768.0);
        }

        mono /= (double)channels;
        if(mono < 0.0)
            mono = -mono;
        if(mono > pk)
            pk = mono;
        if(mono > 0.000030)
            nz++;
        sum2 += mono * mono;
    }

    if(rms)
        *rms = sqrt(sum2 / (double)frames);
    if(peak)
        *peak = pk;
    if(nonzero)
        *nonzero = nz;
}

static int vj_perform_collect_clip_audio_window(veejay_t *info,
                                                performer_t *p,
                                                editlist *el,
                                                long long end_frame,
                                                int video_frames,
                                                int frame_bytes,
                                                uint8_t *dst,
                                                int max_audio_frames)
{
    int total = 0;
    int max_one_frame = 0;
    long long first;

    if(!info || !p || !el || !dst || video_frames <= 0 ||
       frame_bytes <= 0 || max_audio_frames <= 0)
        return 0;

    if(el->audio_rate > 0 && el->video_fps > 0.0)
        max_one_frame = (int)ceil((double)el->audio_rate / (double)el->video_fps) + 128;
    else
        max_one_frame = 4096;
    if(max_one_frame < 256)
        max_one_frame = 256;

    first = end_frame - (long long)video_frames + 1;
    if(first < 0)
        first = 0;

    for(long long vf = first; vf <= end_frame; vf++) {
        int room = max_audio_frames - total;
        int got;
        if(room <= 0 || room < max_one_frame)
            break;

        got = vj_el_get_audio_frame(el, vf, dst + ((size_t)total * (size_t)frame_bytes));
        if(got <= 0)
            continue;
        if(got > room)
            got = room;
        total += got;
    }

    return total;
}


static void vj_perform_track_align_clear_candidate(performer_t *p)
{
    if(!p)
        return;
    p->track_align_candidate_ms = 0;
    p->track_align_candidate_delta = 0;
    p->track_align_candidate_conf = 0;
    p->track_align_candidate_margin = 0;
    p->track_align_candidate_last_conf = 0;
    p->track_align_candidate_last_margin = 0;
    p->track_align_candidate_sum_conf = 0;
    p->track_align_candidate_sum_margin = 0;
    p->track_align_candidate_count = 0;
    p->track_align_candidate_kind = VJ_TRACK_ALIGN_CANDIDATE_NONE;
}

static void vj_perform_track_align_clear_wide_buckets(performer_t *p)
{
    if(!p)
        return;
    for(int i = 0; i < VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS; i++) {
        p->track_align_wide_bucket_ms[i] = 0;
        p->track_align_wide_bucket_delta[i] = 0;
        p->track_align_wide_bucket_score[i] = 0;
        p->track_align_wide_bucket_obs[i] = 0;
        p->track_align_wide_bucket_min_conf[i] = 0;
        p->track_align_wide_bucket_min_margin[i] = 0;
        p->track_align_wide_bucket_last_conf[i] = 0;
        p->track_align_wide_bucket_last_margin[i] = 0;
    }
    p->track_align_wide_bucket_large_seen = 0;
}

static int vj_perform_track_align_bucket_tolerance_frames(double fps)
{
    int tol = (int)(fps * ((double)VJ_TRACK_ALIGN_WIDE_BUCKET_TOLERANCE_MS / 1000.0) + 0.5);
    if(tol < 6)
        tol = 6;
    return tol;
}

static int vj_perform_track_align_bucket_score_value(int conf, int margin)
{
    int m = margin;
    int c = conf;
    if(c < 0) c = 0;
    if(c > 100) c = 100;
    if(m < 0) m = 0;
    if(m > 50) m = 50;
    return c + (m * 2);
}

static int vj_perform_track_align_bucket_same(int a, int b, int tol)
{
    if(a == 0 || b == 0)
        return 0;
    if((a < 0 && b > 0) || (a > 0 && b < 0))
        return 0;
    return abs(a - b) <= tol;
}

static int vj_perform_track_align_hint_window_frames(double fps)
{
    int w = (int)(fps * ((double)VJ_TRACK_ALIGN_WIDE_HINT_ACCEPT_MS / 1000.0) + 0.5);
    if(w < 8)
        w = 8;
    return w;
}

static int vj_perform_track_align_hint_rank_conf(int conf,
                                                 int delta,
                                                 int hint_delta,
                                                 int hint_have,
                                                 double fps)
{
    int ranked = conf;
    int w;

    if(!hint_have || hint_delta == 0 || delta == 0)
        return ranked;

    w = vj_perform_track_align_hint_window_frames(fps);
    if(abs(delta - hint_delta) <= w) {
        ranked += 8;
    } else if((delta < 0 && hint_delta < 0) || (delta > 0 && hint_delta > 0)) {
        ranked -= 10;
    } else {
        ranked -= 18;
    }

    if(ranked < 0)
        ranked = 0;
    if(ranked > 100)
        ranked = 100;
    return ranked;
}

static int vj_perform_track_align_update_wide_bucket(performer_t *p,
                                                     long now_ms,
                                                     int delta,
                                                     int conf,
                                                     int margin,
                                                     double fps,
                                                     int live_delta,
                                                     int live_conf,
                                                     int live_have,
                                                     int *winner_delta,
                                                     int *winner_conf,
                                                     int *winner_margin,
                                                     int *winner_obs,
                                                     int *winner_score,
                                                     int *runner_score,
                                                     int *small_alias_blocked,
                                                     int *strong_one_shot)
{
    int tol;
    int bucket_index = -1;
    int empty_index = -1;
    int weakest_index = 0;
    int weakest_score = 0x7fffffff;
    int score;
    int best_i = -1;
    int second_i = -1;
    int best_score = -1;
    int second_score = -1;
    int large_frames;
    int small_frames;
    int abs_delta;
    int large_competing = 0;

    if(winner_delta) *winner_delta = delta;
    if(winner_conf) *winner_conf = conf;
    if(winner_margin) *winner_margin = margin;
    if(winner_obs) *winner_obs = 0;
    if(winner_score) *winner_score = 0;
    if(runner_score) *runner_score = 0;
    if(small_alias_blocked) *small_alias_blocked = 0;
    if(strong_one_shot) *strong_one_shot = 0;

    if(!p || delta == 0)
        return 0;

    tol = vj_perform_track_align_bucket_tolerance_frames(fps);
    large_frames = (int)(fps * ((double)VJ_TRACK_ALIGN_WIDE_BUCKET_LARGE_MS / 1000.0) + 0.5);
    small_frames = (int)(fps * ((double)VJ_TRACK_ALIGN_WIDE_BUCKET_SMALL_ALIAS_MS / 1000.0) + 0.5);
    if(large_frames < tol * 2)
        large_frames = tol * 2;
    if(small_frames < tol)
        small_frames = tol;

    for(int i = 0; i < VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS; i++) {
        long age = (p->track_align_wide_bucket_ms[i] > 0) ? (now_ms - p->track_align_wide_bucket_ms[i]) : 0;
        if(p->track_align_wide_bucket_ms[i] > 0 &&
           (age < 0 || age > VJ_TRACK_ALIGN_WIDE_BUCKET_TTL_MS))
        {
            p->track_align_wide_bucket_ms[i] = 0;
            p->track_align_wide_bucket_delta[i] = 0;
            p->track_align_wide_bucket_score[i] = 0;
            p->track_align_wide_bucket_obs[i] = 0;
            p->track_align_wide_bucket_min_conf[i] = 0;
            p->track_align_wide_bucket_min_margin[i] = 0;
            p->track_align_wide_bucket_last_conf[i] = 0;
            p->track_align_wide_bucket_last_margin[i] = 0;
        }

        if(p->track_align_wide_bucket_ms[i] == 0) {
            if(empty_index < 0)
                empty_index = i;
            continue;
        }

        if(vj_perform_track_align_bucket_same(p->track_align_wide_bucket_delta[i], delta, tol))
            bucket_index = i;

        if(p->track_align_wide_bucket_score[i] < weakest_score) {
            weakest_score = p->track_align_wide_bucket_score[i];
            weakest_index = i;
        }
    }

    if(bucket_index < 0)
        bucket_index = (empty_index >= 0) ? empty_index : weakest_index;

    score = vj_perform_track_align_bucket_score_value(conf, margin);
    if(live_have && live_conf >= VJ_TRACK_ALIGN_WIDE_HINT_MIN_CONF && live_delta != 0) {
        int live_window = vj_perform_track_align_hint_window_frames(fps);
        if(abs(delta - live_delta) <= live_window) {
            score += VJ_TRACK_ALIGN_WIDE_HINT_SCORE_BONUS;
        } else if((delta < 0 && live_delta < 0) || (delta > 0 && live_delta > 0)) {
            score -= (VJ_TRACK_ALIGN_WIDE_HINT_FAR_PENALTY / 2);
        } else {
            score -= VJ_TRACK_ALIGN_WIDE_HINT_FAR_PENALTY;
        }
        if(score < 0)
            score = 0;
    }

    if(p->track_align_wide_bucket_ms[bucket_index] == 0 ||
       !vj_perform_track_align_bucket_same(p->track_align_wide_bucket_delta[bucket_index], delta, tol))
    {
        p->track_align_wide_bucket_ms[bucket_index] = now_ms;
        p->track_align_wide_bucket_delta[bucket_index] = delta;
        p->track_align_wide_bucket_score[bucket_index] = score;
        p->track_align_wide_bucket_obs[bucket_index] = 1;
        p->track_align_wide_bucket_min_conf[bucket_index] = conf;
        p->track_align_wide_bucket_min_margin[bucket_index] = margin;
    } else {
        int obs = p->track_align_wide_bucket_obs[bucket_index];
        if(obs < 1) obs = 1;
        p->track_align_wide_bucket_delta[bucket_index] =
            ((p->track_align_wide_bucket_delta[bucket_index] * obs) + delta) / (obs + 1);
        p->track_align_wide_bucket_score[bucket_index] += score;
        p->track_align_wide_bucket_obs[bucket_index] = obs + 1;
        if(conf < p->track_align_wide_bucket_min_conf[bucket_index])
            p->track_align_wide_bucket_min_conf[bucket_index] = conf;
        if(margin < p->track_align_wide_bucket_min_margin[bucket_index])
            p->track_align_wide_bucket_min_margin[bucket_index] = margin;
    }

    p->track_align_wide_bucket_ms[bucket_index] = now_ms;
    p->track_align_wide_bucket_last_conf[bucket_index] = conf;
    p->track_align_wide_bucket_last_margin[bucket_index] = margin;

    p->track_align_wide_bucket_large_seen = 0;
    for(int i = 0; i < VJ_TRACK_ALIGN_WIDE_VOTE_BUCKETS; i++) {
        if(p->track_align_wide_bucket_ms[i] == 0)
            continue;
        if(abs(p->track_align_wide_bucket_delta[i]) >= large_frames)
            p->track_align_wide_bucket_large_seen = 1;

        if(p->track_align_wide_bucket_score[i] > best_score) {
            second_score = best_score;
            second_i = best_i;
            best_score = p->track_align_wide_bucket_score[i];
            best_i = i;
        } else if(p->track_align_wide_bucket_score[i] > second_score) {
            second_score = p->track_align_wide_bucket_score[i];
            second_i = i;
        }
    }

    if(best_i < 0)
        return 0;

    abs_delta = abs(p->track_align_wide_bucket_delta[best_i]);

    if(second_i >= 0 && abs_delta <= small_frames &&
       abs(p->track_align_wide_bucket_delta[second_i]) >= large_frames &&
       p->track_align_wide_bucket_score[second_i] >= (best_score / 2))
    {
        large_competing = 1;
    }

    if(abs_delta <= small_frames && p->track_align_wide_bucket_large_seen)
        large_competing = 1;

    if(large_competing && small_alias_blocked)
        *small_alias_blocked = 1;

    if(strong_one_shot) {
        *strong_one_shot = (p->track_align_wide_bucket_obs[best_i] <= 1 &&
                            abs_delta >= large_frames &&
                            p->track_align_wide_bucket_last_conf[best_i] >= VJ_TRACK_ALIGN_WIDE_BUCKET_STRONG_ONE_SHOT_CONF &&
                            p->track_align_wide_bucket_last_margin[best_i] >= VJ_TRACK_ALIGN_WIDE_BUCKET_STRONG_ONE_SHOT_MARGIN);
    }

    if(live_have && live_conf >= VJ_TRACK_ALIGN_WIDE_HINT_MIN_CONF) {
        int live_bonus_tol = tol * 2;
        if(vj_perform_track_align_bucket_same(live_delta,
                                              p->track_align_wide_bucket_delta[best_i],
                                              live_bonus_tol))
        {
            p->track_align_wide_bucket_score[best_i] += 12;
            best_score += 12;
        }
    }

    if(winner_delta) *winner_delta = p->track_align_wide_bucket_delta[best_i];
    if(winner_conf) *winner_conf = p->track_align_wide_bucket_min_conf[best_i];
    if(winner_margin) *winner_margin = p->track_align_wide_bucket_min_margin[best_i];
    if(winner_obs) *winner_obs = p->track_align_wide_bucket_obs[best_i];
    if(winner_score) *winner_score = best_score;
    if(runner_score) *runner_score = (second_score > 0) ? second_score : 0;

    if(large_competing)
        return 0;

    return 1;
}

static int vj_perform_track_align_candidate_same(int old_delta,
                                                 int new_delta,
                                                 int tolerance_frames)
{
    if(old_delta == 0 || new_delta == 0)
        return 0;
    if((old_delta > 0 && new_delta < 0) ||
       (old_delta < 0 && new_delta > 0))
        return 0;
    return abs(new_delta - old_delta) <= tolerance_frames;
}

static void vj_perform_track_align_mark_acquired(performer_t *p,
                                                 long now_ms,
                                                 int delta_frames,
                                                 const char *reason)
{
    if(!p)
        return;

    p->track_align_last_wide_snap_ms = now_ms;
    p->track_align_last_wide_snap_delta = delta_frames;
    vj_perform_track_align_clear_candidate(p);
    vj_perform_track_align_clear_wide_buckets(p);

}

static void vj_perform_track_align_candidate_conflict(performer_t *p,
                                                      int kind,
                                                      int delta,
                                                      int conf,
                                                      int margin,
                                                      int tolerance_frames,
                                                      const char *reason)
{
    if(!p || p->track_align_candidate_kind == VJ_TRACK_ALIGN_CANDIDATE_NONE)
        return;

    if(conf < VJ_TRACK_ALIGN_WIDE_CONFLICT_MIN_CONF ||
       margin < VJ_TRACK_ALIGN_WIDE_CONFLICT_MIN_MARGIN)
        return;

    if(p->track_align_candidate_kind != kind ||
       !vj_perform_track_align_candidate_same(p->track_align_candidate_delta,
                                              delta,
                                              tolerance_frames))
    {
        vj_perform_track_align_clear_candidate(p);
    }
}

static void vj_perform_track_align_expire_candidate(performer_t *p,
                                                    long now_ms,
                                                    const char *reason)
{
    long age_ms;

    if(!p || p->track_align_candidate_kind == VJ_TRACK_ALIGN_CANDIDATE_NONE ||
       p->track_align_candidate_ms == 0)
        return;

    age_ms = now_ms - p->track_align_candidate_ms;
    if(age_ms < 0 || age_ms <= VJ_TRACK_ALIGN_WIDE_CANDIDATE_TTL_MS)
        return;

    vj_perform_track_align_clear_candidate(p);
}


static int vj_perform_track_align_live_required_conf(int abs_offset_ms,
                                                     int candidate_count,
                                                     int after_snap)
{


    double min_offset = (double)VJ_TRACK_ALIGN_LIVE_SNAP_MIN_OFFSET_MS;
    double span = (double)VJ_TRACK_ALIGN_LIVE_DYNAMIC_FULL_SCALE_MS - min_offset;
    double norm;
    int max_conf;
    int min_conf;
    int distance_bonus;
    int stable_bonus;
    int required;

    if(abs_offset_ms <= 0)
        return VJ_TRACK_ALIGN_LIVE_SNAP_MIN_CONF;

    if(span < 1000.0)
        span = 1000.0;

    norm = ((double)abs_offset_ms - min_offset) / span;
    if(norm < 0.0)
        norm = 0.0;
    else if(norm > 1.0)
        norm = 1.0;

    max_conf = VJ_TRACK_ALIGN_LIVE_SNAP_MIN_CONF;
    min_conf = after_snap ? VJ_TRACK_ALIGN_LIVE_SNAP_MIN_CONF
                          : VJ_TRACK_ALIGN_LIVE_SNAP_LARGE_CONF;

    distance_bonus = (int)(sqrt(norm) *
                           (double)(max_conf - min_conf) + 0.5);

    stable_bonus = (candidate_count > 1) ? ((candidate_count - 1) * 4) : 0;
    if(stable_bonus > 8)
        stable_bonus = 8;

    required = max_conf - distance_bonus - stable_bonus;

    if(required < min_conf)
        required = min_conf;
    if(required > max_conf)
        required = max_conf;

    return required;
}

static int vj_perform_track_align_live_required_stable(int abs_offset_ms,
                                                       int candidate_count,
                                                       int after_snap)
{
    double min_offset = (double)VJ_TRACK_ALIGN_LIVE_SNAP_MIN_OFFSET_MS;
    double span = (double)VJ_TRACK_ALIGN_LIVE_DYNAMIC_FULL_SCALE_MS - min_offset;
    double norm;

    if(after_snap)
        return VJ_TRACK_ALIGN_LIVE_SNAP_STABLE_COUNT;

    if(span < 1000.0)
        span = 1000.0;

    norm = ((double)abs_offset_ms - min_offset) / span;
    if(norm < 0.0)
        norm = 0.0;
    else if(norm > 1.0)
        norm = 1.0;


    if(norm >= 0.28 && candidate_count >= 1)
        return VJ_TRACK_ALIGN_LIVE_SNAP_LARGE_STABLE_COUNT;

    return VJ_TRACK_ALIGN_LIVE_SNAP_STABLE_COUNT;
}


static int vj_perform_track_align_try_settled_servo(veejay_t *info,
                                                    performer_t *p,
                                                    int live_ms,
                                                    int live_conf,
                                                    long now_ms,
                                                    double fps,
                                                    const char *reason)
{
    int abs_live_ms;
    int live_delta;
    int sign;
    long age;

    if(!info || !info->settings || !p || fps <= 0.0)
        return 0;

    if(p->track_align_last_wide_snap_ms == 0)
        return 0;

    abs_live_ms = abs(live_ms);
    if(abs_live_ms < VJ_TRACK_ALIGN_SETTLED_SERVO_MIN_OFFSET_MS ||
       abs_live_ms > VJ_TRACK_ALIGN_SETTLED_SERVO_MAX_OFFSET_MS ||
       live_conf < VJ_TRACK_ALIGN_SETTLED_SERVO_MIN_CONF)
    {
        if(abs_live_ms < VJ_TRACK_ALIGN_REACQUIRE_SETTLED_MAX_OFFSET_MS ||
           live_conf < (VJ_TRACK_ALIGN_SETTLED_SERVO_MIN_CONF - 8))
        {
            p->track_align_servo_candidate_ms = 0;
            p->track_align_servo_sign = 0;
            p->track_align_servo_count = 0;
            p->track_align_servo_min_conf = 0;
        }
        return 0;
    }

    live_delta = (int)(((double)live_ms * fps / 1000.0) +
                       (live_ms >= 0 ? 0.5 : -0.5));
    if(live_delta == 0)
        return 0;

    sign = (live_delta > 0) ? 1 : -1;
    age = (p->track_align_servo_candidate_ms > 0) ?
          (now_ms - p->track_align_servo_candidate_ms) : -1;

    if(p->track_align_servo_candidate_ms > 0 &&
       age >= 0 && age <= VJ_TRACK_ALIGN_SETTLED_SERVO_CANDIDATE_TTL_MS &&
       p->track_align_servo_sign == sign)
    {
        if(p->track_align_servo_count < VJ_TRACK_ALIGN_SETTLED_SERVO_STABLE_COUNT)
            p->track_align_servo_count++;
        if(p->track_align_servo_min_conf <= 0 ||
           live_conf < p->track_align_servo_min_conf)
            p->track_align_servo_min_conf = live_conf;
    }
    else {
        p->track_align_servo_sign = sign;
        p->track_align_servo_count = 1;
        p->track_align_servo_min_conf = live_conf;
    }

    p->track_align_servo_candidate_ms = now_ms;

    if(p->track_align_servo_count < VJ_TRACK_ALIGN_SETTLED_SERVO_STABLE_COUNT)
        return 0;

    if(p->track_align_last_servo_offer_ms > 0 &&
       (now_ms - p->track_align_last_servo_offer_ms) >= 0 &&
       (now_ms - p->track_align_last_servo_offer_ms) < VJ_TRACK_ALIGN_SETTLED_SERVO_INTERVAL_MS)
        return 0;

    if(p->track_align_servo_min_conf < VJ_TRACK_ALIGN_SETTLED_SERVO_MIN_CONF)
        return 0;

    if(!vj_audio_sync_track_align_offer_servo_nudge(&info->settings->audio_sync,
                                                     sign,
                                                     live_conf))
        return 0;

    p->track_align_last_servo_offer_ms = now_ms;
    p->track_align_servo_count = 0;
    p->track_align_servo_candidate_ms = 0;
    p->track_align_servo_sign = 0;
    p->track_align_servo_min_conf = 0;

    return 1;
}

static int vj_perform_track_align_try_live_snap(veejay_t *info,
                                                performer_t *p,
                                                editlist *el,
                                                const vj_audio_sync_snapshot_t *snap,
                                                long now_ms,
                                                double fps,
                                                const char *reason)
{
    int live_ms;
    int live_conf;
    int live_delta;
    int stable_tol_frames;
    int same_candidate = 0;
    int abs_live_ms;
    int required_conf;
    int required_stable;
    int after_snap;
    int fresh_reacquire;
    long since_reacquire = -1;
    int min_offset_ms = VJ_TRACK_ALIGN_LIVE_SNAP_MIN_OFFSET_MS;

    if(!info || !p || !el || !snap)
        return 0;

    if(!snap->track_align_locked)
        return 0;

    live_ms = snap->track_align_offset_ms;
    live_conf = snap->track_align_confidence_pct;
    abs_live_ms = abs(live_ms);

    after_snap = (p->track_align_last_wide_snap_ms != 0);
    if(p->track_align_last_reacquire_ms > 0)
        since_reacquire = now_ms - p->track_align_last_reacquire_ms;
    fresh_reacquire = (!after_snap && since_reacquire >= 0 &&
                       since_reacquire <= VJ_TRACK_ALIGN_REACQUIRE_TRIM_WINDOW_MS);

    if(fresh_reacquire &&
       abs_live_ms <= VJ_TRACK_ALIGN_REACQUIRE_SETTLED_MAX_OFFSET_MS &&
       live_conf >= VJ_TRACK_ALIGN_REACQUIRE_SETTLED_MIN_CONF)
    {
        vj_perform_track_align_mark_acquired(p, now_ms, 0, "reacquire-already-aligned");
        return 1;
    }

    if(fresh_reacquire &&
       abs_live_ms <= VJ_TRACK_ALIGN_REACQUIRE_TRIM_MAX_OFFSET_MS &&
       live_conf >= VJ_TRACK_ALIGN_REACQUIRE_TRIM_MIN_CONF)
    {
        min_offset_ms = VJ_TRACK_ALIGN_REACQUIRE_TRIM_MIN_OFFSET_MS;
    }


    if(after_snap &&
       vj_perform_track_align_try_settled_servo(info, p, live_ms, live_conf, now_ms, fps, reason))
        return 1;

    if(p->track_align_last_wide_snap_ms != 0) {
        long since_snap = now_ms - p->track_align_last_wide_snap_ms;
        if(since_snap >= 0 &&
           since_snap < VJ_TRACK_ALIGN_WIDE_POST_SNAP_COOLDOWN_MS)
            return 0;
    }

    if(abs_live_ms < min_offset_ms)
        return 0;

    if(fresh_reacquire && abs_live_ms <= VJ_TRACK_ALIGN_REACQUIRE_TRIM_MAX_OFFSET_MS) {
        required_conf = VJ_TRACK_ALIGN_REACQUIRE_TRIM_MIN_CONF;
        required_stable = VJ_TRACK_ALIGN_REACQUIRE_TRIM_STABLE_COUNT;
    } else {
        required_conf = vj_perform_track_align_live_required_conf(
            abs_live_ms,
            p->track_align_candidate_count,
            after_snap
        );
        required_stable = vj_perform_track_align_live_required_stable(
            abs_live_ms,
            p->track_align_candidate_count,
            after_snap
        );
    }

    if(live_conf < required_conf)
        return 0;

    live_delta = (int)(((double)live_ms * fps / 1000.0) +
                       (live_ms >= 0 ? 0.5 : -0.5));
    if(live_delta == 0)
        return 0;

    if(p->track_align_last_wide_snap_ms != 0 &&
       live_delta < 0 &&
       abs_live_ms < VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_BLOCK_MS)
    {
        return 0;
    }

    stable_tol_frames = (int)(fps *
                              ((double)VJ_TRACK_ALIGN_LIVE_SNAP_TOLERANCE_MS / 1000.0) +
                              0.5);
    if(stable_tol_frames < 4)
        stable_tol_frames = 4;

    if(p->track_align_candidate_kind == VJ_TRACK_ALIGN_CANDIDATE_LIVE &&
       p->track_align_candidate_ms != 0 &&
       (now_ms - p->track_align_candidate_ms) >= 0 &&
       (now_ms - p->track_align_candidate_ms) <= VJ_TRACK_ALIGN_WIDE_CANDIDATE_TTL_MS &&
       vj_perform_track_align_candidate_same(p->track_align_candidate_delta,
                                             live_delta,
                                             stable_tol_frames))
    {
        same_candidate = 1;
        p->track_align_candidate_count++;
        if(p->track_align_candidate_count > required_stable)
            p->track_align_candidate_count = required_stable;
        if(p->track_align_candidate_conf <= 0 || live_conf < p->track_align_candidate_conf)
            p->track_align_candidate_conf = live_conf;
        p->track_align_candidate_sum_conf += live_conf;
    } else {
        p->track_align_candidate_count = 1;
        p->track_align_candidate_conf = live_conf;
        p->track_align_candidate_sum_conf = live_conf;
    }

    p->track_align_candidate_ms = now_ms;
    p->track_align_candidate_delta = live_delta;
    p->track_align_candidate_last_conf = live_conf;
    p->track_align_candidate_last_margin = 0;
    p->track_align_candidate_margin = 0;
    p->track_align_candidate_sum_margin = 0;
    p->track_align_candidate_kind = VJ_TRACK_ALIGN_CANDIDATE_LIVE;
    live_conf = p->track_align_candidate_conf;

    if(fresh_reacquire && abs_live_ms <= VJ_TRACK_ALIGN_REACQUIRE_TRIM_MAX_OFFSET_MS) {
        required_conf = VJ_TRACK_ALIGN_REACQUIRE_TRIM_MIN_CONF;
        required_stable = VJ_TRACK_ALIGN_REACQUIRE_TRIM_STABLE_COUNT;
    } else {
        required_conf = vj_perform_track_align_live_required_conf(
            abs_live_ms,
            p->track_align_candidate_count,
            after_snap
        );
        required_stable = vj_perform_track_align_live_required_stable(
            abs_live_ms,
            p->track_align_candidate_count,
            after_snap
        );
    }

    if(!same_candidate ||
       p->track_align_candidate_count < required_stable)
    {
        return 0;
    }

    {
        int avg_conf = (p->track_align_candidate_count > 0) ?
                       (p->track_align_candidate_sum_conf / p->track_align_candidate_count) : live_conf;
        if(p->track_align_candidate_conf < required_conf ||
           p->track_align_candidate_last_conf < required_conf ||
           avg_conf < required_conf)
        {
            vj_perform_track_align_clear_candidate(p);
            return 0;
        }
        live_conf = avg_conf;
    }

    vj_audio_sync_track_align_offer_snap(&info->settings->audio_sync,
                                         live_delta,
                                         live_conf);
    vj_perform_track_align_mark_acquired(p, now_ms, live_delta, reason ? reason : "live");

    veejay_msg(VEEJAY_MSG_INFO,
               "[TRACK-ALIGN] stable live-offset candidate %+d frames from %+dms conf=%d%% stable=%d/%d",
               live_delta,
               live_ms,
               live_conf,
               required_stable,
               required_stable);
    return 1;
}

static void vj_perform_track_align_wide_search(veejay_t *info,
                                                performer_t *p,
                                                editlist *el,
                                                long long target_frame,
                                                int frame_bytes)
{
    video_playback_setup *settings;
    vj_audio_sync_snapshot_t snap;
    long now_ms;
    int cur_mode;
    int cur_id;
    double fps;
    int radius_frames;
    int step_frames;
    int probe_video_frames;
    int samples_per_video;
    int max_audio_frames;
    long long min_frame = 0;
    long long max_frame = 0;
    int best_delta = 0;
    int best_conf = -1;
    int best_rank_conf = -1;
    int second_conf = -1;
    int coarse_best_delta = 0;
    int coarse_attempts = 0;
    int coarse_rendered = 0;
    int coarse_probe_ok = 0;
    int coarse_no_audio = 0;
    int coarse_best_reject_conf = 0;
    long long coarse_got_sum = 0;
    double coarse_rms_sum = 0.0;
    double coarse_peak_max = 0.0;
    int coarse_nonzero_sum = 0;
    int fine_attempts = 0;
    int fine_rendered = 0;
    int fine_probe_ok = 0;
    int fine_no_audio = 0;
    int fine_best_reject_conf = 0;
    long long fine_got_sum = 0;
    double fine_rms_sum = 0.0;
    double fine_peak_max = 0.0;
    int fine_nonzero_sum = 0;
    int used_short_probe = 0;
    int short_support = 0;
    int hint_delta = 0;
    int hint_have = 0;
    int acquire_mode = 0;
    int mode_changed = 0;
    int acquisition_bucket_ready = 0;
    int acquisition_bucket_score = 0;
    int acquisition_bucket_runner_score = 0;
    int acquisition_bucket_obs = 0;
    int acquisition_live_far_conflict = 0;
    long search_interval = VJ_TRACK_ALIGN_WIDE_SEARCH_INTERVAL_MS;

    if(!info || !info->settings || !p || !el || !el->has_audio ||
       target_frame < 0 || frame_bytes <= 0 || !p->audio_render_buffer)
        return;

    settings = info->settings;

    if(!vj_audio_sync_is_enabled(&settings->audio_sync))
        return;
    if(atomic_load_int(&settings->audio_sync.mode) != VJ_AUDIO_SYNC_MODE_TRACK_ALIGN)
        return;
    if(vj_audio_sync_get_target_mode(&settings->audio_sync) != VJ_AUDIO_SYNC_TARGET_CURRENT_CLIP)
        return;

    vj_perform_track_align_observe_sync_epoch(info, p, "wide-search");


    if(!vj_perform_track_align_normal_transport(info, el))
        return;
    if(!vj_audio_sync_get_snapshot(&settings->audio_sync, &snap))
        return;

    vj_perform_track_align_observe_reacquire_seq(info, p, "wide-search");


    cur_mode = info->uc ? info->uc->playback_mode : -1;
    cur_id   = info->uc ? info->uc->sample_id : -1;

    {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        now_ms = ((long)ts.tv_sec * 1000L) + ((long)ts.tv_nsec / 1000000L);
    }
    if(p->track_align_last_wide_search_mode != cur_mode ||
       p->track_align_last_wide_search_id   != cur_id)
    {
        p->track_align_last_wide_search_mode = cur_mode;
        p->track_align_last_wide_search_id = cur_id;
        p->track_align_last_wide_search_ms = 0;
        p->track_align_last_wide_snap_ms = 0;
        p->track_align_last_wide_snap_delta = 0;
        vj_perform_track_align_clear_candidate(p);
        vj_perform_track_align_clear_wide_buckets(p);
        mode_changed = 1;
    }


    if(!mode_changed) {
        long sync_snap_ms = 0;
        int sync_snap_delta = 0;
        if(vj_audio_sync_track_align_last_snap(&settings->audio_sync,
                                               &sync_snap_ms,
                                               &sync_snap_delta) &&
           sync_snap_ms > 0 &&
           (p->track_align_last_reacquire_ms <= 0 ||
            sync_snap_ms >= p->track_align_last_reacquire_ms) &&
           (p->track_align_last_wide_snap_ms == 0 ||
            p->track_align_last_wide_snap_ms != sync_snap_ms ||
            p->track_align_last_wide_snap_delta != sync_snap_delta))
        {
            p->track_align_last_wide_snap_ms = sync_snap_ms;
            p->track_align_last_wide_snap_delta = sync_snap_delta;
            vj_perform_track_align_clear_candidate(p);
            vj_perform_track_align_clear_wide_buckets(p);
        }
    }

    acquire_mode = (p->track_align_last_wide_snap_ms == 0);
    search_interval = acquire_mode ? VJ_TRACK_ALIGN_WIDE_ACQUIRE_SEARCH_INTERVAL_MS
                                   : VJ_TRACK_ALIGN_WIDE_SEARCH_INTERVAL_MS;

    vj_perform_track_align_expire_candidate(p, now_ms, "wide-search");

    if(p->track_align_last_wide_search_ms != 0 &&
       (now_ms - p->track_align_last_wide_search_ms) < search_interval)
        return;

    fps = vj_perform_track_align_source_fps(el);

    radius_frames = (int)((fps * (double)VJ_TRACK_ALIGN_WIDE_SEARCH_RADIUS_MS / 1000.0) + 0.5);
    step_frames = (int)((fps * (double)(acquire_mode ? VJ_TRACK_ALIGN_WIDE_ACQUIRE_STEP_MS : VJ_TRACK_ALIGN_WIDE_SEARCH_STEP_MS) / 1000.0) + 0.5);
    probe_video_frames = (int)((fps * (double)(acquire_mode ? VJ_TRACK_ALIGN_WIDE_ACQUIRE_PROBE_MS : VJ_TRACK_ALIGN_WIDE_PROBE_MS) / 1000.0) + 0.5);

    if(radius_frames < 4)
        radius_frames = 4;
    if(step_frames < 2)
        step_frames = 2;
    if(probe_video_frames < 6)
        probe_video_frames = 6;

    if(acquire_mode && snap.track_align_locked) {
        if(vj_perform_track_align_try_live_snap(info, p, el, &snap, now_ms, fps, "fresh-reacquire-trim"))
            return;
    }

    {
        int probe_ms = (int)(((double)probe_video_frames * 1000.0 / fps) + 0.5);
        int need_source_features = (probe_ms / VJ_TRACK_ALIGN_WIDE_SOURCE_READY_HOP_MS) +
                                   VJ_TRACK_ALIGN_WIDE_SOURCE_READY_LATENCY_STEPS;
        if(need_source_features < 64)
            need_source_features = 64;

        if(!vj_audio_sync_track_align_source_ready(&settings->audio_sync,
                                                   need_source_features))
        {
            long retry_age = search_interval - VJ_TRACK_ALIGN_WIDE_SOURCE_RETRY_MS;
            if(retry_age < 0)
                retry_age = 0;
            p->track_align_last_wide_search_ms = now_ms - retry_age;
            if(p->track_align_last_wide_search_ms == 0)
            (void)vj_perform_track_align_try_live_snap(info, p, el, &snap, now_ms, fps, "source-not-ready");
            return;
        }
    }

    p->track_align_last_wide_search_ms = now_ms;

    samples_per_video = 1;
    if(el->audio_rate > 0 && fps > 0.0)
        samples_per_video = (int)ceil((double)el->audio_rate / fps);
    if(samples_per_video < 1)
        samples_per_video = 1;

    max_audio_frames = (int)(p->audio_render_buffer_capacity / (size_t)frame_bytes);
    if(max_audio_frames < samples_per_video * 4)
        return;

    if(probe_video_frames * samples_per_video > max_audio_frames)
        probe_video_frames = max_audio_frames / samples_per_video;
    if(probe_video_frames < 4)
        return;

    min_frame = atomic_load_long_long(&settings->min_frame_num);
    max_frame = atomic_load_long_long(&settings->max_frame_num);
    if(max_frame <= min_frame) {
        min_frame = 0;
        if(el->video_frames > 0)
            max_frame = (long long)el->video_frames - 1;
        else
            max_frame = target_frame + radius_frames;
    }

    {
        long long searchable = max_frame - min_frame + 1;
        int max_radius = radius_frames;


        if(searchable > 0 && searchable < (long long)((radius_frames * 2) + probe_video_frames)) {
            max_radius = (int)(searchable / 2);
            if(max_radius < step_frames * 2)
                max_radius = step_frames * 2;
            if(max_radius < radius_frames) {
                radius_frames = max_radius;
            }
        }
    }

    if(snap.track_align_confidence_pct >= VJ_TRACK_ALIGN_WIDE_HINT_MIN_CONF &&
       abs(snap.track_align_offset_ms) >= VJ_TRACK_ALIGN_WIDE_HINT_MIN_OFFSET_MS)
    {
        hint_delta = (int)(((double)snap.track_align_offset_ms * fps / 1000.0) +
                           (snap.track_align_offset_ms >= 0 ? 0.5 : -0.5));
        if(hint_delta != 0)
            hint_have = 1;
    }

    if(hint_have || snap.track_align_locked)


    if(hint_have) {
        int hint_radius = (int)((fps * (double)VJ_TRACK_ALIGN_WIDE_HINT_RADIUS_MS / 1000.0) + 0.5);
        int hint_step = (int)((fps * (double)VJ_TRACK_ALIGN_WIDE_HINT_STEP_MS / 1000.0) + 0.5);
        int hint_start;
        int hint_end;
        int hint_attempts = 0;
        int hint_ok = 0;
        if(hint_radius < step_frames)
            hint_radius = step_frames;
        if(hint_step < 1)
            hint_step = 1;

        hint_start = hint_delta - hint_radius;
        hint_end = hint_delta + hint_radius;
        if(hint_start < -radius_frames)
            hint_start = -radius_frames;
        if(hint_end > radius_frames)
            hint_end = radius_frames;

        for(int delta = hint_start; delta <= hint_end; delta += hint_step) {
            long long candidate = target_frame + (long long)delta;
            int got;
            int conf = 0;
            double probe_rms = 0.0;
            double probe_peak = 0.0;
            int probe_nonzero = 0;

            hint_attempts++;
            coarse_attempts++;

            if(candidate < min_frame + probe_video_frames)
                candidate = min_frame + probe_video_frames;
            if(candidate > max_frame)
                candidate = max_frame;

            got = vj_perform_collect_clip_audio_window(info,
                                                       p,
                                                       el,
                                                       candidate,
                                                       probe_video_frames,
                                                       frame_bytes,
                                                       p->audio_render_buffer,
                                                       max_audio_frames);
            if(got <= 0) {
                coarse_no_audio++;
                continue;
            }

            vj_perform_track_align_pcm_stats(p->audio_render_buffer,
                                             got,
                                             frame_bytes,
                                             el->audio_chans,
                                             &probe_rms,
                                             &probe_peak,
                                             &probe_nonzero);
            coarse_rendered++;
            coarse_got_sum += got;
            coarse_rms_sum += probe_rms;
            coarse_nonzero_sum += probe_nonzero;
            if(probe_peak > coarse_peak_max)
                coarse_peak_max = probe_peak;

            if(!vj_audio_sync_track_align_probe_target_audio(&settings->audio_sync,
                                                             p->audio_render_buffer,
                                                             got,
                                                             frame_bytes,
                                                             el->audio_chans,
                                                             ((el->audio_chans > 0) ? ((frame_bytes / el->audio_chans) * 8) : 16),
                                                             el->audio_rate,
                                                             &conf))
            {
                if(conf > coarse_best_reject_conf) {
                    coarse_best_reject_conf = conf;
                }
                continue;
            }

            coarse_probe_ok++;
            hint_ok++;

            {
                int delta_frames = (int)(candidate - target_frame);
                int rank_conf = vj_perform_track_align_hint_rank_conf(conf,
                                                                       delta_frames,
                                                                       hint_delta,
                                                                       hint_have,
                                                                       fps);
                if(rank_conf > best_rank_conf) {
                    second_conf = best_conf;
                    best_conf = conf;
                    best_rank_conf = rank_conf;
                    best_delta = delta_frames;
                    coarse_best_delta = best_delta;
                } else if(conf > second_conf) {
                    second_conf = conf;
                }
            }
        }

    }


    for(int delta = -radius_frames; delta <= radius_frames; delta += step_frames) {
        long long candidate = target_frame + (long long)delta;
        int got;
        int conf = 0;
        double probe_rms = 0.0;
        double probe_peak = 0.0;
        int probe_nonzero = 0;

        coarse_attempts++;

        if(candidate < min_frame + probe_video_frames)
            candidate = min_frame + probe_video_frames;
        if(candidate > max_frame)
            candidate = max_frame;

        got = vj_perform_collect_clip_audio_window(info,
                                                   p,
                                                   el,
                                                   candidate,
                                                   probe_video_frames,
                                                   frame_bytes,
                                                   p->audio_render_buffer,
                                                   max_audio_frames);
        if(got <= 0) {
            coarse_no_audio++;
            continue;
        }

        vj_perform_track_align_pcm_stats(p->audio_render_buffer,
                                         got,
                                         frame_bytes,
                                         el->audio_chans,
                                         &probe_rms,
                                         &probe_peak,
                                         &probe_nonzero);
        coarse_rendered++;
        coarse_got_sum += got;
        coarse_rms_sum += probe_rms;
        coarse_nonzero_sum += probe_nonzero;
        if(probe_peak > coarse_peak_max)
            coarse_peak_max = probe_peak;

        if(!vj_audio_sync_track_align_probe_target_audio(&settings->audio_sync,
                                                         p->audio_render_buffer,
                                                         got,
                                                         frame_bytes,
                                                         el->audio_chans,
                                                         ((el->audio_chans > 0) ? ((frame_bytes / el->audio_chans) * 8) : 16),
                                                         el->audio_rate,
                                                         &conf))
        {
            if(conf > coarse_best_reject_conf) {
                coarse_best_reject_conf = conf;
            }
            continue;
        }

        coarse_probe_ok++;

        {
            int delta_frames = (int)(candidate - target_frame);
            int rank_conf = vj_perform_track_align_hint_rank_conf(conf,
                                                                   delta_frames,
                                                                   hint_delta,
                                                                   hint_have,
                                                                   fps);
            if(rank_conf > best_rank_conf) {
                second_conf = best_conf;
                best_conf = conf;
                best_rank_conf = rank_conf;
                best_delta = delta_frames;
                coarse_best_delta = best_delta;
            } else if(conf > second_conf) {
                second_conf = conf;
            }
        }
    }

    if(best_conf < 0) {
        (void)vj_perform_track_align_try_live_snap(info, p, el, &snap, now_ms, fps, "no-coarse");
        return;
    }

    if(best_conf >= VJ_TRACK_ALIGN_WIDE_QUIET_SUMMARY_CONF ||
       (best_conf - second_conf) >= VJ_TRACK_ALIGN_WIDE_QUIET_SUMMARY_MARGIN ||
       hint_have)


    {
        int fine_radius = step_frames;
        int fine_start = coarse_best_delta - fine_radius;
        int fine_end   = coarse_best_delta + fine_radius;

        for(int delta = fine_start; delta <= fine_end; delta++) {
            long long candidate = target_frame + (long long)delta;
            int got;
            int conf = 0;
            double probe_rms = 0.0;
            double probe_peak = 0.0;
            int probe_nonzero = 0;

            fine_attempts++;

            if(candidate < min_frame + probe_video_frames)
                candidate = min_frame + probe_video_frames;
            if(candidate > max_frame)
                candidate = max_frame;

            got = vj_perform_collect_clip_audio_window(info,
                                                       p,
                                                       el,
                                                       candidate,
                                                       probe_video_frames,
                                                       frame_bytes,
                                                       p->audio_render_buffer,
                                                       max_audio_frames);
            if(got <= 0) {
                fine_no_audio++;
                continue;
            }

            vj_perform_track_align_pcm_stats(p->audio_render_buffer,
                                             got,
                                             frame_bytes,
                                             el->audio_chans,
                                             &probe_rms,
                                             &probe_peak,
                                             &probe_nonzero);
            fine_rendered++;
            fine_got_sum += got;
            fine_rms_sum += probe_rms;
            fine_nonzero_sum += probe_nonzero;
            if(probe_peak > fine_peak_max)
                fine_peak_max = probe_peak;

            if(!vj_audio_sync_track_align_probe_target_audio(&settings->audio_sync,
                                                             p->audio_render_buffer,
                                                             got,
                                                             frame_bytes,
                                                             el->audio_chans,
                                                             ((el->audio_chans > 0) ? ((frame_bytes / el->audio_chans) * 8) : 16),
                                                             el->audio_rate,
                                                             &conf))
            {
                if(conf > fine_best_reject_conf) {
                    fine_best_reject_conf = conf;
                }
                continue;
            }

            fine_probe_ok++;

            {
                int delta_frames = (int)(candidate - target_frame);
                int rank_conf = vj_perform_track_align_hint_rank_conf(conf,
                                                                       delta_frames,
                                                                       hint_delta,
                                                                       hint_have,
                                                                       fps);
                if(rank_conf > best_rank_conf) {
                    second_conf = best_conf;
                    best_conf = conf;
                    best_rank_conf = rank_conf;
                    best_delta = delta_frames;
                } else if(conf > second_conf && delta != best_delta) {
                    second_conf = conf;
                }
            }
        }
    }

    if(second_conf < 0)
        second_conf = 0;

    if(best_conf >= VJ_TRACK_ALIGN_WIDE_QUIET_SUMMARY_CONF ||
       (best_conf - second_conf) >= VJ_TRACK_ALIGN_WIDE_QUIET_SUMMARY_MARGIN ||
       hint_have)

    {
        int long_best_delta = best_delta;
        int long_margin = best_conf - second_conf;

        if(best_conf >= 0 &&
           (best_conf < VJ_TRACK_ALIGN_WIDE_MIN_CONF ||
            long_margin < VJ_TRACK_ALIGN_WIDE_MIN_MARGIN))
        {
            int short_probe_video_frames = (int)((fps * (double)VJ_TRACK_ALIGN_WIDE_SHORT_PROBE_MS / 1000.0) + 0.5);
            int short_step = step_frames / 3;
            int short_radius = step_frames * 2;
            int short_attempts = 0;
            int short_rendered = 0;
            int short_probe_ok = 0;
            int short_no_audio = 0;
            int short_best_delta = 0;
            int short_best_conf = -1;
            int short_second_conf = -1;
            long long short_got_sum = 0;
            double short_rms_sum = 0.0;
            double short_peak_max = 0.0;
            int short_nonzero_sum = 0;
            int agree_frames = (int)(fps * ((double)VJ_TRACK_ALIGN_WIDE_SHORT_AGREE_MS / 1000.0) + 0.5);

            if(short_probe_video_frames < 12)
                short_probe_video_frames = 12;
            if(short_probe_video_frames > probe_video_frames)
                short_probe_video_frames = probe_video_frames;
            if(short_step < 1)
                short_step = 1;
            if(short_radius < step_frames)
                short_radius = step_frames;
            if(agree_frames < 4)
                agree_frames = 4;

            if(short_probe_video_frames * samples_per_video <= max_audio_frames) {
                int ranges[2][2];
                int range_count = 0;

                ranges[range_count][0] = long_best_delta - short_radius;
                ranges[range_count][1] = long_best_delta + short_radius;
                range_count++;

                if(hint_have) {
                    int hint_short_radius = (int)((fps * (double)VJ_TRACK_ALIGN_WIDE_HINT_RADIUS_MS / 1000.0) + 0.5);
                    if(hint_short_radius < short_radius)
                        hint_short_radius = short_radius;
                    ranges[range_count][0] = hint_delta - hint_short_radius;
                    ranges[range_count][1] = hint_delta + hint_short_radius;
                    range_count++;
                }

                for(int r = 0; r < range_count; r++) {
                    int start = ranges[r][0];
                    int end = ranges[r][1];
                    if(start < -radius_frames)
                        start = -radius_frames;
                    if(end > radius_frames)
                        end = radius_frames;

                    for(int delta = start; delta <= end; delta += short_step) {
                        long long candidate = target_frame + (long long)delta;
                        int got;
                        int conf = 0;
                        double probe_rms = 0.0;
                        double probe_peak = 0.0;
                        int probe_nonzero = 0;

                        short_attempts++;

                        if(candidate < min_frame + short_probe_video_frames)
                            candidate = min_frame + short_probe_video_frames;
                        if(candidate > max_frame)
                            candidate = max_frame;

                        got = vj_perform_collect_clip_audio_window(info,
                                                                   p,
                                                                   el,
                                                                   candidate,
                                                                   short_probe_video_frames,
                                                                   frame_bytes,
                                                                   p->audio_render_buffer,
                                                                   max_audio_frames);
                        if(got <= 0) {
                            short_no_audio++;
                            continue;
                        }

                        vj_perform_track_align_pcm_stats(p->audio_render_buffer,
                                                         got,
                                                         frame_bytes,
                                                         el->audio_chans,
                                                         &probe_rms,
                                                         &probe_peak,
                                                         &probe_nonzero);
                        short_rendered++;
                        short_got_sum += got;
                        short_rms_sum += probe_rms;
                        short_nonzero_sum += probe_nonzero;
                        if(probe_peak > short_peak_max)
                            short_peak_max = probe_peak;

                        if(!vj_audio_sync_track_align_probe_target_audio(&settings->audio_sync,
                                                                         p->audio_render_buffer,
                                                                         got,
                                                                         frame_bytes,
                                                                         el->audio_chans,
                                                                         ((el->audio_chans > 0) ? ((frame_bytes / el->audio_chans) * 8) : 16),
                                                                         el->audio_rate,
                                                                         &conf))
                        {
                            continue;
                        }

                        short_probe_ok++;

                        if(conf > short_best_conf) {
                            short_second_conf = short_best_conf;
                            short_best_conf = conf;
                            short_best_delta = (int)(candidate - target_frame);
                        } else if(conf > short_second_conf && delta != short_best_delta) {
                            short_second_conf = conf;
                        }
                    }
                }

                if(short_second_conf < 0)
                    short_second_conf = 0;

                if(short_best_conf >= 0) {
                    int short_margin = short_best_conf - short_second_conf;
                    int agrees_long = vj_perform_track_align_candidate_same(long_best_delta,
                                                                            short_best_delta,
                                                                            agree_frames);
                    int agrees_live = 0;
                    if(hint_have)
                        agrees_live = vj_perform_track_align_candidate_same(hint_delta,
                                                                            short_best_delta,
                                                                            agree_frames);

                    if(short_best_conf >= VJ_TRACK_ALIGN_WIDE_SHORT_MIN_CONF &&
                       short_margin >= VJ_TRACK_ALIGN_WIDE_SHORT_MIN_MARGIN &&
                       (agrees_long || agrees_live))
                    {
                        int short_rank_conf = vj_perform_track_align_hint_rank_conf(short_best_conf,
                                                                                    short_best_delta,
                                                                                    hint_delta,
                                                                                    hint_have,
                                                                                    fps);
                        if(short_rank_conf > best_rank_conf) {
                            best_delta = short_best_delta;
                            best_conf = short_best_conf;
                            best_rank_conf = short_rank_conf;
                            second_conf = short_second_conf;
                            used_short_probe = 1;
                            short_support = agrees_long ? 1 : 2;
                        }
                    }

                }
            }
        }
    }

    {
        int margin = best_conf - second_conf;
        int min_delta = (int)(fps * 0.25 + 0.5);
        int stable_tol = (int)(fps *
                               ((double)VJ_TRACK_ALIGN_WIDE_STABLE_TOLERANCE_MS / 1000.0) +
                               0.5);
        int same_candidate = 0;
        int best_sign = (best_delta > 0) ? 1 : ((best_delta < 0) ? -1 : 0);
        int last_sign = (p->track_align_last_wide_snap_delta > 0) ? 1 :
                        ((p->track_align_last_wide_snap_delta < 0) ? -1 : 0);
        int allow_small_reverse_trim = 0;

        if(min_delta < 2)
            min_delta = 2;
        if(stable_tol < 4)
            stable_tol = 4;

        {
            int required_conf_now;
            int required_margin_now;

            if(acquire_mode) {
                required_conf_now = VJ_TRACK_ALIGN_WIDE_ACQUIRE_MIN_CONF;
                required_margin_now = VJ_TRACK_ALIGN_WIDE_ACQUIRE_MIN_MARGIN;
            } else {
                required_conf_now = used_short_probe ? VJ_TRACK_ALIGN_WIDE_SHORT_MIN_CONF
                                                     : VJ_TRACK_ALIGN_WIDE_MIN_CONF;
                required_margin_now = used_short_probe ? VJ_TRACK_ALIGN_WIDE_SHORT_MIN_MARGIN
                                                       : VJ_TRACK_ALIGN_WIDE_MIN_MARGIN;
            }

            if(best_conf < required_conf_now ||
               margin < required_margin_now ||
               (best_delta > -min_delta && best_delta < min_delta))
            {


            if(best_delta <= -min_delta || best_delta >= min_delta) {
                vj_perform_track_align_candidate_conflict(p,
                                                          VJ_TRACK_ALIGN_CANDIDATE_WIDE,
                                                          best_delta,
                                                          best_conf,
                                                          margin,
                                                          stable_tol,
                                                          "wide-rejected");
            }


            (void)vj_perform_track_align_try_live_snap(info, p, el, &snap, now_ms, fps, "wide-rejected");
            return;
            }
        }

        if(acquire_mode) {
            int bucket_delta = best_delta;
            int bucket_conf = best_conf;
            int bucket_margin = margin;
            int bucket_obs = 0;
            int bucket_score = 0;
            int runner_score = 0;
            int small_alias_blocked = 0;
            int strong_one_shot = 0;
            int live_delta_for_bucket = 0;
            int live_have_for_bucket = 0;

            if(snap.track_align_confidence_pct >= VJ_TRACK_ALIGN_WIDE_HINT_MIN_CONF &&
               abs(snap.track_align_offset_ms) >= VJ_TRACK_ALIGN_WIDE_HINT_MIN_OFFSET_MS)
            {
                live_delta_for_bucket = (int)(((double)snap.track_align_offset_ms * fps / 1000.0) +
                                              (snap.track_align_offset_ms >= 0 ? 0.5 : -0.5));
                if(live_delta_for_bucket != 0)
                    live_have_for_bucket = 1;
            }

            acquisition_bucket_ready = vj_perform_track_align_update_wide_bucket(
                p,
                now_ms,
                best_delta,
                best_conf,
                margin,
                fps,
                live_delta_for_bucket,
                snap.track_align_confidence_pct,
                live_have_for_bucket,
                &bucket_delta,
                &bucket_conf,
                &bucket_margin,
                &bucket_obs,
                &bucket_score,
                &runner_score,
                &small_alias_blocked,
                &strong_one_shot);

            acquisition_bucket_score = bucket_score;
            acquisition_bucket_runner_score = runner_score;
            acquisition_bucket_obs = bucket_obs;
            acquisition_live_far_conflict = 0;


            if(snap.track_align_confidence_pct >= VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_CONF &&
               abs(snap.track_align_offset_ms) >= VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_OFFSET_MS)
            {
                int weak_live_delta = (int)(((double)snap.track_align_offset_ms * fps / 1000.0) +
                                            (snap.track_align_offset_ms >= 0 ? 0.5 : -0.5));
                int max_disagree = (int)(fps * ((double)VJ_TRACK_ALIGN_WEAK_HINT_BLOCK_STRONG1_DIFF_MS / 1000.0) + 0.5);
                int same_sign = 0;
                if(max_disagree < 8)
                    max_disagree = 8;
                same_sign = (weak_live_delta != 0 &&
                             ((weak_live_delta < 0 && bucket_delta < 0) ||
                              (weak_live_delta > 0 && bucket_delta > 0)));
                if(same_sign && abs(bucket_delta - weak_live_delta) > max_disagree)
                {
                    acquisition_live_far_conflict = 1;
                    if(strong_one_shot) {
                        strong_one_shot = 0;
                    }
                }
            }

            if(!acquisition_bucket_ready ||
               acquisition_live_far_conflict ||
               (!strong_one_shot && bucket_obs < VJ_TRACK_ALIGN_WIDE_ACQUIRE_STABLE_COUNT) ||
               (bucket_score - runner_score) < VJ_TRACK_ALIGN_WIDE_BUCKET_DOMINANCE_SCORE)
            {
                if(acquisition_live_far_conflict)
                (void)vj_perform_track_align_try_live_snap(info, p, el, &snap, now_ms, fps, "wide-bucket-hold");
                return;
            }

            best_delta = bucket_delta;
            best_conf = bucket_conf;
            margin = bucket_margin;
            second_conf = (best_conf > margin) ? (best_conf - margin) : 0;

            {
                int seed_count = (bucket_obs > 0) ? bucket_obs : 1;


                if(strong_one_shot && seed_count < VJ_TRACK_ALIGN_WIDE_ACQUIRE_STABLE_COUNT)
                    seed_count = VJ_TRACK_ALIGN_WIDE_ACQUIRE_STABLE_COUNT;

                p->track_align_candidate_ms = now_ms;
                p->track_align_candidate_delta = best_delta;
                p->track_align_candidate_conf = best_conf;
                p->track_align_candidate_margin = margin;
                p->track_align_candidate_last_conf = best_conf;
                p->track_align_candidate_last_margin = margin;
                p->track_align_candidate_sum_conf = best_conf * seed_count;
                p->track_align_candidate_sum_margin = margin * seed_count;
                p->track_align_candidate_count = seed_count;
                p->track_align_candidate_kind = VJ_TRACK_ALIGN_CANDIDATE_WIDE;
            }

        }

        if(p->track_align_last_wide_snap_ms != 0) {
            long since_snap = now_ms - p->track_align_last_wide_snap_ms;

            if(since_snap >= 0 &&
               since_snap < VJ_TRACK_ALIGN_WIDE_POST_SNAP_COOLDOWN_MS)
            {
                return;
            }

            if(since_snap >= VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_AGE_MS &&
               best_sign != 0 && last_sign != 0 && best_sign != last_sign &&
               abs(best_delta) <= VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_FRAMES &&
               best_conf >= VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_CONF &&
               margin >= VJ_TRACK_ALIGN_WIDE_SMALL_REVERSE_TRIM_MIN_MARGIN &&
               snap.track_align_locked &&
               ((snap.track_align_offset_ms > 0 && best_sign > 0) ||
                (snap.track_align_offset_ms < 0 && best_sign < 0)))
            {
                allow_small_reverse_trim = 1;
            }


            if(!allow_small_reverse_trim &&
               since_snap >= 0 &&
               since_snap < VJ_TRACK_ALIGN_WIDE_REVERSAL_GUARD_MS &&
               best_sign != 0 && last_sign != 0 && best_sign != last_sign)
            {
                return;
            }
        }

        {
            int required_stable = acquire_mode ? VJ_TRACK_ALIGN_WIDE_ACQUIRE_STABLE_COUNT
                                               : VJ_TRACK_ALIGN_WIDE_STABLE_COUNT;
            int small_delta_frames = (int)(fps * ((double)VJ_TRACK_ALIGN_WIDE_SMALL_DELTA_MS / 1000.0) + 0.5);
            int small_live_agrees_now = 0;
            if(small_delta_frames < min_delta)
                small_delta_frames = min_delta;
            if(abs(best_delta) <= small_delta_frames) {
                int live_sign = 0;
                int delta_sign = (best_delta > 0) ? 1 : ((best_delta < 0) ? -1 : 0);
                if(snap.track_align_locked &&
                   abs(snap.track_align_offset_ms) >= VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_OFFSET_MS)
                    live_sign = (snap.track_align_offset_ms > 0) ? 1 : -1;
                if(delta_sign != 0 && live_sign == delta_sign &&
                   best_conf >= VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_CONF &&
                   margin >= VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_MARGIN)
                    small_live_agrees_now = 1;

                if(small_live_agrees_now) {
                    if(required_stable < VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_STABLE_COUNT)
                        required_stable = VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_STABLE_COUNT;
                } else if(required_stable < VJ_TRACK_ALIGN_WIDE_SMALL_STABLE_COUNT) {
                    required_stable = VJ_TRACK_ALIGN_WIDE_SMALL_STABLE_COUNT;
                }
            }

            if(acquisition_bucket_ready) {
                same_candidate = 1;
                if(p->track_align_candidate_count < required_stable)
                    p->track_align_candidate_count = required_stable;
            } else if(p->track_align_candidate_kind == VJ_TRACK_ALIGN_CANDIDATE_WIDE &&
               p->track_align_candidate_ms != 0 &&
               (now_ms - p->track_align_candidate_ms) >= 0 &&
               (now_ms - p->track_align_candidate_ms) <= VJ_TRACK_ALIGN_WIDE_CANDIDATE_TTL_MS &&
               vj_perform_track_align_candidate_same(p->track_align_candidate_delta,
                                                     best_delta,
                                                     stable_tol))
            {
                same_candidate = 1;
                p->track_align_candidate_count++;
                if(p->track_align_candidate_count > required_stable)
                    p->track_align_candidate_count = required_stable;
                if(p->track_align_candidate_conf <= 0 || best_conf < p->track_align_candidate_conf)
                    p->track_align_candidate_conf = best_conf;
                if(p->track_align_candidate_margin <= 0 || margin < p->track_align_candidate_margin)
                    p->track_align_candidate_margin = margin;
                p->track_align_candidate_sum_conf += best_conf;
                p->track_align_candidate_sum_margin += margin;
            } else {
                p->track_align_candidate_count = 1;
                p->track_align_candidate_conf = best_conf;
                p->track_align_candidate_margin = margin;
                p->track_align_candidate_sum_conf = best_conf;
                p->track_align_candidate_sum_margin = margin;
            }

            p->track_align_candidate_ms = now_ms;
            p->track_align_candidate_delta = best_delta;
            p->track_align_candidate_last_conf = best_conf;
            p->track_align_candidate_last_margin = margin;
            p->track_align_candidate_kind = VJ_TRACK_ALIGN_CANDIDATE_WIDE;


            if(!same_candidate ||
               p->track_align_candidate_count < required_stable)
            {
                return;
            }
        }

        {
            int avg_conf = (p->track_align_candidate_count > 0) ?
                           (p->track_align_candidate_sum_conf / p->track_align_candidate_count) : best_conf;
            int small_delta_frames = (int)(fps * ((double)VJ_TRACK_ALIGN_WIDE_SMALL_DELTA_MS / 1000.0) + 0.5);
            int small_needs_strict = 0;
            int small_live_agrees = 0;
            if(small_delta_frames < min_delta)
                small_delta_frames = min_delta;
            if(abs(best_delta) <= small_delta_frames) {
                small_needs_strict = 1;
                if(snap.track_align_locked &&
                   abs(snap.track_align_offset_ms) >= VJ_TRACK_ALIGN_WIDE_SMALL_LIVE_AGREE_MIN_OFFSET_MS) {
                    int live_sign = (snap.track_align_offset_ms > 0) ? 1 :
                                    ((snap.track_align_offset_ms < 0) ? -1 : 0);
                    int delta_sign = (best_delta > 0) ? 1 :
                                     ((best_delta < 0) ? -1 : 0);
                    if(live_sign != 0 && live_sign == delta_sign)
                        small_live_agrees = 1;
                }
            }

            {
                int base_need_conf;
                int base_need_margin;
                int base_need_avg;

                if(acquire_mode) {
                    base_need_conf = VJ_TRACK_ALIGN_WIDE_ACQUIRE_MIN_CONF;
                    base_need_margin = VJ_TRACK_ALIGN_WIDE_ACQUIRE_MIN_MARGIN;
                    base_need_avg = VJ_TRACK_ALIGN_WIDE_ACQUIRE_AVG_MIN_CONF;
                } else {
                    base_need_conf = used_short_probe ? VJ_TRACK_ALIGN_WIDE_SHORT_MIN_CONF : VJ_TRACK_ALIGN_WIDE_MIN_CONF;
                    base_need_margin = used_short_probe ? VJ_TRACK_ALIGN_WIDE_SHORT_MIN_MARGIN : VJ_TRACK_ALIGN_WIDE_MIN_MARGIN;
                    base_need_avg = used_short_probe ? VJ_TRACK_ALIGN_WIDE_SHORT_MIN_CONF : VJ_TRACK_ALIGN_WIDE_AVG_MIN_CONF;
                }

            if(p->track_align_candidate_conf < base_need_conf ||
               p->track_align_candidate_margin < base_need_margin ||
               p->track_align_candidate_last_conf < base_need_conf ||
               p->track_align_candidate_last_margin < base_need_margin ||
               avg_conf < base_need_avg ||
               (!acquire_mode && small_needs_strict && !small_live_agrees &&
                (p->track_align_candidate_conf < VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_MIN_CONF ||
                 p->track_align_candidate_margin < VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_MIN_MARGIN ||
                 p->track_align_candidate_last_conf < VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_MIN_CONF ||
                 p->track_align_candidate_last_margin < VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_MIN_MARGIN ||
                 avg_conf < VJ_TRACK_ALIGN_WIDE_SMALL_STRICT_AVG_CONF)))
            {
                vj_perform_track_align_clear_candidate(p);
                return;
            }
            }

            if(acquire_mode) {
                int peak_conf = p->track_align_candidate_last_conf;
                if(p->track_align_candidate_conf > peak_conf)
                    peak_conf = p->track_align_candidate_conf;
                if(avg_conf > peak_conf)
                    peak_conf = avg_conf;
                best_conf = peak_conf;
            } else {
                best_conf = avg_conf;
            }
            margin = p->track_align_candidate_margin;
        }

        {
            int max_snap = radius_frames;
            int wide_delta = best_delta;
            int offer_delta;
            int live_delta = 0;
            int live_ms = 0;
            int live_conf = 0;
            int live_locked = 0;
            int live_authority_ok = 0;
            int used_live_authority = 0;
            int used_live_hint_authority = 0;
            int live_conflict_suppressed = 0;
            int live_tol_frames = (int)(fps * ((double)VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_TOLERANCE_MS / 1000.0) + 0.5);
            vj_audio_sync_snapshot_t offer_snap;

            if(live_tol_frames < 1)
                live_tol_frames = 1;

            if(wide_delta > max_snap)
                wide_delta = max_snap;
            else if(wide_delta < -max_snap)
                wide_delta = -max_snap;

            offer_delta = wide_delta;
            offer_snap = snap;


            if(!vj_audio_sync_get_snapshot(&settings->audio_sync, &offer_snap))
                offer_snap = snap;

            live_locked = offer_snap.track_align_locked;
            live_ms = offer_snap.track_align_offset_ms;
            live_conf = offer_snap.track_align_confidence_pct;

            if(live_ms != 0) {
                live_delta = (int)(((double)live_ms * fps / 1000.0) +
                                   (live_ms >= 0 ? 0.5 : -0.5));
            }

            live_authority_ok =
                (live_locked &&
                 live_conf >= VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_MIN_CONF &&
                 abs(live_ms) >= VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_MIN_OFFSET_MS);


            if(!live_authority_ok && acquire_mode &&
               live_conf >= VJ_TRACK_ALIGN_WIDE_LIVE_HINT_AUTHORITY_MIN_CONF &&
               abs(live_ms) >= VJ_TRACK_ALIGN_WIDE_LIVE_HINT_AUTHORITY_MIN_OFFSET_MS)
            {
                live_authority_ok = 1;
                used_live_hint_authority = 1;
            }


            if(!live_authority_ok && acquire_mode && live_delta != 0 &&
               live_conf >= VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_CONF &&
               abs(live_ms) >= VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_OFFSET_MS &&
               best_conf >= VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_WIDE_CONF &&
               margin >= VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MIN_MARGIN)
            {
                int wide_sign = (wide_delta > 0) ? 1 : ((wide_delta < 0) ? -1 : 0);
                int live_sign = (live_delta > 0) ? 1 : ((live_delta < 0) ? -1 : 0);
                int diff = abs(wide_delta - live_delta);

                if(wide_sign != 0 && live_sign == wide_sign &&
                   diff <= VJ_TRACK_ALIGN_WIDE_ROUGH_AUTHORITY_MAX_DIFF_FR)
                {
                    live_authority_ok = 1;
                    used_live_hint_authority = 1;
                }
            }

            if(live_authority_ok && live_delta != 0) {
                int wide_sign = (wide_delta > 0) ? 1 : ((wide_delta < 0) ? -1 : 0);
                int live_sign = (live_delta > 0) ? 1 : ((live_delta < 0) ? -1 : 0);

                if(wide_sign != 0 && live_sign == wide_sign) {
                    int diff = abs(wide_delta - live_delta);
                    if(diff > live_tol_frames) {
                        offer_delta = live_delta;
                        used_live_authority = 1;
                    }
                } else if(wide_sign != 0 && live_sign != 0 &&
                          live_conf >= VJ_TRACK_ALIGN_WIDE_LIVE_CONFLICT_SUPPRESS_CONF)
                {
                    live_conflict_suppressed = 1;
                }
            }

            if(live_conflict_suppressed) {
                vj_perform_track_align_clear_candidate(p);
                return;
            }

            if(offer_delta > max_snap)
                offer_delta = max_snap;
            else if(offer_delta < -max_snap)
                offer_delta = -max_snap;

            if(!acquire_mode &&
               offer_delta < 0 &&
               live_locked &&
               live_conf >= VJ_TRACK_ALIGN_WIDE_LIVE_AUTHORITY_MIN_CONF &&
               abs(live_ms) < VJ_TRACK_ALIGN_SETTLED_BACKWARD_SNAP_BLOCK_MS)
            {
                vj_perform_track_align_clear_candidate(p);
                return;
            }

            if(best_conf < VJ_TRACK_ALIGN_WIDE_OFFER_MIN_CONF) {
                return;
            }

            vj_audio_sync_track_align_offer_snap(&settings->audio_sync,
                                                 offer_delta,
                                                 best_conf);
            vj_perform_track_align_mark_acquired(p, now_ms, offer_delta, "wide-offer");

            veejay_msg(VEEJAY_MSG_INFO,
                       "[TRACK-ALIGN] stable wide search candidate wide=%+d offer=%+d authority=%s acq=%d fresh_live=%d live=%+dms/%+dfr live_conf=%d%% conf_avg=%d%% margin_min=%d%% tol=%dfr probe=%s support=%d bucket_obs=%d bucket_score=%d runner=%d",
                       wide_delta,
                       offer_delta,
                       used_live_authority ? (used_live_hint_authority ? "live-hint" : "live") : "wide",
                       acquire_mode,
                       live_locked,
                       live_ms,
                       live_delta,
                       live_conf,
                       best_conf,
                       margin,
                       live_tol_frames,
                       used_short_probe ? "short" : "long",
                       short_support,
                       acquisition_bucket_obs,
                       acquisition_bucket_score,
                       acquisition_bucket_runner_score);
        }
    }
}

static int vj_perform_queue_audio_frame_impl(veejay_t *info, void *ptr, uint8_t *a_buf, int speed, long long target_frame,int sample_id, int *audio_sample_ptr)
{
    if( info->audio == NO_AUDIO )
        return 0;
    editlist *el_fallback = info->current_edit_list;
    editlist *el = (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_editlist(sample_id) : info->edit_list);
    if(el == NULL)
        el = el_fallback;

    performer_t *p = (performer_t*) ptr;
    int *sample_cursor = audio_sample_ptr ? audio_sample_ptr : &(p->play_audio_sample_);

    if(!el->has_audio || speed == 0 || target_frame == -1) {
        int num_samples = (el->audio_rate / el->video_fps);
        int bps = el->audio_bps;
        veejay_memset( a_buf, 0, num_samples * bps);
        return num_samples;
    }

    int num_samples =  (el->audio_rate/el->video_fps);
    int pred_len = num_samples;
    int bps     =   el->audio_bps;

    if (info->audio == AUDIO_PLAY)
    {
        switch (info->uc->playback_mode)
        {
            case VJ_PLAYBACK_MODE_SAMPLE:
                if( el->has_audio )
                    num_samples = vj_perform_fill_audio_buffers(info,el, a_buf, p, sample_cursor, target_frame);
                break;
            case VJ_PLAYBACK_MODE_PLAIN:
                if( el->has_audio )
                    num_samples = vj_perform_fill_audio_buffers(info,el, a_buf, p, sample_cursor, target_frame);
                break;
            case VJ_PLAYBACK_MODE_TAG:
                if(el->has_audio)
                {
                    num_samples = vj_tag_get_audio_frame(info->uc->sample_id, a_buf);
                }
                break;
        }

        if(num_samples <= 0 )
        {
            num_samples = pred_len;
            veejay_memset(a_buf, 0, num_samples * bps );
        }

        return num_samples;
     }
    return 0;
}

int vj_perform_queue_audio_frame(veejay_t *info, void *ptr, uint8_t *a_buf, int speed, long long target_frame,int sample_id )
{
    performer_t *p = (performer_t*) ptr;
    if(!p)
        return 0;

    return vj_perform_queue_audio_frame_impl(info,
                                             ptr,
                                             a_buf,
                                             speed,
                                             target_frame,
                                             sample_id,
                                             &(p->play_audio_sample_));
}


void vj_produce_audio_chain(veejay_t *info, int sample_id) {
    performer_global_t *g = (performer_global_t*) info->performer;
    if (info->uc->playback_mode != VJ_PLAYBACK_MODE_SAMPLE) return;

    sample_eff_chain **chain = sample_get_effect_chain(sample_id);
    if (chain == NULL) return;

    audio_chain_buffer_t *current_buf = &g->audio_chain_buffers[g->audio_chain_index];
    int changed = 0;
    int requested_active_count = 0;

    for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        if (chain[i] && chain[i]->e_flag == 1)
            requested_active_count++;
    }

    if (requested_active_count != current_buf->count) {
        changed = 1;
    } else {
        int current_entry_idx = 0;
        for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
            if (chain[i]->e_flag != 1
                || chain[i]->beat_flag != 1 ) continue;

            audio_chain_entry_t *curr = &current_buf->entries[current_entry_idx];

            if (curr->sample_id   != chain[i]->channel ||
                curr->sample_type != chain[i]->source_type ||
                curr->opacity     != chain[i]->audio_opacity)
            {
                changed = 1;
                break;
            }
            current_entry_idx++;
        }
    }

    if (!changed) return;

    int target_idx = 1 - g->audio_chain_index;
    audio_chain_buffer_t *buf = &g->audio_chain_buffers[target_idx];

    int expected = AC_STATE_IDLE;
    if (!__atomic_compare_exchange_n(&buf->state, &expected, AC_STATE_PRODUCING,
                                   false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return;
    }

    buf->count = 0;

    for (int i = 0; i < SAMPLE_MAX_EFFECTS; i++) {
        if (chain[i]->e_flag != 1 || chain[i]->beat_flag != 1) continue;
        if (chain[i]->source_type != VJ_PLAYBACK_MODE_SAMPLE) continue;

        audio_chain_entry_t *dst = &buf->entries[buf->count];
        int sample_i[6];

        if (sample_get_long_info(chain[i]->channel, &sample_i[0], &sample_i[1],
            &sample_i[2], &sample_i[3], &sample_i[4], &sample_i[5]) == 0)
        {
            dst->sample_type = chain[i]->source_type;
            dst->sample_id   = chain[i]->channel;
            dst->opacity     = chain[i]->audio_opacity;

            dst->start    = sample_i[0];
            dst->end      = sample_i[1];
            dst->loopmode = sample_i[2];
            dst->speed    = sample_i[3];
            dst->cur_sfd  = sample_i[4];
            dst->max_sfd  = sample_i[5];
            dst->el       = sample_get_editlist(dst->sample_id);
            dst->offset   = sample_get_resume(dst->sample_id);
            buf->count++;
        }
    }

    atomic_store_int(&buf->state, AC_STATE_READY);
}

static void vj_audio_load_to_bus(
    float *dest_bus,
    const uint8_t *src,
    int num_samples,
    int num_channels,
    int bps
) {
    const int total_elements = num_samples * num_channels;

    for (int i = 0; i < total_elements; i++) {
        const int off = i * bps;
        float s = (float)read_sample(&src[off], bps);

        if (bps == 1) {
            s -= 128.0f;
        }

        dest_bus[i] = s;
    }
}
static void vj_audio_accumulate_to_bus(
    float *bus,
    const uint8_t *in,
    int num_samples,
    int num_channels,
    int bps,
    float opacity
) {
    const int total_elements = num_samples * num_channels;

    for (int i = 0; i < total_elements; i++) {
        const int off = i * bps;

        float s_new = (float)read_sample(&in[off], bps);

        if (bps == 1) {
            s_new -= 128.0f;
        }

        bus[i] += (s_new * opacity);
    }
}


static void vj_audio_finalize_mix(
    uint8_t *dest,
    const float *mix_bus,
    int num_samples,
    int num_channels,
    int bps
) {
    const float max_val =
        (bps == 1) ? 127.0f :
        (bps == 2) ? 32767.0f :
        (bps == 3) ? 8388607.0f :
                     2147483647.0f;

    const int total_elements = num_samples * num_channels;

    for (int i = 0; i < total_elements; i++) {
        const int off = i * bps;
        float mixed = mix_bus[i] * POST_MIX_TRIM;

        if (mixed > max_val) {
            mixed = max_val;
        } else if (mixed < -max_val) {
            mixed = -max_val;
        }

        int32_t out_val;
        if (bps == 1) {
            out_val = (int32_t)(mixed + 128.0f);
        } else {
            out_val = (int32_t)mixed;
        }

        write_sample(&dest[off], bps, out_val);
    }

}


void vj_audio_consume_chain(veejay_t *info, uint8_t *audio_chunk, int in_samples) {
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    int next_idx = 1 - g->audio_chain_index;
    audio_chain_buffer_t *next_buf = &g->audio_chain_buffers[next_idx];
    int expected = AC_STATE_READY;
    int chans = info->current_edit_list->audio_chans;
    int bytesperframe = info->current_edit_list->audio_bps;
    int bps1 = bytesperframe / chans;

    if (__atomic_compare_exchange_n(&next_buf->state, &expected, AC_STATE_CONSUMING,
                                   false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
    {
        atomic_store_int(&g->audio_chain_buffers[g->audio_chain_index].state, AC_STATE_IDLE);
        g->audio_chain_index = next_idx;
    }
    audio_chain_buffer_t *buf = &g->audio_chain_buffers[g->audio_chain_index];

    if (buf->state != AC_STATE_CONSUMING) {
        return;
    }

    float *mix_bus = g->accum[g->audio_chain_index];
    vj_audio_load_to_bus(mix_bus, audio_chunk, in_samples, chans, bps1);

    for (int i = 0; i < buf->count; i++) {
        audio_chain_entry_t *entry = &buf->entries[i];

        if (entry->sample_id == info->uc->sample_id) {
            continue;
        }
        if (entry->sample_type != VJ_PLAYBACK_MODE_SAMPLE) continue;

        if(entry->sample_id <= 0) continue;

        int num_samples = vj_perform_queue_audio_frame_buf(
            info, p, entry->buffer, entry->el, entry->speed, entry->offset
        );

        if (num_samples > 0) {

            int num_channels = entry->el->audio_chans;
            int bps = entry->el->audio_bps / num_channels;

            vj_audio_accumulate_to_bus(
                mix_bus,
                entry->buffer,
                num_samples,
                chans,
                bps,
                entry->opacity
            );

            entry->offset = vj_calc_next_sub_audioframe(info, entry->sample_id, entry);
        }
    }

    vj_audio_finalize_mix(audio_chunk, mix_bus, in_samples, chans, bps1);
}

#endif


int vj_perform_get_width( veejay_t *info )
{
    return info->video_output_width;
}

int vj_perform_get_height( veejay_t *info )
{
    return info->video_output_height;
}


static char *vj_perform_print_credits(veejay_t *info)
{
    char text[2048];
    char arch[256] = "";
    char simd[512] = "";
    size_t pos;

    pos = 0;
#ifdef ARCH_MIPS
    pos += snprintf(arch + pos, sizeof(arch) - pos, "MIPS ");
#endif
#ifdef ARCH_PPC
    pos += snprintf(arch + pos, sizeof(arch) - pos, "PPC ");
#endif
#ifdef ARCH_X86_64
    pos += snprintf(arch + pos, sizeof(arch) - pos, "X86_64 ");
#endif
#ifdef ARCH_X86
    pos += snprintf(arch + pos, sizeof(arch) - pos, "X86 ");
#endif
#ifdef HAVE_ARM_ASIMD
    pos += snprintf(arch + pos, sizeof(arch) - pos, "ARM ASIMD ");
#endif
#ifdef HAVE_ARM_NEON
    pos += snprintf(arch + pos, sizeof(arch) - pos, "ARM NEON ");
#endif
#ifdef HAVE_ARM
    pos += snprintf(arch + pos, sizeof(arch) - pos, "ARM ");
#endif
#ifdef HAVE_ARMV7A
    pos += snprintf(arch + pos, sizeof(arch) - pos, "ARMv7A ");
#endif
#ifdef HAVE_DARWIN
    pos += snprintf(arch + pos, sizeof(arch) - pos, "Darwin ");
#endif
#ifdef HAVE_PS2
    pos += snprintf(arch + pos, sizeof(arch) - pos, "PS2 ");
#endif

    pos = 0;
#ifdef HAVE_ALTIVEC
    pos += snprintf(simd + pos, sizeof(simd) - pos, "Altivec ");
#endif
#ifdef HAVE_ASM_SSE
    pos += snprintf(simd + pos, sizeof(simd) - pos, "SSE ");
#endif
#ifdef HAVE_ASM_SSE2
    pos += snprintf(simd + pos, sizeof(simd) - pos, "SSE2 ");
#endif
#ifdef HAVE_ASM_SSE4_1
    pos += snprintf(simd + pos, sizeof(simd) - pos, "SSE4.1 ");
#endif
#ifdef HAVE_ASM_SSE4_2
    pos += snprintf(simd + pos, sizeof(simd) - pos, "SSE4.2 ");
#endif
#ifdef HAVE_ASM_MMX
    pos += snprintf(simd + pos, sizeof(simd) - pos, "MMX ");
#endif
#ifdef HAVE_ASM_MMXEXT
    pos += snprintf(simd + pos, sizeof(simd) - pos, "MMXEXT ");
#endif
#ifdef HAVE_ASM_MMX2
    pos += snprintf(simd + pos, sizeof(simd) - pos, "MMX2 ");
#endif
#ifdef HAVE_ASM_3DNOW
    pos += snprintf(simd + pos, sizeof(simd) - pos, "3DNow ");
#endif
#ifdef HAVE_ASM_AVX
    pos += snprintf(simd + pos, sizeof(simd) - pos, "AVX ");
#endif
#ifdef HAVE_ASM_AVX2
    pos += snprintf(simd + pos, sizeof(simd) - pos, "AVX2 ");
#endif
#ifdef HAVE_ASM_AVX512
    pos += snprintf(simd + pos, sizeof(simd) - pos, "AVX512 ");
#endif

    snprintf(text, sizeof(text),
        "This is Veejay %s\n"
        "%s\n\n"
        "%-15s: %s\n"
        "%-15s: %s\n"
        "%-15s: %s\n"
        "%-15s: %d bytes\n"
        "%-15s: %d bytes\n"
        "%-15s: %s\n"
        "%-15s: %s\n",
        VERSION,
        intro,
        "Build OS", BUILD_OS,
        "Kernel", BUILD_KERNEL,
        "Machine", BUILD_MACHINE,
        "CPU Cache", cpu_get_cacheline_size(),
        "Mem Align", mem_align_size(),
        "Architecture", arch,
        "SIMD/Ext", simd
    );

    return vj_strdup(text);
}

static char *osd_drift_indicator(double drift_s, double spvf)
{
    static char buf[32];
    static double ema_drift = 0.0;
    static int initialized = 0;

    const int len = 13;
    const int center = len / 2;

    const double alpha = 0.12;
    const double deadzone = 0.02 * spvf;

    if (!initialized) {
        ema_drift = drift_s;
        initialized = 1;
    } else {
        ema_drift = alpha * drift_s + (1.0 - alpha) * ema_drift;
    }

    double display = ema_drift;

    if (display > -deadzone && display < deadzone)
        display = 0.0;

    double drift_frames = display / spvf;
    double abs_frames = fabs(drift_frames);

    const double range_frames = 0.5;

    if (drift_frames >  range_frames) drift_frames =  range_frames;
    if (drift_frames < -range_frames) drift_frames = -range_frames;

    int bars = (int)(drift_frames / range_frames * center);

    char fill = '#';
    if (abs_frames > 0.5)
        fill = '!';
    else if (abs_frames > 0.25)
        fill = '+';

    for (int i = 0; i < len; i++)
        buf[i] = '-';

    buf[center] = '|';

    if (bars > 0) {
        for (int i = 1; i <= bars && center + i < len; i++)
            buf[center + i] = fill;
    }
    else if (bars < 0) {
        for (int i = -1; i >= bars && center + i >= 0; i--)
            buf[center + i] = fill;
    }

    buf[len] = '\0';

    snprintf(buf + len, sizeof(buf) - len,
             " %+0.2f", drift_frames);

    return buf;
}


static char *osd_performance_indicator(double render_duration, double spvf) {
    static char buf[128];
    static double ema_duration = -1.0;
    static double ema_load_pct = -1.0;

    const double alpha_dur  = 0.15;
    const double alpha_load = 0.25;

    if (ema_duration < 0) {
        ema_duration = render_duration;
        ema_load_pct = (render_duration / spvf) * 100.0;
    } else {
        ema_duration = alpha_dur * render_duration + (1.0 - alpha_dur) * ema_duration;
        double raw_load = (ema_duration / spvf) * 100.0;
        ema_load_pct = alpha_load * raw_load + (1.0 - alpha_load) * ema_load_pct;
    }

    const double skip_threshold = 1.5 * spvf;
    const char *status;
    if (ema_duration > skip_threshold) status = "OVLD";
    else if (ema_duration > spvf)      status = "LAG";
    else if (ema_duration > 0.85*spvf) status = "WARN";
    else                               status = " OK";

    char spark[11];
    int bars = (int)((ema_load_pct + 5) / 10.0);
    if (bars > 10) bars = 10;
    if (bars < 0)  bars = 0;
    for (int i = 0; i < 10; i++) spark[i] = (i < bars) ? '#' : '-';
    spark[10] = '\0';

    snprintf(buf, sizeof(buf),
             "%-4s [%s] %2.4fs (%3.0f%%)",
             status, spark, ema_duration, ema_load_pct);

    return buf;
}

static char *osd_xrun_indicator(long underruns, long xruns)
{
    static char buf[16];
    static long last_u = 0;
    static long last_x = 0;
    static double severity = 0.0;

    const int len = 5;
    const double decay = 0.95;

    long du = underruns - last_u;
    long dx = xruns - last_x;

    last_u = underruns;
    last_x = xruns;

    severity += du * 0.3;
    severity += dx * 2.0;
    severity *= decay;

    if (severity > len)
        severity = len;

    int bars = (int)(severity + 0.5);

    buf[0] = '[';
    for (int i = 0; i < len; i++)
    {
        if (i < bars)
        {
            if (severity > 3.5)
                buf[i + 1] = '!';
            else if (severity > 1.5)
                buf[i + 1] = '+';
            else
                buf[i + 1] = '#';
        }
        else
            buf[i + 1] = '-';
    }
    buf[len + 1] = ']';
    buf[len + 2] = '\0';

    return buf;
}

#ifdef HAVE_JACK
static void osd_audio_clock_line(veejay_t *info, char *dst, size_t dst_len)
{
    video_playback_setup *settings;
    vj_audio_sync_snapshot_t snap;
    int have_sync = 0;
    char tq[128] = "";

    if(!dst || dst_len == 0)
        return;
    dst[0] = '\0';

    if(!info || !info->settings)
        return;

    settings = info->settings;
    have_sync = vj_audio_sync_get_snapshot(&settings->audio_sync, &snap);

    if(have_sync && snap.enabled) {
        snprintf(tq, sizeof(tq),
                 " tq=%d/%dms tqdrop=%ld/%ldfr lock=%ld",
                 snap.target_queue_retained_ms,
                 snap.target_queue_ring_ms,
                 snap.target_queue_overflow_events + snap.target_queue_lock_drops,
                 snap.target_queue_dropped_frames,
                 snap.target_queue_lock_drops);
    }

    snprintf(dst, dst_len,
             "AUDIO src=%d sync=%d spd=%d sfd=%d w=%d/%d q=%dms slp=%dms wait=%lld z/s=%lld/%lld pend=%d pd=%lld vd=%lld qnull=%lld slow=%lld anom=%lld%s",
             atomic_load_int(&settings->audio_osd.last_src),
             atomic_load_int(&settings->audio_osd.last_sync),
             atomic_load_int(&settings->audio_osd.last_speed),
             atomic_load_int(&settings->audio_osd.last_sfd),
             atomic_load_int(&settings->audio_osd.last_written),
             atomic_load_int(&settings->audio_osd.last_decoded),
             atomic_load_int(&settings->audio_osd.last_qdepth_ms),
             atomic_load_int(&settings->audio_osd.last_sleep_ms),
             atomic_load_long_long(&settings->audio_osd.prod_waits),
             atomic_load_long_long(&settings->audio_osd.prod_write_zero),
             atomic_load_long_long(&settings->audio_osd.prod_write_short),
             atomic_load_int(&settings->audio_osd.last_pending),
             atomic_load_long_long(&settings->audio_osd.prod_pending_drop_frames),
             atomic_load_long_long(&settings->audio_osd.prod_video_drop_frames),
             atomic_load_long_long(&settings->audio_osd.prod_queue_nulls),
             atomic_load_long_long(&settings->audio_osd.prod_slow_renders),
             atomic_load_long_long(&settings->audio_osd.prod_anomalies),
             tq);
}

static char *vj_perform_audio_clock_osd_status(veejay_t *info)
{
    char line[512];
    size_t len;
    char *status_str;

    osd_audio_clock_line(info, line, sizeof(line));
    if(line[0] == '\0')
        return NULL;

    len = strlen(line) + 1;
    status_str = (char *)vj_malloc(len);
    if(!status_str)
        return NULL;

    strncpy(status_str, line, len);
    return status_str;
}
#endif


static char *vj_perform_osd_status(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    video_playback_stats *stats = &info->stats;

    char *more = NULL;
    if (info->composite) {
        void *vp = composite_get_vp(info->composite);
        if (viewport_get_mode(vp) == 1)
            more = viewport_get_my_status(vp);
    }

    MPEG_timecode_t tc;
    veejay_memset(&tc, 0, sizeof(tc));

    y4m_ratio_t ratio = mpeg_conform_framerate((double)info->current_edit_list->video_fps);
    int n = mpeg_framerate_code(ratio);

    mpeg_timecode(&tc, stats->current_frame, n, info->current_edit_list->video_fps);
    char timecode[20];
    snprintf(timecode, sizeof(timecode), "%02d:%02d:%02d:%02d", tc.h, tc.m, tc.s, tc.f);

    mpeg_timecode(&tc, stats->total_frames_produced, n, info->current_edit_list->video_fps);
    char master_timecode[20];
    snprintf(master_timecode, sizeof(master_timecode), "%02d:%02d:%02d:%02d", tc.h, tc.m, tc.s, tc.f);

    float speed = settings->current_playback_speed;
    if(info->sfd) {
        speed = 1.0 / info->sfd;
    }

    long ur = 0;
#ifdef HAVE_JACK
    ur = (info->audio == AUDIO_PLAY ? vj_jack_underruns() : 0);
#endif

    char *audio_info = osd_xrun_indicator( ur,stats->xruns);

    const char *mode_str = "Plain";
    switch (info->uc->playback_mode) {
        case VJ_PLAYBACK_MODE_SAMPLE: mode_str = "Sample"; break;
        case VJ_PLAYBACK_MODE_TAG:    mode_str = "Tag";    break;
    }

    char buf[1024];
    if(info->video_output_width < 1920)
        snprintf(buf, sizeof(buf),
            "%s |%s |%-5s %3d/%3d |Frame %7lld\nSkipped %4lld "
            "Speed %2.2fx %s %s\n%s",
            master_timecode,
            timecode,
            mode_str,
            info->uc->sample_id,
            (mode_str[0] == 'S') ? sample_size() :
            (mode_str[0] == 'T') ? vj_tag_size() : 1,
            (long long)stats->current_frame,
            (long long)stats->total_frames_skipped,
            speed,
            osd_drift_indicator(stats->delta_s, settings->spvf),
            audio_info,
            osd_performance_indicator(stats->render_duration, settings->spvf)
        );
    else
        snprintf(buf, sizeof(buf),
            "%12s |%12s |%-5s %3d/%3d |Frame %07lld Skipped %04lld "
            "Speed %2.2fx %s %s %s",
            master_timecode,
            timecode,
            mode_str,
            info->uc->sample_id,
            (mode_str[0] == 'S') ? sample_size() :
            (mode_str[0] == 'T') ? vj_tag_size() : 1,
            (long long)stats->current_frame,
            (long long)stats->total_frames_skipped,
            speed,
            osd_drift_indicator(stats->delta_s, settings->spvf),
            audio_info,
            osd_performance_indicator(stats->render_duration, settings->spvf)
        );

    int total_len = strlen(buf) + 1 + (more ? strlen(more) + 4 : 0);
    char *status_str = (char *)vj_malloc(total_len);
    if (!status_str) {
        if (more) free(more);
        return NULL;
    }

    if (more) {
        snprintf(status_str, total_len, "%s | %s", more, buf);
        free(more);
    } else {
        strncpy(status_str, buf, total_len);
    }

    return status_str;
}

static  void    vj_perform_render_osd( veejay_t *info, video_playback_setup *settings,VJFrame *frame )
{
    if(info->use_osd <= 0 )
        return;

    if( !frame->ssm)
    {
        chroma_supersample(
            settings->sample_mode,
            frame,
            frame->data );
        vj_perform_set_444(frame);
    }

    char *osd_text = NULL;
    char *audio_top_text = NULL;
    int placement = 0;
    if( info->use_osd == 2 ) {
        placement = 1;
        osd_text = vj_perform_print_credits(info);
    } else if (info->use_osd == 1 ) {
        osd_text = vj_perform_osd_status(info);
#ifdef HAVE_JACK
        audio_top_text = vj_perform_audio_clock_osd_status(info);
#endif
    } else if (info->use_osd == 3 && info->composite ) {
        placement = 1;
        osd_text = viewport_get_my_help( composite_get_vp(info->composite ) );
    }
    if(audio_top_text) {
        vj_font_render_osd_status(info->osd,frame,audio_top_text,1);
        free(audio_top_text);
    }
    if(osd_text) {
        vj_font_render_osd_status(info->osd,frame,osd_text,placement);
        free(osd_text);
    }
}

static inline void vj_fx_hold_update(veejay_t *info, VJFrame *current_fx)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;

    video_playback_setup *s = info->settings;

    uint8_t slot = 5;

    if (!s->hold_fx) {
        s->hold_fx_prev = 0;
        return;
    }

    int plane_sizes[4] = {
        current_fx->len, current_fx->uv_len, current_fx->uv_len, current_fx->stride[3] * current_fx->height
    };

    if (!s->hold_fx_prev && s->hold_fx) {

        uint8_t *pri5[4] = {
            p->primary_buffer[slot]->Y,
            p->primary_buffer[slot]->Cb,
            p->primary_buffer[slot]->Cr,
            p->primary_buffer[slot]->alpha
        };

        vj_frame_copy(current_fx->data, pri5, plane_sizes);
        if(current_fx->ssm) {
            chroma_subsample(info->settings->sample_mode, current_fx, pri5);
        }
    }


    s->hold_fx_prev = 1;
}

static  void    vj_perform_finish_chain( veejay_t *info,performer_t *p,VJFrame *frame, int sample_id, int source_type )
{
    int result = 0;

    if(source_type == VJ_PLAYBACK_MODE_TAG )
    {
        result = vj_perform_post_chain_tag(info,p, frame, sample_id);
    }
    else if(source_type == VJ_PLAYBACK_MODE_SAMPLE )
    {
        result = vj_perform_post_chain_sample(info,p, frame, sample_id);
    }

    if( result ) {
        p->pvar_.follow_run = result;
    }
}

static  void    vj_perform_finish_render( veejay_t *info,performer_t *p,VJFrame *frame, video_playback_setup *settings )
{
    uint8_t *pri[4];
    char *osd_text = NULL;
    char *more_text = NULL;
    char *audio_top_text = NULL;
    int   placement= 0;

    pri[0] = p->primary_buffer[0]->Y;
    pri[1] = p->primary_buffer[0]->Cb;
    pri[2] = p->primary_buffer[0]->Cr;
    pri[3] = p->primary_buffer[0]->alpha;

    if( settings->composite  )
    {
        if( settings->ca ) {
            settings->ca = 0;
        }

        if(composite_event( info->composite, pri, info->uc->mouse[0],info->uc->mouse[1],info->uc->mouse[2],
            vj_perform_get_width(info), vj_perform_get_height(info),info->homedir,info->uc->playback_mode,info->uc->sample_id ) ) {
#ifdef HAVE_SDL
            if( info->video_out == 0 ) {

                vj_sdl_grab( info->sdl, 0 );
            }
#endif
        }

#ifdef HAVE_SDL
        if( info->use_osd == 2 ) {
            osd_text = vj_perform_print_credits(info);
            placement= 1;
        } else if (info->use_osd == 1 ) {
            osd_text = vj_perform_osd_status(info);
            placement= 0;
#ifdef HAVE_JACK
            audio_top_text = vj_perform_audio_clock_osd_status(info);
#endif
        } else if (info->use_osd == 3 && info->composite ) {
            placement = 1;
            osd_text = viewport_get_my_help( composite_get_vp(info->composite ) );
            more_text = vj_perform_osd_status(info);
        }
#endif
    }

    if( settings->composite  ) {
        VJFrame out;
        veejay_memcpy( &out, info->effect_frame1, sizeof(VJFrame));
        int curfmt = out.format;
        if( out.ssm )
        {
            out.format = (info->pixel_format == FMT_422F ? PIX_FMT_YUVJ444P : PIX_FMT_YUV444P );
        }

        if(!frame->ssm) {
              chroma_supersample(settings->sample_mode,frame,pri );
              vj_perform_set_444(frame);
        }

        composite_process(info->composite,&out,info->effect_frame1,settings->composite,frame->format);

        if( settings->splitscreen ) {
            composite_process_divert(info->composite,out.data,frame, info->splitter, settings->composite );
        }

        if(osd_text || more_text || audio_top_text) {
            VJFrame *tst = composite_get_draw_buffer( info->composite );
            if(tst) {
                if(audio_top_text)
                    vj_font_render_osd_status(info->osd,tst,audio_top_text,1);
                if(osd_text)
                    vj_font_render_osd_status(info->osd,tst,osd_text,placement);
                if(more_text)
                    vj_font_render_osd_status(info->osd,tst,more_text,0);
                free(tst);
            }
        }

        if( frame->ssm ) {
            frame->uv_len = frame->uv_width * frame->uv_height;
            frame->format = curfmt;
        }
    }
    else {

        if(settings->splitscreen ) {
            if(!frame->ssm) {
              chroma_supersample(settings->sample_mode,frame,pri );
              vj_perform_set_444(frame);
            }
            vj_split_process( info->splitter, frame );
        }

        if(audio_top_text)
            vj_font_render_osd_status(info->osd,frame,audio_top_text,1);
        if( osd_text )
            vj_font_render_osd_status(info->osd,frame,osd_text,placement);
        if(more_text)
            vj_font_render_osd_status(info->osd,frame,more_text,0);
    }

    if( osd_text)
        free(osd_text);
    if( more_text)
        free(more_text);
    if( audio_top_text)
        free(audio_top_text);

    if(!settings->composite && info->uc->mouse[0] > 0 && info->uc->mouse[1] > 0)
    {
        if( info->uc->mouse[2] == 1 ) {
            uint8_t y,u,v,r,g,b;

            y = pri[0][ info->uc->mouse[1] * frame->width + info->uc->mouse[0] ];
            if( frame->ssm == 1 ) {
                u = pri[1][ info->uc->mouse[1] * frame->width + info->uc->mouse[0] ];
                v = pri[2][ info->uc->mouse[1] * frame->width + info->uc->mouse[0] ];
            }
            else {
                u = pri[1][ info->uc->mouse[1] * frame->uv_width + (info->uc->mouse[0]>>1) ];
                v = pri[2][ info->uc->mouse[1] * frame->uv_width + (info->uc->mouse[0]>>1) ];
            }

            r = y + (1.370705f * ( v- 128 ));
            g = y - (0.698001f * ( v - 128)) - (0.337633 * (u-128));
            b = y + (1.732446f * ( u - 128 ));

            if(info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) {
                int pos = sample_get_selected_entry(info->uc->sample_id);
                int fx_id = sample_get_effect( info->uc->sample_id,pos);
                if( vje_has_rgbkey( fx_id ) ) {
                    sample_set_effect_arg( info->uc->sample_id, pos, 1, r );
                    sample_set_effect_arg( info->uc->sample_id, pos, 2, g );
                    sample_set_effect_arg( info->uc->sample_id, pos, 3, b );
                    veejay_msg(VEEJAY_MSG_INFO,"Selected RGB color #%02x%02x%02x",r,g,b);
                }
            }
            else if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG ) {
                int pos = vj_tag_get_selected_entry(info->uc->sample_id);
                int fx_id = vj_tag_get_effect( info->uc->sample_id, pos );
                if( vje_has_rgbkey( fx_id ) ) {
                    vj_tag_set_effect_arg( info->uc->sample_id,pos,1,r);
                    vj_tag_set_effect_arg( info->uc->sample_id,pos,2,g);
                    vj_tag_set_effect_arg( info->uc->sample_id,pos,3,b);
                    veejay_msg(VEEJAY_MSG_INFO,"Selected RGB color #%02x%02x%02x",r,g,b);
                }
            }
        }

        if( info->uc->mouse[2] == 2 ) {
            info->uc->drawmode = !info->uc->drawmode;
        }

        if( info->uc->mouse[2] == 0 && info->uc->drawmode ) {
            int x1 = info->uc->mouse[0] - info->uc->drawsize;
            int y1 = info->uc->mouse[1] - info->uc->drawsize;
            int x2 = info->uc->mouse[0] + info->uc->drawsize;
            int y2 = info->uc->mouse[1] + info->uc->drawsize;

            if( x1 < 0 ) x1 = 0; else if ( x1 > frame->width ) x1 = frame->width;
            if( y1 < 0 ) y1 = 0; else if ( y1 > frame->height ) y1 = frame->height;
            if( x2 < 0 ) x2 = 0; else if ( x2 > frame->width ) x2 = frame->width;
            if( y2 < 0 ) y2 = 0; else if ( y2 > frame->height ) y2 = frame->height;

            unsigned int i,j;
            for( j = x1; j < x2 ; j ++ )
                pri[0][ y1 * frame->width + j ] = 0xff - pri[0][y1 * frame->width + j];

            for( i = y1; i < y2; i ++ )
            {
                pri[0][ i * frame->width + x1 ] = 0xff - pri[0][i * frame->width + x1];
                pri[0][ i * frame->width + x2 ] = 0xff - pri[0][i * frame->width + x2];
            }

            for( j = x1; j < x2 ; j ++ )
                pri[0][ y2 * frame->width + j ] = 0xff - pri[0][y2*frame->width+j];
        }
    }
}

static  void    vj_perform_render_font( veejay_t *info, video_playback_setup *settings, VJFrame *frame )
{
#ifdef HAVE_FREETYPE
    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);
    int n = vj_font_norender( info->font, cur_frame );
    if( n > 0 )
    {
        if( !frame->ssm )
        {
            chroma_supersample(
                settings->sample_mode,
                frame,
                frame->data );
            vj_perform_set_444(frame);
        }
        vj_font_render( info->font, frame , cur_frame );
    }
#endif
}

static  void    vj_perform_record_frame( veejay_t *info )
{
    if( info->settings->offline_record )
    {
        vj_perform_record_offline_tag_frame(info);
    }

    if( info->seq->active && info->seq->rec_id > 0 ) {
        vj_perform_record_sample_frame(info,info->seq->rec_id, 0);
    }
    else {

        if(info->uc->playback_mode == VJ_PLAYBACK_MODE_TAG && info->settings->tag_record )
            vj_perform_record_tag_frame(info);
        else if (info->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE && info->settings->sample_record )
            vj_perform_record_sample_frame(info, info->uc->sample_id,0 );
    }
}

void    vj_perform_record_video_frame(veejay_t *info)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *p = g->A;
    if( p->pvar_.enc_active )
        vj_perform_record_frame(info);
}

void vj_perform_reset_transition(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;
    performer_t *A = g->A;
    performer_t *B = g->B;

    const int seq_transition =
        info->seq &&
        info->seq->active &&
        settings->transition.seq_index >= 0 &&
        settings->transition.seq_index < MAX_SEQUENCES;

    settings->transition.shape = -1;
    settings->transition.skip_audio_edge = 1;

    atomic_store_int(&settings->transition.active, 0);
    atomic_store_long_long(&settings->transition.start, 0);
    atomic_store_long_long(&settings->transition.end, 0);

    if (info->audio != NO_AUDIO) {
        atomic_store_int(&A->audio_edge->pending_edge, AUDIO_EDGE_NONE);
        atomic_store_int(&B->audio_edge->pending_edge, AUDIO_EDGE_NONE);
    }

    if (settings->transition.next_type == VJ_PLAYBACK_MODE_SAMPLE &&
        settings->transition.next_id > 0 &&
        !seq_transition)
    {
        sample_b_t *sb = &A->sample_b;
        long long sample_b_pos = atomic_load_long_long(&sb->offset);
        long long start_pos = atomic_load_long_long(&sb->start);

        sample_set_resume_override(
            settings->transition.next_id,
            sample_b_pos + start_pos
        );
    }

    settings->transition.next_id = 0;
    settings->transition.next_type = 0;
    settings->transition.seq_index = -1;

    veejay_memset(&A->sample_b, 0, sizeof(sample_b_t));
    veejay_memset(&B->sample_b, 0, sizeof(sample_b_t));
}

static void vj_perform_end_transition(veejay_t *info, int mode, int sample)
{
    video_playback_setup *settings = info->settings;

    if (!settings->transition.ready)
        return;

    int target_mode = vj_seq_type_to_playback_mode(mode);
    int target_id = sample;
    int target_slot = settings->transition.seq_index;

    if (info->seq->active &&
        target_slot >= 0 &&
        target_slot < MAX_SEQUENCES &&
        info->seq->samples[target_slot].sample_id > 0)
    {
        target_mode = vj_seq_type_to_playback_mode(
            info->seq->samples[target_slot].type
        );

        target_id = info->seq->samples[target_slot].sample_id;
        info->seq->current = target_slot;
    }

    vj_perform_reset_transition(info);

    veejay_change_playback_mode(info, target_mode, target_id);

    settings->transition.ready = 0;
}

int vj_perform_transition_sample(veejay_t *info, VJFrame *srcA, VJFrame *srcB)
{
    video_playback_setup *settings = info->settings;

    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);

    long long start = atomic_load_long_long(&settings->transition.start);
    long long end = atomic_load_long_long(&settings->transition.end);

    if (settings->current_playback_speed > 0) {
        settings->transition.timecode =
            (cur_frame - start) /
            (double)(end - start);
    }
    else if (settings->current_playback_speed < 0) {
        settings->transition.timecode =
            (start - cur_frame) /
            (double)(start - end);
    }

    if (settings->transition.timecode < 0.0 ||
        settings->transition.timecode > 1.0)
    {
        veejay_msg(0,
                   "invalid transition timecode: frame %lld, transition %lld - %lld",
                   cur_frame,
                   start,
                   end);
        return 0;
    }

    if (!srcA->ssm) {
        chroma_supersample(settings->sample_mode, srcA, srcA->data);
        vj_perform_set_444(srcA);
    }

    if (!srcB->ssm) {
        chroma_supersample(settings->sample_mode, srcB, srcB->data);
        vj_perform_set_444(srcB);
    }

    settings->transition.ready = shapewipe_process(
        settings->transition.ptr,
        srcA,
        srcB,
        settings->transition.timecode,
        settings->transition.shape,
        0,
        1,
        1
    );

    vj_perform_end_transition(info,
                              settings->transition.next_type,
                              settings->transition.next_id);

    return 1;
}

static void vj_perform_queue_fx_entry( veejay_t *info, int sample_id, int entry_id, sample_eff_chain *entry, performer_t *p, VJFrame *a, VJFrame *b,const int is_sample, int *alpha_clear )
{
    if( entry->clear && b->stride[3] > 0 ) {
        veejay_memset( b->data[3], 0, b->stride[3] * b->height );
        entry->clear = 0;
        *alpha_clear = 1;
    }

    if(is_sample) {
        vj_perform_apply_secundary( info, p, sample_id, entry->channel, entry->source_type, entry_id, a, b,
                p->frame_buffer[ entry_id ]->P0,
                p->frame_buffer[ entry_id ]->P1,
                1);
    }
    else {
        vj_perform_apply_secundary_tag( info, p, entry->channel, entry->source_type, entry_id, a, b,
                p->frame_buffer[ entry_id ]->P0,
                p->frame_buffer[ entry_id ]->P1,
                0);
    }

}

int vj_perform_queue_video_frames(veejay_t *info, VJFrame *frame, VJFrame *frame2, performer_t *p, const int sample_id, const int mode, long frame_num)
{
    sample_eff_chain **fx_chain = NULL;
    veejay_memcpy( p->tmp1, frame, sizeof(VJFrame));

    p->tmp1->data[0] = p->primary_buffer[0]->Y;
    p->tmp1->data[1] = p->primary_buffer[0]->Cb;
    p->tmp1->data[2] = p->primary_buffer[0]->Cr;
    p->tmp1->data[3] = p->primary_buffer[0]->alpha;

    int is_sample = (mode == VJ_PLAYBACK_MODE_SAMPLE);

    if(mode != VJ_PLAYBACK_MODE_TAG ) {
        vj_perform_plain_fill_buffer(info,p, p->tmp1, sample_id,mode, frame_num);
        if( is_sample )
            fx_chain = sample_get_effect_chain( sample_id );
    }
    else {
        vj_perform_tag_fill_buffer(info, p, p->tmp1, sample_id);
        fx_chain = vj_tag_get_effect_chain( sample_id );
    }

    int alpha_clear = 0;

    if( fx_chain != NULL )
    {
        veejay_memcpy( p->tmp2, frame2, sizeof(VJFrame));

        if( info->uc->take_bg && p->chain_id == 0 ) {
            vjert_update( fx_chain, p->tmp1 );
        }

        for( int c = 0; c < SAMPLE_MAX_EFFECTS; c ++ ) {
            sample_eff_chain *entry = fx_chain[c];
            if( entry->e_flag == 0 || entry->effect_id <= 0 )
                continue;

            if( vje_get_extra_frame( entry->effect_id ) ) {
                p->tmp2->data[0] = p->frame_buffer[c]->Y;
                p->tmp2->data[1] = p->frame_buffer[c]->Cb;
                p->tmp2->data[2] = p->frame_buffer[c]->Cr;
                p->tmp2->data[3] = p->frame_buffer[c]->alpha;
                vj_perform_queue_fx_entry( info, sample_id, c, entry, p, p->tmp1, p->tmp2, is_sample, &alpha_clear );
            }
        }
    }

    if( info->uc->take_bg == 1 ) {
        info->uc->take_bg = 0;
    }

    if(alpha_clear) {
        veejay_memset( p->tmp1->data[3], 0, p->tmp1->stride[3] * p->tmp1->height );
    }

    return 1;
}

void vj_perform_render_video_frames(veejay_t *info, performer_t *p, vjp_kf *effect_info, const int sample_id, const int source_type, VJFrame *a, VJFrame *b, VJFrameInfo *topinfo, vjp_kf *setup)
{
    video_playback_setup *settings = info->settings;
    performer_global_t *g = (performer_global_t*) info->performer;

    int is444 = 0;
    int i = 0;
    int safe_ff = p->pvar_.follow_fade;
    int safe_fv = p->pvar_.fade_value;

    veejay_memset( &(p->pvar_), 0, sizeof(varcache_t));

    p->pvar_.follow_fade = safe_ff;
    p->pvar_.fade_value = safe_fv;
    p->pvar_.fade_entry = -1;

    int cur_out = info->out_buf;

    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);
    long long max_frame = atomic_load_long_long(&settings->max_frame_num);
    long long min_frame = atomic_load_long_long(&settings->min_frame_num);

    topinfo->timecode = cur_frame;
    a->ssm = 0;
    b->ssm = 0;
    a->timecode = cur_frame / (double)(max_frame - min_frame);

    for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ ) {
        p->frame_buffer[i]->ssm = 0;
    }

    a->data[0] = p->primary_buffer[0]->Y;
    a->data[1] = p->primary_buffer[0]->Cb;
    a->data[2] = p->primary_buffer[0]->Cr;
    a->data[3] = p->primary_buffer[0]->alpha;

    vj_perform_cache_put_frame( g, sample_id, source_type, p->primary_buffer[0]);

    switch (source_type)
    {
        case VJ_PLAYBACK_MODE_SAMPLE:

            sample_var( sample_id,
                        &(p->pvar_.type),
                        &(p->pvar_.fader_active),
                        &(p->pvar_.fx_status),
                        &(p->pvar_.enc_active),
                        &(p->pvar_.active),
                        &(p->pvar_.fade_method),
                        &(p->pvar_.fade_entry),
                        &(p->pvar_.fade_alpha));

            if( (info->seq->active && info->seq->rec_id) || info->settings->offline_record )
                p->pvar_.enc_active = 1;

            sample_eff_chain **beat_local_chain = sample_get_effect_chain(sample_id);

            if(info->global_chain && info->global_chain->enabled)
                vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);

#ifdef HAVE_JACK
            vj_perform_audio_beat_apply_render_chains(
                info,
                sample_id,
                source_type,
                p->chain_id,
                beat_local_chain,
                p->pvar_.fx_status);
#endif

            sample_info *si = sample_get(sample_id);
            if(si) {
                if( info->global_chain->enabled == 1) {
                    if(p->pvar_.fx_status) vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type,setup, si->effect_chain, si);
                    vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);
                    vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, si);
                } else if(info->global_chain->enabled == 2) {
                    vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);
                    vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, si);
                    if(p->pvar_.fx_status) vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type,setup, si->effect_chain, si);
                } else {
                    if(p->pvar_.fx_status) vj_perform_sample_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type,setup, si->effect_chain, si);
                }
            }

            vj_perform_finish_chain( info,p,a,sample_id,source_type );

            vj_fx_hold_update(info, a);

            break;

        case VJ_PLAYBACK_MODE_PLAIN:

            if( info->settings->offline_record )
                p->pvar_.enc_active = 1;


            break;
        case VJ_PLAYBACK_MODE_TAG:

            vj_tag_var( sample_id,
                        &(p->pvar_.type),
                        &(p->pvar_.fader_active),
                        &(p->pvar_.fx_status),
                        &(p->pvar_.enc_active),
                        &(p->pvar_.active),
                        &(p->pvar_.fade_method),
                        &(p->pvar_.fade_entry),
                        &(p->pvar_.fade_alpha));

            if( (info->seq->active && info->seq->rec_id) || info->settings->offline_record )
                p->pvar_.enc_active = 1;

            sample_eff_chain **beat_tag_chain = vj_tag_get_effect_chain(sample_id);

            if(info->global_chain && info->global_chain->enabled)
                vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);

#ifdef HAVE_JACK
            vj_perform_audio_beat_apply_render_chains(
                info,
                sample_id,
                source_type,
                p->chain_id,
                beat_tag_chain,
                p->pvar_.fx_status);
#endif

            vj_tag *tag = vj_tag_get( sample_id );
            if(tag) {
                if( info->global_chain->enabled == 1) {
                    if(p->pvar_.fx_status ) vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,tag->effect_chain, tag);
                    vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);
                    vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, tag);

                } else if(info->global_chain->enabled == 2) {
                    vj_perform_global_chain_sync(info, info->global_chain, sample_id, source_type);
                    vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,info->global_chain->fx_chain, tag);
                    if(p->pvar_.fx_status ) vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,tag->effect_chain, tag);
                } else {
                    if(p->pvar_.fx_status ) vj_perform_tag_complete_buffers(info,p, effect_info, &is444, a, b, sample_id, source_type, setup,tag->effect_chain, tag);
                }
            }

            vj_perform_finish_chain( info,p,a,sample_id,source_type );

            vj_fx_hold_update(info, a);

            break;
        default:
            break;
    }

    info->out_buf = cur_out;

    if( info->settings->feedback && info->settings->feedback_stage == 1 )
    {
        int idx = info->out_buf;
        uint8_t *dst[4] = {
            p->primary_buffer[idx]->Y,
            p->primary_buffer[idx]->Cb,
            p->primary_buffer[idx]->Cr,
            p->primary_buffer[idx]->alpha };

        vj_perform_copy3( dst,g->feedback_buffer, a->len, a->uv_len, a->stride[3] * a->height  );

        info->settings->feedback_stage = 2;
    }
}
#define FP_S    14
#define FP_M    (1 << FP_S)
#define FP_HALF (1 << (FP_S - 1))

static void vj_perform_color_vibrancy(uint8_t *U, uint8_t *V, const int len, int vib)
{
    const float gain = 0.50f + ((float)vib / 255.0f) * 1.30f;
    const int32_t gain_fp = (int32_t)(gain * FP_M);


    const int32_t diff = (gain_fp > FP_M) ? (gain_fp - FP_M) : (FP_M - gain_fp);
    if (diff < 164) return;

    uint8_t *restrict pu = U;
    uint8_t *restrict pv = V;

    for (int i = 0; i < len; i++)
    {
        int u = pu[i] - 128;
        int v = pv[i] - 128;

        u = (u * gain_fp + FP_HALF) >> FP_S;
        v = (v * gain_fp + FP_HALF) >> FP_S;

        pu[i] = (uint8_t)(u + 128);
        pv[i] = (uint8_t)(v + 128);
    }
}

int vj_perform_queue_video_frame(veejay_t *info, VJFrame *dst)
{
    performer_global_t *g = (performer_global_t*) info->performer;
    video_playback_setup *settings = info->settings;
    performer_t *p = g->A;

#ifdef HAVE_JACK
    {
        static int ab_queue_seq = 0;
        int ab_mode = info->uc ? info->uc->playback_mode : -1;

        if(vj_perform_audio_beat_playmode_has_fx_chain(ab_mode))
        {
            ab_queue_seq++;


        }
    }
#endif

    if (info->settings->hold_fx && info->settings->hold_fx_prev)
    {
        performer_global_t *g = (performer_global_t*) info->performer;
        performer_t *p = g->A;

        uint8_t slot = 5;

        uint8_t *fx_hold_data[4] = {
            p->primary_buffer[slot]->Y,
            p->primary_buffer[slot]->Cb,
            p->primary_buffer[slot]->Cr,
            p->primary_buffer[slot]->alpha
        };

        int plane_sizes[4] = {
            dst->len,
            dst->uv_len,
            dst->uv_len,
            0,
        };

        vj_frame_copy( fx_hold_data, dst->data, plane_sizes);

        return 1;
    }

    vj_perform_sample_tick_reset(g);

    long long cur_frame = atomic_load_long_long(&info->settings->current_frame_num);

    vj_perform_queue_video_frames( info, info->effect_frame1, info->effect_frame2, g->A, info->uc->sample_id, info->uc->playback_mode, cur_frame);

    int transition_enabled = atomic_load_int(&settings->transition.active) && atomic_load_int(&settings->transition.global_state);
    if (transition_enabled) {
        long long start = atomic_load_long_long(&settings->transition.start);
        long long end = atomic_load_long_long(&settings->transition.end);

        if (cur_frame < start || cur_frame > end)
            transition_enabled = 0;
    }

    if(transition_enabled) {
        sample_b_t *sb = &p->sample_b;
        long long sample_position = atomic_load_long_long(&sb->offset);
        long long start = atomic_load_long_long(&sb->start);

        sample_position += start;

        vj_perform_queue_video_frames( info,info->effect_frame3, info->effect_frame4, g->B, info->settings->transition.next_id, info->settings->transition.next_type, sample_position );
        vje_disable_parallel();
    }

    if(!transition_enabled) {
        vj_perform_render_video_frames(info, g->A, info->effect_info, info->uc->sample_id, info->uc->playback_mode, info->effect_frame1, info->effect_frame2, info->effect_frame_info, info->effect_info );
        vj_perform_try_sequence(info);
    }
    else
    {
#pragma omp parallel num_threads(2)
{
#pragma omp single
{
#pragma omp task
{
        vj_perform_render_video_frames(info, g->A, info->effect_info, info->uc->sample_id, info->uc->playback_mode, info->effect_frame1, info->effect_frame2, info->effect_frame_info, info->effect_info );
}
#pragma omp task
{
        vj_perform_render_video_frames(info, g->B, info->effect_info2, info->settings->transition.next_id, info->settings->transition.next_type,
                info->effect_frame3, info->effect_frame4, info->effect_frame_info2, info->effect_info2 );
}
#pragma omp taskwait
        vj_perform_transition_sample( info, info->effect_frame1, info->effect_frame3 );
}
}
    }

    vj_perform_render_font( info, settings, info->effect_frame1);

    if(!settings->composite)
        vj_perform_render_osd( info, settings, info->effect_frame1 );

    vj_perform_finish_render( info, g->A, info->effect_frame1, settings );

    if( info->effect_frame1->ssm == 1 )
    {
        chroma_subsample(settings->sample_mode,info->effect_frame1,info->effect_frame1->data);
        vj_perform_set_422(info->effect_frame1);
        vj_perform_set_422(info->effect_frame2);
        vj_perform_set_422(info->effect_frame3);
        vj_perform_set_422(info->effect_frame4);

        g->A->primary_buffer[0]->ssm = 0;
    }

    vje_enable_parallel();

    vj_perform_record_video_frame(info);

    int col_vib = atomic_load_int(&settings->color_vibrance);
    vj_perform_color_vibrancy(info->effect_frame1->data[1], info->effect_frame1->data[2],info->effect_frame1->uv_len, col_vib);


    int strides[4] = {info->effect_frame1->len, info->effect_frame1->uv_len,info->effect_frame1->uv_len,0};
    uint8_t *input[4] = { p->primary_buffer[0]->Y, p->primary_buffer[0]->Cb, p->primary_buffer[0]->Cr, NULL };
    vj_frame_copy( input, dst->data, strides );

    return 1;
}

static int vj_perform_random_loop_ticks(veejay_t *info, int sample_id)
{
    int ticks = sample_get_frame_length(sample_id);

    if (ticks < 1) {
        int start = 0;
        int end = 0;
        int loop = 0;
        int speed = 0;

        if (sample_get_short_info(sample_id, &start, &end, &loop, &speed) == 0) {
            int span = 1 + abs(end - start);
            int abs_speed = abs(speed);

            if (abs_speed < 1)
                abs_speed = 1;

            ticks = span / abs_speed;

            if (ticks < 1)
                ticks = 1;

            int sfd = sample_get_framedup(sample_id);
            if (sfd > 1)
                ticks *= sfd;

            if (loop == 2)
                ticks *= 2;
        }
    }

    return ticks < 1 ? 1 : ticks;
}


void vj_perform_inc_frame(veejay_t *info, int num)
{
    video_playback_setup *settings = info->settings;
    const int mode = info->uc->playback_mode;
    const int seq_active = info->seq && info->seq->active;
    const int seq_transition_active =
        seq_active &&
        atomic_load_int(&settings->transition.active) &&
        atomic_load_int(&settings->transition.global_state);
    int looptype = 1;
    int speed = settings->current_playback_speed;

    if (seq_active && !seq_transition_active && atomic_load_int(&settings->sequence_boundary))
        return;

    long long end = atomic_load_long_long(&settings->max_frame_num);
    long long start = atomic_load_long_long(&settings->min_frame_num);

    const int prev_dir = (settings->current_playback_speed < 0 ? -1 :
                         settings->current_playback_speed > 0 ? 1 : 0);
    int cur_dir = prev_dir;

    if (mode == VJ_PLAYBACK_MODE_SAMPLE) {
        int s_start = 0;
        int s_end = 0;
        int s_loop = 0;
        int s_speed = 0;

        if (sample_get_short_info(info->uc->sample_id,
                                  &s_start,
                                  &s_end,
                                  &s_loop,
                                  &s_speed) != 0)
            return;

        start = s_start;
        end = s_end;
        looptype = s_loop;
        speed = s_speed;
        settings->current_playback_speed = speed;
        cur_dir = (speed < 0) ? -1 : speed > 0 ? 1 : 0;
    }
    else {
        looptype = 1;
        speed = settings->current_playback_speed;
        cur_dir = (speed < 0) ? -1 : speed > 0 ? 1 : 0;
    }

    if (speed == 0) {
        settings->current_playback_speed = 0;
        settings->sequence_random_id = 0;
        settings->sequence_random_ticks_left = 0;
        return;
    }

    long long cur_slice = atomic_load_int(&settings->audio_slice);
    long long max_sfd = atomic_load_int(&settings->audio_slice_len);
    long long cur_frame = atomic_load_long_long(&settings->current_frame_num);
    long long next_frame = cur_frame + num;

    int edge_type = AUDIO_EDGE_NONE;
    int next_dir = cur_dir;
    int sequence_boundary = 0;

    if (mode == VJ_PLAYBACK_MODE_SAMPLE && looptype == 3) {
        const int sample_id = info->uc->sample_id;

        if (settings->sequence_random_id != sample_id ||
            settings->sequence_random_ticks_left <= 0)
        {
            settings->sequence_random_id = sample_id;
            settings->sequence_random_ticks_left =
                vj_perform_random_loop_ticks(info, sample_id);
        }

        settings->sequence_random_ticks_left--;

        if (settings->sequence_random_ticks_left <= 0) {
            sequence_boundary = 1;
            settings->sequence_random_ticks_left =
                vj_perform_random_loop_ticks(info, sample_id);
        }
    }
    else {
        settings->sequence_random_id = 0;
        settings->sequence_random_ticks_left = 0;
    }

    if (max_sfd > 1 && cur_slice < (max_sfd - 1)) {
        atomic_store_int(&settings->audio_slice, cur_slice + 1);

        if (sequence_boundary && mode != VJ_PLAYBACK_MODE_PLAIN) {
            const int id = info->uc->sample_id;
            int loops_before = (mode == VJ_PLAYBACK_MODE_SAMPLE) ?
                sample_get_loops(id) : vj_tag_get_loops(id);
            int playback_ended = (mode == VJ_PLAYBACK_MODE_SAMPLE) ?
                sample_loop_dec(id) : vj_tag_loop_dec(id);
            int loops_after = (mode == VJ_PLAYBACK_MODE_SAMPLE) ?
                sample_get_loops(id) : vj_tag_get_loops(id);

            if (seq_active && !seq_transition_active)
                atomic_store_int(&settings->sequence_boundary, 1);

            if (seq_active || loops_before > 0) {
                const long long log_sfd = max_sfd > 0 ? max_sfd : 1;
                const long long log_slice = max_sfd > 1 ? cur_slice + 1 : 1;

                veejay_msg(VEEJAY_MSG_DEBUG,
                           "[SEQ] boundary mode=%s id=%d cur=%lld next=%lld range=%lld..%lld speed=%d looptype=%d edge=%d loops=%d->%d ended=%d sfd=%lld/%lld seq=%d slot=%d",
                           vj_playback_mode_label(mode),
                           id,
                           cur_frame,
                           next_frame,
                           start,
                           end,
                           speed,
                           looptype,
                           edge_type,
                           loops_before,
                           loops_after,
                           playback_ended,
                           log_slice,
                           log_sfd,
                           seq_active,
                           info->seq ? info->seq->current : -1);
            }
        }

        return;
    }

    if (mode == VJ_PLAYBACK_MODE_SAMPLE && looptype == 3) {
        long long random_frame = vj_frame_rand(
            cur_frame,
            start,
            end,
            settings->master_frame_num
        );

        if (random_frame != cur_frame) {
            next_frame = random_frame;
            edge_type = AUDIO_EDGE_JUMP;
        }
        else {
            next_frame = cur_frame;
        }
    }
    else {
        if (next_frame > end) {
            switch (looptype) {
                case 2:
                    next_dir = -cur_dir;
                    veejay_set_speed(info, next_dir * abs(speed), 1);
                    next_frame = end;
                    edge_type = AUDIO_EDGE_DIRECTION;
                    sequence_boundary = 1;
                    break;

                case 1:
                    next_frame = start;
                    next_dir = 1;
                    edge_type = AUDIO_EDGE_RESET;
                    sequence_boundary = 1;
                    break;

                case 3:
                    next_frame = vj_frame_rand(
                        cur_frame,
                        start,
                        end,
                        settings->master_frame_num
                    );
                    edge_type = AUDIO_EDGE_JUMP;
                    break;

                default:
                    next_frame = end;
                    veejay_set_speed(info, 0, 1);
                    next_dir = 0;
                    edge_type = AUDIO_EDGE_SILENCE;
                    sequence_boundary = 1;
                    break;
            }
        }
        else if (next_frame < start) {
            switch (looptype) {
                case 2:
                    next_dir = -cur_dir;
                    veejay_set_speed(info, next_dir * abs(speed), 1);
                    next_frame = start;
                    edge_type = AUDIO_EDGE_DIRECTION;
                    sequence_boundary = 1;
                    break;

                case 1:
                    next_frame = end;
                    next_dir = -1;
                    edge_type = AUDIO_EDGE_JUMP;
                    sequence_boundary = 1;
                    break;

                case 3:
                    next_frame = vj_frame_rand(
                        cur_frame,
                        start,
                        end,
                        settings->master_frame_num
                    );
                    edge_type = AUDIO_EDGE_JUMP;
                    break;

                default:
                    next_frame = start;
                    veejay_set_speed(info, 0, 1);
                    next_dir = 0;
                    edge_type = AUDIO_EDGE_SILENCE;
                    sequence_boundary = 1;
                    break;
            }
        }

        if (edge_type == AUDIO_EDGE_NONE && speed != 0) {
            int expected_span = abs(speed);
            expected_span = (expected_span < 1) ? 1 : expected_span;

            long long moved = next_frame - cur_frame;
            moved = (moved < 0) ? -moved : moved;

            if (moved > expected_span)
                edge_type = AUDIO_EDGE_JUMP;
        }
    }

    if (next_dir != prev_dir)
        atomic_store_int(&settings->audio_direction_changed, 1);

    if (edge_type != AUDIO_EDGE_NONE)
        vj_perform_initiate_edge_change_ex(info, edge_type, prev_dir, next_dir);

    if (mode != VJ_PLAYBACK_MODE_PLAIN && sequence_boundary) {
        const int id = info->uc->sample_id;
        int loops_before = (mode == VJ_PLAYBACK_MODE_SAMPLE) ?
            sample_get_loops(id) : vj_tag_get_loops(id);
        int playback_ended = (mode == VJ_PLAYBACK_MODE_SAMPLE) ?
            sample_loop_dec(id) : vj_tag_loop_dec(id);
        int loops_after = (mode == VJ_PLAYBACK_MODE_SAMPLE) ?
            sample_get_loops(id) : vj_tag_get_loops(id);

        if (seq_active && !seq_transition_active)
            atomic_store_int(&settings->sequence_boundary, 1);

        if (seq_active || loops_before > 0) {
            const long long log_sfd = max_sfd > 0 ? max_sfd : 1;
            const long long log_slice = max_sfd > 1 ? cur_slice : 1;

            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[SEQ] boundary mode=%s id=%d cur=%lld next=%lld range=%lld..%lld speed=%d looptype=%d edge=%d loops=%d->%d ended=%d sfd=%lld/%lld seq=%d slot=%d",
                       vj_playback_mode_label(mode),
                       id,
                       cur_frame,
                       next_frame,
                       start,
                       end,
                       speed,
                       looptype,
                       edge_type,
                       loops_before,
                       loops_after,
                       playback_ended,
                       log_slice,
                       log_sfd,
                       seq_active,
                       info->seq ? info->seq->current : -1);
        }
    }

    atomic_store_long_long(&settings->current_frame_num, next_frame);

    if (max_sfd > 1)
        atomic_store_int(&settings->audio_slice, 0);

    if (mode == VJ_PLAYBACK_MODE_SAMPLE)
        vj_perform_rand_update(info);
}

void    vj_perform_randomize(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    if(settings->randplayer.mode == RANDMODE_INACTIVE)
        return;

    double n_sample = (double) (sample_highest_valid_id());
    int track_dup  = 0;

    if( settings->randplayer.mode == RANDMODE_SAMPLE && n_sample > 1.0 )
        track_dup = info->uc->sample_id;

    if( settings->randplayer.seed == 0 )
        settings->randplayer.seed = (unsigned long long) time(NULL);

    int take_n   = vj_frame_rand(track_dup, 1, n_sample, settings->randplayer.seed ++ );
    int min_delay = 1;
    int max_delay = 0;

    if(take_n == 1 && !sample_exists(take_n)) {
        veejay_msg(0,"No samples to randomize");
        settings->randplayer.mode = RANDMODE_INACTIVE;
        return;
    }

    while(!sample_exists(take_n))
    {
        veejay_msg(VEEJAY_MSG_DEBUG,
         "Sample to play (at random) %d does not exist",
            take_n);
        take_n = vj_frame_rand(track_dup, 1, n_sample, settings->randplayer.seed ++ );
    }

    int remaining = sample_get_remaining_frames(take_n);

    if (remaining < min_delay)
        remaining = min_delay;

    if( settings->randplayer.timer == RANDTIMER_FRAME )
    {
        max_delay = vj_frame_rand(
            track_dup,
            min_delay,
            remaining,
            settings->randplayer.seed ++ );
    }
    else
    {
        max_delay = remaining;
    }

    settings->randplayer.max_delay = max_delay;
    settings->randplayer.min_delay = min_delay;

    veejay_msg(VEEJAY_MSG_INFO, "Sample randomizer triggers in %d frame periods", max_delay);

    settings->randplayer.next_id = take_n;
    settings->randplayer.next_mode = VJ_PLAYBACK_MODE_SAMPLE;

}

int vj_perform_rand_update(veejay_t *info)
{
    video_playback_setup *settings = info->settings;
    if(settings->randplayer.mode == RANDMODE_INACTIVE)
        return 0;
    if(settings->randplayer.mode == RANDMODE_SAMPLE)
    {
        settings->randplayer.max_delay --;
        if(settings->randplayer.max_delay <= 0 )
            vj_perform_randomize(info);
        return 1;
    }
    return 0;
}
