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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define CHRONOSILT_PARAMS 8

#define P_THRESHOLD      0
#define P_FLOW           1
#define P_EROSION        2
#define P_DECAY          3
#define P_SEDIMENT       4
#define P_SOURCE_BLEED   5
#define P_COLOR_MODE     6
#define P_TURBULENCE     7

#define CS_COLOR_SILT     0
#define CS_COLOR_THERMAL  1
#define CS_COLOR_SOURCE   2
#define CS_COLOR_ELECTRIC 3
#define CS_COLOR_WHITE    4

#define CS_ACTIVITY_GATE  6

typedef struct {
    int w;
    int h;
    int len;
    int seeded;
    int frame;
    int n_threads;

    uint8_t *prev_y;
    uint8_t *ref_y;

    uint8_t *mass;
    uint8_t *next_mass;

    uint8_t *bed;
    uint8_t *next_bed;

    uint8_t *polarity;
    uint8_t *next_polarity;

    uint8_t event_lut[256];
    uint8_t mass_decay_lut[256];
    uint8_t bed_decay_lut[256];
    uint8_t transport_lut[256];
    uint8_t turbulence_lut[256];
    uint8_t erosion_lut[256];
    uint8_t deposit_lut[256];
    uint8_t bleed_y_lut[256];
    uint8_t bleed_uv_lut[256];
    uint8_t adapt_lut[256];

    int lut_valid;
    int last_threshold;
    int last_flow;
    int last_erosion;
    int last_decay;
    int last_sediment;
    int last_source_bleed;
    int last_turbulence;
} chronoglass_t;

static inline int cs_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int cs_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline uint8_t cs_blend_fast_u8(uint8_t a, uint8_t b, int amount)
{
    return (uint8_t) (((int) a * (256 - amount) + (int) b * amount) >> 8);
}

static inline int cs_index_clamped(int w, int h, int x, int y)
{
    if(x < 0)
        x = 0;
    else if(x >= w)
        x = w - 1;

    if(y < 0)
        y = 0;
    else if(y >= h)
        y = h - 1;

    return y * w + x;
}

vj_effect *chronoglass_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = CHRONOSILT_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_THRESHOLD] = 0;
    ve->limits[1][P_THRESHOLD] = 255;
    ve->defaults[P_THRESHOLD] = 18;

    ve->limits[0][P_FLOW] = 0;
    ve->limits[1][P_FLOW] = 255;
    ve->defaults[P_FLOW] = 150;

    ve->limits[0][P_EROSION] = 0;
    ve->limits[1][P_EROSION] = 255;
    ve->defaults[P_EROSION] = 80;

    ve->limits[0][P_DECAY] = 0;
    ve->limits[1][P_DECAY] = 255;
    ve->defaults[P_DECAY] = 205;

    ve->limits[0][P_SEDIMENT] = 0;
    ve->limits[1][P_SEDIMENT] = 255;
    ve->defaults[P_SEDIMENT] = 165;

    ve->limits[0][P_SOURCE_BLEED] = 0;
    ve->limits[1][P_SOURCE_BLEED] = 255;
    ve->defaults[P_SOURCE_BLEED] = 18;

    ve->limits[0][P_COLOR_MODE] = 0;
    ve->limits[1][P_COLOR_MODE] = 4;
    ve->defaults[P_COLOR_MODE] = CS_COLOR_SILT;

    ve->limits[0][P_TURBULENCE] = 0;
    ve->limits[1][P_TURBULENCE] = 255;
    ve->defaults[P_TURBULENCE] = 52;

    ve->description = "Chronosilt";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Flow",
        "Erosion",
        "Decay",
        "Sediment",
        "Source Bleed",
        "Color Mode",
        "Turbulence"
    );

    (void) w;
    (void) h;

    return ve;
}

