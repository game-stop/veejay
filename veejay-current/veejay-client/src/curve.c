/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <src/vj-api.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include "curve.h"
#include "common.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int curve_is_empty = 1;

static uint32_t curve_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint32_t curve_seed_mix(uint32_t seed, int value)
{
    uint32_t v = (uint32_t) value;
    return curve_hash_u32(seed ^ (v + 0x9e3779b9u + (seed << 6) + (seed >> 2)));
}

static uint32_t curve_shape_seed(int fx_id, int parameter_id, int start, int end,
                                 int shape, int shape_min, int shape_max,
                                 int bound_min, int bound_max)
{
    uint32_t seed = 0x6d2b79f5u;

    seed = curve_seed_mix(seed, fx_id);
    seed = curve_seed_mix(seed, parameter_id);
    seed = curve_seed_mix(seed, start);
    seed = curve_seed_mix(seed, end);
    seed = curve_seed_mix(seed, shape);
    seed = curve_seed_mix(seed, shape_min);
    seed = curve_seed_mix(seed, shape_max);
    seed = curve_seed_mix(seed, bound_min);
    seed = curve_seed_mix(seed, bound_max);

    return seed ? seed : 0x9e3779b9u;
}

static float curve_rng01(uint32_t *state)
{
    *state += 0x9e3779b9u;
    uint32_t x = curve_hash_u32(*state);
    return (float)((x >> 8) & 0x00ffffffu) * (1.0f / 16777215.0f);
}

static float curve_rng_signed(uint32_t *state)
{
    return curve_rng01(state) - 0.5f;
}

static float curve_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static float curve_repeat_phase(int k, int veclen1, int steps)
{
    if(veclen1 <= 1)
        return 0.0f;

    if(steps <= 1)
        return (float) k / (float)(veclen1 - 1);

    int cycles = steps;
    if(cycles > veclen1)
        cycles = veclen1;

    int base = veclen1 / cycles;
    int rem = veclen1 - (base * cycles);
    int split = (base + 1) * rem;
    int cycle_start;
    int cycle_len;

    if(k < split) {
        cycle_len = base + 1;
        cycle_start = (k / cycle_len) * cycle_len;
    } else {
        cycle_len = base;
        if(cycle_len <= 0)
            return 0.0f;
        cycle_start = split + (((k - split) / cycle_len) * cycle_len);
    }

    if(cycle_len <= 1)
        return 0.0f;

    int local = k - cycle_start;
    if(local < 0)
        local = 0;
    if(local >= cycle_len)
        local = cycle_len - 1;

    return (float) local / (float)(cycle_len - 1);
}


static float curve_repeat_phase_tiled(int k, int veclen1, int steps)
{
    if(steps <= 1)
        return curve_repeat_phase(k, veclen1, steps);

    if(veclen1 <= 1)
        return 0.0f;

    int cycles = steps;
    if(cycles > veclen1)
        cycles = veclen1;

    int base = veclen1 / cycles;
    int rem = veclen1 - (base * cycles);
    int split = (base + 1) * rem;
    int cycle_start;
    int cycle_len;

    if(k < split) {
        cycle_len = base + 1;
        cycle_start = (k / cycle_len) * cycle_len;
    } else {
        cycle_len = base;
        if(cycle_len <= 0)
            return 0.0f;
        cycle_start = split + (((k - split) / cycle_len) * cycle_len);
    }

    if(cycle_len <= 1)
        return 0.0f;

    int local = k - cycle_start;
    if(local < 0)
        local = 0;
    if(local >= cycle_len)
        local = cycle_len - 1;

    return (float) local / (float) cycle_len;
}

static void curve_repeat_span(int veclen1, int steps, int cycle, int *first, int *last)
{
    int cycles = steps;

    if(cycles < 1)
        cycles = 1;

    if(cycles > veclen1)
        cycles = veclen1;

    int base = veclen1 / cycles;
    int rem = veclen1 - (base * cycles);

    if(cycle < rem) {
        *first = cycle * (base + 1);
        *last = *first + base;
    } else {
        *first = rem * (base + 1) + (cycle - rem) * base;
        *last = *first + base - 1;
    }

    if(*first < 0)
        *first = 0;

    if(*last < *first)
        *last = *first;

    if(*last >= veclen1)
        *last = veclen1 - 1;
}

