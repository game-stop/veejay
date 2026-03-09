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
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vims.h>
#include <libveejay/audioscratcher.h>
#include <math.h>

#define MAX_CHANNELS 8
#define MAX_SCRATCH_HISTORY_SEC 300 // just a big buffer

typedef struct {

    short *buffer;
    int buffer_frames;
    int64_t write_pos;

    double read_pos;
    double speed;
    double prev_speed;
    int last_dir;

    int channels;
    int sample_rate;

    float envelope_vol; // volume envelope ?

    short last_samples[MAX_CHANNELS];

} vj_scratch_t;


void* vj_scratch_init(int channels, int sample_rate, float fps)
{
    vj_scratch_t *s = vj_calloc(sizeof(vj_scratch_t));
    if (!s) return NULL;

    s->channels = channels;
    s->sample_rate = sample_rate;

    s->buffer_frames = ceil(sample_rate * MAX_SCRATCH_HISTORY_SEC * MAX_SPEED_AV * 1.1);
    s->buffer = vj_calloc(s->buffer_frames * channels * sizeof(short));

    if (!s->buffer) {
        free(s);
        return NULL;
    }

    s->read_pos  = -1.0;
    
    return s;
}


static void vj_scratch_write(void *ptr,
                             const short *input,
                             int frames)
{
    vj_scratch_t *s = (vj_scratch_t*) ptr;
    int buf_frames = s->buffer_frames;
    short *buf = s->buffer;
    int wp = s->write_pos;

    int linear = buf_frames - wp;

    if (frames <= linear) {
        for (int i = 0; i < frames; i++) {
            int idx = (wp + i) * 2;
            buf[idx + 0] = input[i * 2 + 0]; // left
            buf[idx + 1] = input[i * 2 + 1]; // right
        }
        wp += frames;
    } else {
        // write first segment until end of buffer
        for (int i = 0; i < linear; i++) {
            int idx = (wp + i) * 2;
            buf[idx + 0] = input[i * 2 + 0];
            buf[idx + 1] = input[i * 2 + 1];
        }

        // write remaining frames from start of buffer
        int rem = frames - linear;
        for (int i = 0; i < rem; i++) {
            int idx = i * 2;
            buf[idx + 0] = input[(linear + i) * 2 + 0];
            buf[idx + 1] = input[(linear + i) * 2 + 1];
        }
        wp = rem;
    }

    s->write_pos = wp; // update scratch state
}

static inline float hamming_frac(float frac)
{
    return 0.54 - 0.46 * cosf(M_PI * frac);
}

static inline float cubic_interp(float y0, float y1, float y2, float y3, float frac)
{
    float a0 = y3 - y2 - y0 + y1;
    float a1 = y0 - y1 - a0;
    float a2 = y2 - y0;
    float a3 = y1;

    return ((a0 * frac + a1) * frac + a2) * frac + a3;
}

static inline float hermite_interp_tension(float y0, float y1,
                                           float y2, float y3,
                                           float frac,
                                           float tension)
{  
    float m1 = (1.0f - tension) * 0.5f * (y2 - y0);
    float m2 = (1.0f - tension) * 0.5f * (y3 - y1);

    float t  = frac;
    float t2 = t * t;
    float t3 = t2 * t;

    float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 =        t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 =        t3 -       t2;

    return h00 * y1 + h10 * m1 + h01 * y2 + h11 * m2;
}

static inline short clip_int16(int val)
{
    if (val < -32768) return -32768;
    if (val >  32767) return  32767;
    return (short)val;
}

static inline int safe_wrap(int index, int max) {
    int r = index % max;
    return (r < 0) ? (r + max) : r;
}

