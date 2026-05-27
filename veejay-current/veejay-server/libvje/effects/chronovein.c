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
#include "chronovein.h"

#define CHRONOVEIN_PARAMS 11

#define P_THRESHOLD       0
#define P_GROWTH          1
#define P_CONDUCTIVITY    2
#define P_DECAY           3
#define P_BRANCH          4
#define P_SOURCE_BLEED    5
#define P_COLOR_MODE      6
#define P_PULSE           7
#define P_BEAT_PUSH       8
#define P_VEIN_GAIN       9
#define P_COLOR_ENERGY   10

#define CV_COLOR_POLARITY 0
#define CV_COLOR_THERMAL  1
#define CV_COLOR_SOURCE   2
#define CV_COLOR_ELECTRIC 3
#define CV_COLOR_WHITE    4

typedef struct {
    int w;
    int h;
    int len;
    int seeded;
    int frame;
    int n_threads;

    uint8_t *prev_y;
    uint8_t *ref_y;

    uint8_t *field;
    uint8_t *next_field;

    uint8_t *polarity;
    uint8_t *next_polarity;

    uint8_t event_lut[256];
    uint8_t decay_lut[256];
    uint8_t conduct_lut[256];
    uint8_t branch_lut[256];
    uint8_t bleed_y_lut[256];
    uint8_t bleed_uv_lut[256];
    uint8_t adapt_lut[256];

    int lut_valid;
    int last_threshold;
    int last_growth;
    int last_conductivity;
    int last_decay;
    int last_branch;
    int last_source_bleed;
} chronovein_t;

static inline int cv_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int cv_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline uint8_t cv_blend_fast_u8(uint8_t a, uint8_t b, int amount)
{
    return (uint8_t) (((int) a * (256 - amount) + (int) b * amount) >> 8);
}

static inline int cv_param1000_to_u8(int v)
{
    v = cv_clampi(v, 0, 1000);
    return (v * 255 + 500) / 1000;
}

static inline int cv_gain1000_to_q8(int v)
{
    v = cv_clampi(v, 0, 1000);
    return 64 + ((v * 384 + 500) / 1000);
}

static inline int cv_push_u8(int v, int push_q8, int gain_q8)
{
    v += (((255 - v) * push_q8 * gain_q8) + 32768) >> 16;
    return cv_clampi(v, 0, 255);
}

static inline int cv_push_1000(int v, int push, int gain_q8)
{
    long n;

    v = cv_clampi(v, 0, 1000);
    push = cv_clampi(push, 0, 1000);
    n = ((long) (1000 - v) * (long) push * (long) gain_q8) + 128000L;

    return cv_clampi(v + (int) (n / 256000L), 0, 1000);
}