void *chronoglass_malloc(int w, int h)
{
    chronoglass_t *c;

    if(w <= 0 || h <= 0)
        return NULL;

    c = (chronoglass_t *) vj_calloc(sizeof(chronoglass_t));
    if(!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->seeded = 0;
    c->frame = 0;
    c->lut_valid = 0;

    c->n_threads = vje_advise_num_threads(w * h);
    if(c->n_threads <= 0)
        c->n_threads = 1;

    c->prev_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->ref_y  = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->mass      = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->next_mass = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->bed      = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->next_bed = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->polarity      = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->next_polarity = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    if(!c->prev_y || !c->ref_y ||
       !c->mass || !c->next_mass ||
       !c->bed || !c->next_bed ||
       !c->polarity || !c->next_polarity) {
        if(c->prev_y)
            free(c->prev_y);
        if(c->ref_y)
            free(c->ref_y);
        if(c->mass)
            free(c->mass);
        if(c->next_mass)
            free(c->next_mass);
        if(c->bed)
            free(c->bed);
        if(c->next_bed)
            free(c->next_bed);
        if(c->polarity)
            free(c->polarity);
        if(c->next_polarity)
            free(c->next_polarity);

        free(c);
        return NULL;
    }

    return (void *) c;
}

void chronoglass_free(void *ptr)
{
    chronoglass_t *c = (chronoglass_t *) ptr;

    if(!c)
        return;

    if(c->prev_y)
        free(c->prev_y);
    if(c->ref_y)
        free(c->ref_y);
    if(c->mass)
        free(c->mass);
    if(c->next_mass)
        free(c->next_mass);
    if(c->bed)
        free(c->bed);
    if(c->next_bed)
        free(c->next_bed);
    if(c->polarity)
        free(c->polarity);
    if(c->next_polarity)
        free(c->next_polarity);

    free(c);
}

static void cs_seed(chronoglass_t *c, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        c->prev_y[i] = Y[i];
        c->ref_y[i] = Y[i];

        c->mass[i] = 0;
        c->next_mass[i] = 0;

        c->bed[i] = 0;
        c->next_bed[i] = 0;

        c->polarity[i] = 128;
        c->next_polarity[i] = 128;
    }

    c->seeded = 1;
}

static void cs_build_luts_if_needed(chronoglass_t *c,
                                    int threshold,
                                    int flow,
                                    int erosion,
                                    int decay,
                                    int sediment,
                                    int source_bleed,
                                    int turbulence)
{
    int i;
    int denom;

    int transport_power;
    int turbulence_power;
    int erosion_power;
    int bed_decay_power;
    int deposit_power;

    if(c->lut_valid &&
       c->last_threshold == threshold &&
       c->last_flow == flow &&
       c->last_erosion == erosion &&
       c->last_decay == decay &&
       c->last_sediment == sediment &&
       c->last_source_bleed == source_bleed &&
       c->last_turbulence == turbulence) {
        return;
    }

    denom = 255 - threshold;
    if(denom < 1)
        denom = 1;

    transport_power = ((64 + flow) * decay + 127) / 255;
    if(transport_power > 255)
        transport_power = 255;

    if(turbulence > 0) {
        turbulence_power = 48 + ((turbulence * 207 + 127) / 255);
        turbulence_power = (turbulence_power * (192 + (decay >> 2)) + 127) / 255;

        if(turbulence_power > 255)
            turbulence_power = 255;
    }
    else {
        turbulence_power = 0;
    }

    if(erosion > 0)
        erosion_power = 24 + ((erosion * 231 + 127) / 255);
    else
        erosion_power = 0;

    bed_decay_power = decay + ((255 - decay) >> 2);
    if(bed_decay_power > 255)
        bed_decay_power = 255;

    deposit_power = (sediment * (128 + (flow >> 1)) + 127) / 255;
    if(deposit_power > 255)
        deposit_power = 255;

    for(i = 0; i < 256; i++) {
        int event_strength = 0;
        int excess;
        int gain;
        int mem;

        if(i > threshold) {
            excess = i - threshold;
            gain = 128 + (sediment >> 1) + (erosion >> 2);

            event_strength = (excess * gain + denom / 2) / denom;
            if(event_strength > 255)
                event_strength = 255;
        }

        c->event_lut[i] = (uint8_t) event_strength;
        c->mass_decay_lut[i] = (uint8_t) ((i * decay + 127) / 255);
        c->bed_decay_lut[i]  = (uint8_t) ((i * bed_decay_power + 127) / 255);

        c->transport_lut[i]  = (uint8_t) ((i * transport_power + 127) / 255);
        c->turbulence_lut[i] = (uint8_t) ((i * turbulence_power + 127) / 255);
        c->erosion_lut[i]    = (uint8_t) ((i * erosion_power + 127) / 255);
        c->deposit_lut[i]    = (uint8_t) ((i * deposit_power + 127) / 255);

        c->bleed_y_lut[i] =
            (uint8_t) ((i * source_bleed + 127) / 255);

        c->bleed_uv_lut[i] =
            (uint8_t) ((128 * (255 - source_bleed) + i * source_bleed + 127) / 255);

        mem = 8 + ((255 - decay) >> 3) + (i >> 4);
        if(mem > 255)
            mem = 255;

        c->adapt_lut[i] = (uint8_t) mem;
    }

    c->last_threshold = threshold;
    c->last_flow = flow;
    c->last_erosion = erosion;
    c->last_decay = decay;
    c->last_sediment = sediment;
    c->last_source_bleed = source_bleed;
    c->last_turbulence = turbulence;
    c->lut_valid = 1;
}

