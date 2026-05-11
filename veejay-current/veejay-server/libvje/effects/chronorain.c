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

#define P_THRESHOLD       0
#define P_GRAVITY         1
#define P_CONDUCTIVITY    2
#define P_DECAY           3
#define P_POLARITY_SPLIT  4
#define P_SOURCE_BLEED    5
#define P_COLOR_MODE      6
#define P_STORM           7

#define CF_COLOR_POLARITY 0
#define CF_COLOR_THERMAL  1
#define CF_COLOR_SOURCE   2
#define CF_COLOR_ELECTRIC 3
#define CF_COLOR_WHITE    4

#define CF_ACTIVITY_GATE  8

#define CF_OMP_FOR _Pragma("omp parallel for schedule(static) num_threads(c->n_threads)")

typedef struct {
    int w;
    int h;
    int len;
    int seeded;
    int n_threads;

    uint8_t *prev_y;
    uint8_t *ref_y;

    uint8_t *on_y;
    uint8_t *off_y;

    uint8_t *nx_on_y;
    uint8_t *nx_off_y;

    uint8_t event_lut[256];
    uint8_t decay_lut[256];
    uint8_t conduct_lut[256];
    uint8_t storm_lut[256];
    uint8_t bleed_y_lut[256];
    uint8_t bleed_uv_lut[256];
    uint8_t adapt_lut[256];

    int lut_valid;
    int last_threshold;
    int last_gravity;
    int last_conductivity;
    int last_decay;
    int last_source_bleed;
    int last_storm;
} chronorain_t;

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

static inline int cf_sign_or_down(int v)
{
    if(v > 0)
        return 1;
    if(v < 0)
        return -1;
    return 1;
}

vj_effect *chronorain_init(int w, int h)
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

    ve->limits[0][P_GRAVITY] = 0;
    ve->limits[1][P_GRAVITY] = 255;
    ve->defaults[P_GRAVITY] = 160;

    ve->limits[0][P_CONDUCTIVITY] = 0;
    ve->limits[1][P_CONDUCTIVITY] = 255;
    ve->defaults[P_CONDUCTIVITY] = 115;

    ve->limits[0][P_DECAY] = 0;
    ve->limits[1][P_DECAY] = 255;
    ve->defaults[P_DECAY] = 140;

    ve->limits[0][P_POLARITY_SPLIT] = 0;
    ve->limits[1][P_POLARITY_SPLIT] = 255;
    ve->defaults[P_POLARITY_SPLIT] = 210;

    ve->limits[0][P_SOURCE_BLEED] = 0;
    ve->limits[1][P_SOURCE_BLEED] = 255;
    ve->defaults[P_SOURCE_BLEED] = 10;

    ve->limits[0][P_COLOR_MODE] = 0;
    ve->limits[1][P_COLOR_MODE] = 4;
    ve->defaults[P_COLOR_MODE] = CF_COLOR_POLARITY;

    ve->limits[0][P_STORM] = 0;
    ve->limits[1][P_STORM] = 255;
    ve->defaults[P_STORM] = 90;

    ve->description = "Chronofold Synaptic Rain";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Gravity",
        "Conductivity",
        "Decay",
        "Polarity Split",
        "Source Bleed",
        "Color Mode",
        "Storm"
    );

    return ve;
}

