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

#define CHRONOFOLD_PARAMS 8

#define P_THRESHOLD      0
#define P_EXCITATION     1
#define P_INHIBITION     2
#define P_DECAY          3
#define P_BRANCHING      4
#define P_POLARITY_DRIFT 5
#define P_SOURCE_BLEED   6
#define P_COLOR_MODE     7

#define CF_COLOR_POLARITY 0
#define CF_COLOR_THERMAL  1
#define CF_COLOR_SOURCE   2
#define CF_COLOR_ELECTRIC 3
#define CF_COLOR_WHITE    4

#define CF_PROP_LR     0
#define CF_PROP_UD     1
#define CF_PROP_NW_SE  2
#define CF_PROP_NE_SW  3

typedef struct {
    int w;
    int h;
    int len;
    int frame;
    int seeded;
    int n_threads;

    uint8_t *ref_y;

    uint8_t *on_y;
    uint8_t *off_y;

    uint8_t *nx_on_y;
    uint8_t *nx_off_y;

    uint8_t event_lut[256];
    uint8_t decay_lut[256];
    uint8_t excite_lut[256];
    uint8_t inhibit_lut[256];
    uint8_t branch_lut[256];
    uint8_t bleed_y_lut[256];
    uint8_t bleed_uv_lut[256];
    uint8_t adapt_lut[256];

    int lut_valid;
    int last_threshold;
    int last_excitation;
    int last_inhibition;
    int last_decay;
    int last_branching;
    int last_source_bleed;
} chronocortex_t;

static inline int cf_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int cf_absi(int v)
{
    return v < 0 ? -v : v;
}

static inline uint8_t cf_u8(int v)
{
    return (uint8_t) cf_clampi(v, 0, 255);
}

static inline uint8_t cf_blend_fast_u8(uint8_t a, uint8_t b, int amount)
{
    return (uint8_t) (((int) a * (256 - amount) + (int) b * amount) >> 8);
}

static inline int cf_maxi(int a, int b)
{
    return a > b ? a : b;
}

static inline int cf_mini(int a, int b)
{
    return a < b ? a : b;
}

vj_effect *chronocortex_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = CHRONOFOLD_PARAMS;

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

    ve->limits[0][P_EXCITATION] = 0;
    ve->limits[1][P_EXCITATION] = 255;
    ve->defaults[P_EXCITATION] = 150;

    ve->limits[0][P_INHIBITION] = 0;
    ve->limits[1][P_INHIBITION] = 255;
    ve->defaults[P_INHIBITION] = 90;

    ve->limits[0][P_DECAY] = 0;
    ve->limits[1][P_DECAY] = 255;
    ve->defaults[P_DECAY] = 220;

    ve->limits[0][P_BRANCHING] = 0;
    ve->limits[1][P_BRANCHING] = 255;
    ve->defaults[P_BRANCHING] = 95;

    ve->limits[0][P_POLARITY_DRIFT] = 0;
    ve->limits[1][P_POLARITY_DRIFT] = 255;
    ve->defaults[P_POLARITY_DRIFT] = 90;

    ve->limits[0][P_SOURCE_BLEED] = 0;
    ve->limits[1][P_SOURCE_BLEED] = 255;
    ve->defaults[P_SOURCE_BLEED] = 10;

    ve->limits[0][P_COLOR_MODE] = 0;
    ve->limits[1][P_COLOR_MODE] = 4;
    ve->defaults[P_COLOR_MODE] = CF_COLOR_POLARITY;

    ve->description = "Chronofold Cortex";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Excitation",
        "Inhibition",
        "Decay",
        "Branching",
        "Polarity Drift",
        "Source Bleed",
        "Color Mode"
    );

    return ve;
}

