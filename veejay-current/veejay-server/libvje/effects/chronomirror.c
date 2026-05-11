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

#define CHRONOSMOKE_PARAMS 8

#define P_THRESHOLD     0
#define P_RISE          1
#define P_CURL          2
#define P_DECAY         3
#define P_DENSITY       4
#define P_SOURCE_BLEED  5
#define P_COLOR_MODE    6
#define P_TURBULENCE    7

#define CS_COLOR_POLARITY 0
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

    uint8_t *on_y;
    uint8_t *off_y;

    uint8_t *nx_on_y;
    uint8_t *nx_off_y;

    uint8_t *veil;
    uint8_t *nx_veil;

    uint8_t *phase;
    uint8_t *nx_phase;

    int8_t phase_dx[256];
    int8_t phase_dy[256];

    uint8_t event_lut[256];
    uint8_t smoke_decay_lut[256];
    uint8_t veil_decay_lut[256];
    uint8_t rise_lut[256];
    uint8_t curl_lut[256];
    uint8_t spread_lut[256];
    uint8_t density_lut[256];

    uint8_t bleed_y_lut[256];
    uint8_t bleed_uv_lut[256];
    uint8_t adapt_lut[256];

    int field_lut_valid;
    int bleed_lut_valid;

    int last_threshold;
    int last_rise;
    int last_curl;
    int last_decay;
    int last_density;
    int last_source_bleed;
    int last_turbulence;
} chronomirror_t;

static inline int cs_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int cs_absi(int v)
{
    int mask = v >> 31;
    return (v + mask) ^ mask;
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

static inline void cs_phase_to_dir_build(int ph, int *dx, int *dy)
{
    ph &= 255;

    if(ph < 32) {
        *dx = 1;
        *dy = 0;
    }
    else if(ph < 64) {
        *dx = 1;
        *dy = -1;
    }
    else if(ph < 96) {
        *dx = 0;
        *dy = -1;
    }
    else if(ph < 128) {
        *dx = -1;
        *dy = -1;
    }
    else if(ph < 160) {
        *dx = -1;
        *dy = 0;
    }
    else if(ph < 192) {
        *dx = -1;
        *dy = 1;
    }
    else if(ph < 224) {
        *dx = 0;
        *dy = 1;
    }
    else {
        *dx = 1;
        *dy = 1;
    }
}

vj_effect *chronomirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = CHRONOSMOKE_PARAMS;

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
    ve->defaults[P_THRESHOLD] = 16;

    ve->limits[0][P_RISE] = 0;
    ve->limits[1][P_RISE] = 255;
    ve->defaults[P_RISE] = 165;

    ve->limits[0][P_CURL] = 0;
    ve->limits[1][P_CURL] = 255;
    ve->defaults[P_CURL] = 95;

    ve->limits[0][P_DECAY] = 0;
    ve->limits[1][P_DECAY] = 255;
    ve->defaults[P_DECAY] = 215;

    ve->limits[0][P_DENSITY] = 0;
    ve->limits[1][P_DENSITY] = 255;
    ve->defaults[P_DENSITY] = 170;

    ve->limits[0][P_SOURCE_BLEED] = 0;
    ve->limits[1][P_SOURCE_BLEED] = 255;
    ve->defaults[P_SOURCE_BLEED] = 12;

    ve->limits[0][P_COLOR_MODE] = 0;
    ve->limits[1][P_COLOR_MODE] = 4;
    ve->defaults[P_COLOR_MODE] = CS_COLOR_POLARITY;

    ve->limits[0][P_TURBULENCE] = 0;
    ve->limits[1][P_TURBULENCE] = 255;
    ve->defaults[P_TURBULENCE] = 80;

    ve->description = "Chronosmoke";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Threshold",
        "Rise",
        "Curl",
        "Decay",
        "Density",
        "Source Bleed",
        "Color Mode",
        "Turbulence"
    );

    (void) w;
    (void) h;

    return ve;
}

void *chronomirror_malloc(int w, int h)
{
    chronomirror_t *c;
    int i;

    if(w <= 0 || h <= 0)
        return NULL;

    c = (chronomirror_t *) vj_calloc(sizeof(chronomirror_t));
    if(!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->seeded = 0;
    c->frame = 0;
    c->field_lut_valid = 0;
    c->bleed_lut_valid = 0;

    c->n_threads = vje_advise_num_threads(w * h);
    if(c->n_threads <= 0)
        c->n_threads = 1;

    for(i = 0; i < 256; i++) {
        int dx;
        int dy;

        cs_phase_to_dir_build(i, &dx, &dy);

        c->phase_dx[i] = (int8_t) dx;
        c->phase_dy[i] = (int8_t) dy;
    }

    c->prev_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->ref_y  = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->on_y     = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->off_y    = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_on_y  = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_off_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->veil    = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_veil = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->phase    = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_phase = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    if(!c->prev_y || !c->ref_y ||
       !c->on_y || !c->off_y || !c->nx_on_y || !c->nx_off_y ||
       !c->veil || !c->nx_veil || !c->phase || !c->nx_phase) {
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

        if(c->veil)
            free(c->veil);
        if(c->nx_veil)
            free(c->nx_veil);

        if(c->phase)
            free(c->phase);
        if(c->nx_phase)
            free(c->nx_phase);

        free(c);
        return NULL;
    }

    return (void *) c;
}

void chronomirror_free(void *ptr)
{
    chronomirror_t *c = (chronomirror_t *) ptr;

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

    if(c->veil)
        free(c->veil);
    if(c->nx_veil)
        free(c->nx_veil);

    if(c->phase)
        free(c->phase);
    if(c->nx_phase)
        free(c->nx_phase);

    free(c);
}

static void cs_seed(chronomirror_t *c, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        c->prev_y[i] = Y[i];
        c->ref_y[i] = Y[i];

        c->on_y[i] = 0;
        c->off_y[i] = 0;
        c->nx_on_y[i] = 0;
        c->nx_off_y[i] = 0;

        c->veil[i] = 0;
        c->nx_veil[i] = 0;

        c->phase[i] = 192;
        c->nx_phase[i] = 192;
    }

    c->seeded = 1;
}