void *chronorain_malloc(int w, int h)
{
    chronorain_t *c;

    if(w <= 0 || h <= 0)
        return NULL;

    c = (chronorain_t *) vj_calloc(sizeof(chronorain_t));
    if(!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->seeded = 0;
    c->lut_valid = 0;

    c->n_threads = vje_advise_num_threads(w * h);
    if(c->n_threads <= 0)
        c->n_threads = 1;

    c->prev_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->ref_y  = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->on_y  = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->off_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->nx_on_y  = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_off_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    if(!c->prev_y || !c->ref_y || !c->on_y || !c->off_y ||
       !c->nx_on_y || !c->nx_off_y) {
        if(c->prev_y)
            free(c->prev_y);
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

void chronorain_free(void *ptr)
{
    chronorain_t *c = (chronorain_t *) ptr;

    if(!c)
        return;

    if(c->prev_y)
        free(c->prev_y);
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

static void cf_seed(chronorain_t *c, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];

    int i;
    int len = c->len;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        c->prev_y[i] = Y[i];
        c->ref_y[i] = Y[i];

        c->on_y[i] = 0;
        c->off_y[i] = 0;
        c->nx_on_y[i] = 0;
        c->nx_off_y[i] = 0;
    }

    c->seeded = 1;
}

static void cf_build_luts_if_needed(chronorain_t *c,
                                    int threshold,
                                    int gravity,
                                    int conductivity,
                                    int decay,
                                    int source_bleed,
                                    int storm)
{
    int i;
    int denom;

    if(c->lut_valid &&
       c->last_threshold == threshold &&
       c->last_gravity == gravity &&
       c->last_conductivity == conductivity &&
       c->last_decay == decay &&
       c->last_source_bleed == source_bleed &&
       c->last_storm == storm) {
        return;
    }

    denom = 255 - threshold;
    if(denom < 1)
        denom = 1;

    for(i = 0; i < 256; i++) {
        int event_strength;
        int excess;
        int gain;
        int mem;

        if(i > threshold) {
            excess = i - threshold;
            gain = 150 + (gravity >> 2) + (storm >> 1);
            event_strength = (excess * gain + denom / 2) / denom;

            if(event_strength > 255)
                event_strength = 255;
        }
        else {
            event_strength = 0;
        }

        c->event_lut[i] = (uint8_t) event_strength;
        c->decay_lut[i] = (uint8_t) ((i * decay + 127) / 255);
        c->conduct_lut[i] = (uint8_t) ((i * conductivity * decay + 32767) / 65025);
        c->storm_lut[i] = (uint8_t) ((i * storm + 127) / 255);

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
    c->last_gravity = gravity;
    c->last_conductivity = conductivity;
    c->last_decay = decay;
    c->last_source_bleed = source_bleed;
    c->last_storm = storm;
    c->lut_valid = 1;
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

static inline int cf_motion_direct_x(chronorain_t *c,
                                     uint8_t *restrict Y,
                                     int pos)
{
    uint8_t cy = Y[pos];

    int ec = cf_absi((int) cy - (int) c->prev_y[pos]);
    int el = cf_absi((int) cy - (int) c->prev_y[pos - 1]);
    int er = cf_absi((int) cy - (int) c->prev_y[pos + 1]);

    if(el + 4 < er && el + 4 < ec)
        return 1;

    if(er + 4 < el && er + 4 < ec)
        return -1;

    return 0;
}

static inline int cf_motion_safe_x(chronorain_t *c,
                                   uint8_t *restrict Y,
                                   int x,
                                   int pos)
{
    int w = c->w;

    uint8_t cy = Y[pos];

    int ec = cf_absi((int) cy - (int) c->prev_y[pos]);
    int el = 999;
    int er = 999;

    if(x > 0)
        el = cf_absi((int) cy - (int) c->prev_y[pos - 1]);

    if(x + 1 < w)
        er = cf_absi((int) cy - (int) c->prev_y[pos + 1]);

    if(el + 4 < er && el + 4 < ec)
        return 1;

    if(er + 4 < el && er + 4 < ec)
        return -1;

    return 0;
}

static inline void cf_rain_vectors(int gravity,
                                   int storm,
                                   int polarity_split,
                                   int *on_dy,
                                   int *off_dy,
                                   int *wind,
                                   int *storm_span,
                                   int *motion_gain)
{
    int base_g = (gravity * 4 + 127) / 255;
    int storm_g = (storm * 3 + 127) / 255;
    int g = base_g + storm_g;
    int split;

    if(g > 7)
        g = 7;

    if(g <= 0) {
        *on_dy = 0;
        *off_dy = 0;
        *wind = 0;
        *storm_span = 0;
        *motion_gain = 1;
        return;
    }

    split = (2 * g * polarity_split + 127) / 255;

    *on_dy = g - split;
    *off_dy = g;

    *wind = ((gravity + storm) * 2 + 127) / 510;
    if(*wind > 2)
        *wind = 2;

    *storm_span = (storm * 5 + 127) / 255;

    *motion_gain = 1 + ((gravity + storm) >> 8);
    if(*motion_gain > 3)
        *motion_gain = 3;
}

static inline int cf_leak_safe(uint8_t *restrict F,
                               int w,
                               int h,
                               int x,
                               int y,
                               int pos)
{
    int best = F[pos];
    int v;

    if(x > 0) {
        v = F[pos - 1];
        best = v > best ? v : best;
    }

    if(x + 1 < w) {
        v = F[pos + 1];
        best = v > best ? v : best;
    }

    (void) h;
    (void) y;

    return best;
}

static inline int cf_leak_direct(uint8_t *restrict F,
                                 int w,
                                 int pos,
                                 int wind)
{
    int best = F[pos];
    int v;

    v = F[pos - 1];
    best = v > best ? v : best;

    v = F[pos + 1];
    best = v > best ? v : best;

    (void) w;
    (void) wind;

    return best;
}

static inline void cf_compute_one_safe(chronorain_t *c,
                                       uint8_t *restrict Y,
                                       int x,
                                       int y,
                                       int pos,
                                       int on_dy,
                                       int off_dy,
                                       int wind,
                                       int storm_span,
                                       int motion_gain,
                                       int use_conduct,
                                       int use_storm)
{
    int w = c->w;
    int h = c->h;

    int dx = 0;
    int xshift = 0;

    int sign_on = cf_sign_or_down(on_dy);
    int sign_off = cf_sign_or_down(off_dy);

    uint8_t cy = Y[pos];

    int ref = c->ref_y[pos];
    int diff = (int) cy - ref;
    int ad = diff < 0 ? -diff : diff;

    int event_strength = c->event_lut[ad];
    int event_for_adapt = event_strength;

    int on_src;
    int off_src;

    int on_base;
    int off_base;

    if(event_strength > 0 ||
       c->on_y[pos] > CF_ACTIVITY_GATE ||
       c->off_y[pos] > CF_ACTIVITY_GATE) {
        dx = cf_motion_safe_x(c, Y, x, pos);
        xshift = dx * motion_gain;
    }

    on_src = cf_index_clamped(
        w,
        h,
        x - wind - xshift,
        y - on_dy
    );

    off_src = cf_index_clamped(
        w,
        h,
        x + wind - xshift,
        y - off_dy
    );

    on_base = c->decay_lut[c->on_y[on_src]];
    off_base = c->decay_lut[c->off_y[off_src]];

    if(use_storm && storm_span > 0) {
        int on_storm_src = cf_index_clamped(
            w,
            h,
            x - wind - xshift - dx * storm_span,
            y - on_dy - sign_on * storm_span
        );

        int off_storm_src = cf_index_clamped(
            w,
            h,
            x + wind - xshift - dx * storm_span,
            y - off_dy - sign_off * storm_span
        );

        on_base += c->storm_lut[c->on_y[on_storm_src]];
        off_base += c->storm_lut[c->off_y[off_storm_src]];
    }

    if(use_conduct) {
        int on_leak = c->conduct_lut[cf_leak_safe(c->on_y, w, h, x, y, pos)];
        int off_leak = c->conduct_lut[cf_leak_safe(c->off_y, w, h, x, y, pos)];

        if(on_leak > on_base)
            on_base = on_leak;

        if(off_leak > off_base)
            off_base = off_leak;
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

    c->nx_on_y[pos] = (uint8_t) on_base;
    c->nx_off_y[pos] = (uint8_t) off_base;

    c->ref_y[pos] = cf_blend_fast_u8(
        (uint8_t) ref,
        cy,
        c->adapt_lut[event_for_adapt]
    );
}

static void cf_compute_safe_bands(chronorain_t *c,
                                  uint8_t *restrict Y,
                                  int xmin,
                                  int xmax,
                                  int ymin,
                                  int ymax,
                                  int on_dy,
                                  int off_dy,
                                  int wind,
                                  int storm_span,
                                  int motion_gain,
                                  int use_conduct,
                                  int use_storm)
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
                cf_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    on_dy,
                    off_dy,
                    wind,
                    storm_span,
                    motion_gain,
                    use_conduct,
                    use_storm
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
                cf_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    on_dy,
                    off_dy,
                    wind,
                    storm_span,
                    motion_gain,
                    use_conduct,
                    use_storm
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
                cf_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    on_dy,
                    off_dy,
                    wind,
                    storm_span,
                    motion_gain,
                    use_conduct,
                    use_storm
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
                cf_compute_one_safe(
                    c,
                    Y,
                    x,
                    y,
                    pos,
                    on_dy,
                    off_dy,
                    wind,
                    storm_span,
                    motion_gain,
                    use_conduct,
                    use_storm
                );
            }
        }
    }
}

#define CF_DEFINE_COMPUTE_RAIN(NAME, USE_CONDUCT, USE_STORM)                         \
static void NAME(chronorain_t *c,                                                     \
                 VJFrame *frame,                                                      \
                 int gravity,                                                         \
                 int polarity_split,                                                  \
                 int storm)                                                           \
{                                                                                     \
    uint8_t *restrict Y = frame->data[0];                                             \
                                                                                      \
    uint8_t *restrict REF = c->ref_y;                                                 \
    uint8_t *restrict ON = c->on_y;                                                   \
    uint8_t *restrict OFF = c->off_y;                                                 \
    uint8_t *restrict NXON = c->nx_on_y;                                              \
    uint8_t *restrict NXOFF = c->nx_off_y;                                            \
                                                                                      \
    uint8_t *restrict EVENT = c->event_lut;                                           \
    uint8_t *restrict DECAY = c->decay_lut;                                           \
    uint8_t *restrict ADAPT = c->adapt_lut;                                           \
                                                                                      \
    int w = c->w;                                                                     \
    int h = c->h;                                                                     \
                                                                                      \
    int on_dy;                                                                        \
    int off_dy;                                                                       \
    int wind;                                                                         \
    int storm_span;                                                                   \
    int motion_gain;                                                                  \
                                                                                      \
    int margin_x;                                                                     \
    int margin_y;                                                                     \
                                                                                      \
    int xmin;                                                                         \
    int xmax;                                                                         \
    int ymin;                                                                         \
    int ymax;                                                                         \
                                                                                      \
    int sign_on;                                                                      \
    int sign_off;                                                                     \
    int on_y_offset;                                                                  \
    int off_y_offset;                                                                 \
    int on_storm_y_offset;                                                            \
    int off_storm_y_offset;                                                           \
                                                                                      \
    int y;                                                                            \
                                                                                      \
    cf_rain_vectors(                                                                  \
        gravity,                                                                      \
        storm,                                                                        \
        polarity_split,                                                               \
        &on_dy,                                                                       \
        &off_dy,                                                                      \
        &wind,                                                                        \
        &storm_span,                                                                  \
        &motion_gain                                                                  \
    );                                                                                \
                                                                                      \
    if(!(USE_STORM))                                                                  \
        storm_span = 0;                                                               \
                                                                                      \
    sign_on = cf_sign_or_down(on_dy);                                                 \
    sign_off = cf_sign_or_down(off_dy);                                               \
                                                                                      \
    on_y_offset = -on_dy * w;                                                         \
    off_y_offset = -off_dy * w;                                                       \
                                                                                      \
    on_storm_y_offset = -sign_on * storm_span * w;                                    \
    off_storm_y_offset = -sign_off * storm_span * w;                                  \
                                                                                      \
    margin_x = 1 + wind + motion_gain + storm_span;                                   \
                                                                                      \
    margin_y = 1 + cf_absi(on_dy);                                                    \
    if(cf_absi(off_dy) > margin_y)                                                    \
        margin_y = cf_absi(off_dy);                                                   \
    margin_y += storm_span;                                                           \
                                                                                      \
    xmin = margin_x;                                                                  \
    xmax = w - 1 - margin_x;                                                          \
    ymin = margin_y;                                                                  \
    ymax = h - 1 - margin_y;                                                          \
                                                                                      \
    if(w >= 3 && h >= 3 && xmin <= xmax && ymin <= ymax) {                            \
        CF_OMP_FOR                                                                    \
        for(y = ymin; y <= ymax; y++) {                                               \
            int x;                                                                    \
            int pos = y * w + xmin;                                                   \
                                                                                      \
            for(x = xmin; x <= xmax; x++, pos++) {                                    \
                uint8_t cy = Y[pos];                                                  \
                                                                                      \
                int ref = REF[pos];                                                   \
                int diff = (int) cy - ref;                                            \
                int ad = diff < 0 ? -diff : diff;                                     \
                                                                                      \
                int event_strength = EVENT[ad];                                       \
                int event_for_adapt = event_strength;                                 \
                                                                                      \
                int dx = 0;                                                           \
                int xshift = 0;                                                       \
                                                                                      \
                int on_src;                                                           \
                int off_src;                                                          \
                                                                                      \
                int on_base;                                                          \
                int off_base;                                                         \
                                                                                      \
                if(event_strength > 0 ||                                              \
                   ON[pos] > CF_ACTIVITY_GATE ||                                      \
                   OFF[pos] > CF_ACTIVITY_GATE) {                                     \
                    dx = cf_motion_direct_x(c, Y, pos);                               \
                    xshift = dx * motion_gain;                                        \
                }                                                                     \
                                                                                      \
                on_src = pos + on_y_offset - wind - xshift;                           \
                off_src = pos + off_y_offset + wind - xshift;                         \
                                                                                      \
                on_base = DECAY[ON[on_src]];                                          \
                off_base = DECAY[OFF[off_src]];                                       \
                                                                                      \
                if(USE_STORM && storm_span > 0) {                                     \
                    uint8_t *restrict STORM = c->storm_lut;                           \
                    int on_storm_src =                                                \
                        on_src + on_storm_y_offset - dx * storm_span;                 \
                                                                                      \
                    int off_storm_src =                                               \
                        off_src + off_storm_y_offset - dx * storm_span;               \
                                                                                      \
                    on_base += STORM[ON[on_storm_src]];                               \
                    off_base += STORM[OFF[off_storm_src]];                            \
                }                                                                     \
                                                                                      \
                if(USE_CONDUCT) {                                                     \
                    uint8_t *restrict CONDUCT = c->conduct_lut;                       \
                    int on_leak = CONDUCT[cf_leak_direct(ON, w, pos, wind)];          \
                    int off_leak = CONDUCT[cf_leak_direct(OFF, w, pos, wind)];        \
                                                                                      \
                    on_base = on_leak > on_base ? on_leak : on_base;                  \
                    off_base = off_leak > off_base ? off_leak : off_base;             \
                }                                                                     \
                                                                                      \
                if(event_strength > 0) {                                              \
                    int edge = cf_source_edge_direct(Y, w, pos);                      \
                                                                                      \
                    event_strength += (event_strength * edge) >> 7;                   \
                    event_strength = event_strength > 255 ? 255 : event_strength;     \
                                                                                      \
                    if(diff >= 0)                                                     \
                        on_base += event_strength;                                    \
                    else                                                              \
                        off_base += event_strength;                                   \
                }                                                                     \
                                                                                      \
                on_base = on_base > 255 ? 255 : on_base;                              \
                off_base = off_base > 255 ? 255 : off_base;                           \
                                                                                      \
                NXON[pos] = (uint8_t) on_base;                                        \
                NXOFF[pos] = (uint8_t) off_base;                                      \
                                                                                      \
                REF[pos] = cf_blend_fast_u8(                                          \
                    (uint8_t) ref,                                                    \
                    cy,                                                               \
                    ADAPT[event_for_adapt]                                            \
                );                                                                    \
            }                                                                         \
        }                                                                             \
                                                                                      \
        cf_compute_safe_bands(                                                        \
            c,                                                                        \
            Y,                                                                        \
            xmin,                                                                     \
            xmax,                                                                     \
            ymin,                                                                     \
            ymax,                                                                     \
            on_dy,                                                                    \
            off_dy,                                                                   \
            wind,                                                                     \
            storm_span,                                                               \
            motion_gain,                                                              \
            USE_CONDUCT,                                                              \
            USE_STORM                                                                 \
        );                                                                            \
    }                                                                                 \
    else {                                                                            \
        CF_OMP_FOR                                                                    \
        for(y = 0; y < h; y++) {                                                      \
            int x;                                                                    \
            int pos = y * w;                                                          \
                                                                                      \
            for(x = 0; x < w; x++, pos++) {                                           \
                cf_compute_one_safe(                                                  \
                    c,                                                                \
                    Y,                                                                \
                    x,                                                                \
                    y,                                                                \
                    pos,                                                              \
                    on_dy,                                                            \
                    off_dy,                                                           \
                    wind,                                                             \
                    storm_span,                                                       \
                    motion_gain,                                                      \
                    USE_CONDUCT,                                                      \
                    USE_STORM                                                         \
                );                                                                    \
            }                                                                         \
        }                                                                             \
    }                                                                                 \
}

CF_DEFINE_COMPUTE_RAIN(cf_compute_rain_plain,   0, 0)
CF_DEFINE_COMPUTE_RAIN(cf_compute_rain_conduct, 1, 0)
CF_DEFINE_COMPUTE_RAIN(cf_compute_rain_storm,   0, 1)
CF_DEFINE_COMPUTE_RAIN(cf_compute_rain_full,    1, 1)

static void cf_render_const_pure(chronorain_t *c,
                                 VJFrame *frame,
                                 int on_u,
                                 int on_v,
                                 int off_u,
                                 int off_v)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];

        int on = ON[i];
        int off = OFF[i];

        PREV[i] = src_y;

        if((on | off) == 0) {
            Y[i] = 0;
            U[i] = 128;
            V[i] = 128;
            continue;
        }

        {
            int ev = on + off;
            int dominant_on = on >= off;
            int amount = 128 + ((dominant_on ? (on - off) : (off - on)) >> 1);

            int ev_u;
            int ev_v;

            ev = ev > 255 ? 255 : ev;
            amount = amount > 255 ? 255 : amount;

            Y[i] = (uint8_t) ev;

            ev_u = dominant_on ?
                cf_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount) :
                cf_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);

            ev_v = dominant_on ?
                cf_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount) :
                cf_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);

            U[i] = cf_blend_fast_u8(128, (uint8_t) ev_u, ev);
            V[i] = cf_blend_fast_u8(128, (uint8_t) ev_v, ev);
        }
    }
}