void *chronocortex_malloc(int w, int h)
{
    chronocortex_t *c;

    if(w <= 0 || h <= 0)
        return NULL;

    c = (chronocortex_t *) vj_calloc(sizeof(chronocortex_t));
    if(!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->frame = 0;
    c->seeded = 0;
    c->lut_valid = 0;

    c->n_threads = vje_advise_num_threads(w * h);
    if(c->n_threads <= 0)
        c->n_threads = 1;

    c->ref_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->on_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->off_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_on_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_off_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    if(!c->ref_y || !c->on_y || !c->off_y || !c->nx_on_y || !c->nx_off_y) {
        if(c->ref_y)
            free(c->ref_y);
        if(c->on_y)
            free(c->on_y);
        if(c->off_y)
            free(c->off_y);
        if(c->nx_on_y)
            free(c->nx_on_y);
        if(c->nx_off_y)
            free(c->nx_off_y);

        free(c);
        return NULL;
    }

    return (void *) c;
}

void chronocortex_free(void *ptr)
{
    chronocortex_t *c = (chronocortex_t *) ptr;

    if(!c)
        return;

    if(c->ref_y)
        free(c->ref_y);
    if(c->on_y)
        free(c->on_y);
    if(c->off_y)
        free(c->off_y);
    if(c->nx_on_y)
        free(c->nx_on_y);
    if(c->nx_off_y)
        free(c->nx_off_y);

    free(c);
}

static void cf_seed(chronocortex_t *c, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];

    int i;
    int len = c->len;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        c->ref_y[i] = Y[i];

        c->on_y[i] = 0;
        c->off_y[i] = 0;
        c->nx_on_y[i] = 0;
        c->nx_off_y[i] = 0;
    }

    c->seeded = 1;
}