static void cs_build_luts_if_needed(chronomirror_t *c,
                                    int threshold,
                                    int rise,
                                    int curl,
                                    int decay,
                                    int density,
                                    int source_bleed,
                                    int turbulence)
{
    int i;

    int rebuild_field =
        !c->field_lut_valid ||
        c->last_threshold != threshold ||
        c->last_rise != rise ||
        c->last_curl != curl ||
        c->last_decay != decay ||
        c->last_density != density ||
        c->last_turbulence != turbulence;

    int rebuild_bleed =
        !c->bleed_lut_valid ||
        c->last_source_bleed != source_bleed;

    if(rebuild_field) {
        int denom;
        int smoke_decay_power;
        int veil_decay_power;
        int rise_power;
        int curl_power;
        int spread_power;
        int density_power;

        denom = 255 - threshold;
        if(denom < 1)
            denom = 1;

        smoke_decay_power = decay;

        veil_decay_power = decay + ((255 - decay) >> 2);
        if(veil_decay_power > 255)
            veil_decay_power = 255;

        rise_power = ((72 + rise + (density >> 2)) * decay + 127) / 255;
        if(rise_power > 255)
            rise_power = 255;

        curl_power = ((32 + curl + (turbulence >> 1)) * (176 + (decay >> 2)) + 127) / 255;
        if(curl_power > 255)
            curl_power = 255;

        spread_power = ((56 + turbulence + (rise >> 2)) * veil_decay_power + 127) / 255;
        if(spread_power > 255)
            spread_power = 255;

        density_power = 96 + density;
        if(density_power > 255)
            density_power = 255;

        for(i = 0; i < 256; i++) {
            int event_strength = 0;
            int excess;
            int gain;
            int mem;

            if(i > threshold) {
                excess = i - threshold;
                gain = 128 + (density >> 1) + (rise >> 2);

                event_strength = (excess * gain + denom / 2) / denom;
                if(event_strength > 255)
                    event_strength = 255;
            }

            c->event_lut[i] = (uint8_t) event_strength;

            c->smoke_decay_lut[i] = (uint8_t) ((i * smoke_decay_power + 127) / 255);
            c->veil_decay_lut[i]  = (uint8_t) ((i * veil_decay_power + 127) / 255);

            c->rise_lut[i]    = (uint8_t) ((i * rise_power + 127) / 255);
            c->curl_lut[i]    = (uint8_t) ((i * curl_power + 127) / 255);
            c->spread_lut[i]  = (uint8_t) ((i * spread_power + 127) / 255);
            c->density_lut[i] = (uint8_t) ((i * density_power + 127) / 255);

            mem = 7 + ((255 - decay) >> 3) + (i >> 4);
            if(mem > 255)
                mem = 255;

            c->adapt_lut[i] = (uint8_t) mem;
        }

        c->last_threshold = threshold;
        c->last_rise = rise;
        c->last_curl = curl;
        c->last_decay = decay;
        c->last_density = density;
        c->last_turbulence = turbulence;
        c->field_lut_valid = 1;
    }

    if(rebuild_bleed) {
        for(i = 0; i < 256; i++) {
            c->bleed_y_lut[i] =
                (uint8_t) ((i * source_bleed + 127) / 255);

            c->bleed_uv_lut[i] =
                (uint8_t) ((128 * (255 - source_bleed) + i * source_bleed + 127) / 255);
        }

        c->last_source_bleed = source_bleed;
        c->bleed_lut_valid = 1;
    }
}

static inline int cs_rise_step(int rise)
{
    return 1 + ((rise * 3 + 127) / 255);
}

static inline int cs_curl_step(int curl, int turbulence)
{
    int v = curl + (turbulence >> 1);

    if(v > 255)
        v = 255;

    return 1 + ((v * 3 + 127) / 255);
}

static inline int cs_eddy_dir_fast(int x, int y, int frame_eddy)
{
    return (((x >> 4) ^ (y >> 4) ^ frame_eddy) & 1) ? 1 : -1;
}

static inline int cs_eddy_dir_row(int x, int ed_base)
{
    int b = ((x >> 4) ^ ed_base) & 1;
    return (b << 1) - 1;
}

static inline int cs_dir_to_phase(int dx, int dy)
{
    if(dx > 0 && dy == 0)
        return 0;
    if(dx > 0 && dy < 0)
        return 48;
    if(dx == 0 && dy < 0)
        return 80;
    if(dx < 0 && dy < 0)
        return 112;
    if(dx < 0 && dy == 0)
        return 144;
    if(dx < 0 && dy > 0)
        return 176;
    if(dx == 0 && dy > 0)
        return 208;

    return 240;
}

static inline int cs_phase_from_gradient_direct(int gx,
                                                int gy,
                                                int diff_raw)
{
    int ax = cs_absi(gx);
    int ay = cs_absi(gy);

    if(ax > ay)
        return gx >= 0 ? 80 : 208;

    if(diff_raw < 0)
        return gy >= 0 ? 48 : 112;

    return gy >= 0 ? 112 : 48;
}