static inline int cs_lateral_from_gradient(uint8_t *restrict Y, int pos)
{
    int gx = (int) Y[pos + 1] - (int) Y[pos - 1];

    if(gx > 10)
        return -1;

    if(gx < -10)
        return 1;

    return 0;
}

static inline int cs_edge_safe(uint8_t *restrict Y,
                               int w,
                               int h,
                               int x,
                               int y,
                               int pos)
{
    int gx = 0;
    int gy = 0;

    if(x > 0 && x + 1 < w)
        gx = cs_absi((int) Y[pos - 1] - (int) Y[pos + 1]);

    if(y > 0 && y + 1 < h)
        gy = cs_absi((int) Y[pos - w] - (int) Y[pos + w]);

    return gx > gy ? gx : gy;
}

static inline int cs_fall_distance(int flow)
{
    return (flow * 3 + 127) / 255;
}

static inline int cs_turbulence_step(int turbulence)
{
    if(turbulence <= 0)
        return 1;

    return 1 + ((turbulence * 3 + 127) / 255);
}

static inline int cs_turbulence_dir(int x, int y, int frame)
{
    return (((x >> 2) ^ (y >> 2) ^ frame) & 1) ? 1 : -1;
}

static inline void cs_compute_one_safe(chronoglass_t *c,
                                       uint8_t *restrict Y,
                                       int x,
                                       int y,
                                       int pos,
                                       int flow,
                                       int erosion,
                                       int sediment,
                                       int turbulence)
{
    int w = c->w;
    int h = c->h;

    uint8_t *restrict M = c->mass;
    uint8_t *restrict NM = c->next_mass;
    uint8_t *restrict B = c->bed;
    uint8_t *restrict NB = c->next_bed;
    uint8_t *restrict P = c->polarity;
    uint8_t *restrict NP = c->next_polarity;
    uint8_t *restrict REF = c->ref_y;
    uint8_t *restrict PREV = c->prev_y;

    uint8_t cy = Y[pos];

    int ref = REF[pos];
    int diff_raw = (int) cy - ref;
    int diff_ref = diff_raw < 0 ? -diff_raw : diff_raw;
    int diff_prev = cs_absi((int) cy - (int) PREV[pos]);
    int diff = diff_prev > diff_ref ? diff_prev : diff_ref;

    int event_strength = c->event_lut[diff];
    int event_for_adapt = event_strength;

    int fall = cs_fall_distance(flow);
    int lateral = 0;

    int src;
    int src_mass;
    int transport;

    int mass = c->mass_decay_lut[M[pos]];
    int bed = c->bed_decay_lut[B[pos]];
    int pol = P[pos];

    int edge = 0;
    int cavity;
    int carve;
    int deposit;

    if(x > 0 && x + 1 < w)
        lateral = cs_lateral_from_gradient(Y, pos);

    if(turbulence > 0 && (M[pos] > CS_ACTIVITY_GATE || B[pos] > CS_ACTIVITY_GATE)) {
        int tstep = cs_turbulence_step(turbulence);
        int tdir = cs_turbulence_dir(x, y, c->frame);

        lateral += tdir * tstep;
    }

    src = cs_index_clamped(w, h, x - lateral, y - fall);
    src_mass = M[src];
    transport = c->transport_lut[src_mass];

    if(transport > mass) {
        mass = transport;
        pol = P[src];
    }

    if(turbulence > 0 &&
       (M[pos] > CS_ACTIVITY_GATE || B[pos] > CS_ACTIVITY_GATE || src_mass > CS_ACTIVITY_GATE)) {
        int tstep = cs_turbulence_step(turbulence);
        int tdir = cs_turbulence_dir(x, y, c->frame);

        int tx = x + tdir * tstep;
        int ty = y - fall - 1;

        int tsrc = cs_index_clamped(w, h, tx, ty);

        int tv = c->turbulence_lut[M[tsrc]];
        int bv = c->turbulence_lut[B[pos]];

        if(tv > 0 || bv > 0) {
            int old_mass = mass;
            int lifted = mass;

            if(tv > lifted)
                lifted = tv;

            lifted += tv >> 2;
            lifted += bv >> 3;

            mass = lifted > 255 ? 255 : lifted;

            if(bv > 0) {
                int bed_cut = bv >> 4;
                bed = bed > bed_cut ? bed - bed_cut : 0;
            }

            if(tv > 0 && (((x ^ y ^ c->frame) & 3) == 0 || tv > old_mass))
                pol = P[tsrc];
        }
    }

    if(event_strength > 0) {
        edge = cs_edge_safe(Y, w, h, x, y, pos);
        cavity = 255 - (int) cy;

        event_strength += (event_strength * edge) >> 8;
        event_strength += (event_strength * cavity) >> 9;

        if(event_strength > 255)
            event_strength = 255;

        mass += event_strength;
        if(mass > 255)
            mass = 255;

        pol = diff_raw >= 0 ? 220 : 48;
    }
    else {
        edge = cs_edge_safe(Y, w, h, x, y, pos);
        cavity = 255 - (int) cy;
    }

    carve = c->erosion_lut[(edge + diff_prev) >> 1];
    if(carve > 0 && bed > 0) {
        int cut = 1 + (carve >> 1);

        bed = bed > cut ? bed - cut : 0;

        mass += cut >> 1;
        if(mass > 255)
            mass = 255;
    }

    deposit = c->deposit_lut[mass];
    deposit += (deposit * cavity) >> 9;
    deposit += (deposit * edge) >> 10;

    if(deposit > 255)
        deposit = 255;

    bed += deposit >> 3;
    if(bed > 255)
        bed = 255;

    NM[pos] = (uint8_t) mass;
    NB[pos] = (uint8_t) bed;
    NP[pos] = (uint8_t) pol;

    REF[pos] = cs_blend_fast_u8(
        (uint8_t) ref,
        cy,
        c->adapt_lut[event_for_adapt]
    );

    (void) erosion;
    (void) sediment;
}