static void cf_build_luts_if_needed(chronocortex_t *c,
                                    int threshold,
                                    int excitation,
                                    int inhibition,
                                    int decay,
                                    int branching,
                                    int source_bleed)
{
    int i;
    int denom;

    if(c->lut_valid &&
       c->last_threshold == threshold &&
       c->last_excitation == excitation &&
       c->last_inhibition == inhibition &&
       c->last_decay == decay &&
       c->last_branching == branching &&
       c->last_source_bleed == source_bleed) {
        return;
    }

    denom = 255 - threshold;
    if(denom < 1)
        denom = 1;

    for(i = 0; i < 256; i++) {
        int event_strength;
        int excess;
        int mem;

        if(i > threshold) {
            excess = i - threshold;

            event_strength =
                (excess * (128 + excitation) * 2 + denom / 2) / denom;

            if(event_strength > 255)
                event_strength = 255;
        }
        else {
            event_strength = 0;
        }

        c->event_lut[i] = (uint8_t) event_strength;
        c->decay_lut[i] = (uint8_t) ((i * decay + 127) / 255);
        c->excite_lut[i] = (uint8_t) ((i * excitation + 127) / 255);
        c->inhibit_lut[i] = (uint8_t) ((i * inhibition + 127) / 255);
        c->branch_lut[i] = (uint8_t) ((i * branching + 127) / 255);

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
    c->last_excitation = excitation;
    c->last_inhibition = inhibition;
    c->last_decay = decay;
    c->last_branching = branching;
    c->last_source_bleed = source_bleed;
    c->lut_valid = 1;
}

static inline void cf_prop_offsets(int prop_mode, int w, int *o1, int *o2)
{
    switch(prop_mode) {
        case CF_PROP_UD:
            *o1 = -w;
            *o2 = w;
            break;

        case CF_PROP_NW_SE:
            *o1 = -w - 1;
            *o2 = w + 1;
            break;

        case CF_PROP_NE_SW:
            *o1 = -w + 1;
            *o2 = w - 1;
            break;

        case CF_PROP_LR:
        default:
            *o1 = -1;
            *o2 = 1;
            break;
    }
}

static inline int cf_index_clamped(int w, int h, int x, int y)
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

static inline int cf_pair_direct_offset(uint8_t *restrict F, int pos, int o1, int o2)
{
    int a = F[pos + o1];
    int b = F[pos + o2];

    return a > b ? a : b;
}

static inline int cf_pair_safe(uint8_t *restrict F,
                               int w,
                               int h,
                               int x,
                               int y,
                               int pos,
                               int prop_mode)
{
    int best = 0;
    int v;

    switch(prop_mode) {
        case CF_PROP_UD:
            if(y > 0) {
                v = F[pos - w];
                if(v > best)
                    best = v;
            }

            if(y + 1 < h) {
                v = F[pos + w];
                if(v > best)
                    best = v;
            }
            break;

        case CF_PROP_NW_SE:
            if(x > 0 && y > 0) {
                v = F[pos - w - 1];
                if(v > best)
                    best = v;
            }

            if(x + 1 < w && y + 1 < h) {
                v = F[pos + w + 1];
                if(v > best)
                    best = v;
            }
            break;

        case CF_PROP_NE_SW:
            if(x + 1 < w && y > 0) {
                v = F[pos - w + 1];
                if(v > best)
                    best = v;
            }

            if(x > 0 && y + 1 < h) {
                v = F[pos + w - 1];
                if(v > best)
                    best = v;
            }
            break;

        case CF_PROP_LR:
        default:
            if(x > 0) {
                v = F[pos - 1];
                if(v > best)
                    best = v;
            }

            if(x + 1 < w) {
                v = F[pos + 1];
                if(v > best)
                    best = v;
            }
            break;
    }

    return best;
}

static inline int cf_source_edge_direct(uint8_t *restrict Y, int w, int pos)
{
    int gx = cf_absi((int) Y[pos - 1] - (int) Y[pos + 1]);
    int gy = cf_absi((int) Y[pos - w] - (int) Y[pos + w]);

    return gx > gy ? gx : gy;
}

static inline int cf_source_edge_safe(uint8_t *restrict Y,
                                      int w,
                                      int h,
                                      int x,
                                      int y,
                                      int pos)
{
    int gx = 0;
    int gy = 0;

    if(x > 0 && x + 1 < w)
        gx = cf_absi((int) Y[pos - 1] - (int) Y[pos + 1]);

    if(y > 0 && y + 1 < h)
        gy = cf_absi((int) Y[pos - w] - (int) Y[pos + w]);

    return gx > gy ? gx : gy;
}

static inline void cf_polarity_drift_vector(int frame,
                                            int drift,
                                            int *dx_on,
                                            int *dy_on,
                                            int *dx_off,
                                            int *dy_off)
{
    int amount = (drift * 4 + 127) / 255;
    int phase;

    if(amount <= 0) {
        *dx_on = 0;
        *dy_on = 0;
        *dx_off = 0;
        *dy_off = 0;
        return;
    }

    phase = (frame >> 4) & 3;

    switch(phase) {
        case 0:
            *dx_on = 0;
            *dy_on = -amount;
            break;
        case 1:
            *dx_on = amount;
            *dy_on = 0;
            break;
        case 2:
            *dx_on = 0;
            *dy_on = amount;
            break;
        default:
            *dx_on = -amount;
            *dy_on = 0;
            break;
    }

    *dx_off = -*dx_on;
    *dy_off = -*dy_on;
}

static inline void cf_apply_pair_propagation_fast(chronocortex_t *c,
                                                  int prop_diag,
                                                  int excitation,
                                                  int branching,
                                                  int pair_on,
                                                  int pair_off,
                                                  int *on_base,
                                                  int *off_base)
{
    int on_ex;
    int off_ex;

    if(excitation > 0) {
        on_ex = c->excite_lut[pair_on];
        off_ex = c->excite_lut[pair_off];

        if(on_ex > *on_base)
            *on_base = on_ex;

        if(off_ex > *off_base)
            *off_base = off_ex;
    }

    if(branching > 0 && prop_diag) {
        int on_br = c->branch_lut[pair_on];
        int off_br = c->branch_lut[pair_off];

        if(on_br > *on_base)
            *on_base = on_br;

        if(off_br > *off_base)
            *off_base = off_br;
    }
}

static inline void cf_compute_one_safe(chronocortex_t *c,
                                       uint8_t *restrict Y,
                                       int x,
                                       int y,
                                       int pos,
                                       int excitation,
                                       int inhibition,
                                       int branching,
                                       int prop_mode,
                                       int prop_diag,
                                       int dx_on,
                                       int dy_on,
                                       int dx_off,
                                       int dy_off)
{
    int w = c->w;
    int h = c->h;

    uint8_t cy = Y[pos];

    int ref = c->ref_y[pos];
    int diff = (int) cy - ref;
    int ad = diff < 0 ? -diff : diff;

    int event_strength = c->event_lut[ad];
    int event_for_adapt = event_strength;

    int on_pos = cf_index_clamped(w, h, x - dx_on, y - dy_on);
    int off_pos = cf_index_clamped(w, h, x - dx_off, y - dy_off);

    int on_base = c->decay_lut[c->on_y[on_pos]];
    int off_base = c->decay_lut[c->off_y[off_pos]];

    int on_val;
    int off_val;

    if(excitation > 0 || branching > 0) {
        int pair_on = cf_pair_safe(c->on_y, w, h, x, y, pos, prop_mode);
        int pair_off = cf_pair_safe(c->off_y, w, h, x, y, pos, prop_mode);

        cf_apply_pair_propagation_fast(
            c,
            prop_diag,
            excitation,
            branching,
            pair_on,
            pair_off,
            &on_base,
            &off_base
        );
    }

    if(event_strength > 0) {
        int edge = cf_source_edge_safe(Y, w, h, x, y, pos);

        event_strength += (event_strength * edge) >> 7;
        if(event_strength > 255)
            event_strength = 255;

        if(diff >= 0)
            on_base += event_strength;
        else
            off_base += event_strength;
    }

    if(on_base > 255)
        on_base = 255;
    if(off_base > 255)
        off_base = 255;

    if(inhibition > 0) {
        on_val = on_base - c->inhibit_lut[off_base];
        off_val = off_base - c->inhibit_lut[on_base];

        if(on_val < 0)
            on_val = 0;
        if(off_val < 0)
            off_val = 0;
    }
    else {
        on_val = on_base;
        off_val = off_base;
    }

    c->nx_on_y[pos] = (uint8_t) on_val;
    c->nx_off_y[pos] = (uint8_t) off_val;

    c->ref_y[pos] = cf_blend_fast_u8(
        (uint8_t) ref,
        cy,
        c->adapt_lut[event_for_adapt]
    );
}

static inline void cf_compute_one_direct(chronocortex_t *c,
                                         uint8_t *restrict Y,
                                         int pos,
                                         int on_pos,
                                         int off_pos,
                                         int po1,
                                         int po2,
                                         int prop_diag,
                                         int excitation,
                                         int inhibition,
                                         int branching)
{
    uint8_t cy = Y[pos];

    int ref = c->ref_y[pos];
    int diff = (int) cy - ref;
    int ad = diff < 0 ? -diff : diff;

    int event_strength = c->event_lut[ad];
    int event_for_adapt = event_strength;

    int on_base = c->decay_lut[c->on_y[on_pos]];
    int off_base = c->decay_lut[c->off_y[off_pos]];

    int on_val;
    int off_val;

    if(excitation > 0 || branching > 0) {
        int pair_on = cf_pair_direct_offset(c->on_y, pos, po1, po2);
        int pair_off = cf_pair_direct_offset(c->off_y, pos, po1, po2);

        cf_apply_pair_propagation_fast(
            c,
            prop_diag,
            excitation,
            branching,
            pair_on,
            pair_off,
            &on_base,
            &off_base
        );
    }

    if(event_strength > 0) {
        int edge = cf_source_edge_direct(Y, c->w, pos);

        event_strength += (event_strength * edge) >> 7;
        if(event_strength > 255)
            event_strength = 255;

        if(diff >= 0)
            on_base += event_strength;
        else
            off_base += event_strength;
    }

    if(on_base > 255)
        on_base = 255;
    if(off_base > 255)
        off_base = 255;

    if(inhibition > 0) {
        on_val = on_base - c->inhibit_lut[off_base];
        off_val = off_base - c->inhibit_lut[on_base];

        if(on_val < 0)
            on_val = 0;
        if(off_val < 0)
            off_val = 0;
    }
    else {
        on_val = on_base;
        off_val = off_base;
    }

    c->nx_on_y[pos] = (uint8_t) on_val;
    c->nx_off_y[pos] = (uint8_t) off_val;

    c->ref_y[pos] = cf_blend_fast_u8(
        (uint8_t) ref,
        cy,
        c->adapt_lut[event_for_adapt]
    );
}

static void cf_compute_safe_region(chronocortex_t *c,
                                   uint8_t *restrict Y,
                                   int xmin,
                                   int xmax,
                                   int ymin,
                                   int ymax,
                                   int excitation,
                                   int inhibition,
                                   int branching,
                                   int prop_mode,
                                   int prop_diag,
                                   int dx_on,
                                   int dy_on,
                                   int dx_off,
                                   int dy_off)
{
    int w = c->w;
    int h = c->h;
    int y;

    if(xmin > xmax || ymin > ymax)
        return;

    for(y = ymin; y <= ymax; y++) {
        int x;
        int pos = y * w + xmin;

        for(x = xmin; x <= xmax; x++, pos++) {
            cf_compute_one_safe(
                c,
                Y,
                x,
                y,
                pos,
                excitation,
                inhibition,
                branching,
                prop_mode,
                prop_diag,
                dx_on,
                dy_on,
                dx_off,
                dy_off
            );
        }
    }

    (void) h;
}

static void cf_compute_cortex_direct_rect(chronocortex_t *c,
                                          VJFrame *frame,
                                          int excitation,
                                          int inhibition,
                                          int branching,
                                          int prop_mode,
                                          int dx_on,
                                          int dy_on,
                                          int dx_off,
                                          int dy_off,
                                          int xmin,
                                          int xmax,
                                          int ymin,
                                          int ymax)
{
    uint8_t *restrict Y = frame->data[0];

    int w = c->w;
    int h = c->h;

    int po1;
    int po2;
    int prop_diag = (prop_mode >= CF_PROP_NW_SE);

    int on_offset;
    int off_offset;

    int y;

    cf_prop_offsets(prop_mode, w, &po1, &po2);

    on_offset = -dy_on * w - dx_on;
    off_offset = -dy_off * w - dx_off;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(y = ymin; y <= ymax; y++) {
        int x;
        int pos = y * w + xmin;
        int on_pos = pos + on_offset;
        int off_pos = pos + off_offset;

        for(x = xmin; x <= xmax; x++, pos++, on_pos++, off_pos++) {
            cf_compute_one_direct(
                c,
                Y,
                pos,
                on_pos,
                off_pos,
                po1,
                po2,
                prop_diag,
                excitation,
                inhibition,
                branching
            );
        }
    }

    /*
     * Safe outside-rectangle bands.
     * These are small when drift is small, and only borders when drift is zero.
     */
    if(ymin > 0) {
        cf_compute_safe_region(
            c, Y,
            0, w - 1,
            0, ymin - 1,
            excitation,
            inhibition,
            branching,
            prop_mode,
            prop_diag,
            dx_on,
            dy_on,
            dx_off,
            dy_off
        );
    }

    if(ymax + 1 < h) {
        cf_compute_safe_region(
            c, Y,
            0, w - 1,
            ymax + 1, h - 1,
            excitation,
            inhibition,
            branching,
            prop_mode,
            prop_diag,
            dx_on,
            dy_on,
            dx_off,
            dy_off
        );
    }

    if(xmin > 0) {
        cf_compute_safe_region(
            c, Y,
            0, xmin - 1,
            ymin, ymax,
            excitation,
            inhibition,
            branching,
            prop_mode,
            prop_diag,
            dx_on,
            dy_on,
            dx_off,
            dy_off
        );
    }

    if(xmax + 1 < w) {
        cf_compute_safe_region(
            c, Y,
            xmax + 1, w - 1,
            ymin, ymax,
            excitation,
            inhibition,
            branching,
            prop_mode,
            prop_diag,
            dx_on,
            dy_on,
            dx_off,
            dy_off
        );
    }
}

static void cf_compute_cortex(chronocortex_t *c,
                              VJFrame *frame,
                              int excitation,
                              int inhibition,
                              int branching,
                              int polarity_drift,
                              int prop_mode)
{
    int w = c->w;
    int h = c->h;

    int dx_on;
    int dy_on;
    int dx_off;
    int dy_off;

    int xmin;
    int xmax;
    int ymin;
    int ymax;

    cf_polarity_drift_vector(
        c->frame,
        polarity_drift,
        &dx_on,
        &dy_on,
        &dx_off,
        &dy_off
    );

    /*
     * Direct interior constraints:
     * - source edge needs x/y one pixel away from border
     * - propagation pair also needs one pixel away from border
     * - drift source positions must stay inside frame
     */
    xmin = 1;
    ymin = 1;
    xmax = w - 2;
    ymax = h - 2;

    if(dx_on > xmin)
        xmin = dx_on;
    if(dx_off > xmin)
        xmin = dx_off;

    if(dy_on > ymin)
        ymin = dy_on;
    if(dy_off > ymin)
        ymin = dy_off;

    xmax = cf_mini(xmax, w - 1 + dx_on);
    xmax = cf_mini(xmax, w - 1 + dx_off);

    ymax = cf_mini(ymax, h - 1 + dy_on);
    ymax = cf_mini(ymax, h - 1 + dy_off);

    if(xmin <= xmax && ymin <= ymax) {
        cf_compute_cortex_direct_rect(
            c,
            frame,
            excitation,
            inhibition,
            branching,
            prop_mode,
            dx_on,
            dy_on,
            dx_off,
            dy_off,
            xmin,
            xmax,
            ymin,
            ymax
        );
    }
    else {
        uint8_t *restrict Y = frame->data[0];
        int prop_diag = (prop_mode >= CF_PROP_NW_SE);
        int y;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
        for(y = 0; y < h; y++) {
            int x;
            int pos = y * w;

            for(x = 0; x < w; x++, pos++) {
                cf_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    excitation,
                    inhibition,
                    branching,
                    prop_mode,
                    prop_diag,
                    dx_on,
                    dy_on,
                    dx_off,
                    dy_off
                );
            }
        }
    }
}

