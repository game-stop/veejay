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
#include "tomolight.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#define TL_PARAMS 12

#define P_AMOUNT           0
#define P_DEPTH_SCALE      1
#define P_SLICES           2
#define P_SCAN_POS         3
#define P_SCAN_WIDTH       4
#define P_SCAN_MOTION      5
#define P_RESIDUE          6
#define P_LIGHT            7
#define P_DEPTH_SOURCE     8
#define P_RENDER_MODE      9
#define P_COLOR_MODE      10
#define P_RESET           11

#define TL_SRC_LUMA        0
#define TL_SRC_INV_LUMA    1
#define TL_SRC_MOTION      2
#define TL_SRC_LUMA_MOTION 3
#define TL_SRC_EDGE        4

#define TL_MOTION_LOCKED   0
#define TL_MOTION_DRIFT_F  1
#define TL_MOTION_DRIFT_R  2
#define TL_MOTION_PULSE    3
#define TL_MOTION_BOUNCE   4
#define TL_MOTION_STEPPED  5

#define TL_MODE_VOLUME     0
#define TL_MODE_SCAN       1
#define TL_MODE_XRAY       2
#define TL_MODE_CONTOUR    3
#define TL_MODE_SHELLS     4
#define TL_MODE_RADAR      5
#define TL_MODE_FOSSIL     6
#define TL_MODE_LIGHTSHEET 7
#define TL_MODE_MRI        8
#define TL_MODE_FOG        9

#define TL_COLOR_SOURCE    0
#define TL_COLOR_ICE       1
#define TL_COLOR_CYAN      2
#define TL_COLOR_GOLD      3
#define TL_COLOR_GREEN     4
#define TL_COLOR_MAGENTA   5
#define TL_COLOR_RED       6
#define TL_COLOR_HEAT      7
#define TL_COLOR_INVERT    8

#define TL_GLOW_SHIFT      3
#define TL_GLOW_STEP       (1 << TL_GLOW_SHIFT)
#define TL_GLOW_MASK       (TL_GLOW_STEP - 1)
#define TL_GLOW_RADIUS     2
#define TL_GLOW_DIAM       ((TL_GLOW_RADIUS * 2) + 1)
#define TL_LUT2_SIZE       65536

static inline int tl_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static inline int tl_abs_i(int v)
{
    return (v < 0) ? -v : v;
}

static inline uint8_t tl_u8(int v)
{
    return (uint8_t)((v < 0) ? 0 : ((v > 255) ? 255 : v));
}

static inline uint8_t tl_blend_q8_u8(int a, int b, int q)
{
    return (uint8_t)(a + (((b - a) * q) >> 8));
}

typedef struct {
    int w;
    int h;
    int len;
    int gw;
    int gh;
    int glen;
    int frame;
    int seeded;
    int last_reset;
    int prev_valid;
    int n_threads;

    int last_slices;
    int last_center;
    int last_width;
    int last_depth_scale_q8;
    int last_amount_q8;
    int last_inject_q8;
    int last_light_strength;
    int last_render_mode;
    int last_color_mode;
    int last_static_chroma;

    uint8_t contour_lut[256];
    uint8_t scan_lut[256];
    uint8_t light_lut[256];
    uint8_t depth_luma_lut[256];
    uint8_t depth_inv_lut[256];
    uint8_t glow_lut[256];
    uint8_t shadow_lut[256];

    uint8_t res_blend_lut[TL_LUT2_SIZE];
    uint8_t y_amount_lut[TL_LUT2_SIZE];
    uint8_t chroma_amount_lut[TL_LUT2_SIZE];
    uint8_t src_chroma_lut[TL_LUT2_SIZE];
    uint8_t inv_chroma_lut[TL_LUT2_SIZE];
    uint8_t fixed_u_lut[TL_LUT2_SIZE];
    uint8_t fixed_v_lut[TL_LUT2_SIZE];

    uint8_t *region;
    uint8_t *src_y;
    uint8_t *prev_y;
    uint8_t *res_y;
    uint8_t *glow;
    uint8_t *glow_tmp;
    uint8_t *glow_next;
} tomolight_t;