static inline int cs_phase_from_source_safe(uint8_t *restrict Y,
                                            int w,
                                            int h,
                                            int x,
                                            int y,
                                            int pos,
                                            int diff_raw)
{
    int gx = 0;
    int gy = 0;

    int ax;
    int ay;

    int dx;
    int dy;

    if(x > 0 && x + 1 < w)
        gx = (int) Y[pos + 1] - (int) Y[pos - 1];

    if(y > 0 && y + 1 < h)
        gy = (int) Y[pos + w] - (int) Y[pos - w];

    ax = cs_absi(gx);
    ay = cs_absi(gy);

    if(ax > ay) {
        dx = 0;
        dy = gx >= 0 ? -1 : 1;
    }
    else {
        dx = gy >= 0 ? -1 : 1;
        dy = -1;
    }

    if(diff_raw < 0)
        dx = -dx;

    return cs_dir_to_phase(dx, dy);
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

static inline void cs_compute_one_safe(chronomirror_t *c,
                                       uint8_t *restrict Y,
                                       int x,
                                       int y,
                                       int pos,
                                       int rise,
                                       int curl,
                                       int turbulence,
                                       int frame_eddy)
{
    int w = c->w;
    int h = c->h;

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict NXON = c->nx_on_y;
    uint8_t *restrict NXOFF = c->nx_off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict NXVEIL = c->nx_veil;
    uint8_t *restrict PH = c->phase;
    uint8_t *restrict NXPH = c->nx_phase;
    uint8_t *restrict REF = c->ref_y;
    uint8_t *restrict PREV = c->prev_y;

    int old_on = ON[pos];
    int old_off = OFF[pos];
    int old_veil = VEIL[pos];
    int active_old = old_on | old_off | old_veil;

    int old_phase = PH[pos];

    uint8_t cy = Y[pos];

    int ref = REF[pos];
    int diff_raw = (int) cy - ref;
    int diff_ref = cs_absi(diff_raw);
    int diff_prev = cs_absi((int) cy - (int) PREV[pos]);
    int diff = diff_prev > diff_ref ? diff_prev : diff_ref;

    int event_strength = c->event_lut[diff];
    int event_for_adapt = event_strength;

    int on = c->smoke_decay_lut[old_on];
    int off = c->smoke_decay_lut[old_off];
    int veil = c->veil_decay_lut[old_veil];
    int out_phase = old_phase;

    int rise_step = cs_rise_step(rise);
    int curl_step = cs_curl_step(curl, turbulence);

    int dx = c->phase_dx[old_phase];
    int dy = c->phase_dy[old_phase];

    int lateral = dx * curl_step;
    int src;
    int vo;
    int vf;

    int avg;

    if(turbulence > 0 && active_old > CS_ACTIVITY_GATE) {
        int ed = cs_eddy_dir_fast(x, y, frame_eddy);
        lateral += ed * (1 + (turbulence >> 7));
    }

    src = cs_index_clamped(w, h, x - lateral, y + rise_step);

    vo = c->rise_lut[ON[src]];
    vf = c->rise_lut[OFF[src]];

    if(vo > on) {
        on = vo;
        out_phase = PH[src];
    }

    if(vf > off) {
        off = vf;
        out_phase = PH[src];
    }

    avg = old_veil + old_on + old_off;

    if(x > 0)
        avg += VEIL[pos - 1];

    if(x + 1 < w)
        avg += VEIL[pos + 1];

    if(y > 0)
        avg += VEIL[pos - w];

    if(y + 1 < h)
        avg += VEIL[pos + w];

    avg = (avg * 37 + 128) >> 8;

    vo = c->spread_lut[avg];
    if(vo > veil)
        veil = vo;

    if((curl > 0 || turbulence > 0) && active_old > CS_ACTIVITY_GATE) {
        int ed = cs_eddy_dir_fast(x, y, frame_eddy);
        int sx = x + ed * curl_step;
        int sy = y + rise_step + (dy > 0 ? 1 : 0);
        int ssrc = cs_index_clamped(w, h, sx, sy);

        int co = c->curl_lut[ON[ssrc]];
        int cf = c->curl_lut[OFF[ssrc]];
        int cv = c->curl_lut[VEIL[ssrc]];

        if(co > on) {
            on = co;
            out_phase = (PH[ssrc] + (ed > 0 ? 32 : 224)) & 255;
        }

        if(cf > off) {
            off = cf;
            out_phase = (PH[ssrc] + (ed > 0 ? 224 : 32)) & 255;
        }

        if(cv > veil)
            veil = cv;
    }

    if(event_strength > 0) {
        int edge = cs_edge_safe(Y, w, h, x, y, pos);
        int cavity = 255 - (int) cy;

        event_strength += (event_strength * edge) >> 8;
        event_strength += (event_strength * cavity) >> 10;

        if(event_strength > 255)
            event_strength = 255;

        if(diff_raw >= 0) {
            on += event_strength;
            if(on > 255)
                on = 255;
        }
        else {
            off += event_strength;
            if(off > 255)
                off = 255;
        }

        veil += (event_strength * (96 + turbulence) + 32767) >> 16;
        if(veil > 255)
            veil = 255;

        out_phase = cs_phase_from_source_safe(
            Y,
            w,
            h,
            x,
            y,
            pos,
            diff_raw
        );
    }

    {
        int m = on < off ? on : off;
        int cut = m >> 4;

        on -= cut;
        off -= cut;
    }

    NXON[pos] = (uint8_t) on;
    NXOFF[pos] = (uint8_t) off;
    NXVEIL[pos] = (uint8_t) veil;
    NXPH[pos] = (uint8_t) out_phase;

    REF[pos] = cs_blend_fast_u8(
        (uint8_t) ref,
        cy,
        c->adapt_lut[event_for_adapt]
    );

    PREV[pos] = cy;
}

static void cs_compute_border(chronomirror_t *c,
                              uint8_t *restrict Y,
                              int rise,
                              int curl,
                              int turbulence,
                              int frame_eddy,
                              int xmin,
                              int xmax,
                              int ymin,
                              int ymax)
{
    int w = c->w;
    int h = c->h;
    int y;

    if(ymin > 0) {
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
                    rise,
                    curl,
                    turbulence,
                    frame_eddy
                );
            }
        }
    }

    if(ymax + 1 < h) {
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
                    rise,
                    curl,
                    turbulence,
                    frame_eddy
                );
            }
        }
    }

    if(xmin > 0 && ymin <= ymax) {
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
                    rise,
                    curl,
                    turbulence,
                    frame_eddy
                );
            }
        }
    }

    if(xmax + 1 < w && ymin <= ymax) {
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
                    rise,
                    curl,
                    turbulence,
                    frame_eddy
                );
            }
        }
    }
}