static void cf_render_cortex_const_pure(chronocortex_t *c,
                                        VJFrame *frame,
                                        int on_u,
                                        int on_v,
                                        int off_u,
                                        int off_v)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        int on = c->on_y[i];
        int off = c->off_y[i];
        int ev = on + off;

        int ev_u;
        int ev_v;

        if(ev > 255)
            ev = 255;

        Y[i] = (uint8_t) ev;

        if(ev <= 0) {
            U[i] = 128;
            V[i] = 128;
            continue;
        }

        if(on >= off) {
            int amount = 128 + ((on - off) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cf_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount);
            ev_v = cf_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount);
        }
        else {
            int amount = 128 + ((off - on) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cf_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);
            ev_v = cf_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);
        }

        U[i] = cf_blend_fast_u8(128, (uint8_t) ev_u, ev);
        V[i] = cf_blend_fast_u8(128, (uint8_t) ev_v, ev);
    }
}

static void cf_render_cortex_const_bleed(chronocortex_t *c,
                                         VJFrame *frame,
                                         int on_u,
                                         int on_v,
                                         int off_u,
                                         int off_v)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        int on = c->on_y[i];
        int off = c->off_y[i];
        int ev = on + off;

        int base_y;
        uint8_t base_u;
        uint8_t base_v;

        int ev_u;
        int ev_v;

        if(ev > 255)
            ev = 255;

        base_y = c->bleed_y_lut[Y[i]];
        base_u = c->bleed_uv_lut[U[i]];
        base_v = c->bleed_uv_lut[V[i]];

        Y[i] = cf_u8(base_y + ev);

        if(ev <= 0) {
            U[i] = base_u;
            V[i] = base_v;
            continue;
        }

        if(on >= off) {
            int amount = 128 + ((on - off) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cf_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount);
            ev_v = cf_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount);
        }
        else {
            int amount = 128 + ((off - on) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cf_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);
            ev_v = cf_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);
        }

        U[i] = cf_blend_fast_u8(base_u, (uint8_t) ev_u, ev);
        V[i] = cf_blend_fast_u8(base_v, (uint8_t) ev_v, ev);
    }
}