static void curve_normalize_cycle_range(float *vec, int veclen1, int steps,
                                        float target_min, float target_max)
{
    if(!vec || veclen1 <= 0)
        return;

    int cycles = steps;

    if(cycles < 1)
        cycles = 1;

    if(cycles > veclen1)
        cycles = veclen1;

    float clip_min = target_min < target_max ? target_min : target_max;
    float clip_max = target_min < target_max ? target_max : target_min;

    for(int c = 0; c < cycles; c++) {
        int first = 0;
        int last = 0;
        float local_min;
        float local_max;

        curve_repeat_span(veclen1, steps, c, &first, &last);

        local_min = vec[first];
        local_max = vec[first];

        for(int k = first + 1; k <= last; k++) {
            if(vec[k] < local_min)
                local_min = vec[k];

            if(vec[k] > local_max)
                local_max = vec[k];
        }

        if(fabsf(local_max - local_min) <= 0.000001f)
            continue;

        float scale = (target_max - target_min) / (local_max - local_min);

        for(int k = first; k <= last; k++) {
            float v = target_min + (vec[k] - local_min) * scale;
            vec[k] = curve_clampf(v, clip_min, clip_max);
        }
    }
}

static float curve_triangle_phase(float t)
{
    return 1.0f - fabsf(1.0f - 2.0f * t);
}

static float curve_sample_motif_linear(const float *motif, int len, float phase)
{
    if(len <= 1)
        return motif[0];

    phase = curve_clampf(phase, 0.0f, 1.0f);

    float f = phase * (float)(len - 1);
    int a = (int) floorf(f);
    int b = a + 1;

    if(b >= len)
        return motif[len - 1];

    float t = f - (float) a;
    return motif[a] + (motif[b] - motif[a]) * t;
}

static float curve_sample_motif_nearest(const float *motif, int len, float phase)
{
    if(len <= 1)
        return motif[0];

    phase = curve_clampf(phase, 0.0f, 1.0f);

    int idx = (int) floorf((phase * (float)(len - 1)) + 0.5f);
    if(idx < 0)
        idx = 0;
    if(idx >= len)
        idx = len - 1;

    return motif[idx];
}

static void curve_apply_motif_sized(float *vec, const float *motif, int motif_len, int veclen1, int steps, int nearest)
{
    float motif_min = motif[0];
    float motif_max = motif[0];

    for(int k = 1; k < motif_len; k++) {
        if(motif[k] < motif_min)
            motif_min = motif[k];

        if(motif[k] > motif_max)
            motif_max = motif[k];
    }

    for(int k = 0; k < veclen1; k++) {
        float phase = curve_repeat_phase(k, veclen1, steps);
        vec[k] = nearest ? curve_sample_motif_nearest(motif, motif_len, phase)
                         : curve_sample_motif_linear(motif, motif_len, phase);
    }

    if(motif_len > 1 && fabsf(motif_max - motif_min) > 0.000001f)
        curve_normalize_cycle_range(vec, veclen1, steps, motif_min, motif_max);
}

static int curve_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void curve_param_minmax(int fx_id, int parameter_id, int *min, int *max)
{
    if(!min || !max)
        return;

    if(parameter_id == VJ_KF_PARAM_CHAIN_OPACITY) {
        *min = 0;
        *max = 255;
        return;
    }

    _effect_get_minmax(fx_id, min, max, parameter_id);
}

static int curve_detail_len(int veclen1, int detail)
{
    detail = curve_clampi(detail, 2, 256);

    if(detail > veclen1)
        detail = veclen1;

    return detail < 2 ? 2 : detail;
}

int     is_curve_empty(void) {
    return curve_is_empty;
}

void	get_points_from_curve( GtkWidget *curve, int len, float *vec )
{
  gtk3_curve_get_vector( curve, len, vec );
}