vj_effect *tomolight_init(int w, int h)
{
    (void) w;
    (void) h;

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve) return NULL;

    ve->num_params = TL_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1])
        return ve;

    ve->limits[0][P_AMOUNT] = 0;
    ve->limits[1][P_AMOUNT] = 100;
    ve->defaults[P_AMOUNT] = 88;

    ve->limits[0][P_DEPTH_SCALE] = 0;
    ve->limits[1][P_DEPTH_SCALE] = 300;
    ve->defaults[P_DEPTH_SCALE] = 130;

    ve->limits[0][P_SLICES] = 2;
    ve->limits[1][P_SLICES] = 64;
    ve->defaults[P_SLICES] = 20;

    ve->limits[0][P_SCAN_POS] = 0;
    ve->limits[1][P_SCAN_POS] = 1000;
    ve->defaults[P_SCAN_POS] = 500;

    ve->limits[0][P_SCAN_WIDTH] = 1;
    ve->limits[1][P_SCAN_WIDTH] = 100;
    ve->defaults[P_SCAN_WIDTH] = 18;

    ve->limits[0][P_SCAN_MOTION] = 0;
    ve->limits[1][P_SCAN_MOTION] = 5;
    ve->defaults[P_SCAN_MOTION] = 1;

    ve->limits[0][P_RESIDUE] = 0;
    ve->limits[1][P_RESIDUE] = 99;
    ve->defaults[P_RESIDUE] = 86;

    ve->limits[0][P_LIGHT] = 0;
    ve->limits[1][P_LIGHT] = 100;
    ve->defaults[P_LIGHT] = 62;

    ve->limits[0][P_DEPTH_SOURCE] = 0;
    ve->limits[1][P_DEPTH_SOURCE] = 4;
    ve->defaults[P_DEPTH_SOURCE] = TL_SRC_LUMA;

    ve->limits[0][P_RENDER_MODE] = 0;
    ve->limits[1][P_RENDER_MODE] = 9;
    ve->defaults[P_RENDER_MODE] = TL_MODE_SCAN;

    ve->limits[0][P_COLOR_MODE] = 0;
    ve->limits[1][P_COLOR_MODE] = 8;
    ve->defaults[P_COLOR_MODE] = TL_COLOR_ICE;

    ve->limits[0][P_RESET] = 0;
    ve->limits[1][P_RESET] = 1;
    ve->defaults[P_RESET] = 0;

    ve->sub_format = 1;
    ve->description = "Tomographic Light Sculpture";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Amount",
        "Depth Scale",
        "Slice Count",
        "Scan Position",
        "Scan Width",
        "Scan Motion",
        "Residue Memory",
        "Light Strength",
        "Depth Source",
        "Render Mode",
        "Color Mode",
        "Reset Memory"
    );

    return ve;
}

static inline int tl_scan_center(int frame, int pos, int motion)
{
    const int base = (pos * 255 + 500) / 1000;
    int phase;
    int tri;

    switch(motion) {
        case TL_MOTION_DRIFT_F:
            return (base + ((frame * 3) & 255)) & 255;
        case TL_MOTION_DRIFT_R:
            return (base - ((frame * 3) & 255)) & 255;
        case TL_MOTION_PULSE:
            phase = (frame * 4) & 511;
            tri = (phase < 256) ? phase : 511 - phase;
            return tl_clampi(base + ((tri - 128) >> 1), 0, 255);
        case TL_MOTION_BOUNCE:
            phase = (frame * 3) & 511;
            return (phase < 256) ? phase : 511 - phase;
        case TL_MOTION_STEPPED:
            return (base + (((frame >> 3) * 32) & 255)) & 255;
        case TL_MOTION_LOCKED:
        default:
            return base;
    }
}

static inline int tl_smooth_q8(int q)
{
    return (q * q * (765 - (q << 1)) + 32768) >> 16;
}

static void tl_rebuild_shape_luts(tomolight_t *t, int slices, int center, int width)
{
    if(t->last_slices == slices && t->last_center == center && t->last_width == width)
        return;

    const int scan_halo_w = width + (width >> 1) + 1;

    for(int d = 0; d < 256; d++) {
        const int phase = (d * slices) & 255;
        const int c = (phase < 128) ? phase : 255 - phase;

        int core = 255 - (c * 14);
        if(core < 0) core = 0;
        int halo = 96 - (c * 2);
        if(halo < 0) halo = 0;
        int cv = core + ((halo * (255 - core)) >> 8);
        if(cv > 255) cv = 255;

        const int dist = tl_abs_i(d - center);
        int sv = 0;
        if(dist < width) {
            int q = 255 - ((dist * 255) / width);
            sv = tl_smooth_q8(q);
        }
        if(dist < scan_halo_w) {
            int hq = 255 - ((dist * 255) / scan_halo_w);
            int hv = (tl_smooth_q8(hq) * 70) >> 8;
            if(hv > sv) sv = hv;
        }

        t->contour_lut[d] = (uint8_t) cv;
        t->scan_lut[d] = (uint8_t) sv;
    }

    t->last_slices = slices;
    t->last_center = center;
    t->last_width = width;
    t->last_render_mode = -1;
}

