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
#include "chronocortex.h"

#define CHRONOFOLD_PARAMS 11

#define P_THRESHOLD     0
#define P_DECAY         1
#define P_GAIN          2
#define P_MEMORY        3
#define P_TRAIL         4
#define P_COLOR_MODE    5
#define P_NOISE         6
#define P_SOURCE_BLEED  7
#define P_BEAT_PUSH     8
#define P_FLASH_GAIN    9
#define P_COLOR_ENERGY 10

#define CF_COLOR_WHITE      0
#define CF_COLOR_POLARITY   1
#define CF_COLOR_SOURCE     2
#define CF_COLOR_THERMAL    3
#define CF_COLOR_INVERT     4

#define CF_OMP_FOR _Pragma("omp parallel for schedule(static) num_threads(c->n_threads)")

typedef struct {
    int w;
    int h;
    int len;
    int frame;
    int seeded;
    int n_threads;

    uint8_t *ref_y;

    uint8_t *ev_y;
    uint8_t *ev_u;
    uint8_t *ev_v;

    uint8_t *nx_y;
    uint8_t *nx_u;
    uint8_t *nx_v;

    uint8_t event_lut[256];
    uint8_t decay_lut[256];
    uint8_t trail_lut[256];
    uint8_t bleed_y_lut[256];
    uint8_t bleed_uv_lut[256];
    uint8_t output_lut[256];
    uint8_t mix_lut[256];
    uint8_t adapt_lut[256];
    uint8_t noise_lut[64];

    int neutralize;

    int lut_valid;
    int last_threshold;
    int last_decay;
    int last_gain;
    int last_memory;
    int last_trail;
    int last_noise;
    int last_source_bleed;
    int last_flash_gain;
    int last_color_energy;

    int last_color_mode;
} chronofold_t;

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

static inline int cf_scale_1000_to_255(int v)
{
    v = cf_clampi(v, 0, 1000);
    return (v * 255 + 500) / 1000;
}

static inline int cf_old_default_1000(int v)
{
    return (v * 1000 + 127) / 255;
}

static inline int cf_push_add_255(int base, int push, int amount)
{
    return cf_clampi(base + ((push * amount + 127) / 255), 0, 255);
}

static inline int cf_push_sub_255(int base, int push, int amount)
{
    return cf_clampi(base - ((push * amount + 127) / 255), 0, 255);
}

static inline int cf_push_add_1000(int base, int push, int amount)
{
    return cf_clampi(base + ((push * amount + 127) / 255), 0, 1000);
}

static inline uint32_t cf_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