void	reset_curve( GtkWidget *curve )
{
  gtk_widget_set_sensitive( curve, TRUE );
  if (!curve_is_empty)
  {
    gtk3_curve_clear(curve);
  }
  gtk3_curve_live_trace_clear(curve);
  curve_is_empty = 1;
  gtk_widget_queue_draw(curve);
}

void set_points_in_curve(Gtk3CurveType type, GtkWidget *curve)
{
    if (curve_is_empty)
        return;

    gtk3_curve_set_curve_type(curve, type);
    curve_is_empty = 0;

    gtk_widget_queue_draw(curve);
}


void set_initial_curve(GtkWidget *curve,
                       int fx_id,
                       int parameter_id,
                       int start,
                       int end,
                       int value,
                       double fps)
{
    int min = 0;
    int max = 0;

    if (!curve)
        return;

    if (end < start) {
        int t = start;
        start = end;
        end = t;
    }

    const int len = end - start + 1;

    if (len <= 0)
        return;

    curve_param_minmax(fx_id, parameter_id, &min, &max);

    value = (value < min) ? min : ((value > max) ? max : value);

    float *vec = (float *) vj_malloc(sizeof(float) * len);
    if (vec == NULL)
        return;

    for (int k = 0; k < len; k++)
        vec[k] = (float) value;

    gtk3_curve_set_fps(curve, fps);
    gtk3_curve_set_range(curve,
                         (gfloat) start,
                         (gfloat) end,
                         (gfloat) min,
                         (gfloat) max);
    gtk3_curve_set_vector(curve, len, vec);
    gtk3_curve_set_curve_type(curve, GTK3_CURVE_TYPE_LINEAR);

    free(vec);

    curve_is_empty = 0;
}

#define KF_PACKED_HEADER_LEN       35
#define KF_PACKED_HEADER_SCAN_FMT  "key%2d%2d%8d%8d%2d%8d%2d"

int set_points_in_curve_ext(GtkWidget *curve,
                            unsigned char *blob,
                            int blen,
                            int id,
                            int fx_entry,
                            int *curve_type,
                            int *shape,
                            int *status,
                            double fps)
{
    (void) fx_entry;

    if (!curve || !blob || !curve_type || !shape || !status)
        return -1;

    if (blen < KF_PACKED_HEADER_LEN)
        return -1;

    int parameter_id = 0;
    int start = 0;
    int end = 0;
    int type = 0;
    int entry = 0;

    char header[KF_PACKED_HEADER_LEN + 1];
    memcpy(header, blob, KF_PACKED_HEADER_LEN);
    header[KF_PACKED_HEADER_LEN] = '\0';

    int n = sscanf(header,
                   KF_PACKED_HEADER_SCAN_FMT,
                   &entry,
                   &parameter_id,
                   &start,
                   &end,
                   &type,
                   shape,
                   status);

    if (n != 7)
        return -1;

    if (start < 0 || end < start)
        return -1;

    const int len = end - start + 1;

    if (len <= 0)
        return -1;

    if (len > (INT_MAX - KF_PACKED_HEADER_LEN) / 4)
        return -1;

    const int expected = KF_PACKED_HEADER_LEN + (4 * len);

    if (blen < expected)
        return -1;

    int min = 0;
    int max = 0;
    curve_param_minmax(id, parameter_id, &min, &max);

    float *vec = (float *) vj_calloc(sizeof(float) * len);
    if (vec == NULL)
        return -1;

    const unsigned char *in = blob + KF_PACKED_HEADER_LEN;

    for (int k = 0; k < len; k++) {
        const unsigned char *ptr = in + (k * 4);

        int value =
            ((int) ptr[0]) |
            ((int) ptr[1] << 8) |
            ((int) ptr[2] << 16) |
            ((int) ptr[3] << 24);

        vec[k] = (float) value;
    }

    switch (type) {
        case 1:
            *curve_type = GTK3_CURVE_TYPE_SPLINE;
            break;
        case 2:
            *curve_type = GTK3_CURVE_TYPE_FREE;
            break;
        default:
            *curve_type = GTK3_CURVE_TYPE_LINEAR;
            break;
    }

    gtk3_curve_reset(curve);
    gtk3_curve_set_fps(curve, fps);
    gtk3_curve_set_range(curve,
                         (gfloat) start,
                         (gfloat) end,
                         (gfloat) min,
                         (gfloat) max);
    gtk3_curve_set_vector(curve, len, vec);
    gtk3_curve_set_curve_type(curve, (Gtk3CurveType) *curve_type);

    free(vec);

    curve_is_empty = 0;

    return parameter_id;
}