static void cf_render_const_bleed(chronorain_t *c,
                                  VJFrame *frame,
                                  int on_u,
                                  int on_v,
                                  int off_u,
                                  int off_v)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;

    uint8_t *restrict BLEEDY = c->bleed_y_lut;
    uint8_t *restrict BLEEDUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];
        uint8_t base_u = BLEEDUV[U[i]];
        uint8_t base_v = BLEEDUV[V[i]];

        int on = ON[i];
        int off = OFF[i];
        int base_y = BLEEDY[src_y];

        PREV[i] = src_y;

        if((on | off) == 0) {
            Y[i] = (uint8_t) base_y;
            U[i] = base_u;
            V[i] = base_v;
            continue;
        }

        {
            int ev = on + off;
            int dominant_on = on >= off;
            int amount = 128 + ((dominant_on ? (on - off) : (off - on)) >> 1);
            int yy;

            int ev_u;
            int ev_v;

            ev = ev > 255 ? 255 : ev;
            amount = amount > 255 ? 255 : amount;

            yy = base_y + ev;
            Y[i] = (uint8_t) (yy > 255 ? 255 : yy);

            ev_u = dominant_on ?
                cf_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount) :
                cf_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);

            ev_v = dominant_on ?
                cf_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount) :
                cf_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);

            U[i] = cf_blend_fast_u8(base_u, (uint8_t) ev_u, ev);
            V[i] = cf_blend_fast_u8(base_v, (uint8_t) ev_v, ev);
        }
    }
}