vj_effect *chronovein_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = CHRONOVEIN_PARAMS;

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
    ve->limits[1][P_THRESHOLD] = 1000;
    ve->defaults[P_THRESHOLD] = 70;

    ve->limits[0][P_GROWTH] = 0;
    ve->limits[1][P_GROWTH] = 1000;
    ve->defaults[P_GROWTH] = 706;

    ve->limits[0][P_CONDUCTIVITY] = 0;
    ve->limits[1][P_CONDUCTIVITY] = 1000;
    ve->defaults[P_CONDUCTIVITY] = 588;

    ve->limits[0][P_DECAY] = 0;
    ve->limits[1][P_DECAY] = 1000;
    ve->defaults[P_DECAY] = 725;

    ve->limits[0][P_BRANCH] = 0;
    ve->limits[1][P_BRANCH] = 1000;
    ve->defaults[P_BRANCH] = 376;

    ve->limits[0][P_SOURCE_BLEED] = 0;
    ve->limits[1][P_SOURCE_BLEED] = 1000;
    ve->defaults[P_SOURCE_BLEED] = 47;

    ve->limits[0][P_COLOR_MODE] = 0;
    ve->limits[1][P_COLOR_MODE] = 4;
    ve->defaults[P_COLOR_MODE] = CV_COLOR_ELECTRIC;

    ve->limits[0][P_PULSE] = 0;
    ve->limits[1][P_PULSE] = 1000;
    ve->defaults[P_PULSE] = 250;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_VEIN_GAIN] = 0;
    ve->limits[1][P_VEIN_GAIN] = 1000;
    ve->defaults[P_VEIN_GAIN] = 500;

    ve->limits[0][P_COLOR_ENERGY] = 0;
    ve->limits[1][P_COLOR_ENERGY] = 1000;
    ve->defaults[P_COLOR_ENERGY] = 500;

    ve->description = "Chronovein";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Trigger Gate",
        "Vein Growth",
        "Conductivity",
        "Memory Decay",
        "Branching",
        "Source Bleed",
        "Color Mode",
        "Auto Pulse",
        "Beat Push",
        "Vein Gain",
        "Color Energy"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DETAIL,       VJ_BEAT_F_PHRASE_ONLY,                    16,                 380,                6,  22,  1600, 3400, 700,  35,    /* Trigger Gate */
        VJ_BEAT_FLOW,         VJ_BEAT_F_CONTINUOUS,                     180,                920,                12, 52,  900,  2400, 0,    75,    /* Vein Growth */
        VJ_BEAT_FLOW,         VJ_BEAT_F_CONTINUOUS,                     80,                 860,                8,  38,  1000, 2800, 0,    55,    /* Conductivity */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY,                    360,                940,                6,  28,  1800, 4400, 900,  45,    /* Memory Decay */
        VJ_BEAT_TURBULENCE,   VJ_BEAT_F_CONTINUOUS,                     0,                  720,                8,  36,  1200, 3200, 0,    50,    /* Branching */
        VJ_BEAT_SOURCE_MIX,   VJ_BEAT_F_CONTINUOUS,                     0,                  280,                6,  28,  1200, 3000, 0,    35,    /* Source Bleed */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Color Mode */
        VJ_BEAT_GLOW,         VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_CLIMAX_ONLY, 0,             620,                4,  24,  1800, 4200, 600,  25,    /* Auto Pulse */
        VJ_BEAT_INTENSITY,    VJ_BEAT_F_CONTINUOUS,                     0,                  1000,               18, 72,  450,  1500, 0,    95,    /* Beat Push */
        VJ_BEAT_CONTRAST,     VJ_BEAT_F_CONTINUOUS,                     250,                920,                10, 46,  700,  1800, 0,    75,    /* Vein Gain */
        VJ_BEAT_INTENSITY,    VJ_BEAT_F_CONTINUOUS,                     260,                900,                8,  38,  900,  2200, 0,    55     /* Color Energy */
    );
    return ve;
}

void *chronovein_malloc(int w, int h)
{
    chronovein_t *c;

    if(w <= 0 || h <= 0)
        return NULL;

    c = (chronovein_t *) vj_calloc(sizeof(chronovein_t));
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
    c->ref_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->field = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->next_field = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->polarity = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->next_polarity = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    if(!c->prev_y || !c->ref_y || !c->field || !c->next_field ||
       !c->polarity || !c->next_polarity) {
        if(c->prev_y)
            free(c->prev_y);
        if(c->ref_y)
            free(c->ref_y);
        if(c->field)
            free(c->field);
        if(c->next_field)
            free(c->next_field);
        if(c->polarity)
            free(c->polarity);
        if(c->next_polarity)
            free(c->next_polarity);

        free(c);
        return NULL;
    }

    return (void *) c;
}

void chronovein_free(void *ptr)
{
    chronovein_t *c = (chronovein_t *) ptr;

    if(!c)
        return;

    if(c->prev_y)
        free(c->prev_y);
    if(c->ref_y)
        free(c->ref_y);
    if(c->field)
        free(c->field);
    if(c->next_field)
        free(c->next_field);
    if(c->polarity)
        free(c->polarity);
    if(c->next_polarity)
        free(c->next_polarity);

    free(c);
}