static void tl_rebuild_depth_luts(tomolight_t *t, int depth_scale_q8)
{
    if(t->last_depth_scale_q8 == depth_scale_q8)
        return;

    for(int i = 0; i < 256; i++) {
        int d = (i * depth_scale_q8 + 128) >> 8;
        if(d > 255) d = 255;
        t->depth_luma_lut[i] = (uint8_t) d;

        d = ((255 - i) * depth_scale_q8 + 128) >> 8;
        if(d > 255) d = 255;
        t->depth_inv_lut[i] = (uint8_t) d;
    }

    t->last_depth_scale_q8 = depth_scale_q8;
}

static void tl_rebuild_light_lut(tomolight_t *t, int render_mode, int slices)
{
    if(t->last_render_mode == render_mode)
        return;

    for(int d = 0; d < 256; d++) {
        const int contour = t->contour_lut[d];
        const int scan = t->scan_lut[d];
        int v;

        switch(render_mode) {
            case TL_MODE_VOLUME:
                v = ((d * 3) >> 2) + (contour >> 3) + (scan >> 2) + ((scan * contour) >> 9);
                break;
            case TL_MODE_SCAN:
                v = scan + (contour >> 3) + ((scan * contour) >> 9);
                break;
            case TL_MODE_XRAY:
                v = (d >> 1) + (scan >> 2) + (contour >> 4);
                break;
            case TL_MODE_CONTOUR:
                v = contour + (scan >> 2);
                break;
            case TL_MODE_SHELLS:
                v = (scan > contour) ? scan : contour;
                v += (d >> 3) + ((scan * contour) >> 10);
                break;
            case TL_MODE_FOSSIL:
                v = (contour >> 1) + (scan >> 3);
                break;
            case TL_MODE_MRI:
                v = (((d * slices) >> 8) & 1) ? (scan + (d >> 1) + (contour >> 4)) : (contour >> 1);
                break;
            case TL_MODE_FOG:
                v = (d >> 1) + (scan >> 1) + (contour >> 2);
                break;
            default:
                v = scan + contour + ((scan * contour) >> 9);
                break;
        }

        t->light_lut[d] = tl_u8(v);
    }

    t->last_render_mode = render_mode;
}

static void tl_rebuild_blend_luts(tomolight_t *t, int amount_q8, int inject_q8)
{
    if(t->last_amount_q8 == amount_q8 && t->last_inject_q8 == inject_q8)
        return;

    for(int a = 0; a < 256; a++) {
        const int row = a << 8;
        for(int b = 0; b < 256; b++) {
            t->res_blend_lut[row | b] = tl_blend_q8_u8(a, b, inject_q8);
            t->y_amount_lut[row | b] = tl_blend_q8_u8(a, b, amount_q8);
            t->chroma_amount_lut[row | b] = tl_blend_q8_u8(b, a, amount_q8);
        }
    }

    t->last_amount_q8 = amount_q8;
    t->last_inject_q8 = inject_q8;
}

static void tl_rebuild_light_scale_luts(tomolight_t *t, int light_strength)
{
    if(t->last_light_strength == light_strength)
        return;

    const int glow_q8 = (light_strength * 256 + 50) / 100;
    const int shadow_q8 = (light_strength * 150 + 50) / 100;

    for(int i = 0; i < 256; i++) {
        t->glow_lut[i] = (uint8_t)((i * glow_q8 + 128) >> 8);
        t->shadow_lut[i] = (uint8_t)((i * shadow_q8 + 128) >> 8);
    }

    t->last_light_strength = light_strength;
}

static inline void tl_fixed_uv_base(int mode, int src_u, int src_v, int d, int ty, int *tu, int *tv)
{
    (void) src_u;
    (void) src_v;

    switch(mode) {
        case TL_COLOR_ICE:
            *tu = 166 + ((d - 128) >> 3);
            *tv = 100 - (ty >> 5);
            break;
        case TL_COLOR_CYAN:
            *tu = 190;
            *tv = 70;
            break;
        case TL_COLOR_GOLD:
            *tu = 78;
            *tv = 166;
            break;
        case TL_COLOR_GREEN:
            *tu = 84;
            *tv = 48;
            break;
        case TL_COLOR_MAGENTA:
            *tu = 176;
            *tv = 214;
            break;
        case TL_COLOR_RED:
            *tu = 88;
            *tv = 230;
            break;
        case TL_COLOR_HEAT:
            *tu = 128 - ((ty * 54) >> 8) + ((255 - d) >> 4);
            *tv = 128 + ((ty * 84) >> 8);
            break;
        default:
            *tu = 166;
            *tv = 100;
            break;
    }
}