static void cs_compute_smoke_plain(chronomirror_t *c,
                                   VJFrame *frame,
                                   int rise)
{
    uint8_t *restrict Y = frame->data[0];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict NXON = c->nx_on_y;
    uint8_t *restrict NXOFF = c->nx_off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict NXVEIL = c->nx_veil;
    uint8_t *restrict PH = c->phase;
    uint8_t *restrict NXPH = c->nx_phase;
    uint8_t *restrict REF = c->ref_y;
    uint8_t *restrict PREV = c->prev_y;

    uint8_t *restrict EVENT = c->event_lut;
    uint8_t *restrict SDECAY = c->smoke_decay_lut;
    uint8_t *restrict VDECAY = c->veil_decay_lut;
    uint8_t *restrict RISE = c->rise_lut;
    uint8_t *restrict SPREAD = c->spread_lut;
    uint8_t *restrict ADAPT = c->adapt_lut;

    int w = c->w;
    int h = c->h;

    int rise_step = cs_rise_step(rise);
    int rise_offset = rise_step * w;
    int frame_eddy = c->frame >> 3;

    int xmin = 1;
    int xmax = w - 2;
    int ymin = 1;
    int ymax = h - 1 - rise_step;

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
                    rise,
                    0,
                    0,
                    frame_eddy
                );
            }
        }

        return;
    }

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(y = ymin; y <= ymax; y++) {
        int x;
        int pos = y * w + xmin;
        int left_veil = VEIL[pos - 1];

        for(x = xmin; x <= xmax; x++, pos++) {
            uint8_t cy = Y[pos];

            int old_on = ON[pos];
            int old_off = OFF[pos];
            int old_veil = VEIL[pos];
            int right_veil = VEIL[pos + 1];
            int old_phase = PH[pos];

            int ref = REF[pos];
            int diff_raw = (int) cy - ref;
            int diff_ref = cs_absi(diff_raw);
            int diff_prev = cs_absi((int) cy - (int) PREV[pos]);
            int diff = diff_prev > diff_ref ? diff_prev : diff_ref;

            int event_strength = EVENT[diff];
            int event_for_adapt = event_strength;

            int on = SDECAY[old_on];
            int off = SDECAY[old_off];
            int veil = VDECAY[old_veil];
            int out_phase = old_phase;

            int src = pos + rise_offset;
            int vo = RISE[ON[src]];
            int vf = RISE[OFF[src]];

            int avg;

            if(vo > on) {
                on = vo;
                out_phase = PH[src];
            }

            if(vf > off) {
                off = vf;
                out_phase = PH[src];
            }

            avg =
                old_veil +
                left_veil +
                right_veil +
                VEIL[pos - w] +
                VEIL[pos + w] +
                old_on +
                old_off;

            left_veil = old_veil;

            avg = (avg * 37 + 128) >> 8;

            vo = SPREAD[avg];
            if(vo > veil)
                veil = vo;

            if(event_strength > 0) {
                int gx = (int) Y[pos + 1] - (int) Y[pos - 1];
                int gy = (int) Y[pos + w] - (int) Y[pos - w];

                int ax = cs_absi(gx);
                int ay = cs_absi(gy);
                int edge = ax > ay ? ax : ay;
                int cavity = 255 - (int) cy;

                event_strength += (event_strength * edge) >> 8;
                event_strength += (event_strength * cavity) >> 10;

                if(event_strength > 255)
                    event_strength = 255;

                if(diff_raw >= 0) {
                    on += event_strength;
                    if(on > 255)
                        on = 255;
                }
                else {
                    off += event_strength;
                    if(off > 255)
                        off = 255;
                }

                veil += (event_strength * 96 + 32767) >> 16;
                if(veil > 255)
                    veil = 255;

                out_phase = cs_phase_from_gradient_direct(
                    gx,
                    gy,
                    diff_raw
                );
            }

            {
                int m = on < off ? on : off;
                int cut = m >> 4;

                on -= cut;
                off -= cut;
            }

            NXON[pos] = (uint8_t) on;
            NXOFF[pos] = (uint8_t) off;
            NXVEIL[pos] = (uint8_t) veil;
            NXPH[pos] = (uint8_t) out_phase;

            REF[pos] = cs_blend_fast_u8(
                (uint8_t) ref,
                cy,
                ADAPT[event_for_adapt]
            );

            PREV[pos] = cy;
        }
    }

    cs_compute_border(
        c,
        Y,
        rise,
        0,
        0,
        frame_eddy,
        xmin,
        xmax,
        ymin,
        ymax
    );
}