static void cs_compute_border(chronoglass_t *c,
                              uint8_t *restrict Y,
                              int flow,
                              int erosion,
                              int sediment,
                              int turbulence,
                              int xmin,
                              int xmax,
                              int ymin,
                              int ymax)
{
    int w = c->w;
    int h = c->h;
    int y;

    if(ymin > 0) {
#pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(y = 0; y < ymin; y++) {
            int x;
            int pos = y * w;

            for(x = 0; x < w; x++, pos++) {
                cs_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    flow,
                    erosion,
                    sediment,
                    turbulence
                );
            }
        }
    }

    if(ymax + 1 < h) {
#pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(y = ymax + 1; y < h; y++) {
            int x;
            int pos = y * w;

            for(x = 0; x < w; x++, pos++) {
                cs_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    flow,
                    erosion,
                    sediment,
                    turbulence
                );
            }
        }
    }

    if(xmin > 0 && ymin <= ymax) {
#pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(y = ymin; y <= ymax; y++) {
            int x;
            int pos = y * w;

            for(x = 0; x < xmin; x++, pos++) {
                cs_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    flow,
                    erosion,
                    sediment,
                    turbulence
                );
            }
        }
    }

    if(xmax + 1 < w && ymin <= ymax) {
#pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(y = ymin; y <= ymax; y++) {
            int x;
            int pos = y * w + xmax + 1;

            for(x = xmax + 1; x < w; x++, pos++) {
                cs_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    flow,
                    erosion,
                    sediment,
                    turbulence
                );
            }
        }
    }
}