vj_effect *chronofold_init(int w, int h)
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
    /*
     * input frame + adaptive reference + temporal difference
     *     -> event field
     *     -> afterimage / polarity / neural response
     *     -> output flash and chroma mix
     *
     * Most continuous controls are exposed at 0..1000 UI resolution and
     * converted to the internal 0..255 field scale in apply().
     *
     * Trigger Gate: lower = more events.
     * Event Decay: higher = longer afterimage.
     * Event Gain: temporal-difference amplification.
     * Retina Memory: adaptive reference speed.
     * Neural Trail: neighbor event diffusion / smear.
     * Beat Push: safe musical impact macro.
     * Flash Gain: output-only event brightness.
     * Color Energy: output-only chroma strength.
     */
    ve->limits[0][P_THRESHOLD] = 0;
    ve->limits[1][P_THRESHOLD] = 1000;
    ve->defaults[P_THRESHOLD] = cf_old_default_1000(18);

    ve->limits[0][P_DECAY] = 0;
    ve->limits[1][P_DECAY] = 1000;
    ve->defaults[P_DECAY] = cf_old_default_1000(224);

    ve->limits[0][P_GAIN] = 0;
    ve->limits[1][P_GAIN] = 1000;
    ve->defaults[P_GAIN] = cf_old_default_1000(212);

    ve->limits[0][P_MEMORY] = 4;
    ve->limits[1][P_MEMORY] = 1000;
    ve->defaults[P_MEMORY] = cf_old_default_1000(42);

    ve->limits[0][P_TRAIL] = 0;
    ve->limits[1][P_TRAIL] = 1000;
    ve->defaults[P_TRAIL] = cf_old_default_1000(128);

    ve->limits[0][P_COLOR_MODE] = 0;
    ve->limits[1][P_COLOR_MODE] = 4;
    ve->defaults[P_COLOR_MODE] = CF_COLOR_POLARITY;

    ve->limits[0][P_NOISE] = 0;
    ve->limits[1][P_NOISE] = 1000;
    ve->defaults[P_NOISE] = cf_old_default_1000(4);

    ve->limits[0][P_SOURCE_BLEED] = 0;
    ve->limits[1][P_SOURCE_BLEED] = 1000;
    ve->defaults[P_SOURCE_BLEED] = cf_old_default_1000(8);

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_FLASH_GAIN] = 0;
    ve->limits[1][P_FLASH_GAIN] = 1000;
    ve->defaults[P_FLASH_GAIN] = 500;

    ve->limits[0][P_COLOR_ENERGY] = 0;
    ve->limits[1][P_COLOR_ENERGY] = 1000;
    ve->defaults[P_COLOR_ENERGY] = 500;

    ve->description = "Chronofold Retina";
    ve->sub_format = 1;


    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Trigger Gate",
        "Event Decay",
        "Event Gain",
        "Retina Memory",
        "Neural Trail",
        "Color Mode",
        "Neural Noise",
        "Source Bleed",
        "Beat Push",
        "Flash Gain",
        "Color Energy"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DETAIL,       VJ_BEAT_F_PHRASE_ONLY,                    16,                 375,                20, 70,  1600, 3400, 700,  35,    /* Trigger Gate */
        VJ_BEAT_MEMORY,       VJ_BEAT_F_PHRASE_ONLY,                    470,                1000,               18, 72,  1800, 4200, 900,  55,    /* Event Decay */
        VJ_BEAT_TRIGGER,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE, 250,                940,                55, 180, 80,   420,  0,    90,    /* Event Gain */
        VJ_BEAT_INERTIA,      VJ_BEAT_F_PHRASE_ONLY,                    45,                 705,                20, 80,  1800, 4200, 900,  40,    /* Retina Memory */
        VJ_BEAT_FLOW,         VJ_BEAT_F_CONTINUOUS,                     60,                 825,                35, 140, 900,  2600, 0,    70,    /* Neural Trail */
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Color Mode */
        VJ_BEAT_DETAIL,       VJ_BEAT_F_CLIMAX_ONLY,                    0,                  280,                12, 70,  1800, 4200, 600,  20,    /* Neural Noise */
        VJ_BEAT_SOURCE_MIX,   VJ_BEAT_F_CONTINUOUS,                     0,                  375,                20, 95,  1200, 3000, 0,    40,    /* Source Bleed */
        VJ_BEAT_TRIGGER,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE, 0,                  1000,               70, 240, 60,   360,  0,    100,   /* Beat Push */
        VJ_BEAT_CONTRAST,     VJ_BEAT_F_CONTINUOUS,                     220,                1000,               45, 170, 700,  1800, 0,    85,    /* Flash Gain */
        VJ_BEAT_INTENSITY,    VJ_BEAT_F_CONTINUOUS,                     160,                1000,               35, 140, 800,  2200, 0,    65     /* Color Energy */
    );
    return ve;
}

void *chronofold_malloc(int w, int h)
{
    chronofold_t *c;

    if(w <= 0 || h <= 0)
        return NULL;

    c = (chronofold_t *) vj_calloc(sizeof(chronofold_t));
    if(!c)
        return NULL;

    c->w = w;
    c->h = h;
    c->len = w * h;
    c->frame = 0;
    c->seeded = 0;
    c->lut_valid = 0;
    c->last_color_mode = -1;

    c->n_threads = vje_advise_num_threads(w * h);
    if(c->n_threads <= 0)
        c->n_threads = 1;

    c->ref_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->ev_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->ev_u = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->ev_v = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    c->nx_y = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_u = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);
    c->nx_v = (uint8_t *) vj_calloc(sizeof(uint8_t) * (size_t) c->len);

    if(!c->ref_y || !c->ev_y || !c->ev_u || !c->ev_v ||
       !c->nx_y || !c->nx_u || !c->nx_v) {
        if(c->ref_y)
            free(c->ref_y);

        if(c->ev_y)
            free(c->ev_y);
        if(c->ev_u)
            free(c->ev_u);
        if(c->ev_v)
            free(c->ev_v);

        if(c->nx_y)
            free(c->nx_y);
        if(c->nx_u)
            free(c->nx_u);
        if(c->nx_v)
            free(c->nx_v);

        free(c);
        return NULL;
    }

    return (void *) c;
}