static void cs_compute_smoke_curl(chronomirror_t *c,
                                  VJFrame *frame,
                                  int rise,
                                  int curl)
{
    uint8_t *restrict Y = frame->data[0];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict NXON = c->nx_on_y;
    uint8_t *restrict NXOFF = c->nx_off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict NXVEIL = c->nx_veil;
    uint8_t *restrict PH = c->phase;
    uint8_t *restrict NXPH = c->nx_phase;
    uint8_t *restrict REF = c->ref_y;
    uint8_t *restrict PREV = c->prev_y;

    uint8_t *restrict EVENT = c->event_lut;
    uint8_t *restrict SDECAY = c->smoke_decay_lut;
    uint8_t *restrict VDECAY = c->veil_decay_lut;
    uint8_t *restrict RISE = c->rise_lut;
    uint8_t *restrict CURL = c->curl_lut;
    uint8_t *restrict SPREAD = c->spread_lut;
    uint8_t *restrict ADAPT = c->adapt_lut;

    int8_t *restrict PDX = c->phase_dx;
    int8_t *restrict PDY = c->phase_dy;

    int w = c->w;
    int h = c->h;

    int rise_step = cs_rise_step(rise);
    int rise_offset = rise_step * w;
    int curl_step = cs_curl_step(curl, 0);
    int frame_eddy = c->frame >> 3;

    int margin_x = 2 + curl_step;
    int margin_y = 3 + rise_step;

    int xmin = margin_x;
    int xmax = w - 1 - margin_x;
    int ymin = margin_y;
    int ymax = h - 1 - margin_y;

    int y;

    if(w < 7 || h < 7 || xmin > xmax || ymin > ymax) {
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
                    rise,
                    curl,
                    0,
                    frame_eddy
                );
            }
        }

        return;
    }

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(y = ymin; y <= ymax; y++) {
        int x;
        int pos = y * w + xmin;
        int left_veil = VEIL[pos - 1];
        int ed_base = (y >> 4) ^ frame_eddy;

        for(x = xmin; x <= xmax; x++, pos++) {
            uint8_t cy = Y[pos];

            int old_on = ON[pos];
            int old_off = OFF[pos];
            int old_veil = VEIL[pos];
            int right_veil = VEIL[pos + 1];
            int active_old = old_on | old_off | old_veil;

            int old_phase = PH[pos];

            int ref = REF[pos];
            int diff_raw = (int) cy - ref;
            int diff_ref = cs_absi(diff_raw);
            int diff_prev = cs_absi((int) cy - (int) PREV[pos]);
            int diff = diff_prev > diff_ref ? diff_prev : diff_ref;

            int event_strength = EVENT[diff];
            int event_for_adapt = event_strength;

            int on = SDECAY[old_on];
            int off = SDECAY[old_off];
            int veil = VDECAY[old_veil];
            int out_phase = old_phase;

            int dx = PDX[old_phase];
            int lateral = dx * curl_step;

            int src = pos + rise_offset - lateral;
            int vo = RISE[ON[src]];
            int vf = RISE[OFF[src]];

            int avg;

            if(vo > on) {
                on = vo;
                out_phase = PH[src];
            }

            if(vf > off) {
                off = vf;
                out_phase = PH[src];
            }

            avg =
                old_veil +
                left_veil +
                right_veil +
                VEIL[pos - w] +
                VEIL[pos + w] +
                old_on +
                old_off;

            left_veil = old_veil;

            avg = (avg * 37 + 128) >> 8;

            vo = SPREAD[avg];
            if(vo > veil)
                veil = vo;

            if(active_old > CS_ACTIVITY_GATE) {
                int ed = cs_eddy_dir_row(x, ed_base);
                int dy = PDY[old_phase];
                int ssrc = pos + rise_offset + ed * curl_step + (dy > 0 ? w : 0);

                int co = CURL[ON[ssrc]];
                int cf = CURL[OFF[ssrc]];
                int cv = CURL[VEIL[ssrc]];

                if(co > on) {
                    on = co;
                    out_phase = (PH[ssrc] + (ed > 0 ? 32 : 224)) & 255;
                }

                if(cf > off) {
                    off = cf;
                    out_phase = (PH[ssrc] + (ed > 0 ? 224 : 32)) & 255;
                }

                if(cv > veil)
                    veil = cv;
            }

            if(event_strength > 0) {
                int gx = (int) Y[pos + 1] - (int) Y[pos - 1];
                int gy = (int) Y[pos + w] - (int) Y[pos - w];

                int ax = cs_absi(gx);
                int ay = cs_absi(gy);
                int edge = ax > ay ? ax : ay;
                int cavity = 255 - (int) cy;

                event_strength += (event_strength * edge) >> 8;
                event_strength += (event_strength * cavity) >> 10;

                if(event_strength > 255)
                    event_strength = 255;

                if(diff_raw >= 0) {
                    on += event_strength;
                    if(on > 255)
                        on = 255;
                }
                else {
                    off += event_strength;
                    if(off > 255)
                        off = 255;
                }

                veil += (event_strength * 96 + 32767) >> 16;
                if(veil > 255)
                    veil = 255;

                out_phase = cs_phase_from_gradient_direct(
                    gx,
                    gy,
                    diff_raw
                );
            }

            {
                int m = on < off ? on : off;
                int cut = m >> 4;

                on -= cut;
                off -= cut;
            }

            NXON[pos] = (uint8_t) on;
            NXOFF[pos] = (uint8_t) off;
            NXVEIL[pos] = (uint8_t) veil;
            NXPH[pos] = (uint8_t) out_phase;

            REF[pos] = cs_blend_fast_u8(
                (uint8_t) ref,
                cy,
                ADAPT[event_for_adapt]
            );

            PREV[pos] = cy;
        }
    }

    cs_compute_border(
        c,
        Y,
        rise,
        curl,
        0,
        frame_eddy,
        xmin,
        xmax,
        ymin,
        ymax
    );
}