static void cs_compute_silt(chronoglass_t *c,
                            VJFrame *frame,
                            int flow,
                            int erosion,
                            int sediment,
                            int turbulence)
{
    uint8_t *restrict Y = frame->data[0];

    uint8_t *restrict M = c->mass;
    uint8_t *restrict NM = c->next_mass;
    uint8_t *restrict B = c->bed;
    uint8_t *restrict NB = c->next_bed;
    uint8_t *restrict P = c->polarity;
    uint8_t *restrict NP = c->next_polarity;
    uint8_t *restrict REF = c->ref_y;
    uint8_t *restrict PREV = c->prev_y;

    uint8_t *restrict EVENT = c->event_lut;
    uint8_t *restrict MDECAY = c->mass_decay_lut;
    uint8_t *restrict BDECAY = c->bed_decay_lut;
    uint8_t *restrict TRANS = c->transport_lut;
    uint8_t *restrict TURB = c->turbulence_lut;
    uint8_t *restrict ERODE = c->erosion_lut;
    uint8_t *restrict DEP = c->deposit_lut;
    uint8_t *restrict ADAPT = c->adapt_lut;

    int w = c->w;
    int h = c->h;

    int fall = cs_fall_distance(flow);
    int turb_step = turbulence > 0 ? cs_turbulence_step(turbulence) : 1;

    int margin_x = 2 + turb_step;
    int margin_y = 1 + fall;

    int xmin = margin_x;
    int xmax = w - 1 - margin_x;
    int ymin = margin_y;
    int ymax = h - 1 - margin_y;

    int frame_phase = c->frame;
    int use_turb = (turbulence > 0);

    int y;

    if(w < 5 || h < 5 || xmin > xmax || ymin > ymax) {
#pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(y = 0; y < h; y++) {
            int x;
            int pos = y * w;

            for(x = 0; x < w; x++, pos++) {
                cs_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    flow,
                    erosion,
                    sediment,
                    turbulence
                );
            }
        }

        return;
    }

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(y = ymin; y <= ymax; y++) {
        int x;
        int pos = y * w + xmin;

        for(x = xmin; x <= xmax; x++, pos++) {
            uint8_t cy = Y[pos];

            int ref = REF[pos];
            int diff_raw = (int) cy - ref;
            int diff_ref = diff_raw < 0 ? -diff_raw : diff_raw;
            int diff_prev = cs_absi((int) cy - (int) PREV[pos]);
            int diff = diff_prev > diff_ref ? diff_prev : diff_ref;

            int event_strength = EVENT[diff];
            int event_for_adapt = event_strength;

            int gx_signed = (int) Y[pos + 1] - (int) Y[pos - 1];
            int gy_signed = (int) Y[pos + w] - (int) Y[pos - w];

            int gx = gx_signed < 0 ? -gx_signed : gx_signed;
            int gy = gy_signed < 0 ? -gy_signed : gy_signed;
            int edge = gx > gy ? gx : gy;

            int lateral;
            int src;
            int mass;
            int bed;
            int pol;

            int src_mass;
            int transport;

            int cavity;
            int carve;
            int deposit;

            lateral = gx_signed > 10 ? -1 : (gx_signed < -10 ? 1 : 0);

            if(use_turb && (M[pos] > CS_ACTIVITY_GATE || B[pos] > CS_ACTIVITY_GATE)) {
                int tdir = cs_turbulence_dir(x, y, frame_phase);
                lateral += tdir * turb_step;
            }

            src = pos - fall * w - lateral;

            mass = MDECAY[M[pos]];
            bed = BDECAY[B[pos]];
            pol = P[pos];

            src_mass = M[src];
            transport = TRANS[src_mass];

            if(transport > mass) {
                mass = transport;
                pol = P[src];
            }

            if(use_turb &&
               (M[pos] > CS_ACTIVITY_GATE || B[pos] > CS_ACTIVITY_GATE || src_mass > CS_ACTIVITY_GATE)) {
                int tdir = cs_turbulence_dir(x, y, frame_phase);
                int tsrc = pos - (fall + 1) * w + tdir * turb_step;

                int tv = TURB[M[tsrc]];
                int bv = TURB[B[pos]];

                if(tv > 0 || bv > 0) {
                    int old_mass = mass;
                    int lifted = mass;

                    if(tv > lifted)
                        lifted = tv;

                    lifted += tv >> 2;
                    lifted += bv >> 3;

                    mass = lifted > 255 ? 255 : lifted;

                    if(bv > 0) {
                        int bed_cut = bv >> 4;
                        bed = bed > bed_cut ? bed - bed_cut : 0;
                    }

                    if(tv > 0 && (((x ^ y ^ frame_phase) & 3) == 0 || tv > old_mass))
                        pol = P[tsrc];
                }
            }

            cavity = 255 - (int) cy;

            if(event_strength > 0) {
                event_strength += (event_strength * edge) >> 8;
                event_strength += (event_strength * cavity) >> 9;
                event_strength = event_strength > 255 ? 255 : event_strength;

                mass += event_strength;
                mass = mass > 255 ? 255 : mass;

                pol = diff_raw >= 0 ? 220 : 48;
            }

            carve = ERODE[(edge + diff_prev) >> 1];
            if(carve > 0 && bed > 0) {
                int cut = 1 + (carve >> 1);

                bed = bed > cut ? bed - cut : 0;

                mass += cut >> 1;
                mass = mass > 255 ? 255 : mass;
            }

            deposit = DEP[mass];
            deposit += (deposit * cavity) >> 9;
            deposit += (deposit * edge) >> 10;
            deposit = deposit > 255 ? 255 : deposit;

            bed += deposit >> 3;
            bed = bed > 255 ? 255 : bed;

            NM[pos] = (uint8_t) mass;
            NB[pos] = (uint8_t) bed;
            NP[pos] = (uint8_t) pol;

            REF[pos] = cs_blend_fast_u8(
                (uint8_t) ref,
                cy,
                ADAPT[event_for_adapt]
            );
        }
    }

    cs_compute_border(
        c,
        Y,
        flow,
        erosion,
        sediment,
        turbulence,
        xmin,
        xmax,
        ymin,
        ymax
    );

    (void) sediment;
}