static void cf_render_cortex_source(chronocortex_t *c,
                                    VJFrame *frame,
                                    int source_bleed)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_u = U[i];
        uint8_t src_v = V[i];

        int on = c->on_y[i];
        int off = c->off_y[i];
        int ev = on + off;

        int base_y;
        uint8_t base_u;
        uint8_t base_v;

        int on_u = src_u;
        int on_v = src_v;
        int off_u = 255 - src_u;
        int off_v = 255 - src_v;

        int ev_u;
        int ev_v;

        if(ev > 255)
            ev = 255;

        if(source_bleed > 0) {
            base_y = c->bleed_y_lut[Y[i]];
            base_u = c->bleed_uv_lut[src_u];
            base_v = c->bleed_uv_lut[src_v];
        }
        else {
            base_y = 0;
            base_u = 128;
            base_v = 128;
        }

        Y[i] = cf_u8(base_y + ev);

        if(ev <= 0) {
            U[i] = base_u;
            V[i] = base_v;
            continue;
        }

        if(on >= off) {
            int amount = 128 + ((on - off) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cf_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount);
            ev_v = cf_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount);
        }
        else {
            int amount = 128 + ((off - on) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cf_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);
            ev_v = cf_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);
        }

        U[i] = cf_blend_fast_u8(base_u, (uint8_t) ev_u, ev);
        V[i] = cf_blend_fast_u8(base_v, (uint8_t) ev_v, ev);
    }
}