static void cs_compute_smoke_turbulent(chronomirror_t *c,
                                       VJFrame *frame,
                                       int rise,
                                       int curl,
                                       int turbulence)
{
    uint8_t *restrict Y = frame->data[0];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict NXON = c->nx_on_y;
    uint8_t *restrict NXOFF = c->nx_off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict NXVEIL = c->nx_veil;
    uint8_t *restrict PH = c->phase;
    uint8_t *restrict NXPH = c->nx_phase;
    uint8_t *restrict REF = c->ref_y;
    uint8_t *restrict PREV = c->prev_y;

    uint8_t *restrict EVENT = c->event_lut;
    uint8_t *restrict SDECAY = c->smoke_decay_lut;
    uint8_t *restrict VDECAY = c->veil_decay_lut;
    uint8_t *restrict RISE = c->rise_lut;
    uint8_t *restrict CURL = c->curl_lut;
    uint8_t *restrict SPREAD = c->spread_lut;
    uint8_t *restrict ADAPT = c->adapt_lut;

    int8_t *restrict PDX = c->phase_dx;
    int8_t *restrict PDY = c->phase_dy;

    int w = c->w;
    int h = c->h;

    int rise_step = cs_rise_step(rise);
    int rise_offset = rise_step * w;
    int curl_step = cs_curl_step(curl, turbulence);
    int frame_eddy = c->frame >> 3;
    int turb_side = 1 + (turbulence >> 7);

    int margin_x = 2 + curl_step + (turbulence >> 7);
    int margin_y = 3 + rise_step;

    int xmin = margin_x;
    int xmax = w - 1 - margin_x;
    int ymin = margin_y;
    int ymax = h - 1 - margin_y;

    int y;

    if(w < 7 || h < 7 || xmin > xmax || ymin > ymax) {
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
                    rise,
                    curl,
                    turbulence,
                    frame_eddy
                );
            }
        }

        return;
    }

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(y = ymin; y <= ymax; y++) {
        int x;
        int pos = y * w + xmin;
        int left_veil = VEIL[pos - 1];
        int ed_base = (y >> 4) ^ frame_eddy;

        for(x = xmin; x <= xmax; x++, pos++) {
            uint8_t cy = Y[pos];

            int old_on = ON[pos];
            int old_off = OFF[pos];
            int old_veil = VEIL[pos];
            int right_veil = VEIL[pos + 1];
            int active_old = old_on | old_off | old_veil;
            int is_active = active_old > CS_ACTIVITY_GATE;

            int old_phase = PH[pos];

            int ref = REF[pos];
            int diff_raw = (int) cy - ref;
            int diff_ref = cs_absi(diff_raw);
            int diff_prev = cs_absi((int) cy - (int) PREV[pos]);
            int diff = diff_prev > diff_ref ? diff_prev : diff_ref;

            int event_strength = EVENT[diff];
            int event_for_adapt = event_strength;

            int on = SDECAY[old_on];
            int off = SDECAY[old_off];
            int veil = VDECAY[old_veil];
            int out_phase = old_phase;

            int dx = PDX[old_phase];
            int lateral = dx * curl_step;
            int ed = 0;

            int src;
            int vo;
            int vf;
            int avg;

            if(is_active) {
                ed = cs_eddy_dir_row(x, ed_base);
                lateral += ed * turb_side;
            }

            src = pos + rise_offset - lateral;

            vo = RISE[ON[src]];
            vf = RISE[OFF[src]];

            if(vo > on) {
                on = vo;
                out_phase = PH[src];
            }

            if(vf > off) {
                off = vf;
                out_phase = PH[src];
            }

            avg =
                old_veil +
                left_veil +
                right_veil +
                VEIL[pos - w] +
                VEIL[pos + w] +
                old_on +
                old_off;

            left_veil = old_veil;

            avg = (avg * 37 + 128) >> 8;

            vo = SPREAD[avg];
            if(vo > veil)
                veil = vo;

            if(is_active) {
                int dy = PDY[old_phase];
                int ssrc = pos + rise_offset + ed * curl_step + (dy > 0 ? w : 0);

                int co = CURL[ON[ssrc]];
                int cf = CURL[OFF[ssrc]];
                int cv = CURL[VEIL[ssrc]];

                if(co > on) {
                    on = co;
                    out_phase = (PH[ssrc] + (ed > 0 ? 32 : 224)) & 255;
                }

                if(cf > off) {
                    off = cf;
                    out_phase = (PH[ssrc] + (ed > 0 ? 224 : 32)) & 255;
                }

                if(cv > veil)
                    veil = cv;
            }

            if(event_strength > 0) {
                int gx = (int) Y[pos + 1] - (int) Y[pos - 1];
                int gy = (int) Y[pos + w] - (int) Y[pos - w];

                int ax = cs_absi(gx);
                int ay = cs_absi(gy);
                int edge = ax > ay ? ax : ay;
                int cavity = 255 - (int) cy;

                event_strength += (event_strength * edge) >> 8;
                event_strength += (event_strength * cavity) >> 10;

                if(event_strength > 255)
                    event_strength = 255;

                if(diff_raw >= 0) {
                    on += event_strength;
                    if(on > 255)
                        on = 255;
                }
                else {
                    off += event_strength;
                    if(off > 255)
                        off = 255;
                }

                veil += (event_strength * (96 + turbulence) + 32767) >> 16;
                if(veil > 255)
                    veil = 255;

                out_phase = cs_phase_from_gradient_direct(
                    gx,
                    gy,
                    diff_raw
                );
            }

            {
                int m = on < off ? on : off;
                int cut = m >> 4;

                on -= cut;
                off -= cut;
            }

            NXON[pos] = (uint8_t) on;
            NXOFF[pos] = (uint8_t) off;
            NXVEIL[pos] = (uint8_t) veil;
            NXPH[pos] = (uint8_t) out_phase;

            REF[pos] = cs_blend_fast_u8(
                (uint8_t) ref,
                cy,
                ADAPT[event_for_adapt]
            );

            PREV[pos] = cy;
        }
    }

    cs_compute_border(
        c,
        Y,
        rise,
        curl,
        turbulence,
        frame_eddy,
        xmin,
        xmax,
        ymin,
        ymax
    );
}