static void cf_render_source(chronorain_t *c,
                             VJFrame *frame,
                             int source_bleed)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;

    uint8_t *restrict BLEEDY = c->bleed_y_lut;
    uint8_t *restrict BLEEDUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];
        uint8_t src_u = U[i];
        uint8_t src_v = V[i];

        int on = ON[i];
        int off = OFF[i];

        int base_y;
        uint8_t base_u;
        uint8_t base_v;

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

        if((on | off) == 0) {
            Y[i] = (uint8_t) base_y;
            U[i] = base_u;
            V[i] = base_v;
            continue;
        }

        {
            int ev = on + off;
            int dominant_on = on >= off;
            int amount = 128 + ((dominant_on ? (on - off) : (off - on)) >> 1);
            int yy;

            int on_u = src_u;
            int on_v = src_v;
            int off_u = 255 - src_u;
            int off_v = 255 - src_v;

            int ev_u;
            int ev_v;

            ev = ev > 255 ? 255 : ev;
            amount = amount > 255 ? 255 : amount;

            yy = base_y + ev;
            Y[i] = (uint8_t) (yy > 255 ? 255 : yy);

            ev_u = dominant_on ?
                cf_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount) :
                cf_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);

            ev_v = dominant_on ?
                cf_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount) :
                cf_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);

            U[i] = cf_blend_fast_u8(base_u, (uint8_t) ev_u, ev);
            V[i] = cf_blend_fast_u8(base_v, (uint8_t) ev_v, ev);
        }
    }
}