static void cs_swap_fields(chronoglass_t *c)
{
    uint8_t *t;

    t = c->mass;
    c->mass = c->next_mass;
    c->next_mass = t;

    t = c->bed;
    c->bed = c->next_bed;
    c->next_bed = t;

    t = c->polarity;
    c->polarity = c->next_polarity;
    c->next_polarity = t;
}

static void cs_render_const(chronoglass_t *c,
                            VJFrame *frame,
                            int source_bleed,
                            int color_mode)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict M = c->mass;
    uint8_t *restrict B = c->bed;
    uint8_t *restrict P = c->polarity;

    uint8_t *restrict BY = c->bleed_y_lut;
    uint8_t *restrict BUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

    int low_u = 108;
    int low_v = 146;
    int high_u = 92;
    int high_v = 176;

    switch(color_mode) {
        case CS_COLOR_THERMAL:
            low_u = 212;
            low_v = 84;
            high_u = 84;
            high_v = 220;
            break;

        case CS_COLOR_ELECTRIC:
            low_u = 210;
            low_v = 54;
            high_u = 54;
            high_v = 196;
            break;

        case CS_COLOR_WHITE:
            low_u = 128;
            low_v = 128;
            high_u = 128;
            high_v = 128;
            break;

        case CS_COLOR_SILT:
        default:
            low_u = 112;
            low_v = 142;
            high_u = 92;
            high_v = 174;
            break;
    }

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];

        int mass = M[i];
        int bed = B[i];
        int pol = P[i];

        int ev = mass + (bed >> 1);
        int base_y;
        int yy;

        uint8_t base_u;
        uint8_t base_v;

        int ev_u;
        int ev_v;

        PREV[i] = src_y;

        if(ev > 255)
            ev = 255;

        if(source_bleed > 0) {
            base_y = BY[src_y];
            base_u = BUV[U[i]];
            base_v = BUV[V[i]];
        }
        else {
            base_y = 0;
            base_u = 128;
            base_v = 128;
        }

        if(ev <= 0) {
            Y[i] = (uint8_t) base_y;
            U[i] = base_u;
            V[i] = base_v;
            continue;
        }

        ev_u = cs_blend_fast_u8((uint8_t) low_u, (uint8_t) high_u, pol);
        ev_v = cs_blend_fast_u8((uint8_t) low_v, (uint8_t) high_v, pol);

        yy = base_y + mass - (bed >> 2);
        if(yy < 0)
            yy = 0;
        else if(yy > 255)
            yy = 255;

        if(color_mode == CS_COLOR_WHITE)
            yy = base_y + ev;

        if(yy > 255)
            yy = 255;

        Y[i] = (uint8_t) yy;
        U[i] = cs_blend_fast_u8(base_u, (uint8_t) ev_u, ev);
        V[i] = cs_blend_fast_u8(base_v, (uint8_t) ev_v, ev);
    }
}