void chronofold_free(void *ptr)
{
    chronofold_t *c = (chronofold_t *) ptr;

    if(!c)
        return;

    if(c->ref_y)
        free(c->ref_y);

    if(c->ev_y)
        free(c->ev_y);
    if(c->ev_u)
        free(c->ev_u);
    if(c->ev_v)
        free(c->ev_v);

    if(c->nx_y)
        free(c->nx_y);
    if(c->nx_u)
        free(c->nx_u);
    if(c->nx_v)
        free(c->nx_v);

    free(c);
}

static void cf_seed(chronofold_t *c, VJFrame *frame)
{
    uint8_t *Y = frame->data[0];

    int i;
    int len = c->len;

    CF_OMP_FOR
    for(i = 0; i < len; i++) {
        c->ref_y[i] = Y[i];

        c->ev_y[i] = 0;
        c->ev_u[i] = 128;
        c->ev_v[i] = 128;

        c->nx_y[i] = 0;
        c->nx_u[i] = 128;
        c->nx_v[i] = 128;
    }

    c->seeded = 1;
}

static void cf_build_luts_if_needed(chronofold_t *c,
                                    int threshold,
                                    int decay,
                                    int gain,
                                    int memory,
                                    int trail,
                                    int noise,
                                    int source_bleed,
                                    int flash_gain,
                                    int color_energy)
{
    int i;
    int denom;
    int flash_q8;
    int color_q8;

    if(c->lut_valid &&
       c->last_threshold == threshold &&
       c->last_decay == decay &&
       c->last_gain == gain &&
       c->last_memory == memory &&
       c->last_trail == trail &&
       c->last_noise == noise &&
       c->last_source_bleed == source_bleed &&
       c->last_flash_gain == flash_gain &&
       c->last_color_energy == color_energy) {
        return;
    }

    denom = 255 - threshold;
    if(denom < 1)
        denom = 1;

    flash_gain = cf_clampi(flash_gain, 0, 1000);
    color_energy = cf_clampi(color_energy, 0, 1000);

    flash_q8 = 64 + ((flash_gain * 384 + 500) / 1000);
    color_q8 = 64 + ((color_energy * 384 + 500) / 1000);

    for(i = 0; i < 256; i++) {
        int event_strength;
        int excess;
        int mix;
        int mem;

        if(i > threshold) {
            excess = i - threshold;
            event_strength = (excess * gain * 2 + denom / 2) / denom;
            if(event_strength > 255)
                event_strength = 255;
        }
        else {
            event_strength = 0;
        }

        c->event_lut[i] = (uint8_t) event_strength;
        c->decay_lut[i] = (uint8_t) ((i * decay + 127) / 255);
        c->trail_lut[i] = (uint8_t) ((i * trail * decay + 32767) / 65025);

        c->bleed_y_lut[i] = (uint8_t) ((i * source_bleed + 127) / 255);
        c->bleed_uv_lut[i] =
            (uint8_t) ((128 * (255 - source_bleed) + i * source_bleed + 127) / 255);

        c->output_lut[i] = cf_u8((i * flash_q8 + 128) >> 8);

        mix = i + (i >> 1);
        if(mix > 255)
            mix = 255;

        mix = (mix * color_q8 + 128) >> 8;
        if(mix > 255)
            mix = 255;

        c->mix_lut[i] = (uint8_t) mix;

        mem = memory + (i >> 3);
        if(mem > 255)
            mem = 255;
        c->adapt_lut[i] = (uint8_t) mem;
    }

    for(i = 0; i < 64; i++) {
        int spontaneous = 16 + i;
        spontaneous = (spontaneous * noise + 127) / 255;
        if(spontaneous > 255)
            spontaneous = 255;
        c->noise_lut[i] = (uint8_t) spontaneous;
    }

    c->neutralize = 8 + ((255 - decay) >> 3);
    if(c->neutralize < 1)
        c->neutralize = 1;
    if(c->neutralize > 64)
        c->neutralize = 64;

    c->last_threshold = threshold;
    c->last_decay = decay;
    c->last_gain = gain;
    c->last_memory = memory;
    c->last_trail = trail;
    c->last_noise = noise;
    c->last_source_bleed = source_bleed;
    c->last_flash_gain = flash_gain;
    c->last_color_energy = color_energy;
    c->lut_valid = 1;
}