static void cv_seed(chronovein_t *c, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];

    int i;
    int len = c->len;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        c->prev_y[i] = Y[i];
        c->ref_y[i] = Y[i];

        c->field[i] = 0;
        c->next_field[i] = 0;

        c->polarity[i] = 128;
        c->next_polarity[i] = 128;
    }

    c->seeded = 1;
}

static void cv_build_luts_if_needed(chronovein_t *c,
                                    int threshold,
                                    int growth,
                                    int conductivity,
                                    int decay,
                                    int branch,
                                    int source_bleed)
{
    int i;
    int denom;

    int growth_scale;
    int conduct_power;
    int branch_power;

    if(c->lut_valid &&
       c->last_threshold == threshold &&
       c->last_growth == growth &&
       c->last_conductivity == conductivity &&
       c->last_decay == decay &&
       c->last_branch == branch &&
       c->last_source_bleed == source_bleed) {
        return;
    }

    denom = 255 - threshold;
    if(denom < 1)
        denom = 1;

    growth_scale = (growth * 320 + 127) / 255; /* 0..320 */

    conduct_power = (conductivity * decay + 127) / 255;
    branch_power  = (branch * decay + 127) / 255;

    conduct_power = (conduct_power * growth_scale + 128) >> 8;
    branch_power  = (branch_power  * growth_scale + 128) >> 8;

    if(conduct_power > 255)
        conduct_power = 255;

    if(branch_power > 255)
        branch_power = 255;

    for(i = 0; i < 256; i++) {
        int event_strength;
        int excess;
        int gain;
        int mem;

        if(i > threshold) {
            excess = i - threshold;

            gain = 128 + (growth >> 1);

            event_strength = (excess * gain + denom / 2) / denom;
            if(event_strength > 255)
                event_strength = 255;
        }
        else {
            event_strength = 0;
        }

        c->event_lut[i] = (uint8_t) event_strength;
        c->decay_lut[i] = (uint8_t) ((i * decay + 127) / 255);

        c->conduct_lut[i] = (uint8_t) ((i * conduct_power + 127) / 255);
        c->branch_lut[i]  = (uint8_t) ((i * branch_power  + 127) / 255);

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
    c->last_growth = growth;
    c->last_conductivity = conductivity;
    c->last_decay = decay;
    c->last_branch = branch;
    c->last_source_bleed = source_bleed;
    c->lut_valid = 1;
}

static inline int cv_source_edge_safe(uint8_t *restrict Y,
                                      int w,
                                      int h,
                                      int x,
                                      int y,
                                      int pos)
{
    int gx = 0;
    int gy = 0;

    if(x > 0 && x + 1 < w)
        gx = cv_absi((int) Y[pos - 1] - (int) Y[pos + 1]);

    if(y > 0 && y + 1 < h)
        gy = cv_absi((int) Y[pos - w] - (int) Y[pos + w]);

    return gx > gy ? gx : gy;
}

static inline void cv_compute_one_safe(chronovein_t *c,
                                       uint8_t *restrict Y,
                                       int x,
                                       int y,
                                       int pos,
                                       int use_conduct,
                                       int use_branch)
{
    int w = c->w;
    int h = c->h;

    uint8_t *restrict F = c->field;
    uint8_t *restrict NF = c->next_field;
    uint8_t *restrict P = c->polarity;
    uint8_t *restrict NP = c->next_polarity;
    uint8_t *restrict REF = c->ref_y;

    uint8_t cy = Y[pos];

    int ref = REF[pos];
    int diff = (int) cy - ref;
    int ad = diff < 0 ? -diff : diff;

    int event_strength = c->event_lut[ad];
    int event_for_adapt = event_strength;

    int base = c->decay_lut[F[pos]];
    int out_pol = P[pos];

    if(use_conduct) {
        int best_src = pos;
        int best_v = F[pos];
        int v;

        if(x > 0) {
            v = F[pos - 1];
            if(v > best_v) {
                best_v = v;
                best_src = pos - 1;
            }
        }

        if(x + 1 < w) {
            v = F[pos + 1];
            if(v > best_v) {
                best_v = v;
                best_src = pos + 1;
            }
        }

        if(y > 0) {
            v = F[pos - w];
            if(v > best_v) {
                best_v = v;
                best_src = pos - w;
            }
        }

        if(y + 1 < h) {
            v = F[pos + w];
            if(v > best_v) {
                best_v = v;
                best_src = pos + w;
            }
        }

        v = c->conduct_lut[best_v];
        if(v > base) {
            base = v;
            out_pol = P[best_src];
        }
    }

    if(use_branch) {
        int best_src = pos;
        int best_v = 0;
        int flip = (x ^ y ^ (c->frame >> 1)) & 1;
        int v;

        if(flip) {
            if(x > 0 && y > 0) {
                v = F[pos - w - 1];
                if(v > best_v) {
                    best_v = v;
                    best_src = pos - w - 1;
                }
            }

            if(x + 1 < w && y + 1 < h) {
                v = F[pos + w + 1];
                if(v > best_v) {
                    best_v = v;
                    best_src = pos + w + 1;
                }
            }
        }
        else {
            if(x + 1 < w && y > 0) {
                v = F[pos - w + 1];
                if(v > best_v) {
                    best_v = v;
                    best_src = pos - w + 1;
                }
            }

            if(x > 0 && y + 1 < h) {
                v = F[pos + w - 1];
                if(v > best_v) {
                    best_v = v;
                    best_src = pos + w - 1;
                }
            }
        }

        v = c->branch_lut[best_v];
        if(v > base) {
            base = v;
            out_pol = P[best_src];
        }
    }

    if(event_strength > 0) {
        int edge = cv_source_edge_safe(Y, w, h, x, y, pos);

        event_strength += (event_strength * edge) >> 7;
        if(event_strength > 255)
            event_strength = 255;

        base += event_strength;
        if(base > 255)
            base = 255;

        out_pol = diff >= 0 ? 255 : 0;
    }

    NF[pos] = (uint8_t) base;
    NP[pos] = (uint8_t) out_pol;

    REF[pos] = cv_blend_fast_u8(
        (uint8_t) ref,
        cy,
        c->adapt_lut[event_for_adapt]
    );
}

static void cv_compute_safe_border(chronovein_t *c,
                                   uint8_t *restrict Y,
                                   int use_conduct,
                                   int use_branch)
{
    int w = c->w;
    int h = c->h;
    int y;

    if(h <= 2 || w <= 2) {
#pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(y = 0; y < h; y++) {
            int x;
            int pos = y * w;

            for(x = 0; x < w; x++, pos++) {
                cv_compute_one_safe(c, Y, x, y, pos, use_conduct, use_branch);
            }
        }

        return;
    }

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(y = 0; y < h; y++) {
        int x;

        if(y == 0 || y == h - 1) {
            int pos = y * w;

            for(x = 0; x < w; x++, pos++) {
                cv_compute_one_safe(c, Y, x, y, pos, use_conduct, use_branch);
            }
        }
        else {
            int pos_l = y * w;
            int pos_r = y * w + (w - 1);

            cv_compute_one_safe(c, Y, 0, y, pos_l, use_conduct, use_branch);
            cv_compute_one_safe(c, Y, w - 1, y, pos_r, use_conduct, use_branch);
        }
    }
}