static void cf_render_white_pure(chronorain_t *c,
                                 VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];

        int ev = ON[i] + OFF[i];

        PREV[i] = src_y;

        ev = ev > 255 ? 255 : ev;

        Y[i] = (uint8_t) ev;
        U[i] = 128;
        V[i] = 128;
    }
}

static void cf_render_white_bleed(chronorain_t *c,
                                  VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict PREV = c->prev_y;
    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;

    uint8_t *restrict BLEEDY = c->bleed_y_lut;
    uint8_t *restrict BLEEDUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];

        int ev = ON[i] + OFF[i];
        int base_y = BLEEDY[src_y];
        int yy;

        PREV[i] = src_y;

        ev = ev > 255 ? 255 : ev;
        yy = base_y + ev;

        Y[i] = (uint8_t) (yy > 255 ? 255 : yy);
        U[i] = BLEEDUV[U[i]];
        V[i] = BLEEDUV[V[i]];
    }
}

static void cf_render(chronorain_t *c,
                      VJFrame *frame,
                      int source_bleed,
                      int color_mode)
{
    switch(color_mode) {
        case CF_COLOR_WHITE:
            if(source_bleed == 0)
                cf_render_white_pure(c, frame);
            else
                cf_render_white_bleed(c, frame);
            break;

        case CF_COLOR_THERMAL:
            if(source_bleed == 0)
                cf_render_const_pure(c, frame, 84, 220, 212, 84);
            else
                cf_render_const_bleed(c, frame, 84, 220, 212, 84);
            break;

        case CF_COLOR_SOURCE:
            cf_render_source(c, frame, source_bleed);
            break;

        case CF_COLOR_ELECTRIC:
            if(source_bleed == 0)
                cf_render_const_pure(c, frame, 54, 196, 210, 54);
            else
                cf_render_const_bleed(c, frame, 54, 196, 210, 54);
            break;

        case CF_COLOR_POLARITY:
        default:
            if(source_bleed == 0)
                cf_render_const_pure(c, frame, 92, 226, 226, 92);
            else
                cf_render_const_bleed(c, frame, 92, 226, 226, 92);
            break;
    }
}