static inline void cf_event_color(int color_mode,
                                  int polarity,
                                  uint8_t src_u,
                                  uint8_t src_v,
                                  uint8_t *out_u,
                                  uint8_t *out_v)
{
    switch(color_mode) {
        case CF_COLOR_SOURCE:
            *out_u = src_u;
            *out_v = src_v;
            break;

        case CF_COLOR_THERMAL:
            if(polarity >= 0) {
                *out_u = 84;
                *out_v = 220;
            }
            else {
                *out_u = 212;
                *out_v = 84;
            }
            break;

        case CF_COLOR_INVERT:
            *out_u = (uint8_t) (255 - src_u);
            *out_v = (uint8_t) (255 - src_v);
            break;

        case CF_COLOR_POLARITY:
        default:
            if(polarity >= 0) {
                *out_u = 92;
                *out_v = 226;
            }
            else {
                *out_u = 226;
                *out_v = 92;
            }
            break;
    }
}

static inline int cf_neighbor_best_y_4(chronofold_t *c,
                                       int x,
                                       int y,
                                       int pos)
{
    int w = c->w;
    int h = c->h;

    int best = c->ev_y[pos];

    if(x > 0) {
        int v = c->ev_y[pos - 1];
        if(v > best)
            best = v;
    }

    if(x + 1 < w) {
        int v = c->ev_y[pos + 1];
        if(v > best)
            best = v;
    }

    if(y > 0) {
        int v = c->ev_y[pos - w];
        if(v > best)
            best = v;
    }

    if(y + 1 < h) {
        int v = c->ev_y[pos + w];
        if(v > best)
            best = v;
    }

    return best;
}

static inline int cf_neighbor_best_4(chronofold_t *c,
                                     int x,
                                     int y,
                                     int pos,
                                     uint8_t *nu,
                                     uint8_t *nv)
{
    int w = c->w;
    int h = c->h;

    int best = c->ev_y[pos];
    int best_pos = pos;

    if(x > 0) {
        int p = pos - 1;
        int v = c->ev_y[p];

        if(v > best) {
            best = v;
            best_pos = p;
        }
    }

    if(x + 1 < w) {
        int p = pos + 1;
        int v = c->ev_y[p];

        if(v > best) {
            best = v;
            best_pos = p;
        }
    }

    if(y > 0) {
        int p = pos - w;
        int v = c->ev_y[p];

        if(v > best) {
            best = v;
            best_pos = p;
        }
    }

    if(y + 1 < h) {
        int p = pos + w;
        int v = c->ev_y[p];

        if(v > best) {
            best = v;
            best_pos = p;
        }
    }

    *nu = c->ev_u[best_pos];
    *nv = c->ev_v[best_pos];

    return best;
}

