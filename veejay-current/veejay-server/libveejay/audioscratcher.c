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
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/atomic.h>
#include <libveejay/audioscratcher.h>

#define MAX_CHANNELS 8

#define SCRATCH_MAX_RATE              16.0
#define SCRATCH_MIN_RATE              0.01
#define SCRATCH_STOP_EPS              0.000001

#define SCRATCH_HISTORY_SEC           12
#define SCRATCH_GUARD_FRAMES          8
#define SCRATCH_SAFE_MARGIN_FRAMES    24
#define SCRATCH_MIN_BUFFER_FRAMES     4096

#define SCRATCH_XFADE_FRAMES          96
#define SCRATCH_RELOC_XFADE_FRAMES    256
#define SCRATCH_EDGE_XFADE_FRAMES     1536

#define SCRATCH_MOTOR_ACCEL_PER_SEC   90.0
#define SCRATCH_MOTOR_BRAKE_PER_SEC   160.0
#define SCRATCH_PARK_SPEED            0.0025
#define SCRATCH_STOP_GAIN_MS          42.0
#define SCRATCH_START_GAIN_MS         8.0

#define SCRATCH_AA_START              1.35
#define SCRATCH_AA_STRONG             2.75

typedef struct {
    short  *buffer;
    int     buffer_frames;
    int     filled_frames;
    int64_t write_pos;

    double  read_pos;
    double  motor_speed;
    double  last_target_speed;
    double  output_gain;
    double  fps;
    double  host_frame_accum;

    int     channels;
    int     sample_rate;
    int     primed;
    int     parked;
    int     last_out_frames;

    int     xfade_remaining;

    int     reloc_xfade_remaining;
    int     reloc_xfade_total;
    double  reloc_pos;
    double  reloc_speed;

    short   last_samples[MAX_CHANNELS];
} vj_scratch_t;

static inline double scratch_absd(double v)
{
    return (v < 0.0) ? -v : v;
}