static void cf_swap_fields(chronorain_t *c)
{
    uint8_t *t;

    t = c->on_y;
    c->on_y = c->nx_on_y;
    c->nx_on_y = t;

    t = c->off_y;
    c->off_y = c->nx_off_y;
    c->nx_off_y = t;
}

void chronorain_apply(void *ptr, VJFrame *frame, int *args)
{
    chronorain_t *c = (chronorain_t *) ptr;

    int threshold;
    int gravity;
    int conductivity;
    int decay;
    int polarity_split;
    int source_bleed;
    int color_mode;
    int storm;

    int use_conduct;
    int use_storm;

    if(!c->seeded)
        cf_seed(c, frame);

    threshold      = cf_clampi(args[P_THRESHOLD], 0, 255);
    gravity        = cf_clampi(args[P_GRAVITY], 0, 255);
    conductivity   = cf_clampi(args[P_CONDUCTIVITY], 0, 255);
    decay          = cf_clampi(args[P_DECAY], 0, 255);
    polarity_split = cf_clampi(args[P_POLARITY_SPLIT], 0, 255);
    source_bleed   = cf_clampi(args[P_SOURCE_BLEED], 0, 255);
    color_mode     = cf_clampi(args[P_COLOR_MODE], 0, 4);
    storm          = cf_clampi(args[P_STORM], 0, 255);

    cf_build_luts_if_needed(
        c,
        threshold,
        gravity,
        conductivity,
        decay,
        source_bleed,
        storm
    );


    {
        int conduct_power = conductivity * decay;
        int storm_span_hint = (storm * 5 + 127) / 255;

        use_conduct = (conduct_power >= 128);
        use_storm = (storm_span_hint > 0);
    }

    if(use_conduct) {
        if(use_storm)
            cf_compute_rain_full(c, frame, gravity, polarity_split, storm);
        else
            cf_compute_rain_conduct(c, frame, gravity, polarity_split, storm);
    }
    else {
        if(use_storm)
            cf_compute_rain_storm(c, frame, gravity, polarity_split, storm);
        else
            cf_compute_rain_plain(c, frame, gravity, polarity_split, storm);
    }

    cf_swap_fields(c);

    cf_render(
        c,
        frame,
        source_bleed,
        color_mode
    );
}