static void cv_compute(chronovein_t *c,
                       VJFrame *frame,
                       int use_conduct,
                       int use_branch)
{
    uint8_t *restrict Y = frame->data[0];

    uint8_t *restrict F = c->field;
    uint8_t *restrict NF = c->next_field;
    uint8_t *restrict P = c->polarity;
    uint8_t *restrict NP = c->next_polarity;
    uint8_t *restrict REF = c->ref_y;

    uint8_t *restrict EVENT = c->event_lut;
    uint8_t *restrict DECAY = c->decay_lut;
    uint8_t *restrict CONDUCT = c->conduct_lut;
    uint8_t *restrict BRANCH = c->branch_lut;
    uint8_t *restrict ADAPT = c->adapt_lut;

    int w = c->w;
    int h = c->h;
    int frame_phase = c->frame >> 1;

    int y;

    if(w <= 2 || h <= 2) {
        cv_compute_safe_border(c, Y, use_conduct, use_branch);
        return;
    }

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(y = 1; y < h - 1; y++) {
        int x;
        int pos = y * w + 1;

        for(x = 1; x < w - 1; x++, pos++) {
            uint8_t cy = Y[pos];

            int ref = REF[pos];
            int diff = (int) cy - ref;
            int ad = diff < 0 ? -diff : diff;

            int event_strength = EVENT[ad];
            int event_for_adapt = event_strength;

            int gx = cv_absi((int) Y[pos - 1] - (int) Y[pos + 1]);
            int gy = cv_absi((int) Y[pos - w] - (int) Y[pos + w]);

            int base = DECAY[F[pos]];
            int out_pol = P[pos];

            if(use_conduct) {
                int src_a;
                int src_b;
                int src;
                int v;

                if(gx > gy) {
                    src_a = pos - w;
                    src_b = pos + w;
                }
                else {
                    src_a = pos - 1;
                    src_b = pos + 1;
                }

                src = F[src_a] > F[src_b] ? src_a : src_b;
                v = CONDUCT[F[src]];

                if(v > base) {
                    base = v;
                    out_pol = P[src];
                }
            }

            if(use_branch) {
                int flip = (x ^ y ^ frame_phase) & 1;
                int src_a = flip ? (pos - w - 1) : (pos - w + 1);
                int src_b = flip ? (pos + w + 1) : (pos + w - 1);
                int src = F[src_a] > F[src_b] ? src_a : src_b;
                int v = BRANCH[F[src]];

                if(v > base) {
                    base = v;
                    out_pol = P[src];
                }
            }

            if(event_strength > 0) {
                int edge = gx > gy ? gx : gy;

                event_strength += (event_strength * edge) >> 7;
                event_strength = event_strength > 255 ? 255 : event_strength;

                base += event_strength;
                base = base > 255 ? 255 : base;

                out_pol = diff >= 0 ? 255 : 0;
            }

            NF[pos] = (uint8_t) base;
            NP[pos] = (uint8_t) out_pol;

            REF[pos] = cv_blend_fast_u8(
                (uint8_t) ref,
                cy,
                ADAPT[event_for_adapt]
            );
        }
    }

    cv_compute_safe_border(c, Y, use_conduct, use_branch);
}