static void cs_render_source(chronoglass_t *c,
                             VJFrame *frame,
                             int source_bleed)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict M = c->mass;
    uint8_t *restrict B = c->bed;
    uint8_t *restrict P = c->polarity;

    uint8_t *restrict BY = c->bleed_y_lut;
    uint8_t *restrict BUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];
        uint8_t src_u = U[i];
        uint8_t src_v = V[i];

        int mass = M[i];
        int bed = B[i];
        int pol = P[i];

        int ev = mass + (bed >> 1);
        int base_y;
        int yy;

        uint8_t base_u;
        uint8_t base_v;

        int ev_u;
        int ev_v;

        PREV[i] = src_y;

        if(ev > 255)
            ev = 255;

        if(source_bleed > 0) {
            base_y = BY[src_y];
            base_u = BUV[src_u];
            base_v = BUV[src_v];
        }
        else {
            base_y = 0;
            base_u = 128;
            base_v = 128;
        }

        if(ev <= 0) {
            Y[i] = (uint8_t) base_y;
            U[i] = base_u;
            V[i] = base_v;
            continue;
        }

        ev_u = cs_blend_fast_u8((uint8_t) (255 - src_u), src_u, pol);
        ev_v = cs_blend_fast_u8((uint8_t) (255 - src_v), src_v, pol);

        yy = base_y + mass - (bed >> 2);
        if(yy < 0)
            yy = 0;
        else if(yy > 255)
            yy = 255;

        Y[i] = (uint8_t) yy;
        U[i] = cs_blend_fast_u8(base_u, (uint8_t) ev_u, ev);
        V[i] = cs_blend_fast_u8(base_v, (uint8_t) ev_v, ev);
    }
}

static void cs_render(chronoglass_t *c,
                      VJFrame *frame,
                      int source_bleed,
                      int color_mode)
{
    if(color_mode == CS_COLOR_SOURCE)
        cs_render_source(c, frame, source_bleed);
    else
        cs_render_const(c, frame, source_bleed, color_mode);
}

void chronoglass_apply(void *ptr, VJFrame *frame, int *args)
{
    chronoglass_t *c = (chronoglass_t *) ptr;

    int threshold;
    int flow;
    int erosion;
    int decay;
    int sediment;
    int source_bleed;
    int color_mode;
    int turbulence;

    if(!c->seeded)
        cs_seed(c, frame);

    threshold    = cs_clampi(args[P_THRESHOLD], 0, 255);
    flow         = cs_clampi(args[P_FLOW], 0, 255);
    erosion      = cs_clampi(args[P_EROSION], 0, 255);
    decay        = cs_clampi(args[P_DECAY], 0, 255);
    sediment     = cs_clampi(args[P_SEDIMENT], 0, 255);
    source_bleed = cs_clampi(args[P_SOURCE_BLEED], 0, 255);
    color_mode   = cs_clampi(args[P_COLOR_MODE], 0, 4);
    turbulence   = cs_clampi(args[P_TURBULENCE], 0, 255);

    cs_build_luts_if_needed(
        c,
        threshold,
        flow,
        erosion,
        decay,
        sediment,
        source_bleed,
        turbulence
    );

    cs_compute_silt(
        c,
        frame,
        flow,
        erosion,
        sediment,
        turbulence
    );

    cs_swap_fields(c);

    cs_render(
        c,
        frame,
        source_bleed,
        color_mode
    );

    c->frame++;
}