static void cs_compute_smoke(chronomirror_t *c,
                             VJFrame *frame,
                             int rise,
                             int curl,
                             int turbulence)
{
    if(turbulence > 0) {
        cs_compute_smoke_turbulent(
            c,
            frame,
            rise,
            curl,
            turbulence
        );
    }
    else if(curl > 0) {
        cs_compute_smoke_curl(
            c,
            frame,
            rise,
            curl
        );
    }
    else {
        cs_compute_smoke_plain(
            c,
            frame,
            rise
        );
    }
}

static void cs_swap_fields(chronomirror_t *c)
{
    uint8_t *t;

    t = c->on_y;
    c->on_y = c->nx_on_y;
    c->nx_on_y = t;

    t = c->off_y;
    c->off_y = c->nx_off_y;
    c->nx_off_y = t;

    t = c->veil;
    c->veil = c->nx_veil;
    c->nx_veil = t;

    t = c->phase;
    c->phase = c->nx_phase;
    c->nx_phase = t;
}

static void cs_render_const_pure(chronomirror_t *c,
                                 VJFrame *frame,
                                 int on_u,
                                 int on_v,
                                 int off_u,
                                 int off_v)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict DLUT = c->density_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        int on = ON[i];
        int off = OFF[i];
        int veil = VEIL[i];

        int plume = on + off;
        int active;
        int yy;

        int ev_u;
        int ev_v;

        if(plume > 255)
            plume = 255;

        active = plume + (veil >> 1);
        if(active > 255)
            active = 255;

        if(active <= 0) {
            Y[i] = 0;
            U[i] = 128;
            V[i] = 128;
            continue;
        }

        yy = plume - (DLUT[veil] >> 2);
        if(yy < 0)
            yy = 0;
        else if(yy > 255)
            yy = 255;

        if(plume > 0) {
            if(on >= off) {
                int amount = 128 + ((on - off) >> 1);
                if(amount > 255)
                    amount = 255;

                ev_u = cs_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount);
                ev_v = cs_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount);
            }
            else {
                int amount = 128 + ((off - on) >> 1);
                if(amount > 255)
                    amount = 255;

                ev_u = cs_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);
                ev_v = cs_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);
            }
        }
        else {
            ev_u = 128;
            ev_v = 128;
        }

        Y[i] = (uint8_t) yy;
        U[i] = cs_blend_fast_u8(128, (uint8_t) ev_u, active);
        V[i] = cs_blend_fast_u8(128, (uint8_t) ev_v, active);
    }
}

static void cs_render_const_bleed(chronomirror_t *c,
                                  VJFrame *frame,
                                  int on_u,
                                  int on_v,
                                  int off_u,
                                  int off_v)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict BY = c->bleed_y_lut;
    uint8_t *restrict BUV = c->bleed_uv_lut;
    uint8_t *restrict DLUT = c->density_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        int on = ON[i];
        int off = OFF[i];
        int veil = VEIL[i];

        int plume = on + off;
        int active;
        int base_y;
        int yy;

        uint8_t base_u;
        uint8_t base_v;

        int ev_u;
        int ev_v;

        if(plume > 255)
            plume = 255;

        active = plume + (veil >> 1);
        if(active > 255)
            active = 255;

        base_y = BY[Y[i]];
        base_u = BUV[U[i]];
        base_v = BUV[V[i]];

        if(active <= 0) {
            Y[i] = (uint8_t) base_y;
            U[i] = base_u;
            V[i] = base_v;
            continue;
        }

        yy = base_y + plume - (DLUT[veil] >> 2);
        if(yy < 0)
            yy = 0;
        else if(yy > 255)
            yy = 255;

        if(plume > 0) {
            if(on >= off) {
                int amount = 128 + ((on - off) >> 1);
                if(amount > 255)
                    amount = 255;

                ev_u = cs_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount);
                ev_v = cs_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount);
            }
            else {
                int amount = 128 + ((off - on) >> 1);
                if(amount > 255)
                    amount = 255;

                ev_u = cs_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);
                ev_v = cs_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);
            }
        }
        else {
            ev_u = 128;
            ev_v = 128;
        }

        Y[i] = (uint8_t) yy;
        U[i] = cs_blend_fast_u8(base_u, (uint8_t) ev_u, active);
        V[i] = cs_blend_fast_u8(base_v, (uint8_t) ev_v, active);
    }
}

static void cs_render_source_pure(chronomirror_t *c,
                                  VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict DLUT = c->density_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_u = U[i];
        uint8_t src_v = V[i];

        int on = ON[i];
        int off = OFF[i];
        int veil = VEIL[i];

        int plume = on + off;
        int active;
        int yy;

        int on_u = src_u;
        int on_v = src_v;
        int off_u = 255 - src_u;
        int off_v = 255 - src_v;

        int ev_u;
        int ev_v;

        if(plume > 255)
            plume = 255;

        active = plume + (veil >> 1);
        if(active > 255)
            active = 255;

        if(active <= 0) {
            Y[i] = 0;
            U[i] = 128;
            V[i] = 128;
            continue;
        }

        yy = plume - (DLUT[veil] >> 2);
        if(yy < 0)
            yy = 0;
        else if(yy > 255)
            yy = 255;

        if(on >= off) {
            int amount = 128 + ((on - off) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cs_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount);
            ev_v = cs_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount);
        }
        else {
            int amount = 128 + ((off - on) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cs_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);
            ev_v = cs_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);
        }

        Y[i] = (uint8_t) yy;
        U[i] = cs_blend_fast_u8(128, (uint8_t) ev_u, active);
        V[i] = cs_blend_fast_u8(128, (uint8_t) ev_v, active);
    }
}