static inline int cv_pulse_gain(chronovein_t *c, int pulse)
{
    int phase;
    int tri;

    if(pulse <= 0)
        return 256;

    phase = (c->frame * (1 + (pulse >> 5))) & 63;
    tri = phase < 32 ? phase : 63 - phase;

    return 256 + ((pulse * tri) >> 5);
}

static void cv_render_const(chronovein_t *c,
                            VJFrame *frame,
                            int source_bleed,
                            int pulse_gain,
                            int color_energy_q8,
                            int on_u,
                            int on_v,
                            int off_u,
                            int off_v)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict F = c->field;
    uint8_t *restrict P = c->polarity;

    uint8_t *restrict BLEEDY = c->bleed_y_lut;
    uint8_t *restrict BLEEDUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];

        int ev = F[i];
        int pol = P[i];

        int base_y;
        uint8_t base_u;
        uint8_t base_v;

        int ev_u;
        int ev_v;
        int chroma_amount;
        int yy;

        PREV[i] = src_y;

        if(source_bleed > 0) {
            base_y = BLEEDY[src_y];
            base_u = BLEEDUV[U[i]];
            base_v = BLEEDUV[V[i]];
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

        ev = (ev * pulse_gain + 128) >> 8;
        ev = ev > 255 ? 255 : ev;

        ev_u = cv_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, pol);
        ev_v = cv_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, pol);
        chroma_amount = (ev * color_energy_q8 + 128) >> 8;
        chroma_amount = chroma_amount > 255 ? 255 : chroma_amount;

        yy = base_y + ev;

        Y[i] = (uint8_t) (yy > 255 ? 255 : yy);
        U[i] = cv_blend_fast_u8(base_u, (uint8_t) ev_u, chroma_amount);
        V[i] = cv_blend_fast_u8(base_v, (uint8_t) ev_v, chroma_amount);
    }
}