static void tl_rebuild_static_chroma_luts(tomolight_t *t)
{
    if(t->last_static_chroma)
        return;

    for(int ty = 0; ty < 256; ty++) {
        const int row = ty << 8;
        const int neutral = 128 * (255 - ty);
        for(int c = 0; c < 256; c++) {
            int v = ((c * ty) + neutral) >> 8;
            t->src_chroma_lut[row | c] = tl_u8(v);
            v = (((255 - c) * ty) + neutral) >> 8;
            t->inv_chroma_lut[row | c] = tl_u8(v);
        }
    }

    t->last_static_chroma = 1;
}

static void tl_rebuild_fixed_chroma_luts(tomolight_t *t, int color_mode)
{
    if(t->last_color_mode == color_mode)
        return;

    if(color_mode != TL_COLOR_SOURCE && color_mode != TL_COLOR_INVERT) {
        for(int d = 0; d < 256; d++) {
            const int row = d << 8;
            for(int ty = 0; ty < 256; ty++) {
                int tu;
                int tv;
                tl_fixed_uv_base(color_mode, 128, 128, d, ty, &tu, &tv);
                tu = ((tu * ty) + (128 * (255 - ty))) >> 8;
                tv = ((tv * ty) + (128 * (255 - ty))) >> 8;
                t->fixed_u_lut[row | ty] = tl_u8(tu);
                t->fixed_v_lut[row | ty] = tl_u8(tv);
            }
        }
    }

    t->last_color_mode = color_mode;
}

static void tl_seed(tomolight_t *t, VJFrame *frame)
{
    const size_t len = (size_t) t->len;
    const size_t glen = (size_t) t->glen;

    memcpy(t->prev_y, frame->data[0], len);
    memset(t->src_y, 0, len);
    memset(t->res_y, 0, len);
    memset(t->glow, 0, glen);
    memset(t->glow_tmp, 0, glen);
    memset(t->glow_next, 0, glen);

    t->prev_valid = 1;
    t->seeded = 1;
}

void *tomolight_malloc(int w, int h)
{
    tomolight_t *t = (tomolight_t*) vj_calloc(sizeof(tomolight_t));
    if(!t) return NULL;

    const int len = w * h;
    const int gw = (w + TL_GLOW_MASK) >> TL_GLOW_SHIFT;
    const int gh = (h + TL_GLOW_MASK) >> TL_GLOW_SHIFT;
    const int glen = gw * gh;

    const size_t plane = (size_t) len;
    const size_t glow_plane = (size_t) glen;
    const size_t total = (plane * 3) + (glow_plane * 3);

    t->region = (uint8_t*) vj_calloc(total);
    if(!t->region) {
        free(t);
        return NULL;
    }

    uint8_t *p = t->region;
    t->src_y     = p; p += plane;
    t->prev_y    = p; p += plane;
    t->res_y     = p; p += plane;
    t->glow      = p; p += glow_plane;
    t->glow_tmp  = p; p += glow_plane;
    t->glow_next = p;

    t->w = w;
    t->h = h;
    t->len = len;
    t->gw = gw;
    t->gh = gh;
    t->glen = glen;
    t->frame = 0;
    t->seeded = 0;
    t->last_reset = 0;
    t->prev_valid = 0;
    t->n_threads = vje_advise_num_threads(len);

    t->last_slices = -1;
    t->last_center = -1;
    t->last_width = -1;
    t->last_depth_scale_q8 = -1;
    t->last_amount_q8 = -1;
    t->last_inject_q8 = -1;
    t->last_light_strength = -1;
    t->last_render_mode = -1;
    t->last_color_mode = -1;
    t->last_static_chroma = 0;

    tl_rebuild_static_chroma_luts(t);

    return (void*) t;
}

void tomolight_free(void *ptr)
{
    tomolight_t *t = (tomolight_t*) ptr;
    if(!t) return;
    free(t->region);
    free(t);
}

static inline int tl_depth_from_source(int depth_source, int sy, int prev, int edge,
                                       const uint8_t *depth_luma_lut,
                                       const uint8_t *depth_inv_lut,
                                       int depth_scale_q8,
                                       int motion_react_q8)
{
    if(depth_source == TL_SRC_INV_LUMA)
        return depth_inv_lut[sy];

    if(depth_source == TL_SRC_EDGE) {
        int d = (edge * depth_scale_q8 + 128) >> 8;
        return (d > 255) ? 255 : d;
    }

    if(depth_source == TL_SRC_MOTION) {
        int d = (tl_abs_i(sy - prev) * motion_react_q8 + 128) >> 8;
        if(d > 255) d = 255;
        d = (d * depth_scale_q8 + 128) >> 8;
        return (d > 255) ? 255 : d;
    }

    if(depth_source == TL_SRC_LUMA_MOTION) {
        int m = (tl_abs_i(sy - prev) * motion_react_q8 + 128) >> 8;
        int d = sy + m;
        if(d > 255) d = 255;
        d = (d * depth_scale_q8 + 128) >> 8;
        return (d > 255) ? 255 : d;
    }

    return depth_luma_lut[sy];
}