static void cf_render_cortex_white_pure(chronocortex_t *c,
                                        VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        int ev = c->on_y[i] + c->off_y[i];
        if(ev > 255)
            ev = 255;
        Y[i] = (uint8_t) ev;
    }

    veejay_memset(frame->data[1], 128, (size_t) len);
    veejay_memset(frame->data[2], 128, (size_t) len);
}

static void cf_render_cortex_white_bleed(chronocortex_t *c,
                                         VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        int ev = c->on_y[i] + c->off_y[i];
        int base_y;

        if(ev > 255)
            ev = 255;

        base_y = c->bleed_y_lut[Y[i]];

        Y[i] = cf_u8(base_y + ev);
        U[i] = c->bleed_uv_lut[U[i]];
        V[i] = c->bleed_uv_lut[V[i]];
    }
}

static void cf_render_cortex(chronocortex_t *c,
                             VJFrame *frame,
                             int source_bleed,
                             int color_mode)
{
    switch(color_mode) {
        case CF_COLOR_WHITE:
            if(source_bleed == 0)
                cf_render_cortex_white_pure(c, frame);
            else
                cf_render_cortex_white_bleed(c, frame);
            break;

        case CF_COLOR_THERMAL:
            if(source_bleed == 0)
                cf_render_cortex_const_pure(c, frame, 84, 220, 212, 84);
            else
                cf_render_cortex_const_bleed(c, frame, 84, 220, 212, 84);
            break;

        case CF_COLOR_SOURCE:
            cf_render_cortex_source(c, frame, source_bleed);
            break;

        case CF_COLOR_ELECTRIC:
            if(source_bleed == 0)
                cf_render_cortex_const_pure(c, frame, 54, 196, 210, 54);
            else
                cf_render_cortex_const_bleed(c, frame, 54, 196, 210, 54);
            break;

        case CF_COLOR_POLARITY:
        default:
            if(source_bleed == 0)
                cf_render_cortex_const_pure(c, frame, 92, 226, 226, 92);
            else
                cf_render_cortex_const_bleed(c, frame, 92, 226, 226, 92);
            break;
    }
}