static inline int cf_source_edge(uint8_t *restrict Y,
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

#define CF_DEFINE_COMPUTE_WHITE(NAME, USE_TRAIL, USE_NOISE)                         \
static void NAME(chronofold_t *c, VJFrame *frame)                                   \
{                                                                                   \
    uint8_t *restrict Y = frame->data[0];                                           \
    int w = c->w;                                                                   \
    int h = c->h;                                                                   \
    int y;                                                                          \
                                                                                    \
    CF_OMP_FOR                                                                      \
    for(y = 0; y < h; y++) {                                                        \
        int x;                                                                      \
        int pos = y * w;                                                            \
                                                                                    \
        for(x = 0; x < w; x++, pos++) {                                             \
            uint8_t cy = Y[pos];                                                    \
            int ref = c->ref_y[pos];                                                \
            int diff = (int) cy - ref;                                              \
            int ad = diff < 0 ? -diff : diff;                                       \
                                                                                    \
            int event_strength = c->event_lut[ad];                                  \
            int decayed = c->decay_lut[c->ev_y[pos]];                               \
            int final_ev;                                                           \
                                                                                    \
            if(event_strength > 0) {                                                \
                int edge = cf_source_edge(Y, w, h, x, y, pos);                      \
                event_strength += (event_strength * edge) >> 7;                     \
                if(event_strength > 255)                                            \
                    event_strength = 255;                                           \
            }                                                                       \
                                                                                    \
            if(USE_NOISE) {                                                         \
                uint32_t rnd = cf_hash_u32(                                         \
                    (uint32_t) pos ^                                                \
                    (uint32_t) (c->frame * 2654435761U));                           \
                if(((rnd >> 8) & 255U) < (unsigned int) (c->last_noise >> 2)) {      \
                    int spontaneous = c->noise_lut[rnd & 63U];                      \
                    if(spontaneous > event_strength)                                \
                        event_strength = spontaneous;                               \
                }                                                                   \
            }                                                                       \
                                                                                    \
            if(USE_TRAIL) {                                                         \
                int neighbor_ev = cf_neighbor_best_y_4(c, x, y, pos);               \
                int trailed = c->trail_lut[neighbor_ev];                            \
                if(trailed > decayed)                                               \
                    decayed = trailed;                                              \
            }                                                                       \
                                                                                    \
            final_ev = event_strength >= decayed ? event_strength : decayed;        \
            c->nx_y[pos] = (uint8_t) final_ev;                                      \
                                                                                    \
            c->ref_y[pos] = cf_blend_fast_u8(                                       \
                (uint8_t) ref,                                                      \
                cy,                                                                 \
                c->adapt_lut[event_strength]);                                      \
        }                                                                           \
    }                                                                               \
}

/*
 * Colored compute path:
 * - Maintains event U/V.
 * - Used for polarity/source/thermal/invert modes.
 */
#define CF_DEFINE_COMPUTE_COLOR(NAME, USE_TRAIL, USE_NOISE)                         \
static void NAME(chronofold_t *c, VJFrame *frame, int color_mode)                   \
{                                                                                   \
    uint8_t *restrict Y = frame->data[0];                                           \
    uint8_t *restrict U = frame->data[1];                                           \
    uint8_t *restrict V = frame->data[2];                                           \
    int w = c->w;                                                                   \
    int h = c->h;                                                                   \
    int y;                                                                          \
                                                                                    \
    CF_OMP_FOR                                                                      \
    for(y = 0; y < h; y++) {                                                        \
        int x;                                                                      \
        int pos = y * w;                                                            \
                                                                                    \
        for(x = 0; x < w; x++, pos++) {                                             \
            uint8_t cy = Y[pos];                                                    \
            uint8_t cu = U[pos];                                                    \
            uint8_t cv = V[pos];                                                    \
            int ref = c->ref_y[pos];                                                \
            int diff = (int) cy - ref;                                              \
            int ad = diff < 0 ? -diff : diff;                                       \
            int polarity = diff >= 0 ? 1 : -1;                                      \
                                                                                    \
            int event_strength = c->event_lut[ad];                                  \
            int decayed = c->decay_lut[c->ev_y[pos]];                               \
            int final_ev;                                                           \
                                                                                    \
            uint8_t final_u = c->ev_u[pos];                                         \
            uint8_t final_v = c->ev_v[pos];                                         \
                                                                                    \
            if(event_strength > 0) {                                                \
                int edge = cf_source_edge(Y, w, h, x, y, pos);                      \
                event_strength += (event_strength * edge) >> 7;                     \
                if(event_strength > 255)                                            \
                    event_strength = 255;                                           \
            }                                                                       \
                                                                                    \
            if(USE_NOISE) {                                                         \
                uint32_t rnd = cf_hash_u32(                                         \
                    (uint32_t) pos ^                                                \
                    (uint32_t) (c->frame * 2654435761U));                           \
                if(((rnd >> 8) & 255U) < (unsigned int) (c->last_noise >> 2)) {      \
                    int spontaneous = c->noise_lut[rnd & 63U];                      \
                    if(spontaneous > event_strength) {                              \
                        event_strength = spontaneous;                               \
                        polarity = (rnd & 1U) ? 1 : -1;                             \
                    }                                                               \
                }                                                                   \
            }                                                                       \
                                                                                    \
            if(USE_TRAIL) {                                                         \
                uint8_t trail_u;                                                    \
                uint8_t trail_v;                                                    \
                int neighbor_ev = cf_neighbor_best_4(                               \
                    c, x, y, pos, &trail_u, &trail_v);                              \
                int trailed = c->trail_lut[neighbor_ev];                            \
                if(trailed > decayed) {                                             \
                    decayed = trailed;                                              \
                    final_u = trail_u;                                              \
                    final_v = trail_v;                                              \
                }                                                                   \
            }                                                                       \
                                                                                    \
            if(event_strength >= decayed && event_strength > 0) {                   \
                uint8_t event_u;                                                    \
                uint8_t event_v;                                                    \
                final_ev = event_strength;                                          \
                cf_event_color(color_mode, polarity, cu, cv, &event_u, &event_v);   \
                final_u = event_u;                                                  \
                final_v = event_v;                                                  \
            }                                                                       \
            else {                                                                  \
                final_ev = decayed;                                                 \
                final_u = cf_blend_fast_u8(final_u, 128, c->neutralize);            \
                final_v = cf_blend_fast_u8(final_v, 128, c->neutralize);            \
            }                                                                       \
                                                                                    \
            c->nx_y[pos] = (uint8_t) final_ev;                                      \
            c->nx_u[pos] = final_u;                                                 \
            c->nx_v[pos] = final_v;                                                 \
                                                                                    \
            c->ref_y[pos] = cf_blend_fast_u8(                                       \
                (uint8_t) ref,                                                      \
                cy,                                                                 \
                c->adapt_lut[event_strength]);                                      \
        }                                                                           \
    }                                                                               \
}

CF_DEFINE_COMPUTE_WHITE(cf_compute_white_plain,       0, 0)
CF_DEFINE_COMPUTE_WHITE(cf_compute_white_trail,       1, 0)
CF_DEFINE_COMPUTE_WHITE(cf_compute_white_noise,       0, 1)
CF_DEFINE_COMPUTE_WHITE(cf_compute_white_trail_noise, 1, 1)

CF_DEFINE_COMPUTE_COLOR(cf_compute_color_plain,       0, 0)
CF_DEFINE_COMPUTE_COLOR(cf_compute_color_trail,       1, 0)
CF_DEFINE_COMPUTE_COLOR(cf_compute_color_noise,       0, 1)
CF_DEFINE_COMPUTE_COLOR(cf_compute_color_trail_noise, 1, 1)

static void cf_render_white_pure(chronofold_t *c, VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];

    int len = c->len;
    int i;

    CF_OMP_FOR
    for(i = 0; i < len; i++)
        Y[i] = c->output_lut[c->ev_y[i]];

    veejay_memset(frame->data[1], 128, (size_t) len);
    veejay_memset(frame->data[2], 128, (size_t) len);
}