static inline int tl_light_spatial(int render_mode, int x, int y_band, int frame,
                                   int sy, int d, int edge, int contour, int scan,
                                   int slices, int center, int width)
{
    int v;

    switch(render_mode) {
        case TL_MODE_SCAN:
            v = scan + (contour >> 3) + (edge >> 2) + ((scan * contour) >> 9);
            return (v > 255) ? 255 : v;

        case TL_MODE_XRAY:
            v = ((255 - sy) >> 1) + (d >> 1) + (edge >> 1) + (scan >> 2) + (contour >> 4);
            return (v > 255) ? 255 : v;

        case TL_MODE_FOSSIL:
            v = (edge >> 1) + (contour >> 1) + (scan >> 3);
            return (v > 255) ? 255 : v;

        case TL_MODE_LIGHTSHEET: {
            const int dist = tl_abs_i(y_band - center);
            int band = 0;
            if(dist < width) {
                int q = 255 - ((dist * 255) / width);
                band = tl_smooth_q8(q);
            }
            v = band + (scan >> 1) + (edge >> 2) + (contour >> 4);
            return (v > 255) ? 255 : v;
        }

        case TL_MODE_RADAR: {
            int sweep = tl_abs_i((((x + y_band + (frame << 2)) & 255) - 128));
            v = 255 - (sweep << 1);
            if(v < 0) v = 0;
            v = (v * (scan + 64)) >> 8;
            v += (contour >> 2);
            return (v > 255) ? 255 : v;
        }

        case TL_MODE_VOLUME:
            v = ((d * 3) >> 2) + (contour >> 3) + (scan >> 2) + ((scan * contour) >> 9);
            return (v > 255) ? 255 : v;

        case TL_MODE_CONTOUR:
            v = contour + (scan >> 2);
            return (v > 255) ? 255 : v;

        case TL_MODE_SHELLS:
            v = (scan > contour) ? scan : contour;
            v += (d >> 3) + ((scan * contour) >> 10);
            return (v > 255) ? 255 : v;

        case TL_MODE_MRI:
            v = (((d * slices) >> 8) & 1) ? (scan + (d >> 1) + (contour >> 4)) : (contour >> 1);
            return (v > 255) ? 255 : v;

        case TL_MODE_FOG:
            v = (d >> 1) + (scan >> 1) + (contour >> 2);
            return (v > 255) ? 255 : v;

        default:
            v = scan + contour;
            return (v > 255) ? 255 : v;
    }
}


static inline int tl_sample_glow_centered(const uint8_t * restrict glow, int gw, int gh, int x, int y)
{
    int xs = x - (TL_GLOW_STEP >> 1);
    int ys = y - (TL_GLOW_STEP >> 1);
    int gx;
    int gy;
    int fx;
    int fy;

    if(xs <= 0) {
        gx = 0;
        fx = 0;
    } else {
        gx = xs >> TL_GLOW_SHIFT;
        fx = xs & TL_GLOW_MASK;
        if(gx >= gw - 1) {
            gx = gw - 1;
            fx = 0;
        }
    }

    if(ys <= 0) {
        gy = 0;
        fy = 0;
    } else {
        gy = ys >> TL_GLOW_SHIFT;
        fy = ys & TL_GLOW_MASK;
        if(gy >= gh - 1) {
            gy = gh - 1;
            fy = 0;
        }
    }

    const int gx1 = (gx + 1 < gw) ? gx + 1 : gx;
    const int gy1 = (gy + 1 < gh) ? gy + 1 : gy;
    const int row0 = gy * gw;
    const int row1 = gy1 * gw;
    const int wx0 = TL_GLOW_STEP - fx;
    const int wy0 = TL_GLOW_STEP - fy;

    const int a = ((int)glow[row0 + gx] * wx0) + ((int)glow[row0 + gx1] * fx);
    const int b = ((int)glow[row1 + gx] * wx0) + ((int)glow[row1 + gx1] * fx);
    return ((a * wy0) + (b * fy) + 32) >> 6;
}

static inline int tl_min_i(int a, int b)
{
    return (a < b) ? a : b;
}

static inline int tl_max_i(int a, int b)
{
    return (a > b) ? a : b;
}