static void cf_swap_fields(chronocortex_t *c)
{
    uint8_t *t;

    t = c->on_y;
    c->on_y = c->nx_on_y;
    c->nx_on_y = t;

    t = c->off_y;
    c->off_y = c->nx_off_y;
    c->nx_off_y = t;
}

void chronocortex_apply(void *ptr, VJFrame *frame, int *args)
{
    chronocortex_t *c = (chronocortex_t *) ptr;

    int threshold;
    int excitation;
    int inhibition;
    int decay;
    int branching;
    int polarity_drift;
    int source_bleed;
    int color_mode;

    int prop_mode;

    if(!c->seeded)
        cf_seed(c, frame);

    threshold      = cf_clampi(args[P_THRESHOLD], 0, 255);
    excitation     = cf_clampi(args[P_EXCITATION], 0, 255);
    inhibition     = cf_clampi(args[P_INHIBITION], 0, 255);
    decay          = cf_clampi(args[P_DECAY], 0, 255);
    branching      = cf_clampi(args[P_BRANCHING], 0, 255);
    polarity_drift = cf_clampi(args[P_POLARITY_DRIFT], 0, 255);
    source_bleed   = cf_clampi(args[P_SOURCE_BLEED], 0, 255);
    color_mode     = cf_clampi(args[P_COLOR_MODE], 0, 4);

    cf_build_luts_if_needed(
        c,
        threshold,
        excitation,
        inhibition,
        decay,
        branching,
        source_bleed
    );

    prop_mode = c->frame & 3;

    cf_compute_cortex(
        c,
        frame,
        excitation,
        inhibition,
        branching,
        polarity_drift,
        prop_mode
    );

    cf_swap_fields(c);

    cf_render_cortex(
        c,
        frame,
        source_bleed,
        color_mode
    );

    c->frame++;
}