static void cf_render_white_bleed(chronofold_t *c, VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    int len = c->len;
    int i;

    CF_OMP_FOR
    for(i = 0; i < len; i++) {
        int base_y = c->bleed_y_lut[Y[i]];
        int ev = c->output_lut[c->ev_y[i]];

        Y[i] = cf_u8(base_y + ev);
        U[i] = c->bleed_uv_lut[U[i]];
        V[i] = c->bleed_uv_lut[V[i]];
    }
}

static void cf_render_color_pure(chronofold_t *c, VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    int len = c->len;
    int i;

    CF_OMP_FOR
    for(i = 0; i < len; i++) {
        int ev = c->ev_y[i];
        int out_ev = c->output_lut[ev];
        int mix = c->mix_lut[ev];

        Y[i] = (uint8_t) out_ev;
        U[i] = cf_blend_fast_u8(128, c->ev_u[i], mix);
        V[i] = cf_blend_fast_u8(128, c->ev_v[i], mix);
    }
}

static void cf_render_color_bleed(chronofold_t *c, VJFrame *frame)
{
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    int len = c->len;
    int i;

    CF_OMP_FOR
    for(i = 0; i < len; i++) {
        int ev = c->ev_y[i];
        int out_ev = c->output_lut[ev];
        int mix = c->mix_lut[ev];

        int base_y = c->bleed_y_lut[Y[i]];
        uint8_t base_u = c->bleed_uv_lut[U[i]];
        uint8_t base_v = c->bleed_uv_lut[V[i]];

        Y[i] = cf_u8(base_y + out_ev);
        U[i] = cf_blend_fast_u8(base_u, c->ev_u[i], mix);
        V[i] = cf_blend_fast_u8(base_v, c->ev_v[i], mix);
    }
}