static inline void tl_store_chroma(int color_mode, int i, int d, int ty,
                                   uint8_t * restrict U, uint8_t * restrict V,
                                   const uint8_t * restrict chroma_amount_lut,
                                   const uint8_t * restrict fixed_u_lut,
                                   const uint8_t * restrict fixed_v_lut,
                                   const uint8_t * restrict src_chroma_lut,
                                   const uint8_t * restrict inv_chroma_lut)
{
    int tu;
    int tv;

    if(color_mode == TL_COLOR_SOURCE) {
        const int row = ty << 8;
        tu = src_chroma_lut[row | U[i]];
        tv = src_chroma_lut[row | V[i]];
    } else if(color_mode == TL_COLOR_INVERT) {
        const int row = ty << 8;
        tu = inv_chroma_lut[row | U[i]];
        tv = inv_chroma_lut[row | V[i]];
    } else {
        const int idx = (d << 8) | ty;
        tu = fixed_u_lut[idx];
        tv = fixed_v_lut[idx];
    }

    U[i] = chroma_amount_lut[(tu << 8) | U[i]];
    V[i] = chroma_amount_lut[(tv << 8) | V[i]];
}

void tomolight_apply(void *ptr, VJFrame *frame, int *args)
{
    tomolight_t *t = (tomolight_t*) ptr;
    if(!t || !frame || !args) return;

    const int len = t->len;
    const int reset = args[P_RESET] ? 1 : 0;

    if(!t->seeded || (reset && !t->last_reset))
        tl_seed(t, frame);

    t->last_reset = reset;

    const int depth_source = tl_clampi(args[P_DEPTH_SOURCE], 0, 4);
    const int use_motion = (depth_source == TL_SRC_MOTION || depth_source == TL_SRC_LUMA_MOTION);

    if(args[P_AMOUNT] <= 0) {
        if(use_motion) {
            memcpy(t->prev_y, frame->data[0], (size_t) len);
            t->prev_valid = 1;
        } else {
            t->prev_valid = 0;
        }
        t->frame++;
        return;
    }

    const int w = t->w;
    const int h = t->h;
    const int gw = t->gw;
    const int gh = t->gh;
    const int frame_no = t->frame;

    const int amount_q8 = (tl_clampi(args[P_AMOUNT], 0, 100) * 256 + 50) / 100;
    const int residue_q8 = (tl_clampi(args[P_RESIDUE], 0, 99) * 256 + 50) / 100;
    const int inject_q8 = 256 - residue_q8;
    const int light_strength = tl_clampi(args[P_LIGHT], 0, 100);
    const int depth_scale_q8 = (tl_clampi(args[P_DEPTH_SCALE], 0, 300) * 256 + 50) / 100;
    const int motion_react_q8 = ((tl_clampi(50 + (tl_clampi(args[P_DEPTH_SCALE], 0, 300) / 5), 50, 110)) * 256 + 50) / 100;
    const int slices = tl_clampi(args[P_SLICES], 2, 64);
    const int render_mode = tl_clampi(args[P_RENDER_MODE], 0, 9);
    const int color_mode = tl_clampi(args[P_COLOR_MODE], 0, 8);
    const int center = tl_scan_center(frame_no, tl_clampi(args[P_SCAN_POS], 0, 1000),
                                      tl_clampi(args[P_SCAN_MOTION], 0, 5));
    int width = (tl_clampi(args[P_SCAN_WIDTH], 1, 100) * 255 + 50) / 100;
    if(width < 1) width = 1;

    const int true_edge =
        (depth_source == TL_SRC_EDGE) ||
        (render_mode == TL_MODE_XRAY) ||
        (render_mode == TL_MODE_FOSSIL) ||
        (render_mode == TL_MODE_LIGHTSHEET);

    const int spatial_mode = (render_mode == TL_MODE_RADAR) || (render_mode == TL_MODE_LIGHTSHEET);

    tl_rebuild_shape_luts(t, slices, center, width);
    tl_rebuild_depth_luts(t, depth_scale_q8);
    tl_rebuild_light_lut(t, render_mode, slices);
    tl_rebuild_blend_luts(t, amount_q8, inject_q8);
    tl_rebuild_light_scale_luts(t, light_strength);
    tl_rebuild_fixed_chroma_luts(t, color_mode);

    if(use_motion && !t->prev_valid) {
        memcpy(t->prev_y, frame->data[0], (size_t) len);
        t->prev_valid = 1;
    }

    uint8_t * restrict Y = frame->data[0];
    uint8_t * restrict U = frame->data[1];
    uint8_t * restrict V = frame->data[2];

    uint8_t * restrict src_y = t->src_y;
    uint8_t * restrict prev_y = t->prev_y;
    uint8_t * restrict res_y = t->res_y;
    uint8_t * restrict glow = t->glow;
    uint8_t * restrict glow_tmp = t->glow_tmp;
    uint8_t * restrict glow_next = t->glow_next;

    const uint8_t * restrict contour_lut = t->contour_lut;
    const uint8_t * restrict scan_lut = t->scan_lut;
    const uint8_t * restrict light_lut = t->light_lut;
    const uint8_t * restrict depth_luma_lut = t->depth_luma_lut;
    const uint8_t * restrict depth_inv_lut = t->depth_inv_lut;
    const uint8_t * restrict glow_lut = t->glow_lut;
    const uint8_t * restrict shadow_lut = t->shadow_lut;
    const uint8_t * restrict res_blend_lut = t->res_blend_lut;
    const uint8_t * restrict y_amount_lut = t->y_amount_lut;
    const uint8_t * restrict chroma_amount_lut = t->chroma_amount_lut;
    const uint8_t * restrict src_chroma_lut = t->src_chroma_lut;
    const uint8_t * restrict inv_chroma_lut = t->inv_chroma_lut;
    const uint8_t * restrict fixed_u_lut = t->fixed_u_lut;
    const uint8_t * restrict fixed_v_lut = t->fixed_v_lut;

#pragma omp parallel num_threads(t->n_threads)
    {
        if(true_edge) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++)
                src_y[i] = Y[i];

#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                const int row = y * w;
                const int ym = (y > 0) ? y - 1 : y;
                const int yp = (y + 1 < h) ? y + 1 : y;
                const int rowm = ym * w;
                const int rowp = yp * w;
                const int y_band = (h > 1) ? ((y * 255) / (h - 1)) : 0;
                for(int x = 0; x < w; x++) {
                    const int i = row + x;
                    const int xm = (x > 0) ? x - 1 : x;
                    const int xp = (x + 1 < w) ? x + 1 : x;
                    const int sy = src_y[i];
                    int edge = tl_abs_i((int)src_y[row + xp] - (int)src_y[row + xm]) +
                               tl_abs_i((int)src_y[rowp + x] - (int)src_y[rowm + x]);
                    edge >>= 1;
                    if(edge > 255) edge = 255;

                    const int prev = use_motion ? prev_y[i] : sy;
                    const int d = tl_depth_from_source(depth_source, sy, prev, edge,
                                                       depth_luma_lut, depth_inv_lut,
                                                       depth_scale_q8, motion_react_q8);
                    const int contour = contour_lut[d];
                    const int scan = scan_lut[d];
                    const int light = tl_light_spatial(render_mode, x, y_band, frame_no,
                                                       sy, d, edge, contour, scan,
                                                       slices, center, width);
                    const int gl = glow_lut[tl_sample_glow_centered(glow, gw, gh, x, y)];
                    const int sh = shadow_lut[edge];
                    const int ly = light + gl - sh;
                    int ty;

                    if(render_mode == TL_MODE_FOSSIL)
                        ty = (sy >> 3) + ly - (sh >> 1);
                    else if(render_mode == TL_MODE_XRAY)
                        ty = ((255 - sy) >> 2) + ly;
                    else if(render_mode == TL_MODE_FOG)
                        ty = (sy >> 3) + ((ly * 3) >> 2);
                    else
                        ty = (sy >> 4) + ly;

                    ty = (ty < 0) ? 0 : ((ty > 255) ? 255 : ty);

                    const int ri = (res_y[i] << 8) | ty;
                    const int ry = res_blend_lut[ri];
                    const int cy = (ty + ry + 1) >> 1;
                    res_y[i] = (uint8_t) ry;
                    Y[i] = y_amount_lut[(sy << 8) | ry];

                    tl_store_chroma(color_mode, i, d, cy, U, V,
                                    chroma_amount_lut, fixed_u_lut, fixed_v_lut,
                                    src_chroma_lut, inv_chroma_lut);

                    if(use_motion)
                        prev_y[i] = (uint8_t) sy;

                }
            }
        } else {
#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                const int row = y * w;
                const int y_band = (h > 1) ? ((y * 255) / (h - 1)) : 0;
                for(int x = 0; x < w; x++) {
                    const int i = row + x;
                    const int sy = Y[i];
                    const int prev = use_motion ? prev_y[i] : sy;
                    const int d = tl_depth_from_source(depth_source, sy, prev, 0,
                                                       depth_luma_lut, depth_inv_lut,
                                                       depth_scale_q8, motion_react_q8);
                    int light;

                    if(spatial_mode) {
                        const int contour = contour_lut[d];
                        const int scan = scan_lut[d];
                        light = tl_light_spatial(render_mode, x, y_band, frame_no,
                                                 sy, d, 0, contour, scan,
                                                 slices, center, width);
                    } else {
                        light = light_lut[d];
                    }

                    const int gl = glow_lut[tl_sample_glow_centered(glow, gw, gh, x, y)];
                    const int ly = light + gl;
                    int ty;

                    if(render_mode == TL_MODE_FOG)
                        ty = (sy >> 3) + ((ly * 3) >> 2);
                    else
                        ty = (sy >> 4) + ly;

                    ty = (ty > 255) ? 255 : ty;

                    const int ri = (res_y[i] << 8) | ty;
                    const int ry = res_blend_lut[ri];
                    const int cy = (ty + ry + 1) >> 1;
                    res_y[i] = (uint8_t) ry;
                    Y[i] = y_amount_lut[(sy << 8) | ry];

                    tl_store_chroma(color_mode, i, d, cy, U, V,
                                    chroma_amount_lut, fixed_u_lut, fixed_v_lut,
                                    src_chroma_lut, inv_chroma_lut);

                    if(use_motion)
                        prev_y[i] = (uint8_t) sy;

                }
            }
        }