static void cv_render_source(chronovein_t *c,
                             VJFrame *frame,
                             int source_bleed,
                             int pulse_gain,
                             int color_energy_q8)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict F = c->field;
    uint8_t *restrict P = c->polarity;

    uint8_t *restrict BLEEDY = c->bleed_y_lut;
    uint8_t *restrict BLEEDUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];
        uint8_t src_u = U[i];
        uint8_t src_v = V[i];

        int ev = F[i];
        int pol = P[i];

        int base_y;
        uint8_t base_u;
        uint8_t base_v;

        int ev_u;
        int ev_v;
        int chroma_amount;
        int yy;

        PREV[i] = src_y;

        if(source_bleed > 0) {
            base_y = BLEEDY[src_y];
            base_u = BLEEDUV[src_u];
            base_v = BLEEDUV[src_v];
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

        ev = (ev * pulse_gain + 128) >> 8;
        ev = ev > 255 ? 255 : ev;

        ev_u = cv_blend_fast_u8((uint8_t) (255 - src_u), src_u, pol);
        ev_v = cv_blend_fast_u8((uint8_t) (255 - src_v), src_v, pol);
        chroma_amount = (ev * color_energy_q8 + 128) >> 8;
        chroma_amount = chroma_amount > 255 ? 255 : chroma_amount;

        yy = base_y + ev;

        Y[i] = (uint8_t) (yy > 255 ? 255 : yy);
        U[i] = cv_blend_fast_u8(base_u, (uint8_t) ev_u, chroma_amount);
        V[i] = cv_blend_fast_u8(base_v, (uint8_t) ev_v, chroma_amount);
    }
}

static void cv_render_white(chronovein_t *c,
                            VJFrame *frame,
                            int source_bleed,
                            int pulse_gain)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict F = c->field;

    uint8_t *restrict BLEEDY = c->bleed_y_lut;
    uint8_t *restrict BLEEDUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];

        int ev = F[i];

        int base_y;
        uint8_t base_u;
        uint8_t base_v;
        int yy;

        PREV[i] = src_y;

        if(source_bleed > 0) {
            base_y = BLEEDY[src_y];
            base_u = BLEEDUV[U[i]];
            base_v = BLEEDUV[V[i]];
        }
        else {
            base_y = 0;
            base_u = 128;
            base_v = 128;
        }

        ev = (ev * pulse_gain + 128) >> 8;
        ev = ev > 255 ? 255 : ev;

        yy = base_y + ev;

        Y[i] = (uint8_t) (yy > 255 ? 255 : yy);
        U[i] = base_u;
        V[i] = base_v;
    }
}

static void cv_render(chronovein_t *c,
                      VJFrame *frame,
                      int source_bleed,
                      int color_mode,
                      int pulse,
                      int vein_gain,
                      int color_energy)
{
    int pulse_gain = cv_pulse_gain(c, pulse);
    int vein_gain_q8 = cv_gain1000_to_q8(vein_gain);
    int color_energy_q8 = cv_gain1000_to_q8(color_energy);

    pulse_gain = (pulse_gain * vein_gain_q8 + 128) >> 8;
    if(pulse_gain > 1024)
        pulse_gain = 1024;

    switch(color_mode) {
        case CV_COLOR_WHITE:
            cv_render_white(c, frame, source_bleed, pulse_gain);
            break;

        case CV_COLOR_THERMAL:
            cv_render_const(c, frame, source_bleed, pulse_gain, color_energy_q8, 84, 220, 212, 84);
            break;

        case CV_COLOR_SOURCE:
            cv_render_source(c, frame, source_bleed, pulse_gain, color_energy_q8);
            break;

        case CV_COLOR_ELECTRIC:
            cv_render_const(c, frame, source_bleed, pulse_gain, color_energy_q8, 54, 196, 210, 54);
            break;

        case CV_COLOR_POLARITY:
        default:
            cv_render_const(c, frame, source_bleed, pulse_gain, color_energy_q8, 92, 226, 226, 92);
            break;
    }
}