static void cs_render_source_bleed(chronomirror_t *c,
                                   VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict BY = c->bleed_y_lut;
    uint8_t *restrict BUV = c->bleed_uv_lut;
    uint8_t *restrict DLUT = c->density_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        uint8_t src_y = Y[i];
        uint8_t src_u = U[i];
        uint8_t src_v = V[i];

        int on = ON[i];
        int off = OFF[i];
        int veil = VEIL[i];

        int plume = on + off;
        int active;
        int base_y;
        int yy;

        uint8_t base_u;
        uint8_t base_v;

        int on_u = src_u;
        int on_v = src_v;
        int off_u = 255 - src_u;
        int off_v = 255 - src_v;

        int ev_u;
        int ev_v;

        if(plume > 255)
            plume = 255;

        active = plume + (veil >> 1);
        if(active > 255)
            active = 255;

        base_y = BY[src_y];
        base_u = BUV[src_u];
        base_v = BUV[src_v];

        if(active <= 0) {
            Y[i] = (uint8_t) base_y;
            U[i] = base_u;
            V[i] = base_v;
            continue;
        }

        yy = base_y + plume - (DLUT[veil] >> 2);
        if(yy < 0)
            yy = 0;
        else if(yy > 255)
            yy = 255;

        if(on >= off) {
            int amount = 128 + ((on - off) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cs_blend_fast_u8((uint8_t) off_u, (uint8_t) on_u, amount);
            ev_v = cs_blend_fast_u8((uint8_t) off_v, (uint8_t) on_v, amount);
        }
        else {
            int amount = 128 + ((off - on) >> 1);
            if(amount > 255)
                amount = 255;

            ev_u = cs_blend_fast_u8((uint8_t) on_u, (uint8_t) off_u, amount);
            ev_v = cs_blend_fast_u8((uint8_t) on_v, (uint8_t) off_v, amount);
        }

        Y[i] = (uint8_t) yy;
        U[i] = cs_blend_fast_u8(base_u, (uint8_t) ev_u, active);
        V[i] = cs_blend_fast_u8(base_v, (uint8_t) ev_v, active);
    }
}

static void cs_render_white_pure(chronomirror_t *c,
                                 VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict VEIL = c->veil;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        int ev = ON[i] + OFF[i] + (VEIL[i] >> 1);

        if(ev > 255)
            ev = 255;

        Y[i] = (uint8_t) ev;
    }

    veejay_memset(U, 128, (size_t) len);
    veejay_memset(V, 128, (size_t) len);
}

static void cs_render_white_bleed(chronomirror_t *c,
                                  VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict ON = c->on_y;
    uint8_t *restrict OFF = c->off_y;
    uint8_t *restrict VEIL = c->veil;
    uint8_t *restrict BY = c->bleed_y_lut;
    uint8_t *restrict BUV = c->bleed_uv_lut;

    int len = c->len;
    int i;

#pragma omp parallel for schedule(static) num_threads(c->n_threads)
    for(i = 0; i < len; i++) {
        int ev = ON[i] + OFF[i] + (VEIL[i] >> 1);
        int base_y = BY[Y[i]];
        int yy;

        if(ev > 255)
            ev = 255;

        yy = base_y + ev;
        if(yy > 255)
            yy = 255;

        Y[i] = (uint8_t) yy;
        U[i] = BUV[U[i]];
        V[i] = BUV[V[i]];
    }
}

static void cs_render(chronomirror_t *c,
                      VJFrame *frame,
                      int source_bleed,
                      int color_mode)
{
    switch(color_mode) {
        case CS_COLOR_WHITE:
            if(source_bleed == 0)
                cs_render_white_pure(c, frame);
            else
                cs_render_white_bleed(c, frame);
            break;

        case CS_COLOR_THERMAL:
            if(source_bleed == 0)
                cs_render_const_pure(c, frame, 84, 220, 212, 84);
            else
                cs_render_const_bleed(c, frame, 84, 220, 212, 84);
            break;

        case CS_COLOR_SOURCE:
            if(source_bleed == 0)
                cs_render_source_pure(c, frame);
            else
                cs_render_source_bleed(c, frame);
            break;

        case CS_COLOR_ELECTRIC:
            if(source_bleed == 0)
                cs_render_const_pure(c, frame, 54, 196, 210, 54);
            else
                cs_render_const_bleed(c, frame, 54, 196, 210, 54);
            break;

        case CS_COLOR_POLARITY:
        default:
            if(source_bleed == 0)
                cs_render_const_pure(c, frame, 92, 226, 226, 92);
            else
                cs_render_const_bleed(c, frame, 92, 226, 226, 92);
            break;
    }
}

void chronomirror_apply(void *ptr, VJFrame *frame, int *args)
{
    chronomirror_t *c = (chronomirror_t *) ptr;

    int threshold;
    int rise;
    int curl;
    int decay;
    int density;
    int source_bleed;
    int color_mode;
    int turbulence;

    if(!c->seeded)
        cs_seed(c, frame);

    threshold    = cs_clampi(args[P_THRESHOLD], 0, 255);
    rise         = cs_clampi(args[P_RISE], 0, 255);
    curl         = cs_clampi(args[P_CURL], 0, 255);
    decay        = cs_clampi(args[P_DECAY], 0, 255);
    density      = cs_clampi(args[P_DENSITY], 0, 255);
    source_bleed = cs_clampi(args[P_SOURCE_BLEED], 0, 255);
    color_mode   = cs_clampi(args[P_COLOR_MODE], 0, 4);
    turbulence   = cs_clampi(args[P_TURBULENCE], 0, 255);

    cs_build_luts_if_needed(
        c,
        threshold,
        rise,
        curl,
        decay,
        density,
        source_bleed,
        turbulence
    );

    cs_compute_smoke(
        c,
        frame,
        rise,
        curl,
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