#pragma omp for schedule(static)
        for(int gy = 0; gy < gh; gy++) {
            const int y0 = gy << TL_GLOW_SHIFT;
            const int y1 = tl_min_i(y0 + (TL_GLOW_STEP >> 1), h - 1);
            const int y2 = tl_min_i(y0 + TL_GLOW_MASK, h - 1);
            const int row0 = y0 * w;
            const int row1 = y1 * w;
            const int row2 = y2 * w;
            const int grow = gy * gw;

            for(int gx = 0; gx < gw; gx++) {
                const int x0 = gx << TL_GLOW_SHIFT;
                const int x1 = tl_min_i(x0 + (TL_GLOW_STEP >> 1), w - 1);
                const int x2 = tl_min_i(x0 + TL_GLOW_MASK, w - 1);

                const int c  = res_y[row1 + x1];
                const int p0 = res_y[row0 + x0];
                const int p1 = res_y[row0 + x2];
                const int p2 = res_y[row2 + x0];
                const int p3 = res_y[row2 + x2];
                const int mx = tl_max_i(tl_max_i(p0, p1), tl_max_i(p2, p3));
                const int avg = ((c << 1) + p0 + p1 + p2 + p3 + 3) / 6;
                const int v = (avg + mx + 1) >> 1;
                glow_next[grow + gx] = (uint8_t)((v > 255) ? 255 : v);
            }
        }