static void cf_neutralize_chroma_buffers(chronofold_t *c)
{
    veejay_memset(c->ev_u, 128, (size_t) c->len);
    veejay_memset(c->ev_v, 128, (size_t) c->len);
    veejay_memset(c->nx_u, 128, (size_t) c->len);
    veejay_memset(c->nx_v, 128, (size_t) c->len);
}

void chronofold_apply(void *ptr, VJFrame *frame, int *args)
{
    chronofold_t *c = (chronofold_t *) ptr;

    int threshold;
    int decay;
    int gain;
    int memory;
    int trail;
    int color_mode;
    int noise;
    int source_bleed;
    int beat_push;
    int flash_gain;
    int color_energy;

    uint8_t *swap;

    int use_white;
    int use_trail;
    int use_noise;

    if(!c->seeded)
        cf_seed(c, frame);

    beat_push = cf_scale_1000_to_255(args[P_BEAT_PUSH]);

    threshold    = cf_scale_1000_to_255(args[P_THRESHOLD]);
    decay        = cf_scale_1000_to_255(args[P_DECAY]);
    gain         = cf_scale_1000_to_255(args[P_GAIN]);
    memory       = cf_clampi(cf_scale_1000_to_255(args[P_MEMORY]), 1, 255);
    trail        = cf_scale_1000_to_255(args[P_TRAIL]);
    color_mode   = cf_clampi(args[P_COLOR_MODE], 0, 4);
    noise        = cf_scale_1000_to_255(args[P_NOISE]);
    source_bleed = cf_scale_1000_to_255(args[P_SOURCE_BLEED]);
    flash_gain   = cf_clampi(args[P_FLASH_GAIN], 0, 1000);
    color_energy = cf_clampi(args[P_COLOR_ENERGY], 0, 1000);

    threshold    = cf_push_sub_255(threshold, beat_push, 42);
    decay        = cf_push_add_255(decay, beat_push, 10);
    gain         = cf_push_add_255(gain, beat_push, 64);
    trail        = cf_push_add_255(trail, beat_push, 72);
    noise        = cf_push_add_255(noise, beat_push, 8);
    source_bleed = cf_push_add_255(source_bleed, beat_push, 34);
    flash_gain   = cf_push_add_1000(flash_gain, beat_push, 220);
    color_energy = cf_push_add_1000(color_energy, beat_push, 180);

    use_white = (color_mode == CF_COLOR_WHITE);
    use_trail = (trail > 0);
    use_noise = (noise > 0);

    if(c->last_color_mode == CF_COLOR_WHITE && !use_white)
        cf_neutralize_chroma_buffers(c);

    cf_build_luts_if_needed(
        c,
        threshold,
        decay,
        gain,
        memory,
        trail,
        noise,
        source_bleed,
        flash_gain,
        color_energy
    );

    if(use_white) {
        if(use_trail) {
            if(use_noise)
                cf_compute_white_trail_noise(c, frame);
            else
                cf_compute_white_trail(c, frame);
        }
        else {
            if(use_noise)
                cf_compute_white_noise(c, frame);
            else
                cf_compute_white_plain(c, frame);
        }

        swap = c->ev_y;
        c->ev_y = c->nx_y;
        c->nx_y = swap;

    }
    else {
        if(use_trail) {
            if(use_noise)
                cf_compute_color_trail_noise(c, frame, color_mode);
            else
                cf_compute_color_trail(c, frame, color_mode);
        }
        else {
            if(use_noise)
                cf_compute_color_noise(c, frame, color_mode);
            else
                cf_compute_color_plain(c, frame, color_mode);
        }

        swap = c->ev_y;
        c->ev_y = c->nx_y;
        c->nx_y = swap;

        swap = c->ev_u;
        c->ev_u = c->nx_u;
        c->nx_u = swap;

        swap = c->ev_v;
        c->ev_v = c->nx_v;
        c->nx_v = swap;
    }

    if(use_white) {
        if(source_bleed == 0)
            cf_render_white_pure(c, frame);
        else
            cf_render_white_bleed(c, frame);
    }
    else {
        if(source_bleed == 0)
            cf_render_color_pure(c, frame);
        else
            cf_render_color_bleed(c, frame);
    }

    c->last_color_mode = color_mode;
    c->frame++;
}