static inline int clamp(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// phase locked audio scratcher for veejay
// caller must feed it enough samples (for speed up, n_samples * speed)
// and provide a big enough buffer (for slow down, n_samples * duplication_factor)
// FIXME: discontinuity handling & tape deck like speed ramps
int vj_scratch_process(void *ptr,
                       short *output,
                       int max_out_frames,
                       const short *input,
                       int src_frames,
                       double speed)
{
    vj_scratch_t *s = (vj_scratch_t*)ptr;

    int direction = (speed >= 0.0) ? 1 : -1;
    int ch = s->channels;

    if (input && src_frames > 0) {
        vj_scratch_write(s, input, src_frames);
    }

    double abs_speed = fabs(speed);
    int expected_out_frames;
      if (abs_speed < 1.0)
        expected_out_frames = ceil(src_frames * (1.0 / abs_speed));
    else
        expected_out_frames = (int)floor(src_frames / abs_speed);
    float tension = 0.5f + 0.5f * (abs_speed / (abs_speed + 1.0));
    //float tension = 0.6f + 0.4f * (float)(abs_speed / (abs_speed + 0.5)); vinyll mode

    if( expected_out_frames > max_out_frames ) {
        expected_out_frames = max_out_frames;
        veejay_msg(VEEJAY_MSG_ERROR, "Buffer overrun protection, capped to output %d frames", max_out_frames);
    }
    
    // phase-locked read pointer
    // increment is based on exact ratio of frames
    double read_inc = (double)src_frames / (double)expected_out_frames;
  
    if (direction == 1) {
        // forward: snap if outside newly written chunk
        if (s->read_pos < (double)(s->write_pos - src_frames) ||
            s->read_pos > (double)s->write_pos)
        {
            s->read_pos = (double)(s->write_pos - src_frames);
        }
    } else {
        // reverse: snap if outside newly written chunk
        double block_start = (double)(s->write_pos - src_frames);
        double block_end   = (double)(s->write_pos - 1);

        if (s->read_pos < block_start || s->read_pos > block_end) {
            s->read_pos = block_end; // always start at end of new block
        }

        read_inc = -fabs(read_inc); // preserve negative increment
    }

    double max_read = (double)(s->write_pos - 1);
    double min_read = fmax(0.0, (double)(s->write_pos - s->buffer_frames));

    // FIXME change buffer mechanism
    // change to incoming audio -> sliding buffer -> continous read head -> interpolate -> output
    for (int i = 0; i < expected_out_frames; i++) {

        if (s->read_pos > max_read) s->read_pos = max_read; // clamping -> repeats sample FIXME
        if (s->read_pos < min_read) s->read_pos = min_read;

        int base = (int)floor(s->read_pos);
        float frac = (float)(s->read_pos - base);

        int i0 = base - 1;
        int i1 = base;
        int i2 = base + 1;
        int i3 = base + 2;

        if (direction < 0) {
            int block_start = (int)(s->write_pos - src_frames);
            int block_end   = (int)(s->write_pos - 1);

            i0 = clamp(i0, block_start, block_end);
            i1 = clamp(i1, block_start, block_end);
            i2 = clamp(i2, block_start, block_end);
            i3 = clamp(i3, block_start, block_end);
        } else {
            i0 = safe_wrap(i0, s->buffer_frames);
            i1 = safe_wrap(i1, s->buffer_frames);
            i2 = safe_wrap(i2, s->buffer_frames);
            i3 = safe_wrap(i3, s->buffer_frames);
        }

        for (int c = 0; c < ch; c++) {
            float y0 = s->buffer[i0 * ch + c];
            float y1 = s->buffer[i1 * ch + c];
            float y2 = s->buffer[i2 * ch + c];
            float y3 = s->buffer[i3 * ch + c];

            float sample = hermite_interp_tension(y0, y1, y2, y3, frac, tension);
            
            if (i == 0 && s->last_dir != direction) {
                sample = 0.5f * sample + 0.5f * s->last_samples[c];
            }

            short out = clip_int16((int)sample);
            s->last_samples[c] = out;
            output[i*ch +c] = out;
        }

        s->read_pos += read_inc;
    }

    s->last_dir = direction;

    return expected_out_frames;
}