#pragma omp for schedule(static)
        for(int gy = 0; gy < gh; gy++) {
            const int row = gy * gw;
            int sum = 0;
            for(int k = -TL_GLOW_RADIUS; k <= TL_GLOW_RADIUS; k++) {
                int sx = k;
                if(sx < 0) sx = 0;
                else if(sx >= gw) sx = gw - 1;
                sum += glow_next[row + sx];
            }
            for(int gx = 0; gx < gw; gx++) {
                glow_tmp[row + gx] = (uint8_t)((sum + (TL_GLOW_DIAM >> 1)) / TL_GLOW_DIAM);
                int rm = gx - TL_GLOW_RADIUS;
                int ap = gx + TL_GLOW_RADIUS + 1;
                if(rm < 0) rm = 0;
                else if(rm >= gw) rm = gw - 1;
                if(ap < 0) ap = 0;
                else if(ap >= gw) ap = gw - 1;
                sum += (int)glow_next[row + ap] - (int)glow_next[row + rm];
            }
        }

#pragma omp for schedule(static)
        for(int gx = 0; gx < gw; gx++) {
            int sum = 0;
            for(int k = -TL_GLOW_RADIUS; k <= TL_GLOW_RADIUS; k++) {
                int sy = k;
                if(sy < 0) sy = 0;
                else if(sy >= gh) sy = gh - 1;
                sum += glow_tmp[sy * gw + gx];
            }
            for(int gy = 0; gy < gh; gy++) {
                const int idx = gy * gw + gx;
                const int blurred = (sum + (TL_GLOW_DIAM >> 1)) / TL_GLOW_DIAM;
                glow[idx] = (uint8_t)((((int)glow[idx] * 3) + blurred + 2) >> 2);
                int rm = gy - TL_GLOW_RADIUS;
                int ap = gy + TL_GLOW_RADIUS + 1;
                if(rm < 0) rm = 0;
                else if(rm >= gh) rm = gh - 1;
                if(ap < 0) ap = 0;
                else if(ap >= gh) ap = gh - 1;
                sum += (int)glow_tmp[ap * gw + gx] - (int)glow_tmp[rm * gw + gx];
            }
        }
    }

    if(!use_motion)
        t->prev_valid = 0;

    t->frame++;
}