void curve_set_position( GtkWidget *curve, double pos)
{
    gtk3_curve_set_position( curve, pos);
}

void curve_set_predefined_shape(GtkWidget *curve, int fx_id, int parameter_id,
                                int start, int end, int shape,
                                int bound_min, int bound_max,
                                int steps, int seed, int detail,
                                gboolean reverse, double fps)
{
    if(shape == FX_ANIM_SHAPE_NO_SHAPE || shape < 0 || shape >= FX_ANIM_SHAPE_MAX || fx_id < 0 || parameter_id < 0)
    {
        if(curve)
            gtk3_curve_clear(curve);
        curve_is_empty = 1;
        if(curve)
            gtk_widget_queue_draw(curve);
        return;
    }

    int param_min = 0;
    int param_max = 0;

    curve_param_minmax(fx_id, parameter_id, &param_min, &param_max);

    if(end < start)
        return;

    if(steps < 1)
        steps = 1;

    detail = curve_clampi(detail, 1, 256);

    if(bound_min > bound_max) {
        int tmp = bound_min;
        bound_min = bound_max;
        bound_max = tmp;
    }

    int param_diff = param_max - param_min;

    int shape_min = ((param_diff / 100.0f) * bound_min) + param_min;
    int shape_max = ((param_diff / 100.0f) * bound_max) + param_min;
    int shape_diff = shape_max - shape_min;

    int veclen1 = end - start + 1;

    if(veclen1 <= 0)
        return;

    float *vec = (float*) vj_calloc(sizeof(float) * veclen1);
    if(vec == NULL)
        return;

    if(shape_diff == 0) {
        for(int k = 0; k < veclen1; k++)
            vec[k] = shape_min;

        gtk3_curve_reset(curve);
        gtk3_curve_set_fps(curve, fps);
        gtk3_curve_set_range(curve,
                             (gfloat) start,
                             (gfloat) end,
                             (gfloat) param_min,
                             (gfloat) param_max);
        gtk3_curve_set_curve_type(curve, GTK3_CURVE_TYPE_FREE);
        gtk3_curve_set_vector(curve, veclen1, vec);
        gtk_widget_queue_draw(curve);

        curve_is_empty = 0;

        free(vec);
        return;
    }

    uint32_t random_seed = curve_shape_seed(fx_id,
                                            parameter_id,
                                            start,
                                            end,
                                            shape,
                                            shape_min,
                                            shape_max,
                                            bound_min,
                                            bound_max);

    random_seed = curve_seed_mix(random_seed, seed);
    int motif_len = curve_detail_len(veclen1, detail);

    switch(shape)
    {
        case FX_ANIM_SHAPE_ZIGZAG:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase_tiled(k, veclen1, steps);
                vec[k] = shape_min + shape_diff * curve_triangle_phase(t);
            }
            break;

        case FX_ANIM_SHAPE_SINE:
        {
            float midpoint = (shape_max + shape_min) * 0.5f;
            float radius = (shape_max - shape_min) * 0.5f;

            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase_tiled(k, veclen1, steps);
                vec[k] = midpoint + radius * sinf(2.0f * M_PI * t);
            }
        }
        break;

        case FX_ANIM_SHAPE_COSINE:
        {
            float midpoint = (shape_max + shape_min) * 0.5f;
            float radius = (shape_max - shape_min) * 0.5f;

            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase_tiled(k, veclen1, steps);
                vec[k] = midpoint + radius * cosf(2.0f * M_PI * t);
            }
        }
        break;

        case FX_ANIM_SHAPE_SAWTOOTH:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                vec[k] = shape_min + shape_diff * t;
            }
            break;

        case FX_ANIM_SHAPE_SQUARE:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                vec[k] = (t < 0.5f) ? shape_max : shape_min;
            }
            break;

        case FX_ANIM_SHAPE_BOUNCE:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase_tiled(k, veclen1, steps);
                float b = fabsf(1.0f - 2.0f * t);
                float bounce = 1.0f - (b * b);
                vec[k] = shape_min + shape_diff * bounce;
            }
            break;

        case FX_ANIM_SHAPE_NOISE:
        {
            float *motif = (float *) vj_calloc(sizeof(float) * motif_len);
            if(!motif) {
                free(vec);
                return;
            }

            uint32_t rng = random_seed ^ 0x4f1bbcdcu;

            for(int k = 0; k < motif_len; k++) {
                float random_factor = curve_rng01(&rng);
                motif[k] = shape_min + (shape_diff * random_factor);
            }

            curve_apply_motif_sized(vec, motif, motif_len, veclen1, steps, 1);
            free(motif);
        }
        break;

        case FX_ANIM_SHAPE_SMOOTHSTEP:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                float smooth_factor = t * t * (3.0f - 2.0f * t);
                vec[k] = shape_min + (shape_diff * smooth_factor);
            }
            break;

        case FX_ANIM_SHAPE_RANDOMWALK:
        {
            float *motif = (float *) vj_calloc(sizeof(float) * motif_len);
            if(!motif) {
                free(vec);
                return;
            }

            uint32_t rng = random_seed ^ 0xa5a5c3c3u;
            float value = (shape_min + shape_max) * 0.5f;
            float step_scale = shape_diff * 0.05f;

            for(int k = 0; k < motif_len; k++) {
                float step = curve_rng_signed(&rng);
                value += step * step_scale;

                if(value < shape_min) value = shape_min + (shape_min - value);
                if(value > shape_max) value = shape_max - (value - shape_max);

                value = curve_clampf(value, (float) shape_min, (float) shape_max);
                motif[k] = value;
            }

            curve_apply_motif_sized(vec, motif, motif_len, veclen1, steps, 0);
            free(motif);
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_INERTIA:
        {
            float *motif = (float *) vj_calloc(sizeof(float) * motif_len);
            if(!motif) {
                free(vec);
                return;
            }

            uint32_t rng = random_seed ^ 0x9b9773e1u;
            float value = (shape_min + shape_max) * 0.5f;
            float velocity = 0.0f;
            float accel_scale = shape_diff * 0.02f;
            float damping = 0.90f;

            for(int k = 0; k < motif_len; k++) {
                float accel = curve_rng_signed(&rng) * accel_scale;

                velocity += accel;
                velocity *= damping;
                value += velocity;

                if(value < shape_min) {
                    value = shape_min;
                    velocity *= -0.5f;
                }

                if(value > shape_max) {
                    value = shape_max;
                    velocity *= -0.5f;
                }

                motif[k] = value;
            }

            curve_apply_motif_sized(vec, motif, motif_len, veclen1, steps, 0);
            free(motif);
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_MEAN:
        {
            float *motif = (float *) vj_calloc(sizeof(float) * motif_len);
            if(!motif) {
                free(vec);
                return;
            }

            uint32_t rng = random_seed ^ 0x27d4eb2fu;
            float value = (shape_min + shape_max) * 0.5f;
            float mean = value;
            float step_scale = shape_diff * 0.04f;
            float pull = 0.05f;

            for(int k = 0; k < motif_len; k++) {
                float noise = curve_rng_signed(&rng) * step_scale;
                value += noise + (mean - value) * pull;
                value = curve_clampf(value, (float) shape_min, (float) shape_max);
                motif[k] = value;
            }

            curve_apply_motif_sized(vec, motif, motif_len, veclen1, steps, 0);
            free(motif);
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_QUANTIZED:
        {
            float *motif = (float *) vj_calloc(sizeof(float) * motif_len);
            if(!motif) {
                free(vec);
                return;
            }

            uint32_t rng = random_seed ^ 0x85ebca6bu;
            float value = (shape_min + shape_max) * 0.5f;
            float step_scale = shape_diff * 0.05f;
            int levels = curve_clampi(detail, 2, 64);

            for(int k = 0; k < motif_len; k++) {
                float step = curve_rng_signed(&rng) * step_scale;
                value += step;
                value = curve_clampf(value, (float) shape_min, (float) shape_max);

                float norm = (value - shape_min) / (float) shape_diff;
                norm = floorf(norm * levels) / (float)(levels - 1);
                value = curve_clampf(shape_min + norm * shape_diff,
                                     (float) shape_min,
                                     (float) shape_max);

                motif[k] = value;
            }

            curve_apply_motif_sized(vec, motif, motif_len, veclen1, steps, 1);
            free(motif);
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_BURST:
        {
            float *motif = (float *) vj_calloc(sizeof(float) * motif_len);
            if(!motif) {
                free(vec);
                return;
            }

            uint32_t rng = random_seed ^ 0xc2b2ae35u;
            float value = (shape_min + shape_max) * 0.5f;
            float small_step = shape_diff * 0.01f;
            float big_step = shape_diff * 0.8f;
            int burst_at = motif_len > 1 ? (int)(curve_rng01(&rng) * (float) motif_len) : 0;

            if(burst_at >= motif_len)
                burst_at = motif_len - 1;

            for(int k = 0; k < motif_len; k++) {
                float step;

                if(k == burst_at)
                    step = curve_rng_signed(&rng) * big_step;
                else
                    step = curve_rng_signed(&rng) * small_step;

                value += step;
                value = curve_clampf(value, (float) shape_min, (float) shape_max);
                motif[k] = value;
            }

            curve_apply_motif_sized(vec, motif, motif_len, veclen1, steps, 0);
            free(motif);
        }
        break;

        case FX_ANIM_SHAPE_RANDOMWALK_SMOOTH:
        {
            float *motif = (float *) vj_calloc(sizeof(float) * motif_len);
            if(!motif) {
                free(vec);
                return;
            }

            uint32_t rng = random_seed ^ 0x165667b1u;
            float value = (shape_min + shape_max) * 0.5f;
            float step_scale = shape_diff * 0.05f;
            float smooth = 0.85f;

            for(int k = 0; k < motif_len; k++) {
                float step = curve_rng_signed(&rng) * step_scale;
                float target = value + step;

                value = value * smooth + target * (1.0f - smooth);
                value = curve_clampf(value, (float) shape_min, (float) shape_max);
                motif[k] = value;
            }

            curve_apply_motif_sized(vec, motif, motif_len, veclen1, steps, 0);
            free(motif);
        }
        break;

        case FX_ANIM_SHAPE_GAUSSIAN:
        {
            float sigma = 0.25f;
            float g0 = expf(-0.5f * (0.5f / sigma) * (0.5f / sigma));

            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase_tiled(k, veclen1, steps);
                float x = (t - 0.5f) / sigma;
                float g = expf(-0.5f * x * x);
                float normalized = (g - g0) / (1.0f - g0);

                vec[k] = shape_min + shape_diff * normalized;
            }
        }
        break;

        case FX_ANIM_SHAPE_EXPONENTIAL:
        {
            float denom = expf(4.0f) - 1.0f;

            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                float expv = (expf(4.0f * t) - 1.0f) / denom;
                vec[k] = shape_min + shape_diff * expv;
            }
        }
        break;

        case FX_ANIM_SHAPE_EASE_IN:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                float v = t * t;
                vec[k] = shape_min + shape_diff * v;
            }
            break;

        case FX_ANIM_SHAPE_EASE_OUT:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                float v = 1.0f - (1.0f - t) * (1.0f - t);
                vec[k] = shape_min + shape_diff * v;
            }
            break;

        case FX_ANIM_SHAPE_PULSE:
        {
            float duty = 0.2f;

            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                vec[k] = (t < duty) ? shape_max : shape_min;
            }
        }
        break;

        case FX_ANIM_SHAPE_DAMPED_SINE:
        {
            float midpoint = (shape_max + shape_min) * 0.5f;
            float radius = (shape_max - shape_min) * 0.5f;
            float damping = 3.0f;

            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase_tiled(k, veclen1, steps);
                float env = expf(-damping * t);
                float v = sinf(2.0f * M_PI * t);
                vec[k] = midpoint + radius * v * env;
            }
        }
        break;

        case FX_ANIM_SHAPE_SMOOTH_NOISE:
        {
            float *motif = (float *) vj_calloc(sizeof(float) * motif_len);
            if(!motif) {
                free(vec);
                return;
            }

            uint32_t rng = random_seed ^ 0xd1b54a32u;
            float last = (shape_min + shape_max) * 0.5f;

            for(int k = 0; k < motif_len; k++) {
                float rnd = curve_rng01(&rng);
                float target = shape_min + shape_diff * rnd;
                last = last * 0.85f + target * 0.15f;
                motif[k] = last;
            }

            curve_apply_motif_sized(vec, motif, motif_len, veclen1, steps, 0);
            free(motif);
        }
        break;

        case FX_ANIM_SHAPE_STEPS:
        {
            int levels = curve_clampi(detail, 2, 64);

            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                int level = (t >= 1.0f) ? (levels - 1) : (int) floorf(t * (float) levels);
                float q = (float) level / (float)(levels - 1);
                vec[k] = shape_min + shape_diff * q;
            }
        }
        break;

        case FX_ANIM_SHAPE_RAMP_DROP:
        {
            float denom = expf(4.0f) - 1.0f;

            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                float v = (expf(4.0f * t) - 1.0f) / denom;
                vec[k] = shape_min + shape_diff * v;
            }
        }
        break;

        case FX_ANIM_SHAPE_BURST_ENVELOPE:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase_tiled(k, veclen1, steps);
                float v;

                if(t < 0.1f) {
                    v = t / 0.1f;
                } else {
                    float d = (t - 0.1f) / 0.9f;
                    v = expf(-5.0f * d);
                }

                vec[k] = shape_min + shape_diff * v;
            }
            break;

        case FX_ANIM_SHAPE_NO_SHAPE:
            free(vec);
            if(curve)
                gtk3_curve_clear(curve);
            curve_is_empty = 1;
            if(curve)
                gtk_widget_queue_draw(curve);
            return;

        default:
            for(int k = 0; k < veclen1; k++) {
                float t = curve_repeat_phase(k, veclen1, steps);
                vec[k] = shape_min + shape_diff * t;
            }
            break;
    }

    switch(shape)
    {
        case FX_ANIM_SHAPE_ZIGZAG:
        case FX_ANIM_SHAPE_BOUNCE:
        case FX_ANIM_SHAPE_GAUSSIAN:
        case FX_ANIM_SHAPE_SINE:
        case FX_ANIM_SHAPE_COSINE:
        case FX_ANIM_SHAPE_BURST_ENVELOPE:
            curve_normalize_cycle_range(vec, veclen1, steps, (float) shape_min, (float) shape_max);
            break;

        case FX_ANIM_SHAPE_DAMPED_SINE:
        {
            float midpoint = (shape_max + shape_min) * 0.5f;
            float radius = (shape_max - shape_min) * 0.5f;
            float damping = 3.0f;
            float omega = 2.0f * M_PI;
            float peak_phase = atanf(omega / damping) / omega;
            float trough_phase = (atanf(omega / damping) + M_PI) / omega;
            float peak_value = midpoint + radius * sinf(omega * peak_phase) * expf(-damping * peak_phase);
            float trough_value = midpoint + radius * sinf(omega * trough_phase) * expf(-damping * trough_phase);

            curve_normalize_cycle_range(vec, veclen1, steps, trough_value, peak_value);
        }
        break;

        default:
            break;
    }

    if(reverse) {
        for(int k = 0; k < veclen1; k++)
            vec[k] = shape_max - vec[k] + shape_min;
    }

    gtk3_curve_reset(curve);
    gtk3_curve_set_fps(curve, fps);
    gtk3_curve_set_range(curve,
                         (gfloat) start,
                         (gfloat) end,
                         (gfloat) param_min,
                         (gfloat) param_max);
    gtk3_curve_set_curve_type(curve, GTK3_CURVE_TYPE_FREE);
    gtk3_curve_set_vector(curve, veclen1, vec);

    gtk_widget_queue_draw(curve);

    curve_is_empty = 0;

    free(vec);
}