static inline double scratch_clampd(double v, double lo, double hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline int64_t scratch_clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline short scratch_clip16(int v)
{
    return (v < -32768) ? (short)-32768 : ((v > 32767) ? (short)32767 : (short)v);
}

static inline int scratch_ring_index(const vj_scratch_t *s, int64_t frame)
{
    int64_t r = frame % (int64_t)s->buffer_frames;
    return (int)((r < 0) ? (r + s->buffer_frames) : r);
}

static inline double scratch_normalize_speed(double speed)
{
    const double a = scratch_absd(speed);

    if (a < SCRATCH_STOP_EPS)
        return 0.0;

    const double rate = (a < SCRATCH_MIN_RATE) ? SCRATCH_MIN_RATE : ((a > SCRATCH_MAX_RATE) ? SCRATCH_MAX_RATE : a);
    return (speed < 0.0) ? -rate : rate;
}

static inline short scratch_get_sample(const vj_scratch_t *s,
                                       int64_t frame,
                                       int channel,
                                       int64_t min_frame,
                                       int64_t max_frame)
{
    frame = scratch_clamp64(frame, min_frame, max_frame);
    return s->buffer[(scratch_ring_index(s, frame) * s->channels) + channel];
}

static inline float scratch_hermite4(float y0, float y1, float y2, float y3, float t)
{
    const float c0 = y1;
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

static inline float scratch_sample_hermite(const vj_scratch_t *s,
                                           double pos,
                                           int channel,
                                           int64_t min_frame,
                                           int64_t max_frame)
{
    pos = scratch_clampd(pos, (double)min_frame, (double)max_frame);

    const int64_t base = (int64_t)floor(pos);
    const float frac = (float)(pos - (double)base);

    const float y0 = (float)scratch_get_sample(s, base - 1, channel, min_frame, max_frame);
    const float y1 = (float)scratch_get_sample(s, base,     channel, min_frame, max_frame);
    const float y2 = (float)scratch_get_sample(s, base + 1, channel, min_frame, max_frame);
    const float y3 = (float)scratch_get_sample(s, base + 2, channel, min_frame, max_frame);

    return scratch_hermite4(y0, y1, y2, y3, frac);
}

static inline float scratch_sample_filtered(const vj_scratch_t *s,
                                            double pos,
                                            int channel,
                                            int64_t min_frame,
                                            int64_t max_frame,
                                            double abs_speed)
{
    if (abs_speed < SCRATCH_AA_START)
        return scratch_sample_hermite(s, pos, channel, min_frame, max_frame);

    double radius = 0.5 * abs_speed;
    radius = (radius < 0.75) ? 0.75 : ((radius > 2.0) ? 2.0 : radius);

    if (abs_speed < SCRATCH_AA_STRONG) {
        const float a = scratch_sample_hermite(s, pos - radius, channel, min_frame, max_frame);
        const float b = scratch_sample_hermite(s, pos,          channel, min_frame, max_frame);
        const float c = scratch_sample_hermite(s, pos + radius, channel, min_frame, max_frame);
        return (a + 2.0f * b + c) * 0.25f;
    }

    const float a = scratch_sample_hermite(s, pos - radius,       channel, min_frame, max_frame);
    const float b = scratch_sample_hermite(s, pos - radius * 0.5, channel, min_frame, max_frame);
    const float c = scratch_sample_hermite(s, pos,                channel, min_frame, max_frame);
    const float d = scratch_sample_hermite(s, pos + radius * 0.5, channel, min_frame, max_frame);
    const float e = scratch_sample_hermite(s, pos + radius,       channel, min_frame, max_frame);

    return (a + 2.0f * b + 3.0f * c + 2.0f * d + e) * 0.1111111111f;
}

static int scratch_write(vj_scratch_t *s, const short *input, int frames)
{
    if (s == NULL || input == NULL || frames <= 0)
        return 0;

    const int ch = s->channels;
    int frames_to_store = frames;
    int skip = 0;

    if (frames_to_store > s->buffer_frames) {
        skip = frames_to_store - s->buffer_frames;
        frames_to_store = s->buffer_frames;
        s->write_pos += skip;
    }

    const short *src = input + ((size_t)skip * (size_t)ch);
    int remaining = frames_to_store;

    while (remaining > 0) {
        const int dst_frame = scratch_ring_index(s, s->write_pos);
        int n = s->buffer_frames - dst_frame;
        n = (n > remaining) ? remaining : n;

        memcpy(s->buffer + ((size_t)dst_frame * (size_t)ch),
               src,
               (size_t)n * (size_t)ch * sizeof(short));

        src += (size_t)n * (size_t)ch;
        s->write_pos += n;
        remaining -= n;
    }

    s->filled_frames += frames_to_store;
    if (s->filled_frames > s->buffer_frames)
        s->filled_frames = s->buffer_frames;

    return frames_to_store;
}

static int scratch_expected_out_frames(vj_scratch_t *s,
                                       int src_frames,
                                       int max_out_frames,
                                       double requested_speed)
{
    if (max_out_frames <= 0)
        return 0;

    /* Caller/scratcher contract.
     *
     * The caller chooses how many input frames are handed to the scratcher.
     * The scratcher must then convert that block by the requested speed:
     *
     *   slow 0.25: caller gives one audio frame, scratcher returns 4 frames
     *              so the slow-motion code can slice them.
     *   fast 2.0: caller gives two audio frames, scratcher returns one frame
     *              for the current host tick.
     *   normal 1: caller gives one audio frame, scratcher returns one frame.
     *
     * This MUST use requested_speed, never motor_speed. The tape motor may
     * lag sonically, but the number of returned samples is a caller contract.
     */
    int base_frames = src_frames;
    if (base_frames <= 0)
        base_frames = (s->last_out_frames > 0) ? s->last_out_frames : max_out_frames;

    double a = scratch_absd(requested_speed);
    int out_frames = base_frames;

    if (a >= SCRATCH_STOP_EPS) {
        if (a < SCRATCH_MIN_RATE)
            a = SCRATCH_MIN_RATE;
        if (a > SCRATCH_MAX_RATE)
            a = SCRATCH_MAX_RATE;

        out_frames = (int)ceil(((double)base_frames / a) - 1.0e-9);
    }

    if (out_frames < 1)
        out_frames = 1;
    if (out_frames > max_out_frames)
        out_frames = max_out_frames;

    return out_frames;
}

static void scratch_valid_range(const vj_scratch_t *s,
                                int64_t *min_frame,
                                int64_t *max_frame)
{
    int64_t lo = s->write_pos - (int64_t)s->filled_frames;
    int64_t hi = s->write_pos - 1;

    if ((hi - lo) > (SCRATCH_GUARD_FRAMES * 2)) {
        lo += SCRATCH_GUARD_FRAMES;
        hi -= SCRATCH_GUARD_FRAMES;
    }

    if (lo > hi)
        lo = hi = s->write_pos - 1;

    *min_frame = lo;
    *max_frame = hi;
}

static double scratch_anchor_for_speed(const vj_scratch_t *s,
                                       double target_speed,
                                       int src_frames,
                                       int out_frames,
                                       int64_t min_frame,
                                       int64_t max_frame)
{
    const double range = (double)(max_frame - min_frame);
    double span = scratch_absd(target_speed) * (double)((out_frames > 0) ? out_frames : 1);

    span += (double)SCRATCH_SAFE_MARGIN_FRAMES;
    if (span < (double)SCRATCH_SAFE_MARGIN_FRAMES)
        span = (double)SCRATCH_SAFE_MARGIN_FRAMES;
    if (span > range * 0.45)
        span = range * 0.45;

    if (target_speed < 0.0)
        return scratch_clampd((double)max_frame - 2.0, (double)min_frame + span, (double)max_frame);

    int64_t newest_block_start = s->write_pos - (int64_t)((src_frames > 0) ? src_frames : 1);
    newest_block_start = scratch_clamp64(newest_block_start, min_frame, max_frame);

    return scratch_clampd((double)newest_block_start, (double)min_frame, (double)max_frame - span);
}

static void scratch_raise_xfade(vj_scratch_t *s, int frames)
{
    if (frames > s->xfade_remaining)
        s->xfade_remaining = frames;
}

static void scratch_begin_reloc_xfade(vj_scratch_t *s,
                                      double old_pos,
                                      double old_speed,
                                      int frames)
{
    if (frames <= 0)
        return;

    if (frames > SCRATCH_RELOC_XFADE_FRAMES)
        frames = SCRATCH_RELOC_XFADE_FRAMES;

    if (s->reloc_xfade_remaining > 0) {
        old_pos = s->reloc_pos;
        old_speed = s->reloc_speed;
    }

    s->reloc_pos = old_pos;
    s->reloc_speed = old_speed;
    s->reloc_xfade_remaining = frames;
    s->reloc_xfade_total = frames;
}

static void scratch_relocate_readhead(vj_scratch_t *s,
                                      double new_pos,
                                      double speed,
                                      int frames)
{
    const double old_pos = s->read_pos;

    if (scratch_absd(new_pos - old_pos) < 0.5) {
        s->read_pos = new_pos;
        return;
    }

    scratch_begin_reloc_xfade(s, old_pos, speed, frames);
    s->read_pos = new_pos;
}

static void scratch_keep_readhead_safe(vj_scratch_t *s,
                                       int out_frames,
                                       double target_speed,
                                       int64_t min_frame,
                                       int64_t max_frame)
{
    if (!s->primed)
        return;

    const double range = (double)(max_frame - min_frame);
    if (range < (double)(SCRATCH_SAFE_MARGIN_FRAMES * 2))
        return;

    double worst = scratch_absd(s->motor_speed);
    const double at = scratch_absd(target_speed);
    worst = (at > worst) ? at : worst;
    worst = (worst < SCRATCH_MIN_RATE) ? SCRATCH_MIN_RATE : worst;

    double span = worst * (double)((out_frames > 0) ? out_frames : 1);
    span += (double)(SCRATCH_SAFE_MARGIN_FRAMES + SCRATCH_XFADE_FRAMES);
    if (span > range * 0.45)
        span = range * 0.45;

    double lo = (double)min_frame + span;
    double hi = (double)max_frame - span;

    if (lo > hi) {
        const double mid = ((double)min_frame + (double)max_frame) * 0.5;
        lo = mid;
        hi = mid;
    }

    const int need_low_room = (s->motor_speed < 0.0 || target_speed < 0.0);
    const int need_high_room = (s->motor_speed > 0.0 || target_speed > 0.0);

    double new_pos = s->read_pos;

    if (need_low_room && new_pos < lo)
        new_pos = lo;
    if (need_high_room && new_pos > hi)
        new_pos = hi;

    if (new_pos < (double)min_frame || new_pos > (double)max_frame)
        new_pos = scratch_clampd(new_pos, (double)min_frame, (double)max_frame);

    if (new_pos != s->read_pos)
        scratch_relocate_readhead(s, new_pos, s->motor_speed, SCRATCH_RELOC_XFADE_FRAMES);
}

static inline double scratch_limit_speed_to_headroom(const vj_scratch_t *s,
                                                     double proposed,
                                                     int remaining,
                                                     int64_t min_frame,
                                                     int64_t max_frame)
{
    const double lo = (double)min_frame + 2.0;
    const double hi = (double)max_frame - 2.0;

    if (hi <= lo)
        return 0.0;

    if (proposed > 0.0) {
        const double room = hi - s->read_pos;
        if (room <= 0.0)
            return 0.0;

        const double per_frame = room / (double)((remaining > 1) ? remaining : 1);
        const double hard = (room < proposed) ? room : proposed;
        return (hard > per_frame) ? per_frame : hard;
    }

    if (proposed < 0.0) {
        const double room = s->read_pos - lo;
        if (room <= 0.0)
            return 0.0;

        const double per_frame = room / (double)((remaining > 1) ? remaining : 1);
        const double hard = (room < -proposed) ? -room : proposed;
        return (hard < -per_frame) ? -per_frame : hard;
    }

    return 0.0;
}

static inline double scratch_slew_motor(const vj_scratch_t *s,
                                        double current,
                                        double target)
{
    const double delta = target - current;
    if (scratch_absd(delta) <= 0.0)
        return target;

    const int braking = ((current > 0.0 && target <= 0.0) ||
                         (current < 0.0 && target >= 0.0) ||
                         (scratch_absd(target) < scratch_absd(current)));

    const double slew_per_sec = braking ? SCRATCH_MOTOR_BRAKE_PER_SEC : SCRATCH_MOTOR_ACCEL_PER_SEC;
    const double step = slew_per_sec / (double)((s->sample_rate > 0) ? s->sample_rate : 48000);

    if (scratch_absd(delta) <= step)
        return target;

    return current + ((delta < 0.0) ? -step : step);
}

static inline double scratch_update_gain(vj_scratch_t *s, double target_speed)
{
    const double sr = (double)((s->sample_rate > 0) ? s->sample_rate : 48000);
    const int stopping = (scratch_absd(target_speed) < SCRATCH_STOP_EPS && scratch_absd(s->motor_speed) < SCRATCH_PARK_SPEED);

    if (stopping) {
        const double fade_frames = (sr * SCRATCH_STOP_GAIN_MS) / 1000.0;
        const double step = (fade_frames > 1.0) ? (1.0 / fade_frames) : 1.0;
        s->output_gain -= step;
        if (s->output_gain < 0.0)
            s->output_gain = 0.0;
    } else {
        const double fade_frames = (sr * SCRATCH_START_GAIN_MS) / 1000.0;
        const double step = (fade_frames > 1.0) ? (1.0 / fade_frames) : 1.0;
        s->output_gain += step;
        if (s->output_gain > 1.0)
            s->output_gain = 1.0;
    }

    return s->output_gain;
}

void* vj_scratch_init(int channels, int sample_rate, float fps)
{
    if (channels <= 0 || channels > MAX_CHANNELS || sample_rate <= 0)
        return NULL;

    vj_scratch_t *s = vj_calloc(sizeof(vj_scratch_t));
    if (!s)
        return NULL;

    int history_frames = sample_rate * SCRATCH_HISTORY_SEC;
    if (history_frames < SCRATCH_MIN_BUFFER_FRAMES)
        history_frames = SCRATCH_MIN_BUFFER_FRAMES;
    history_frames += (SCRATCH_GUARD_FRAMES * 4);

    s->channels = channels;
    s->sample_rate = sample_rate;
    s->fps = (fps > 1.0f) ? (double)fps : 0.0;
    s->host_frame_accum = 0.0;
    s->buffer_frames = history_frames;
    s->read_pos = -1.0;
    s->motor_speed = 0.0;
    s->last_target_speed = 0.0;
    s->output_gain = 1.0;
    s->primed = 0;
    s->parked = 0;
    s->last_out_frames = 0;
    s->xfade_remaining = SCRATCH_XFADE_FRAMES;
    s->reloc_xfade_remaining = 0;
    s->reloc_xfade_total = 0;
    s->reloc_pos = 0.0;
    s->reloc_speed = 0.0;

    s->buffer = vj_calloc((size_t)s->buffer_frames * (size_t)channels * sizeof(short));
    if (!s->buffer) {
        free(s);
        return NULL;
    }

    return s;
}

static void scratch_reset_common(vj_scratch_t *s, int keep_tail, int fade_frames)
{
    if (s == NULL)
        return;

    short tail[MAX_CHANNELS];
    if (keep_tail)
        memcpy(tail, s->last_samples, sizeof(tail));

    if (fade_frames < SCRATCH_XFADE_FRAMES)
        fade_frames = SCRATCH_XFADE_FRAMES;
    if (fade_frames > SCRATCH_EDGE_XFADE_FRAMES)
        fade_frames = SCRATCH_EDGE_XFADE_FRAMES;

    s->filled_frames = 0;
    s->write_pos = 0;
    s->read_pos = -1.0;
    s->motor_speed = 0.0;
    s->last_target_speed = 0.0;
    s->output_gain = 1.0;
    s->host_frame_accum = 0.0;
    s->primed = 0;
    s->parked = 0;
    s->last_out_frames = 0;
    s->xfade_remaining = fade_frames;
    s->reloc_xfade_remaining = 0;
    s->reloc_xfade_total = 0;
    s->reloc_pos = 0.0;
    s->reloc_speed = 0.0;

    if (keep_tail)
        memcpy(s->last_samples, tail, sizeof(s->last_samples));
    else
        memset(s->last_samples, 0, sizeof(s->last_samples));
}

void vj_scratch_reset(void *ptr)
{
    scratch_reset_common((vj_scratch_t*)ptr, 0, SCRATCH_XFADE_FRAMES);
}

void vj_scratch_soft_reset(void *ptr)
{
    scratch_reset_common((vj_scratch_t*)ptr, 1, SCRATCH_EDGE_XFADE_FRAMES);
}


void vj_scratch_free(void *ptr)
{
    vj_scratch_t *s = (vj_scratch_t*)ptr;
    if (s) {
        free(s->buffer);
        free(s);
    }
}

int vj_scratch_process(void *ptr,
                       short *output,
                       int max_out_frames,
                       const short *input,
                       int src_frames,
                       double speed)
{
    vj_scratch_t *s = (vj_scratch_t*)ptr;

    if (s == NULL || output == NULL || max_out_frames <= 0)
        return 0;

    const int out_frames = scratch_expected_out_frames(s, src_frames, max_out_frames, speed);
    if (out_frames <= 0)
        return 0;

    scratch_write(s, input, src_frames);

    const int ch = s->channels;
    const double target_speed = scratch_normalize_speed(speed);

    if (s->filled_frames <= 0) {
        memset(output, 0, (size_t)out_frames * (size_t)ch * sizeof(short));
        s->last_out_frames = out_frames;
        return out_frames;
    }

    const int resume_from_park = (s->parked && scratch_absd(target_speed) >= SCRATCH_STOP_EPS);

    if (resume_from_park) {
        s->primed = 0;
        s->parked = 0;
        s->motor_speed = 0.0;
        s->output_gain = 0.0;
        scratch_raise_xfade(s, SCRATCH_XFADE_FRAMES);
        s->reloc_xfade_remaining = 0;
        s->reloc_xfade_total = 0;
    }

    int64_t min_frame = 0;
    int64_t max_frame = 0;
    scratch_valid_range(s, &min_frame, &max_frame);

    if (max_frame <= min_frame) {
        memset(output, 0, (size_t)out_frames * (size_t)ch * sizeof(short));
        s->last_out_frames = out_frames;
        return out_frames;
    }

    if (!s->primed || s->read_pos < (double)min_frame || s->read_pos > (double)max_frame) {
        s->read_pos = scratch_anchor_for_speed(s, target_speed, src_frames, out_frames, min_frame, max_frame);
        s->motor_speed = resume_from_park ? 0.0 : target_speed;
        if (scratch_absd(target_speed) < SCRATCH_STOP_EPS)
            s->motor_speed = 0.0;
        s->primed = 1;
        scratch_raise_xfade(s, SCRATCH_XFADE_FRAMES);
    }

    scratch_keep_readhead_safe(s, out_frames, target_speed, min_frame, max_frame);

    for (int i = 0; i < out_frames; i++) {
        const int remaining = out_frames - i;
        double proposed_speed = scratch_slew_motor(s, s->motor_speed, target_speed);
        proposed_speed = scratch_limit_speed_to_headroom(s, proposed_speed, remaining, min_frame, max_frame);
        s->motor_speed = proposed_speed;

        if (s->read_pos < (double)min_frame || s->read_pos > (double)max_frame) {
            const double new_pos = scratch_anchor_for_speed(s, target_speed, src_frames, remaining, min_frame, max_frame);
            scratch_relocate_readhead(s, new_pos, s->motor_speed, SCRATCH_RELOC_XFADE_FRAMES);
        }

        const double abs_current_speed = scratch_absd(s->motor_speed);
        const double gain = scratch_update_gain(s, target_speed);
        const int oi = i * ch;

        if (gain <= 0.0 && scratch_absd(target_speed) < SCRATCH_STOP_EPS && abs_current_speed < SCRATCH_PARK_SPEED) {
            for (int c = 0; c < ch; c++) {
                output[oi + c] = 0;
                s->last_samples[c] = 0;
            }
        } else {
            const int relocating = (s->reloc_xfade_remaining > 0 && s->reloc_xfade_total > 0);
            float wet = 1.0f;
            float dry = 0.0f;

            if (relocating) {
                float t = (float)(s->reloc_xfade_total - s->reloc_xfade_remaining + 1) /
                          (float)(s->reloc_xfade_total + 1);
                t = t * t * (3.0f - 2.0f * t);
                wet = t;
                dry = 1.0f - t;
            } else if (s->xfade_remaining > 0) {
                float t = (float)(SCRATCH_XFADE_FRAMES - s->xfade_remaining + 1) /
                          (float)(SCRATCH_XFADE_FRAMES + 1);
                t = t * t * (3.0f - 2.0f * t);
                wet = t;
                dry = 1.0f - t;
            }

            for (int c = 0; c < ch; c++) {
                float sample = scratch_sample_filtered(s, s->read_pos, c, min_frame, max_frame, abs_current_speed);

                if (relocating) {
                    const float old_sample = scratch_sample_filtered(s, s->reloc_pos, c, min_frame, max_frame, abs_current_speed);
                    sample = (sample * wet) + (old_sample * dry);
                } else if (dry > 0.0f) {
                    sample = (sample * wet) + ((float)s->last_samples[c] * dry);
                }

                sample *= (float)gain;

                const short out = scratch_clip16((int)((sample >= 0.0f) ? (sample + 0.5f) : (sample - 0.5f)));
                output[oi + c] = out;
                s->last_samples[c] = out;
            }
        }

        if (s->xfade_remaining > 0)
            s->xfade_remaining--;

        if (s->reloc_xfade_remaining > 0) {
            s->reloc_xfade_remaining--;
            s->reloc_pos += s->motor_speed;
            s->reloc_pos = scratch_clampd(s->reloc_pos, (double)min_frame, (double)max_frame);
            s->reloc_speed = s->motor_speed;
            if (s->reloc_xfade_remaining == 0)
                s->reloc_xfade_total = 0;
        }

        s->read_pos += s->motor_speed;
    }

    if (scratch_absd(target_speed) < SCRATCH_STOP_EPS &&
        scratch_absd(s->motor_speed) < SCRATCH_PARK_SPEED &&
        s->output_gain <= 0.0) {
        s->parked = 1;
        s->primed = 0;
        s->motor_speed = 0.0;
        s->read_pos = -1.0;
        s->reloc_xfade_remaining = 0;
        s->reloc_xfade_total = 0;
    }

    s->last_target_speed = target_speed;
    s->last_out_frames = out_frames;

    return out_frames;
}


#define AUDIO_DECLICK_SLOTS              8
#define AUDIO_DECLICK_FRAMES             128
#define AUDIO_DECLICK_STATE_FRAMES       256
#define AUDIO_DECLICK_EDGE_FRAMES        384
#define AUDIO_DECLICK_SLOW_EDGE_FRAMES   768
#define AUDIO_DECLICK_DIRECT_DIR_MIN     256
#define AUDIO_DECLICK_DIRECT_DIR_MID     384
#define AUDIO_DECLICK_DIRECT_DIR_MAX     640
#define AUDIO_DECLICK_DIRECT_HARD_MIN    384
#define AUDIO_DECLICK_DIRECT_HARD_MID    640
#define AUDIO_DECLICK_DIRECT_HARD_MAX    960
#define AUDIO_DECLICK_SLOW_DIR_MIN       192
#define AUDIO_DECLICK_SLOW_DIR_MID       256
#define AUDIO_DECLICK_SLOW_DIR_MAX       384
#define AUDIO_DECLICK_SLOW_HARD_MIN      384
#define AUDIO_DECLICK_SLOW_HARD_MID      640
#define AUDIO_DECLICK_SLOW_HARD_MAX      960
#define AUDIO_DECLICK_SLOW_AUTO_FRAMES   256
#define AUDIO_DECLICK_SLOW_AUTO_DELTA    2048
#define AUDIO_DECLICK_HARD_DELTA         4096
#define AUDIO_DECLICK_SLOW_BOUNDARY_DELTA 96
#define AUDIO_DECLICK_SLOW_BOUNDARY_SLOPE 32
#define AUDIO_DECLICK_MAX_BYTES          64
#define AUDIO_DECLICK_MAX_WORDS          (AUDIO_DECLICK_MAX_BYTES / 2)

typedef struct {
    const void *owner;
    int valid;
    int last_path;
    int last_speed;
    int last_dir;
    int frame_bytes;
    uint8_t prev_frame[AUDIO_DECLICK_MAX_BYTES];
    uint8_t last_frame[AUDIO_DECLICK_MAX_BYTES];
} vj_audio_declick_state_t;

static vj_audio_declick_state_t vj_audio_declick_states[AUDIO_DECLICK_SLOTS];

static vj_audio_declick_state_t *vj_audio_declick_get(const void *owner)
{
    int free_slot = -1;

    for (int i = 0; i < AUDIO_DECLICK_SLOTS; i++) {
        if (vj_audio_declick_states[i].owner == owner)
            return &vj_audio_declick_states[i];

        if (free_slot < 0 && vj_audio_declick_states[i].owner == NULL)
            free_slot = i;
    }

    if (free_slot < 0)
        free_slot = 0;

    vj_audio_declick_states[free_slot].owner = owner;
    vj_audio_declick_states[free_slot].valid = 0;
    vj_audio_declick_states[free_slot].last_path = AUDIO_PATH_SILENCE;
    vj_audio_declick_states[free_slot].last_speed = 0;
    vj_audio_declick_states[free_slot].last_dir = 0;
    vj_audio_declick_states[free_slot].frame_bytes = 0;
    veejay_memset(vj_audio_declick_states[free_slot].prev_frame, 0,
                  sizeof(vj_audio_declick_states[free_slot].prev_frame));
    veejay_memset(vj_audio_declick_states[free_slot].last_frame, 0,
                  sizeof(vj_audio_declick_states[free_slot].last_frame));

    return &vj_audio_declick_states[free_slot];
}

void vj_audio_declick_forget_owner(const void *owner)
{
    if (owner == NULL)
        return;

    for (int i = 0; i < AUDIO_DECLICK_SLOTS; i++) {
        if (vj_audio_declick_states[i].owner == owner) {
            vj_audio_declick_states[i].owner = NULL;
            vj_audio_declick_states[i].valid = 0;
            vj_audio_declick_states[i].last_path = AUDIO_PATH_SILENCE;
            vj_audio_declick_states[i].last_speed = 0;
            vj_audio_declick_states[i].last_dir = 0;
            vj_audio_declick_states[i].frame_bytes = 0;
            veejay_memset(vj_audio_declick_states[i].prev_frame, 0,
                          sizeof(vj_audio_declick_states[i].prev_frame));
            veejay_memset(vj_audio_declick_states[i].last_frame, 0,
                          sizeof(vj_audio_declick_states[i].last_frame));
            return;
        }
    }
}

int vj_audio_edge_is_hard(int edge_type)
{
    return (edge_type == AUDIO_EDGE_JUMP ||
            edge_type == AUDIO_EDGE_RESET ||
            edge_type == AUDIO_EDGE_SILENCE);
}

void vj_audio_clear_edge(audio_edge_t *edge, int cur_dir)
{
    if (edge == NULL)
        return;

    atomic_store_int(&edge->last_direction, cur_dir);
    atomic_store_int(&edge->pending_edge, AUDIO_EDGE_NONE);
}


static const char *vj_audio_declick_path_name(int path)
{
    switch (path) {
        case AUDIO_PATH_SILENCE: return "silence";
        case AUDIO_PATH_DIRECT:  return "direct";
        case AUDIO_PATH_FAST:    return "fast";
        case AUDIO_PATH_SLOW:    return "slow";
        default:                 return "unknown";
    }
}

static const char *vj_audio_declick_edge_name(int edge)
{
    switch (edge) {
        case AUDIO_EDGE_NONE:      return "none";
        case AUDIO_EDGE_DIRECTION: return "direction";
        case AUDIO_EDGE_JUMP:      return "jump";
        case AUDIO_EDGE_RESET:     return "reset";
        case AUDIO_EDGE_SILENCE:   return "silence";
        default:                   return "unknown";
    }
}

static int vj_audio_declick_delta_s16(const uint8_t *a,
                                      const uint8_t *b,
                                      int frame_bytes)
{
    if (a == NULL || b == NULL || frame_bytes <= 0 || (frame_bytes & 1))
        return -1;

    const int words = frame_bytes / (int)sizeof(int16_t);
    const int16_t *aa = (const int16_t*)a;
    const int16_t *bb = (const int16_t*)b;
    int peak = 0;

    for (int i = 0; i < words; i++) {
        int d = (int)aa[i] - (int)bb[i];
        d = (d < 0) ? -d : d;
        if (d > peak)
            peak = d;
    }

    return peak;
}

static int vj_audio_declick_slope_delta_s16(const uint8_t *prev_prev,
                                            const uint8_t *prev_last,
                                            const uint8_t *cur0,
                                            const uint8_t *cur1,
                                            int frame_bytes)
{
    if (prev_prev == NULL || prev_last == NULL || cur0 == NULL ||
        cur1 == NULL || frame_bytes <= 0 || (frame_bytes & 1))
        return -1;

    const int words = frame_bytes / (int)sizeof(int16_t);
    const int16_t *pp = (const int16_t*)prev_prev;
    const int16_t *pl = (const int16_t*)prev_last;
    const int16_t *c0 = (const int16_t*)cur0;
    const int16_t *c1 = (const int16_t*)cur1;
    int peak = 0;

    for (int i = 0; i < words; i++) {
        int prev_slope = (int)pl[i] - (int)pp[i];
        int cur_slope = (int)c1[i] - (int)c0[i];
        int d = prev_slope - cur_slope;
        d = (d < 0) ? -d : d;
        if (d > peak)
            peak = d;
    }

    return peak;
}

static int vj_audio_declick_peak_s16_local(const uint8_t *buf,
                                           int samples,
                                           int frame_bytes)
{
    if (buf == NULL || samples <= 0 || frame_bytes <= 0 || (frame_bytes & 1))
        return 0;

    const int words = (samples * frame_bytes) / (int)sizeof(int16_t);
    const int16_t *p = (const int16_t*)buf;
    int peak = 0;

    for (int i = 0; i < words; i++) {
        int v = (int)p[i];
        v = (v < 0) ? -v : v;
        if (v > peak)
            peak = v;
    }

    return peak;
}


static int vj_audio_declick_pick_edge_fade(int path,
                                           int edge_type,
                                           int direction_flipped,
                                           int state_changed,
                                           int slow_auto_splice,
                                           int hard_auto_splice,
                                           int pre_delta)
{
    if (edge_type == AUDIO_EDGE_DIRECTION || direction_flipped) {
        if (path == AUDIO_PATH_SLOW) {
            if (pre_delta >= 1024)
                return AUDIO_DECLICK_SLOW_DIR_MAX;
            if (pre_delta >= 256)
                return AUDIO_DECLICK_SLOW_DIR_MID;
            return AUDIO_DECLICK_SLOW_DIR_MIN;
        }

        if (path == AUDIO_PATH_DIRECT || path == AUDIO_PATH_FAST) {
            if (pre_delta >= AUDIO_DECLICK_HARD_DELTA)
                return AUDIO_DECLICK_DIRECT_DIR_MAX;
            if (pre_delta >= 1024)
                return AUDIO_DECLICK_DIRECT_DIR_MID;
            return AUDIO_DECLICK_DIRECT_DIR_MIN;
        }

        return AUDIO_DECLICK_FRAMES;
    }

    if (edge_type == AUDIO_EDGE_JUMP || edge_type == AUDIO_EDGE_RESET ||
        edge_type == AUDIO_EDGE_SILENCE) {
        if (path == AUDIO_PATH_SLOW) {
            if (pre_delta >= 8192)
                return AUDIO_DECLICK_SLOW_HARD_MAX;
            if (pre_delta >= 1024)
                return AUDIO_DECLICK_SLOW_HARD_MID;
            return AUDIO_DECLICK_SLOW_HARD_MIN;
        }

        if (pre_delta >= 8192)
            return AUDIO_DECLICK_DIRECT_HARD_MAX;
        if (pre_delta >= 2048)
            return AUDIO_DECLICK_DIRECT_HARD_MID;
        return AUDIO_DECLICK_DIRECT_HARD_MIN;
    }

    if (slow_auto_splice)
        return AUDIO_DECLICK_SLOW_AUTO_FRAMES;

    if (hard_auto_splice)
        return AUDIO_DECLICK_EDGE_FRAMES;

    if (state_changed)
        return AUDIO_DECLICK_STATE_FRAMES;

    return AUDIO_DECLICK_FRAMES;
}

static void vj_audio_declick_cubic_splice(vj_audio_declick_state_t *st,
                                          uint8_t *buf,
                                          int samples,
                                          int frame_bytes,
                                          int fade_frames,
                                          int *pre_delta,
                                          int *post_delta,
                                          int *pre_slope,
                                          int *post_slope,
                                          int *max_corr,
                                          int *clip_count)
{
    if (st == NULL || buf == NULL || samples <= 0 || frame_bytes <= 0 ||
        frame_bytes > AUDIO_DECLICK_MAX_BYTES || (frame_bytes & 1))
        return;

    if (!st->valid || st->frame_bytes != frame_bytes)
        return;

    if (fade_frames < 4)
        fade_frames = 4;
    if (fade_frames > samples)
        fade_frames = samples;

    const int words = frame_bytes / (int)sizeof(int16_t);
    int16_t *dst = (int16_t*)buf;
    const int16_t *prev_prev = (const int16_t*)st->prev_frame;
    const int16_t *prev_last = (const int16_t*)st->last_frame;
    const int16_t *cur0 = (const int16_t*)buf;

    const uint8_t *cur1_ptr = (samples > 1) ?
        (buf + (size_t)frame_bytes) : buf;
    const int16_t *cur1 = (const int16_t*)cur1_ptr;

    int corr_peak = 0;
    int clips = 0;

    if (pre_delta != NULL)
        *pre_delta = vj_audio_declick_delta_s16(buf, st->last_frame, frame_bytes);

    if (pre_slope != NULL) {
        *pre_slope = vj_audio_declick_slope_delta_s16(
            st->prev_frame,
            st->last_frame,
            buf,
            cur1_ptr,
            frame_bytes
        );
    }
    for (int c = 0; c < words; c++) {
        const int value_corr = (int)prev_last[c] - (int)cur0[c];
        const int prev_slope = (int)prev_last[c] - (int)prev_prev[c];
        const int cur_slope = (int)cur1[c] - (int)cur0[c];
        const int slope_corr = prev_slope - cur_slope;
        const int second_corr = value_corr + slope_corr;

        for (int i = 0; i < fade_frames; i++) {
            float corr;

            if (i == 0 || fade_frames <= 2) {
                corr = (float)value_corr;
            } else {
                const float t = (fade_frames <= 2) ? 1.0f :
                    ((float)(i - 1) / (float)(fade_frames - 2));
                const float tt = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
                const float smooth = tt * tt * (3.0f - 2.0f * tt);
                corr = (float)second_corr * (1.0f - smooth);
            }

            const int idx = (i * words) + c;
            int acorr = (int)((corr < 0.0f) ? (-corr + 0.5f) : (corr + 0.5f));
            if (acorr > corr_peak)
                corr_peak = acorr;

            float v = (float)dst[idx] + corr;

            if (v < -32768.0f) {
                v = -32768.0f;
                clips++;
            } else if (v > 32767.0f) {
                v = 32767.0f;
                clips++;
            }

            dst[idx] = (int16_t)((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
        }
    }

    if (post_delta != NULL)
        *post_delta = vj_audio_declick_delta_s16(buf, st->last_frame, frame_bytes);

    if (post_slope != NULL) {
        *post_slope = vj_audio_declick_slope_delta_s16(
            st->prev_frame,
            st->last_frame,
            buf,
            cur1_ptr,
            frame_bytes
        );
    }

    if (max_corr != NULL)
        *max_corr = corr_peak;
    if (clip_count != NULL)
        *clip_count = clips;
}

void vj_audio_declick_apply(const void *owner,
                            uint8_t *buf,
                            int samples,
                            int frame_bytes,
                            int path,
                            int speed,
                            int dir,
                            int edge_type,
                            int direction_flipped)
{
    if (owner == NULL || buf == NULL || samples <= 0 ||
        frame_bytes <= 0 || frame_bytes > AUDIO_DECLICK_MAX_BYTES)
        return;

    if ((frame_bytes & 1) != 0)
        return;

    vj_audio_declick_state_t *st = vj_audio_declick_get(owner);

    const int frame_compatible =
        (st->valid && st->frame_bytes == frame_bytes);

    const int state_changed =
        (!st->valid ||
         st->last_path != path ||
         st->last_speed != speed ||
         st->last_dir != dir ||
         st->frame_bytes != frame_bytes);

    const int edge_changed =
        (edge_type != AUDIO_EDGE_NONE || direction_flipped);

    int pre_delta = -1;
    if (frame_compatible)
        pre_delta = vj_audio_declick_delta_s16(buf, st->last_frame, frame_bytes);

    const int slow_auto_splice =
        (frame_compatible && !state_changed && !edge_changed &&
         path == AUDIO_PATH_SLOW &&
         pre_delta >= AUDIO_DECLICK_SLOW_AUTO_DELTA);

    const int hard_auto_splice =
        (frame_compatible && !state_changed && !edge_changed &&
         path != AUDIO_PATH_SILENCE &&
         pre_delta >= AUDIO_DECLICK_HARD_DELTA);
    const int already_continuous_slow_state_change =
        (frame_compatible && path == AUDIO_PATH_SLOW && state_changed &&
         pre_delta >= 0 && pre_delta <= 16 &&
         (edge_type == AUDIO_EDGE_NONE || edge_type == AUDIO_EDGE_DIRECTION ||
          direction_flipped));

    const int apply_splice =
        (frame_compatible && !already_continuous_slow_state_change &&
         (state_changed || edge_changed || slow_auto_splice || hard_auto_splice));

    int used_frames = 0;
    int post_delta = pre_delta;
    int pre_slope = -1;
    int post_slope = -1;
    int max_corr = 0;
    int clip_count = 0;

    if (apply_splice) {
        int fade_frames = vj_audio_declick_pick_edge_fade(
            path,
            edge_type,
            direction_flipped,
            state_changed,
            slow_auto_splice,
            hard_auto_splice,
            pre_delta
        );

        if (fade_frames > samples)
            fade_frames = samples;

        vj_audio_declick_cubic_splice(st,
                                      buf,
                                      samples,
                                      frame_bytes,
                                      fade_frames,
                                      &pre_delta,
                                      &post_delta,
                                      &pre_slope,
                                      &post_slope,
                                      &max_corr,
                                      &clip_count);

        used_frames = fade_frames;
#ifdef VEEJAY_AUDIO_DEBUG
        if (clip_count > 0 || slow_auto_splice || hard_auto_splice ||
            pre_delta >= 1024 || post_delta != 0) {
            veejay_msg(VEEJAY_MSG_DEBUG,
                       "[AUDIO-DIAG] declick-watch path=%s(%d) edge=%s(%d) flip=%d state=%d auto_slow=%d auto_hard=%d speed=%d dir=%d samples=%d fade=%d pre_delta=%d post_delta=%d pre_slope=%d post_slope=%d corr=%d clips=%d",
                       vj_audio_declick_path_name(path), path,
                       vj_audio_declick_edge_name(edge_type), edge_type,
                       direction_flipped, state_changed, slow_auto_splice,
                       hard_auto_splice, speed, dir, samples, used_frames,
                       pre_delta, post_delta, pre_slope, post_slope,
                       max_corr, clip_count);
        }
#endif
    }

    if (samples > 1) {
        veejay_memcpy(st->prev_frame,
                      buf + ((size_t)(samples - 2) * (size_t)frame_bytes),
                      frame_bytes);
    } else if (st->valid) {
        veejay_memcpy(st->prev_frame, st->last_frame, frame_bytes);
    } else {
        veejay_memset(st->prev_frame, 0, frame_bytes);
    }

    veejay_memcpy(st->last_frame,
                  buf + ((size_t)(samples - 1) * (size_t)frame_bytes),
                  frame_bytes);

    st->valid = 1;
    st->last_path = path;
    st->last_speed = speed;
    st->last_dir = dir;
    st->frame_bytes = frame_bytes;
}


void vj_audio_declick_observe(const void *owner,
                              const uint8_t *buf,
                              int samples,
                              int frame_bytes,
                              int path,
                              int speed,
                              int dir)
{
    if (owner == NULL || buf == NULL || samples <= 0 ||
        frame_bytes <= 0 || frame_bytes > AUDIO_DECLICK_MAX_BYTES)
        return;

    if ((frame_bytes & 1) != 0)
        return;

    vj_audio_declick_state_t *st = vj_audio_declick_get(owner);

    if (samples > 1) {
        veejay_memcpy(st->prev_frame,
                      buf + ((size_t)(samples - 2) * (size_t)frame_bytes),
                      frame_bytes);
    } else if (st->valid) {
        veejay_memcpy(st->prev_frame, st->last_frame, frame_bytes);
    } else {
        veejay_memset(st->prev_frame, 0, frame_bytes);
    }

    veejay_memcpy(st->last_frame,
                  buf + ((size_t)(samples - 1) * (size_t)frame_bytes),
                  frame_bytes);

    st->valid = 1;
    st->last_path = path;
    st->last_speed = speed;
    st->last_dir = dir;
    st->frame_bytes = frame_bytes;
}

void vj_audio_reverse_buffer(uint8_t *buf, int n_samples, int frame_bytes)
{
    if (buf == NULL || n_samples <= 1 || frame_bytes <= 0)
        return;

    int i = 0;
    int j = n_samples - 1;

    switch (frame_bytes) {
        case 2: {
            uint16_t *p = (uint16_t *)buf;
            while (i < j) {
                uint16_t tmp = p[i];
                p[i] = p[j];
                p[j] = tmp;
                i++;
                j--;
            }
            break;
        }
        case 4: {
            uint32_t *p = (uint32_t *)buf;
            while (i < j) {
                uint32_t tmp = p[i];
                p[i] = p[j];
                p[j] = tmp;
                i++;
                j--;
            }
            break;
        }
        case 8: {
            uint64_t *p = (uint64_t *)buf;
            while (i < j) {
                uint64_t tmp = p[i];
                p[i] = p[j];
                p[j] = tmp;
                i++;
                j--;
            }
            break;
        }
        default: {
            uint8_t tmp[32];

            if (frame_bytes > (int)sizeof(tmp))
                return;

            while (i < j) {
                uint8_t *a = buf + ((size_t)i * (size_t)frame_bytes);
                uint8_t *b = buf + ((size_t)j * (size_t)frame_bytes);

                veejay_memcpy(tmp, a, frame_bytes);
                veejay_memcpy(a, b, frame_bytes);
                veejay_memcpy(b, tmp, frame_bytes);

                i++;
                j--;
            }
            break;
        }
    }
}

static void vj_audio_pad_tail_with_last_frame(uint8_t *buf,
                                              int produced_samples,
                                              int expected_samples,
                                              int frame_bytes)
{
    if (buf == NULL || frame_bytes <= 0 || expected_samples <= 0)
        return;

    if (produced_samples <= 0) {
        veejay_memset(buf, 0, expected_samples * frame_bytes);
        return;
    }

    if (produced_samples >= expected_samples)
        return;

    uint8_t *last = buf + ((size_t)(produced_samples - 1) * (size_t)frame_bytes);
    uint8_t *dst  = buf + ((size_t)produced_samples * (size_t)frame_bytes);

    for (int i = produced_samples; i < expected_samples; i++) {
        veejay_memcpy(dst, last, frame_bytes);
        dst += frame_bytes;
    }
}

int vj_audio_scratch_process_exact(void *scratcher,
                                   uint8_t *dst,
                                   int expected_samples,
                                   const uint8_t *src,
                                   int src_samples,
                                   double speed,
                                   int frame_bytes)
{
    if (dst == NULL || expected_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (scratcher == NULL || src == NULL || src_samples <= 0) {
        veejay_memset(dst, 0, expected_samples * frame_bytes);
        return expected_samples;
    }

    int produced = vj_scratch_process(
        scratcher,
        (short*)dst,
        expected_samples,
        (const short*)src,
        src_samples,
        speed
    );

    produced = (produced < 0) ? 0 : produced;
    produced = (produced > expected_samples) ? expected_samples : produced;

    vj_audio_pad_tail_with_last_frame(dst, produced, expected_samples, frame_bytes);

    return expected_samples;
}

static inline int16_t vj_audio_clip16_from_float(float v)
{
    if (v < -32768.0f)
        return (int16_t)-32768;
    if (v > 32767.0f)
        return (int16_t)32767;
    return (int16_t)((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
}


int vj_audio_frame_delta_s16(const uint8_t *a,
                             const uint8_t *b,
                             int frame_bytes)
{
    if (a == NULL || b == NULL || frame_bytes <= 0 || (frame_bytes & 1))
        return -1;

    const int words = frame_bytes / (int)sizeof(int16_t);
    const int16_t *aa = (const int16_t*)a;
    const int16_t *bb = (const int16_t*)b;
    int peak = 0;

    for (int i = 0; i < words; i++) {
        int d = (int)aa[i] - (int)bb[i];
        d = (d < 0) ? -d : d;
        if (d > peak)
            peak = d;
    }

    return peak;
}

int vj_audio_peak_s16(const uint8_t *buf,
                      int samples,
                      int frame_bytes)
{
    if (buf == NULL || samples <= 0 || frame_bytes <= 0 || (frame_bytes & 1))
        return 0;

    const int words = frame_bytes / (int)sizeof(int16_t);
    const int total = samples * words;
    const int16_t *p = (const int16_t*)buf;
    int peak = 0;

    for (int i = 0; i < total; i++) {
        int v = (int)p[i];
        v = (v < 0) ? -v : v;
        if (v > peak)
            peak = v;
    }

    return peak;
}

void vj_audio_copy_last_frame(uint8_t *dst,
                              int dst_bytes,
                              const uint8_t *buf,
                              int samples,
                              int frame_bytes)
{
    if (dst == NULL || buf == NULL || samples <= 0 ||
        frame_bytes <= 0 || dst_bytes < frame_bytes)
        return;

    veejay_memcpy(dst,
                  buf + ((size_t)(samples - 1) * (size_t)frame_bytes),
                  frame_bytes);
}

/*
 * Deterministic per-video-frame slow stretcher.
 *
 * This deliberately does not use the persistent scratch ring. Slow-motion
 * playback is slice-clocked by the performer: one decoded audio frame is
 * stretched to N audio ticks and then sliced. A persistent read-head can be
 * correct for hand-scratch style motion, but it is the wrong primitive here:
 * after a direction flip or seek it can re-prime against a tiny fresh block
 * and audibly repeat/stutter at very low rates such as 0.11x.
 *
 * The output is exactly expected_samples long and maps the first/last output
 * sample to the first/last input sample. Direction only selects traversal.
 */
int vj_audio_stretch_block_s16(uint8_t *dst,
                               int expected_samples,
                               const uint8_t *src,
                               int src_samples,
                               double speed,
                               int frame_bytes)
{
    if (dst == NULL || expected_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || src_samples <= 0 || speed == 0.0) {
        veejay_memset(dst, 0, expected_samples * frame_bytes);
        return expected_samples;
    }

    const int words = frame_bytes / (int)sizeof(int16_t);
    if (words <= 0 || (frame_bytes & 1)) {
        veejay_memset(dst, 0, expected_samples * frame_bytes);
        return expected_samples;
    }

    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;

    if (src_samples == 1 || expected_samples == 1) {
        const int src_index = (speed < 0.0) ? (src_samples - 1) : 0;
        const int si = src_index * words;
        for (int i = 0; i < expected_samples; i++) {
            const int oi = i * words;
            for (int c = 0; c < words; c++)
                out[oi + c] = in[si + c];
        }
        return expected_samples;
    }

    const int reverse = (speed < 0.0);
    const double span = (double)(src_samples - 1);
    const double step = span / (double)(expected_samples - 1);

    for (int i = 0; i < expected_samples; i++) {
        double pos = (double)i * step;
        if (reverse)
            pos = span - pos;

        int i0 = (int)pos;
        int i1 = i0 + 1;

        if (i0 < 0)
            i0 = 0;
        else if (i0 >= src_samples)
            i0 = src_samples - 1;

        if (i1 < 0)
            i1 = 0;
        else if (i1 >= src_samples)
            i1 = src_samples - 1;

        const float frac = (float)(pos - (double)i0);
        const float a_gain = 1.0f - frac;
        const float b_gain = frac;

        const int in0 = i0 * words;
        const int in1 = i1 * words;
        const int oi = i * words;

        for (int c = 0; c < words; c++) {
            const float a = (float)in[in0 + c];
            const float b = (float)in[in1 + c];
            out[oi + c] = vj_audio_clip16_from_float((a * a_gain) + (b * b_gain));
        }
    }


    return expected_samples;
}

/*
 * Continuous slow-motion stretcher.
 *
 * Unlike vj_audio_stretch_block_s16(), this does not force each stretched
 * block to land exactly on the last sample of the current video audio frame.
 * Instead it advances with a fixed source step of 1/sfd through a source
 * context that contains the current audio frame plus a guard frame in playback
 * direction. This removes the ordinary slow-block seam that produced a
 * speed-dependent tick every sfd video ticks.
 *
 * The source context must already be oriented in playback direction:
 *   forward: current frame, then next frame
 *   reverse: current frame reversed, then previous frame reversed
 */
int vj_audio_stretch_continuous_s16(uint8_t *dst,
                                    int expected_samples,
                                    const uint8_t *src,
                                    int src_samples,
                                    int context_samples,
                                    int slice_count,
                                    int frame_bytes)
{
    if (dst == NULL || expected_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || context_samples <= 0 || slice_count <= 1) {
        veejay_memset(dst, 0, expected_samples * frame_bytes);
        return expected_samples;
    }

    const int words = frame_bytes / (int)sizeof(int16_t);
    if (words <= 0 || (frame_bytes & 1)) {
        veejay_memset(dst, 0, expected_samples * frame_bytes);
        return expected_samples;
    }

    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;
    const double inv_sfd = 1.0 / (double)slice_count;
    const int max_index = context_samples - 1;

    for (int i = 0; i < expected_samples; i++) {
        double pos = (double)i * inv_sfd;

        if (pos < 0.0)
            pos = 0.0;
        else if (pos > (double)max_index)
            pos = (double)max_index;

        int i0 = (int)pos;
        int i1 = i0 + 1;

        if (i1 > max_index)
            i1 = max_index;

        const float frac = (float)(pos - (double)i0);
        const float a_gain = 1.0f - frac;
        const float b_gain = frac;

        const int in0 = i0 * words;
        const int in1 = i1 * words;
        const int oi = i * words;

        for (int c = 0; c < words; c++) {
            const float a = (float)in[in0 + c];
            const float b = (float)in[in1 + c];
            out[oi + c] = vj_audio_clip16_from_float((a * a_gain) + (b * b_gain));
        }
    }

    return expected_samples;
}


static inline int16_t vj_audio_cubic_interp_s16(int p0, int p1, int p2, int p3, int frac_q16)
{
    int64_t t  = frac_q16;
    int64_t t2 = (t * t) >> 16;
    int64_t t3 = (t2 * t) >> 16;

    int64_t a0 = -(int64_t)p0 + (3LL * p1) - (3LL * p2) + p3;
    int64_t a1 = (2LL * p0) - (5LL * p1) + (4LL * p2) - p3;
    int64_t a2 = -(int64_t)p0 + p2;

    int64_t y = ((int64_t)2 * p1) << 16;
    y += a2 * t;
    y += a1 * t2;
    y += a0 * t3;
    y >>= 17;

    if (y > 32767)
        y = 32767;
    else if (y < -32768)
        y = -32768;

    return (int16_t)y;
}

int vj_audio_render_slow_stream_s16(uint8_t *dst,
                                    int dst_samples,
                                    const uint8_t *src,
                                    int source_base_sample,
                                    int context_samples,
                                    int slice_count,
                                    int start_stretched_sample,
                                    int frame_bytes)
{
    if (dst == NULL || dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || source_base_sample < 0 || context_samples <= 0 ||
        slice_count <= 1) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    const int words = frame_bytes / (int)sizeof(int16_t);
    if (words <= 0 || (frame_bytes & 1)) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    if (source_base_sample >= context_samples)
        source_base_sample = context_samples - 1;

    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;
    const int max_index = context_samples - 1;

    for (int i = 0; i < dst_samples; i++) {
        int64_t phase_q16 = ((int64_t)start_stretched_sample + (int64_t)i) << 16;
        phase_q16 /= (int64_t)slice_count;
        phase_q16 += ((int64_t)source_base_sample << 16);

        if (phase_q16 < 0)
            phase_q16 = 0;

        int idx = (int)(phase_q16 >> 16);
        int frac = (int)(phase_q16 & 0xffff);

        if (idx < 0) {
            idx = 0;
            frac = 0;
        } else if (idx > max_index) {
            idx = max_index;
            frac = 0;
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
            out[bo + c] = vj_audio_cubic_interp_s16(
                in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac
            );
        }

    }

    return dst_samples;
}


int vj_audio_render_slow_stream_bend_s16(uint8_t *dst,
                                         int dst_samples,
                                         const uint8_t *src,
                                         int source_base_sample,
                                         int context_samples,
                                         int slice_count,
                                         int start_stretched_sample,
                                         int phase_offset_start,
                                         int phase_offset_end,
                                         int frame_bytes)
{
    if (dst == NULL || dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || source_base_sample < 0 || context_samples <= 0 ||
        slice_count <= 1) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    const int words = frame_bytes / (int)sizeof(int16_t);
    if (words <= 0 || (frame_bytes & 1)) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    if (source_base_sample >= context_samples)
        source_base_sample = context_samples - 1;

    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;
    const int max_index = context_samples - 1;
    const int64_t base_q16 = ((int64_t)source_base_sample) << 16;
    const int denom = (dst_samples > 1) ? (dst_samples - 1) : 1;
    const int delta_offset = phase_offset_end - phase_offset_start;

    for (int i = 0; i < dst_samples; i++) {
        const int64_t t = ((int64_t)i << 16) / (int64_t)denom;
        const int64_t t2 = (t * t) >> 16;
        const int64_t t3 = (t2 * t) >> 16;
        int64_t smooth = (3LL * t2) - (2LL * t3);
        if (smooth < 0)
            smooth = 0;
        else if (smooth > 65536)
            smooth = 65536;

        const int64_t offset_q16 = (((int64_t)phase_offset_start) << 16) +
            ((int64_t)delta_offset * smooth);
        int64_t stretched_q16 = (((int64_t)start_stretched_sample + (int64_t)i) << 16) +
            offset_q16;

        int64_t phase_q16 = stretched_q16 / (int64_t)slice_count;
        phase_q16 += base_q16;

        if (phase_q16 < 0)
            phase_q16 = 0;

        int idx = (int)(phase_q16 >> 16);
        int frac = (int)(phase_q16 & 0xffff);

        if (idx < 0) {
            idx = 0;
            frac = 0;
        } else if (idx > max_index) {
            idx = max_index;
            frac = 0;
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
            out[bo + c] = vj_audio_cubic_interp_s16(
                in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac
            );
        }
    }

    return dst_samples;
}


int vj_audio_render_slow_stream_turn_s16(uint8_t *dst,
                                         int dst_samples,
                                         const uint8_t *src,
                                         int source_base_sample,
                                         int context_samples,
                                         int slice_count,
                                         int start_stretched_sample,
                                         int phase_offset_start,
                                         int phase_offset_end,
                                         int frame_bytes)
{
    if (dst == NULL || dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || source_base_sample < 0 || context_samples <= 0 ||
        slice_count <= 1) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    const int words = frame_bytes / (int)sizeof(int16_t);
    if (words <= 0 || (frame_bytes & 1)) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    if (source_base_sample >= context_samples)
        source_base_sample = context_samples - 1;

    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;
    const int max_index = context_samples - 1;
    const int64_t base_q16 = ((int64_t)source_base_sample) << 16;
    const int denom = (dst_samples > 1) ? (dst_samples - 1) : 1;
    const int delta_offset = phase_offset_end - phase_offset_start;

    for (int i = 0; i < dst_samples; i++) {
        const int64_t u = ((int64_t)i << 16) / (int64_t)denom;
        const int64_t u2 = (u * u) >> 16;
        const int64_t u3 = (u2 * u) >> 16;
        const int64_t u4 = (u2 * u2) >> 16;
        const int64_t u5 = (u4 * u) >> 16;

        /*
         * Minimum-jerk phase turn:
         *   p(u) = 3u^5 - 8u^4 + 6u^3
         * gives p(0)=0, p'(0)=0, p''(0)=0 and p(1)=1, p'(1)=1,
         * p''(1)=0.  The block therefore starts with zero source velocity
         * and returns to the normal external audio-slice velocity at the end.
         */
        int64_t turn = (3LL * u5) - (8LL * u4) + (6LL * u3);
        if (turn < 0)
            turn = 0;
        else if (turn > 65536)
            turn = 65536;

        const int64_t sm2 = (3LL * u2) - (2LL * u3);
        int64_t smooth = sm2;
        if (smooth < 0)
            smooth = 0;
        else if (smooth > 65536)
            smooth = 65536;

        const int64_t normal_span = (int64_t)denom;
        const int64_t progress_q16 = normal_span * turn;
        const int64_t offset_q16 = (((int64_t)phase_offset_start) << 16) +
            ((int64_t)delta_offset * smooth);

        int64_t stretched_q16 = (((int64_t)start_stretched_sample) << 16) +
            progress_q16 + offset_q16;

        int64_t phase_q16 = stretched_q16 / (int64_t)slice_count;
        phase_q16 += base_q16;

        if (phase_q16 < 0)
            phase_q16 = 0;

        int idx = (int)(phase_q16 >> 16);
        int frac = (int)(phase_q16 & 0xffff);

        if (idx < 0) {
            idx = 0;
            frac = 0;
        } else if (idx > max_index) {
            idx = max_index;
            frac = 0;
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
            out[bo + c] = vj_audio_cubic_interp_s16(
                in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac
            );
        }
    }

    return dst_samples;
}


int vj_audio_render_slow_stream_velocity_turn_s16(uint8_t *dst,
                                                  int dst_samples,
                                                  const uint8_t *src,
                                                  int source_base_sample,
                                                  int context_samples,
                                                  int slice_count,
                                                  int start_stretched_sample,
                                                  int phase_offset_start,
                                                  int phase_offset_end,
                                                  int frame_bytes)
{
    if (dst == NULL || dst_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || source_base_sample < 0 || context_samples <= 0 ||
        slice_count <= 1) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    const int words = frame_bytes / (int)sizeof(int16_t);
    if (words <= 0 || (frame_bytes & 1)) {
        veejay_memset(dst, 0, dst_samples * frame_bytes);
        return dst_samples;
    }

    if (source_base_sample >= context_samples)
        source_base_sample = context_samples - 1;

    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;
    const int max_index = context_samples - 1;
    const int64_t base_q16 = ((int64_t)source_base_sample) << 16;
    const int denom = (dst_samples > 1) ? (dst_samples - 1) : 1;
    const int delta_offset = phase_offset_end - phase_offset_start;

    for (int i = 0; i < dst_samples; i++) {
        const int64_t u = ((int64_t)i << 16) / (int64_t)denom;
        const int64_t u2 = (u * u) >> 16;
        const int64_t u3 = (u2 * u) >> 16;

        int64_t turn = (-2LL * u3) + (4LL * u2) - u;

        const int64_t sm2 = (3LL * u2) - (2LL * u3);
        int64_t smooth = sm2;
        if (smooth < 0)
            smooth = 0;
        else if (smooth > 65536)
            smooth = 65536;

        const int64_t progress_q16 = ((int64_t)denom * turn);
        const int64_t offset_q16 = (((int64_t)phase_offset_start) << 16) +
            ((int64_t)delta_offset * smooth);

        int64_t stretched_q16 = (((int64_t)start_stretched_sample) << 16) +
            progress_q16 + offset_q16;

        int64_t phase_q16 = stretched_q16 / (int64_t)slice_count;
        phase_q16 += base_q16;

        if (phase_q16 < 0)
            phase_q16 = 0;

        int idx = (int)(phase_q16 >> 16);
        int frac = (int)(phase_q16 & 0xffff);

        if (idx < 0) {
            idx = 0;
            frac = 0;
        } else if (idx > max_index) {
            idx = max_index;
            frac = 0;
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
            out[bo + c] = vj_audio_cubic_interp_s16(
                in[b0 + c], in[b1 + c], in[b2 + c], in[b3 + c], frac
            );
        }
    }

    return dst_samples;
}

int vj_audio_resample_block_s16(uint8_t *dst,
                                int expected_samples,
                                const uint8_t *src,
                                int src_samples,
                                double speed,
                                int frame_bytes)
{
    if (dst == NULL || expected_samples <= 0 || frame_bytes <= 0)
        return 0;

    if (src == NULL || src_samples <= 0 || speed == 0.0) {
        veejay_memset(dst, 0, expected_samples * frame_bytes);
        return expected_samples;
    }

    const int words = frame_bytes / (int)sizeof(int16_t);
    if (words <= 0 || (frame_bytes & 1)) {
        veejay_memset(dst, 0, expected_samples * frame_bytes);
        return expected_samples;
    }

    const int16_t *in = (const int16_t*)src;
    int16_t *out = (int16_t*)dst;

    double rate = (speed < 0.0) ? -speed : speed;
    if (rate < 1.0)
        rate = 1.0;

    const int reverse = (speed < 0.0);

    for (int i = 0; i < expected_samples; i++) {
        double pos = reverse ? ((double)(src_samples - 1) - ((double)i * rate))
                             : ((double)i * rate);

        if (pos < 0.0)
            pos = 0.0;
        else if (pos > (double)(src_samples - 1))
            pos = (double)(src_samples - 1);

        int i0 = (int)pos;
        int i1 = i0 + 1;

        if (i1 >= src_samples)
            i1 = src_samples - 1;

        const float frac = (float)(pos - (double)i0);
        const float a_gain = 1.0f - frac;
        const float b_gain = frac;

        const int in0 = i0 * words;
        const int in1 = i1 * words;
        const int oi = i * words;

        for (int c = 0; c < words; c++) {
            const float a = (float)in[in0 + c];
            const float b = (float)in[in1 + c];
            out[oi + c] = vj_audio_clip16_from_float((a * a_gain) + (b * b_gain));
        }
    }

    return expected_samples;
}