static void cv_swap_fields(chronovein_t *c)
{
    uint8_t *t;

    t = c->field;
    c->field = c->next_field;
    c->next_field = t;

    t = c->polarity;
    c->polarity = c->next_polarity;
    c->next_polarity = t;
}

void chronovein_apply(void *ptr, VJFrame *frame, int *args)
{
    chronovein_t *c = (chronovein_t *) ptr;

    int threshold;
    int growth;
    int conductivity;
    int decay;
    int branch;
    int source_bleed;
    int color_mode;
    int pulse;
    int beat_push;
    int vein_gain;
    int color_energy;

    int use_conduct;
    int use_branch;

    if(!c->seeded)
        cv_seed(c, frame);

    threshold    = cv_param1000_to_u8(args[P_THRESHOLD]);
    growth       = cv_param1000_to_u8(args[P_GROWTH]);
    conductivity = cv_param1000_to_u8(args[P_CONDUCTIVITY]);
    decay        = cv_param1000_to_u8(args[P_DECAY]);
    branch       = cv_param1000_to_u8(args[P_BRANCH]);
    source_bleed = cv_param1000_to_u8(args[P_SOURCE_BLEED]);
    color_mode   = cv_clampi(args[P_COLOR_MODE], 0, 4);
    pulse        = cv_param1000_to_u8(args[P_PULSE]);
    beat_push    = cv_clampi(args[P_BEAT_PUSH], 0, 1000);
    vein_gain    = cv_clampi(args[P_VEIN_GAIN], 0, 1000);
    color_energy = cv_clampi(args[P_COLOR_ENERGY], 0, 1000);

    if(beat_push > 0) {
        int push_q8 = (beat_push * 256 + 500) / 1000;
        int gate_drop = ((18 + (threshold >> 2)) * push_q8 + 128) >> 8;
        int bleed_target = 120;

        threshold -= gate_drop;
        if(threshold < 0)
            threshold = 0;

        growth = cv_push_u8(growth, push_q8, 220);
        conductivity = cv_push_u8(conductivity, push_q8, 120);
        branch = cv_push_u8(branch, push_q8, 190);
        pulse = cv_push_u8(pulse, push_q8, 230);

        if(source_bleed < bleed_target) {
            source_bleed += (((bleed_target - source_bleed) * push_q8 * 120) + 32768) >> 16;
            if(source_bleed > bleed_target)
                source_bleed = bleed_target;
        }

        vein_gain = cv_push_1000(vein_gain, beat_push, 190);
        color_energy = cv_push_1000(color_energy, beat_push, 150);
    }

    cv_build_luts_if_needed(
        c,
        threshold,
        growth,
        conductivity,
        decay,
        branch,
        source_bleed
    );

    {
        int growth_scale = (growth * 320 + 127) / 255;
        int conduct_power = (conductivity * decay + 127) / 255;
        int branch_power  = (branch * decay + 127) / 255;

        conduct_power = (conduct_power * growth_scale + 128) >> 8;
        branch_power  = (branch_power  * growth_scale + 128) >> 8;

        use_conduct = (conduct_power > 0);
        use_branch  = (branch_power > 0);
    }

    cv_compute(c, frame, use_conduct, use_branch);

    cv_swap_fields(c);

    cv_render(
        c,
        frame,
        source_bleed,
        color_mode,
        pulse,
        vein_gain,
        color_energy
    );

    c->frame++;
}
