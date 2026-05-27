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

/* UI parameter order: choose the look first, then strength/memory,
 * then depth/slice/scan controls, then color/reset. */
#define P_RENDER_MODE      0
#define P_AMOUNT           1
#define P_LIGHT            2
#define P_RESIDUE          3
#define P_DEPTH_SOURCE     4
#define P_DEPTH_SCALE      5
#define P_SLICES           6
#define P_SCAN_POS         7
#define P_SCAN_WIDTH       8
#define P_SCAN_MOTION      9
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

/* Render Mode UI order:
 * 0 Scan        - clean light-slice scanner, good default
 * 1 Volume      - broad volumetric body
 * 2 Fog         - soft density cloud
 * 3 Light Sheet - horizontal/vertical sheet illumination
 * 4 Radar       - moving sweep through the density
 * 5 XRay        - bright diagnostic negative/edge mix
 * 6 MRI         - dense medical/tomographic response
 * 7 Contour     - source-gradient contour relief
 * 8 Shells      - broad quantized depth bodies
 * 9 Fossil      - edge emboss / stone-patina look
 */
#define TL_MODE_SCAN       0
#define TL_MODE_VOLUME     1
#define TL_MODE_FOG        2
#define TL_MODE_LIGHTSHEET 3
#define TL_MODE_RADAR      4
#define TL_MODE_XRAY       5
#define TL_MODE_MRI        6
#define TL_MODE_CONTOUR    7
#define TL_MODE_SHELLS     8
#define TL_MODE_FOSSIL     9

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
#define TL_DIV5(v)         (((v) * 205 + 512) >> 10)
#define TL_DIV6(v)         (((v) * 171 + 512) >> 10)

static inline int tl_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
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
    int last_motion_react_q8;
    int last_amount_q8;
    int last_inject_q8;
    int last_light_strength;
    int last_render_mode;
    int last_apply_render_mode;
    int last_color_mode;
    int last_static_chroma;

    uint8_t contour_lut[256];
    uint8_t contour_relief_lut[256];
    uint8_t shell_body_lut[256];
    uint8_t fossil_lut[256];
    uint8_t mri_lut[256];
    uint8_t scan_lut[256];
    uint8_t light_lut[256];
    uint8_t lightsheet_lut[256];
    uint8_t depth_luma_lut[256];
    uint8_t depth_inv_lut[256];
    uint8_t edge_depth_lut[256];
    uint8_t motion_depth_lut[256];
    uint8_t motion_add_lut[256];
    uint8_t glow_lut[256];
    uint8_t shadow_lut[256];

    uint8_t res_blend_lut[TL_LUT2_SIZE];
    uint8_t y_amount_lut[TL_LUT2_SIZE];
    uint8_t chroma_amount_lut[TL_LUT2_SIZE];
    uint8_t src_chroma_lut[TL_LUT2_SIZE];
    uint8_t inv_chroma_lut[TL_LUT2_SIZE];
    uint8_t fixed_u_lut[TL_LUT2_SIZE];
    uint8_t fixed_v_lut[TL_LUT2_SIZE];
    uint8_t absdiff_lut[511];

    uint8_t *region;
    uint8_t *src_y;
    uint8_t *prev_y;
    uint8_t *res_y;
    uint8_t *glow;
    uint8_t *glow_tmp;
    uint8_t *glow_next;

    uint8_t *map_region;
    uint16_t *glow_x0;
    uint16_t *glow_x1;
    uint8_t *glow_fx;
    uint8_t *glow_wx0;
    int *glow_y0;
    int *glow_y1;
    uint8_t *glow_fy;
    uint8_t *glow_wy0;
    uint8_t *y_band_lut;
    uint16_t *edge_xm;
    uint16_t *edge_xp;
    int *edge_rowm;
    int *edge_rowp;
    uint16_t *down_x0;
    uint16_t *down_x1;
    uint16_t *down_x2;
    uint16_t *blur_x_rm;
    uint16_t *blur_x_ap;
    int *down_row0;
    int *down_row1;
    int *down_row2;
    int *blur_y_rm;
    int *blur_y_ap;
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

    ve->limits[0][P_RENDER_MODE] = 0;
    ve->limits[1][P_RENDER_MODE] = 9;
    ve->defaults[P_RENDER_MODE] = TL_MODE_SCAN;

    ve->limits[0][P_AMOUNT] = 0;
    ve->limits[1][P_AMOUNT] = 100;
    ve->defaults[P_AMOUNT] = 88;

    ve->limits[0][P_LIGHT] = 0;
    ve->limits[1][P_LIGHT] = 100;
    ve->defaults[P_LIGHT] = 62;

    ve->limits[0][P_RESIDUE] = 0;
    ve->limits[1][P_RESIDUE] = 99;
    ve->defaults[P_RESIDUE] = 86;

    ve->limits[0][P_DEPTH_SOURCE] = 0;
    ve->limits[1][P_DEPTH_SOURCE] = 4;
    ve->defaults[P_DEPTH_SOURCE] = TL_SRC_LUMA;

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

    ve->limits[0][P_COLOR_MODE] = 0;
    ve->limits[1][P_COLOR_MODE] = 8;
    ve->defaults[P_COLOR_MODE] = TL_COLOR_ICE;

    ve->limits[0][P_RESET] = 0;
    ve->limits[1][P_RESET] = 1;
    ve->defaults[P_RESET] = 0;

    ve->sub_format = 1;
    ve->description = "Tomographic Light Sculpture";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Render Mode",
        "Opacity",
        "Light Strength",
        "Residue Memory",
        "Depth Source",
        "Depth Scale",
        "Slice Count",
        "Scan Position",
        "Scan Width",
        "Scan Motion",
        "Color Mode",
        "Reset Memory"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Render Mode */
        VJ_BEAT_ALPHA_OR_OPACITY,   VJ_BEAT_F_REJECT,                                      VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Opacity */
        VJ_BEAT_INTENSITY,          VJ_BEAT_F_CONTINUOUS,                                  24,                 100,                14, 52,  450,  1500, 0,    85,    /* Light Strength */
        VJ_BEAT_MEMORY,             VJ_BEAT_F_CONTINUOUS,                                  62,                 98,                 8,  26,  1800, 4200, 700,  35,    /* Residue Memory */
        VJ_BEAT_SELECTOR,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Depth Source */
        VJ_BEAT_GEOMETRY_AMPLITUDE, VJ_BEAT_F_CONTINUOUS,                                  45,                 245,                14, 48,  800,  2200, 0,    70,    /* Depth Scale */
        VJ_BEAT_DETAIL,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE, 4, 48, 6, 22, 1800, 3600, 1200, 20, /* Slice Count */
        VJ_BEAT_GEOMETRY_PHASE,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,                  0,                  1000,               10, 42,  700,  1900, 0,    55,    /* Scan Position */
        VJ_BEAT_WINDOW_RADIUS,      VJ_BEAT_F_CONTINUOUS,                                  4,                  58,                 8,  34,  900,  2200, 0,    55,    /* Scan Width */
        VJ_BEAT_SELECTOR,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Scan Motion */
        VJ_BEAT_SELECTOR,           VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000, /* Color Mode */
        VJ_BEAT_RESET,              VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,    0,    0,    -1000  /* Reset Memory */
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
    const int slices_changed = (t->last_slices != slices);
    const int scan_changed = (t->last_center != center || t->last_width != width);

    if(!slices_changed && !scan_changed)
        return;

    if(slices_changed) {
        /*
         * Keep the original high-frequency contour family for the modes that
         * already looked good, but do NOT drive modes 3/4/6 from it anymore.
         * Those modes now get their own softer/non-periodic relief tables:
         *   - 3 Contour: source-edge contour relief, no equal-depth islands.
         *   - 4 Shells : broad posterized depth bodies, no razor ridges.
         *   - 6 Fossil : edge emboss + mild depth patina, no shell ringing.
         */
        int period_q8 = 65536 / slices;
        if(period_q8 < 1024) period_q8 = 1024;

        int ridge_w_q8 = period_q8 >> 3;
        if(ridge_w_q8 < 160) ridge_w_q8 = 160;
        if(ridge_w_q8 > 448) ridge_w_q8 = 448;

        int halo_w_q8 = period_q8 >> 1;
        if(halo_w_q8 < 512) halo_w_q8 = 512;
        if(halo_w_q8 > 2304) halo_w_q8 = 2304;

        int shell_levels = 3 + (((slices - 2) * 9 + 31) / 62); /* 3..12 */
        if(shell_levels < 3) shell_levels = 3;
        if(shell_levels > 12) shell_levels = 12;
        int shell_step = 256 / shell_levels;
        if(shell_step < 1) shell_step = 1;

        for(int d = 0; d < 256; d++) {
            const int pos_q8 = d << 8;
            int shell_center_q8 = ((pos_q8 + (period_q8 >> 1)) / period_q8) * period_q8;
            int dist_q8 = pos_q8 - shell_center_q8;
            if(dist_q8 < 0) dist_q8 = -dist_q8;

            int ridge = 0;
            if(dist_q8 < ridge_w_q8) {
                const int q = 255 - ((dist_q8 * 255) / ridge_w_q8);
                ridge = tl_smooth_q8(q);
            }

            int halo = 0;
            if(dist_q8 < halo_w_q8) {
                const int q = 255 - ((dist_q8 * 255) / halo_w_q8);
                halo = (tl_smooth_q8(q) * 72) >> 8;
            }

            int cv = ridge + ((halo * (255 - ridge)) >> 8);
            if(cv > 255) cv = 255;

            /* Mode 3 helper: monotonic depth relief. The line work comes from
             * the source gradient in the pixel loop, not from periodic depth. */
            int contour_relief = 12 + ((d * 42) >> 8) + ((d * d) >> 12);
            if(contour_relief > 96) contour_relief = 96;

            /* Mode 4 helper: broad shell bodies. Slice Count controls the number
             * of large bands, compressed to 3..12 so HD video does not turn into
             * dense contour-map ringing. Boundary accent is intentionally weak. */
            int bucket = d / shell_step;
            if(bucket >= shell_levels) bucket = shell_levels - 1;
            int base = (shell_levels > 1) ? ((bucket * 196) / (shell_levels - 1)) : 0;
            int within = d - (bucket * shell_step);
            int q = (within * 255) / shell_step;
            if(q > 255) q = 255;
            int edge_q = (q < 128) ? q : (255 - q);
            edge_q <<= 1;
            if(edge_q > 255) edge_q = 255;
            int soft_boundary = (tl_smooth_q8(edge_q) * 24) >> 8;
            int shell_body = 22 + ((base * 150) >> 8) + (d >> 4) + soft_boundary;
            if(shell_body > 230) shell_body = 230;

            /* Mode 6 helper: old-stone patina only. Real fossil detail is the
             * source edge/emboss term in the edge path. */
            int inv = 255 - d;
            int fossil = 18 + (d >> 5) + ((inv * inv) >> 13);
            if(fossil > 96) fossil = 96;

            /* Mode 8: dense monotonic MRI response. No periodic shell/ridge term. */
            int mid = d - 128;
            if(mid < 0) mid = -mid;
            int shoulder = 255 - (mid << 1);
            if(shoulder < 0) shoulder = 0;
            shoulder = (tl_smooth_q8(shoulder) * 34) >> 8;

            int mri = ((d * 178) >> 8) + shoulder + ((d * d) >> 11);
            if(mri > 255) mri = 255;

            t->contour_lut[d] = (uint8_t) cv;
            t->contour_relief_lut[d] = (uint8_t) contour_relief;
            t->shell_body_lut[d] = (uint8_t) shell_body;
            t->fossil_lut[d] = (uint8_t) fossil;
            t->mri_lut[d] = (uint8_t) mri;
        }

        t->last_slices = slices;
        t->last_render_mode = -1;
    }

    if(scan_changed) {
        const int scan_halo_w = width + (width >> 1) + 1;

        for(int d = 0; d < 256; d++) {
            int band = 0;
            int sv = 0;
            int dist = d - center;
            if(dist < 0) dist = -dist;

            if(dist < width) {
                int q = 255 - ((dist * 255) / width);
                band = tl_smooth_q8(q);
                sv = band;
            }
            if(dist < scan_halo_w) {
                int hq = 255 - ((dist * 255) / scan_halo_w);
                int hv = (tl_smooth_q8(hq) * 70) >> 8;
                if(hv > sv) sv = hv;
            }

            t->scan_lut[d] = (uint8_t) sv;
            t->lightsheet_lut[d] = (uint8_t) band;
        }

        t->last_center = center;
        t->last_width = width;
        t->last_render_mode = -1;
    }
}

static void tl_rebuild_depth_luts(tomolight_t *t, int depth_scale_q8, int motion_react_q8)
{
    if(t->last_depth_scale_q8 == depth_scale_q8 &&
       t->last_motion_react_q8 == motion_react_q8)
        return;

    for(int i = 0; i < 256; i++) {
        int d = (i * depth_scale_q8 + 128) >> 8;
        d = (d > 255) ? 255 : d;
        t->depth_luma_lut[i] = (uint8_t) d;
        t->edge_depth_lut[i] = (uint8_t) d;

        d = ((255 - i) * depth_scale_q8 + 128) >> 8;
        t->depth_inv_lut[i] = (uint8_t)((d > 255) ? 255 : d);

        int m = (i * motion_react_q8 + 128) >> 8;
        m = (m > 255) ? 255 : m;
        t->motion_add_lut[i] = (uint8_t) m;
        t->motion_depth_lut[i] = t->depth_luma_lut[m];
    }

    t->last_depth_scale_q8 = depth_scale_q8;
    t->last_motion_react_q8 = motion_react_q8;
}

static void tl_rebuild_light_lut(tomolight_t *t, int render_mode)
{
    if(t->last_render_mode == render_mode)
        return;

    for(int d = 0; d < 256; d++) {
        const int contour = t->contour_lut[d];
        const int contour_relief = t->contour_relief_lut[d];
        const int shell_body = t->shell_body_lut[d];
        const int fossil = t->fossil_lut[d];
        const int mri = t->mri_lut[d];
        const int scan = t->scan_lut[d];
        int v;

        switch(render_mode) {
            case TL_MODE_SCAN:
                v = scan + (contour >> 3) + ((scan * contour) >> 9);
                break;
            case TL_MODE_VOLUME:
                /* Broad volumetric density.  Do not add contour ridges here:
                 * they make codec/DCT block boundaries look like internal
                 * volume slices after residue/glow has integrated them. */
                v = ((d * 176) >> 8) + ((scan * 56) >> 8) + ((d * (255 - d)) >> 12);
                break;
            case TL_MODE_FOG:
                /* Fog must be density, not contour.  The old contour term made
                 * low-amplitude codec/blocking noise look like decompression
                 * artifacts after residue/glow accumulated it. */
                v = ((d * 118) >> 8) + ((scan * 72) >> 8) + ((d * (255 - d)) >> 11);
                break;
            case TL_MODE_XRAY:
                v = (d >> 1) + (scan >> 2) + (contour >> 4);
                break;
            case TL_MODE_CONTOUR:
                v = contour_relief;
                break;
            case TL_MODE_SHELLS:
                v = shell_body;
                break;
            case TL_MODE_FOSSIL:
                v = fossil;
                break;
            case TL_MODE_MRI:
                v = mri;
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

static inline void tl_fixed_uv_base(int mode, int d, int ty, int *tu, int *tv)
{
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
                tl_fixed_uv_base(color_mode, d, ty, &tu, &tv);
                tu = ((tu * ty) + (128 * (255 - ty))) >> 8;
                tv = ((tv * ty) + (128 * (255 - ty))) >> 8;
                t->fixed_u_lut[row | ty] = tl_u8(tu);
                t->fixed_v_lut[row | ty] = tl_u8(tv);
            }
        }
    }

    t->last_color_mode = color_mode;
}

static void tl_init_absdiff_lut(tomolight_t *t)
{
    for(int d = -255; d <= 255; d++)
        t->absdiff_lut[d + 255] = (uint8_t)((d < 0) ? -d : d);
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

static inline uint8_t *tl_align_ptr(uint8_t *p, size_t align)
{
    uintptr_t v = (uintptr_t) p;
    v = (v + (uintptr_t)(align - 1)) & ~((uintptr_t)(align - 1));
    return (uint8_t*) v;
}

static int tl_init_geometry_maps(tomolight_t *t)
{
    const int w = t->w;
    const int h = t->h;
    const int gw = t->gw;
    const int gh = t->gh;

    size_t total = 64;
    total += (sizeof(uint16_t) * 2u + sizeof(uint8_t) * 2u) * (size_t) w; /* glow x */
    total += sizeof(uint16_t) * 2u * (size_t) w;                         /* edge x */
    total += (sizeof(int) * 2u + sizeof(uint8_t) * 3u) * (size_t) h;      /* glow y + y band */
    total += sizeof(int) * 2u * (size_t) h;                              /* edge rows */
    total += sizeof(uint16_t) * 5u * (size_t) gw;                        /* glow downsample + hblur */
    total += sizeof(int) * 5u * (size_t) gh;                             /* glow downsample + vblur */
    total += 128;

    t->map_region = (uint8_t*) vj_calloc(total);
    if(!t->map_region)
        return 0;

    uint8_t *p = t->map_region;

    p = tl_align_ptr(p, sizeof(uint16_t));
    t->glow_x0 = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) w;
    t->glow_x1 = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) w;

    t->glow_fx  = p; p += (size_t) w;
    t->glow_wx0 = p; p += (size_t) w;

    p = tl_align_ptr(p, sizeof(uint16_t));
    t->edge_xm = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) w;
    t->edge_xp = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) w;

    p = tl_align_ptr(p, sizeof(int));
    t->glow_y0 = (int*) p; p += sizeof(int) * (size_t) h;
    t->glow_y1 = (int*) p; p += sizeof(int) * (size_t) h;

    t->glow_fy  = p; p += (size_t) h;
    t->glow_wy0 = p; p += (size_t) h;
    t->y_band_lut = p; p += (size_t) h;

    p = tl_align_ptr(p, sizeof(int));
    t->edge_rowm = (int*) p; p += sizeof(int) * (size_t) h;
    t->edge_rowp = (int*) p; p += sizeof(int) * (size_t) h;

    p = tl_align_ptr(p, sizeof(uint16_t));
    t->down_x0   = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) gw;
    t->down_x1   = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) gw;
    t->down_x2   = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) gw;
    t->blur_x_rm = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) gw;
    t->blur_x_ap = (uint16_t*) p; p += sizeof(uint16_t) * (size_t) gw;

    p = tl_align_ptr(p, sizeof(int));
    t->down_row0  = (int*) p; p += sizeof(int) * (size_t) gh;
    t->down_row1  = (int*) p; p += sizeof(int) * (size_t) gh;
    t->down_row2  = (int*) p; p += sizeof(int) * (size_t) gh;
    t->blur_y_rm  = (int*) p; p += sizeof(int) * (size_t) gh;
    t->blur_y_ap  = (int*) p;

    for(int x = 0; x < w; x++) {
        int xs = x - (TL_GLOW_STEP >> 1);
        int gx;
        int fx;

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

        t->glow_x0[x] = (uint16_t) gx;
        t->glow_x1[x] = (uint16_t) ((gx + 1 < gw) ? gx + 1 : gx);
        t->glow_fx[x] = (uint8_t) fx;
        t->glow_wx0[x] = (uint8_t) (TL_GLOW_STEP - fx);
        t->edge_xm[x] = (uint16_t) ((x > 0) ? x - 1 : x);
        t->edge_xp[x] = (uint16_t) ((x + 1 < w) ? x + 1 : x);
    }

    for(int y = 0; y < h; y++) {
        int ys = y - (TL_GLOW_STEP >> 1);
        int gy;
        int fy;

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

        t->glow_y0[y] = gy * gw;
        t->glow_y1[y] = ((gy + 1 < gh) ? gy + 1 : gy) * gw;
        t->glow_fy[y] = (uint8_t) fy;
        t->glow_wy0[y] = (uint8_t) (TL_GLOW_STEP - fy);
        t->y_band_lut[y] = (uint8_t) ((h > 1) ? ((y * 255) / (h - 1)) : 0);
        t->edge_rowm[y] = ((y > 0) ? y - 1 : y) * w;
        t->edge_rowp[y] = ((y + 1 < h) ? y + 1 : y) * w;
    }

    for(int gx = 0; gx < gw; gx++) {
        const int x0 = gx << TL_GLOW_SHIFT;
        const int x1 = x0 + (TL_GLOW_STEP >> 1);
        const int x2 = x0 + TL_GLOW_MASK;
        t->down_x0[gx] = (uint16_t) ((x0 < w) ? x0 : w - 1);
        t->down_x1[gx] = (uint16_t) ((x1 < w) ? x1 : w - 1);
        t->down_x2[gx] = (uint16_t) ((x2 < w) ? x2 : w - 1);
        t->blur_x_rm[gx] = (uint16_t) ((gx > TL_GLOW_RADIUS) ? gx - TL_GLOW_RADIUS : 0);
        t->blur_x_ap[gx] = (uint16_t) ((gx + TL_GLOW_RADIUS + 1 < gw) ? gx + TL_GLOW_RADIUS + 1 : gw - 1);
    }

    for(int gy = 0; gy < gh; gy++) {
        const int y0 = gy << TL_GLOW_SHIFT;
        const int y1 = y0 + (TL_GLOW_STEP >> 1);
        const int y2 = y0 + TL_GLOW_MASK;
        t->down_row0[gy] = ((y0 < h) ? y0 : h - 1) * w;
        t->down_row1[gy] = ((y1 < h) ? y1 : h - 1) * w;
        t->down_row2[gy] = ((y2 < h) ? y2 : h - 1) * w;
        t->blur_y_rm[gy] = ((gy > TL_GLOW_RADIUS) ? gy - TL_GLOW_RADIUS : 0) * gw;
        t->blur_y_ap[gy] = ((gy + TL_GLOW_RADIUS + 1 < gh) ? gy + TL_GLOW_RADIUS + 1 : gh - 1) * gw;
    }

    return 1;
}

static inline int tl_sample_glow_mapped(const uint8_t * restrict glow,
                                        const uint16_t * restrict glow_x0,
                                        const uint16_t * restrict glow_x1,
                                        const uint8_t * restrict glow_fx,
                                        const uint8_t * restrict glow_wx0,
                                        const int * restrict glow_y0,
                                        const int * restrict glow_y1,
                                        const uint8_t * restrict glow_fy,
                                        const uint8_t * restrict glow_wy0,
                                        int x, int y)
{
    const int gx0 = glow_x0[x];
    const int gx1 = glow_x1[x];
    const int fx = glow_fx[x];
    const int wx0 = glow_wx0[x];
    const int row0 = glow_y0[y];
    const int row1 = glow_y1[y];
    const int fy = glow_fy[y];
    const int wy0 = glow_wy0[y];

    const int a = ((int)glow[row0 + gx0] * wx0) + ((int)glow[row0 + gx1] * fx);
    const int b = ((int)glow[row1 + gx0] * wx0) + ((int)glow[row1 + gx1] * fx);
    return ((a * wy0) + (b * fy) + 32) >> 6;
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

    if(!tl_init_geometry_maps(t)) {
        free(t->region);
        free(t);
        return NULL;
    }

    t->last_slices = -1;
    t->last_center = -1;
    t->last_width = -1;
    t->last_depth_scale_q8 = -1;
    t->last_motion_react_q8 = -1;
    t->last_amount_q8 = -1;
    t->last_inject_q8 = -1;
    t->last_light_strength = -1;
    t->last_render_mode = -1;
    t->last_apply_render_mode = -1;
    t->last_color_mode = -1;
    t->last_static_chroma = 0;

    tl_init_absdiff_lut(t);
    tl_rebuild_static_chroma_luts(t);

    return (void*) t;
}

void tomolight_free(void *ptr)
{
    tomolight_t *t = (tomolight_t*) ptr;
    if(!t) return;
    free(t->map_region);
    free(t->region);
    free(t);
}

#define TL_OMP_FOR _Pragma("omp for schedule(static)")

#define TL_DEPTH_LUMA() \
    do { \
        d = depth_luma_lut[sy]; \
    } while(0)

#define TL_DEPTH_INV_LUMA() \
    do { \
        d = depth_inv_lut[sy]; \
    } while(0)

#define TL_DEPTH_EDGE() \
    do { \
        d = edge_depth_lut[edge]; \
    } while(0)

#define TL_DEPTH_MOTION() \
    do { \
        const int md__ = absdiff_lut[(int)sy - (int)prev_y[i] + 255]; \
        d = motion_depth_lut[md__]; \
        prev_y[i] = (uint8_t) sy; \
    } while(0)

#define TL_DEPTH_LUMA_MOTION() \
    do { \
        const int md__ = absdiff_lut[(int)sy - (int)prev_y[i] + 255]; \
        int dd__ = sy + motion_add_lut[md__]; \
        dd__ = (dd__ > 255) ? 255 : dd__; \
        d = depth_luma_lut[dd__]; \
        prev_y[i] = (uint8_t) sy; \
    } while(0)

#define TL_STORE_CHROMA_SOURCE() \
    do { \
        const int rowc__ = cy << 8; \
        const int u0__ = U[i]; \
        const int v0__ = V[i]; \
        const int tu__ = src_chroma_lut[rowc__ | u0__]; \
        const int tv__ = src_chroma_lut[rowc__ | v0__]; \
        U[i] = chroma_amount_lut[(tu__ << 8) | u0__]; \
        V[i] = chroma_amount_lut[(tv__ << 8) | v0__]; \
    } while(0)

#define TL_STORE_CHROMA_INVERT() \
    do { \
        const int rowc__ = cy << 8; \
        const int u0__ = U[i]; \
        const int v0__ = V[i]; \
        const int tu__ = inv_chroma_lut[rowc__ | u0__]; \
        const int tv__ = inv_chroma_lut[rowc__ | v0__]; \
        U[i] = chroma_amount_lut[(tu__ << 8) | u0__]; \
        V[i] = chroma_amount_lut[(tv__ << 8) | v0__]; \
    } while(0)

#define TL_STORE_CHROMA_FIXED() \
    do { \
        const int idxc__ = (d << 8) | cy; \
        const int u0__ = U[i]; \
        const int v0__ = V[i]; \
        const int tu__ = fixed_u_lut[idxc__]; \
        const int tv__ = fixed_v_lut[idxc__]; \
        U[i] = chroma_amount_lut[(tu__ << 8) | u0__]; \
        V[i] = chroma_amount_lut[(tv__ << 8) | v0__]; \
    } while(0)

#define TL_GLOW_SAMPLE() \
    glow_lut[tl_sample_glow_mapped(glow, glow_x0, glow_x1, glow_fx, glow_wx0, \
                                   glow_y0, glow_y1, glow_fy, glow_wy0, x, y)]

#define TL_LIGHT_TY_EDGE_VOLUME() \
    do { \
        const int scan__ = scan_lut[d]; \
        const int light__ = ((d * 176) >> 8) + ((scan__ * 56) >> 8) + ((d * (255 - d)) >> 12); \
        const int ly__ = light__ + TL_GLOW_SAMPLE() - shadow_lut[edge]; \
        ty = (sy >> 4) + ly__; \
    } while(0)

#define TL_LIGHT_TY_EDGE_SCAN() \
    do { \
        const int contour__ = contour_lut[d]; \
        const int scan__ = scan_lut[d]; \
        int light__ = scan__ + (contour__ >> 3) + (edge >> 2) + ((scan__ * contour__) >> 9); \
        light__ = (light__ > 255) ? 255 : light__; \
        const int ly__ = light__ + TL_GLOW_SAMPLE() - shadow_lut[edge]; \
        ty = (sy >> 4) + ly__; \
    } while(0)

#define TL_LIGHT_TY_EDGE_XRAY() \
    do { \
        const int contour__ = contour_lut[d]; \
        const int scan__ = scan_lut[d]; \
        int light__ = ((255 - sy) >> 1) + (d >> 1) + (edge >> 1) + (scan__ >> 2) + (contour__ >> 4); \
        light__ = (light__ > 255) ? 255 : light__; \
        const int ly__ = light__ + TL_GLOW_SAMPLE() - shadow_lut[edge]; \
        ty = ((255 - sy) >> 2) + ly__; \
    } while(0)

#define TL_LIGHT_TY_EDGE_CONTOUR() \
    do { \
        int light__ = contour_relief_lut[d] + ((edge * 210) >> 8); \
        light__ = (light__ > 255) ? 255 : light__; \
        ty = (sy >> 4) + light__ + TL_GLOW_SAMPLE(); \
    } while(0)

#define TL_LIGHT_TY_EDGE_SHELLS() \
    do { \
        int light__ = shell_body_lut[d] + ((edge * 56) >> 8); \
        light__ = (light__ > 255) ? 255 : light__; \
        ty = (sy >> 4) + light__ + TL_GLOW_SAMPLE(); \
    } while(0)

#define TL_LIGHT_TY_EDGE_RADAR() \
    do { \
        const int scan__ = scan_lut[d]; \
        int light__ = ((((int)radar_lut[(x + y_band) & 255]) * (scan__ + 72)) >> 8) + (d >> 4); \
        light__ = (light__ > 255) ? 255 : light__; \
        const int ly__ = light__ + TL_GLOW_SAMPLE() - shadow_lut[edge]; \
        ty = (sy >> 4) + ly__; \
    } while(0)

#define TL_LIGHT_TY_EDGE_FOSSIL() \
    do { \
        int light__ = fossil_lut[d] + ((edge * 230) >> 8); \
        light__ = (light__ > 255) ? 255 : light__; \
        ty = (sy >> 3) + light__ + TL_GLOW_SAMPLE(); \
    } while(0)

#define TL_LIGHT_TY_EDGE_LIGHTSHEET() \
    do { \
        const int contour__ = contour_lut[d]; \
        const int scan__ = scan_lut[d]; \
        int light__ = lightsheet_lut[y_band] + (scan__ >> 1) + (edge >> 2) + (contour__ >> 4); \
        light__ = (light__ > 255) ? 255 : light__; \
        const int ly__ = light__ + TL_GLOW_SAMPLE() - shadow_lut[edge]; \
        ty = (sy >> 4) + ly__; \
    } while(0)

#define TL_LIGHT_TY_EDGE_MRI() \
    do { \
        int light__ = mri_lut[d] + (edge >> 3); \
        light__ = (light__ > 255) ? 255 : light__; \
        const int ly__ = light__ + TL_GLOW_SAMPLE() - shadow_lut[edge]; \
        ty = (sy >> 4) + ly__; \
    } while(0)

#define TL_LIGHT_TY_EDGE_FOG() \
    do { \
        const int scan__ = scan_lut[d]; \
        const int light__ = ((d * 118) >> 8) + ((scan__ * 72) >> 8) + ((d * (255 - d)) >> 11); \
        const int ly__ = light__ + TL_GLOW_SAMPLE() - shadow_lut[edge]; \
        ty = (sy >> 4) + ((ly__ * 5) >> 3); \
    } while(0)

#define TL_EDGE_LOOP(DEPTH_CODE, LIGHT_TY_CODE, CHROMA_CODE) \
    do { \
        TL_OMP_FOR \
        for(int y = 0; y < h; y++) { \
            const int row = y * w; \
            const int rowm = edge_rowm[y]; \
            const int rowp = edge_rowp[y]; \
            const int y_band = y_band_lut[y]; \
            (void)y_band; \
            for(int x = 0; x < w; x++) { \
                const int i = row + x; \
                const int sy = src_y[i]; \
                int edge = absdiff_lut[(int)src_y[row + edge_xp[x]] - (int)src_y[row + edge_xm[x]] + 255] + \
                           absdiff_lut[(int)src_y[rowp + x] - (int)src_y[rowm + x] + 255]; \
                edge = (edge > 510) ? 255 : (edge >> 1); \
                int d; \
                DEPTH_CODE(); \
                int ty; \
                LIGHT_TY_CODE(); \
                ty = (ty < 0) ? 0 : ((ty > 255) ? 255 : ty); \
                const int ry = res_blend_lut[(res_y[i] << 8) | ty]; \
                const int cy = (ty + ry + 1) >> 1; \
                res_y[i] = (uint8_t) ry; \
                Y[i] = y_amount_lut[(sy << 8) | ry]; \
                CHROMA_CODE(); \
            } \
        } \
    } while(0)

#define TL_SOFT_LOOP(DEPTH_CODE, LIGHT_TY_CODE, CHROMA_CODE) \
    do { \
        TL_OMP_FOR \
        for(int y = 0; y < h; y++) { \
            const int row = y * w; \
            const int rowm = edge_rowm[y]; \
            const int rowp = edge_rowp[y]; \
            const int y_band = y_band_lut[y]; \
            (void)y_band; \
            for(int x = 0; x < w; x++) { \
                const int i = row + x; \
                const int src = src_y[i]; \
                const int sy = ((src << 2) + (int)src_y[row + edge_xm[x]] + \
                                (int)src_y[row + edge_xp[x]] + \
                                (int)src_y[rowm + x] + (int)src_y[rowp + x] + 4) >> 3; \
                int edge = 0; \
                (void)edge; \
                int d; \
                DEPTH_CODE(); \
                int ty; \
                LIGHT_TY_CODE(); \
                ty = (ty > 255) ? 255 : ty; \
                const int ry = res_blend_lut[(res_y[i] << 8) | ty]; \
                const int cy = (ty + ry + 1) >> 1; \
                res_y[i] = (uint8_t) ry; \
                Y[i] = y_amount_lut[(src << 8) | ry]; \
                CHROMA_CODE(); \
            } \
        } \
    } while(0)

#define TL_LIGHT_TY_SOFT_LUT() \
    do { \
        const int ly__ = light_lut[d] + TL_GLOW_SAMPLE(); \
        ty = (sy >> 4) + ly__; \
    } while(0)

#define TL_LIGHT_TY_SOFT_RADAR() \
    do { \
        const int scan__ = scan_lut[d]; \
        const int light__ = ((((int)radar_lut[(x + y_band) & 255]) * (scan__ + 72)) >> 8) + (d >> 4); \
        const int ly__ = light__ + TL_GLOW_SAMPLE(); \
        ty = (sy >> 4) + ly__; \
    } while(0)

#define TL_LIGHT_TY_SOFT_FOG() \
    do { \
        const int ly__ = light_lut[d] + TL_GLOW_SAMPLE(); \
        ty = (sy >> 4) + ((ly__ * 5) >> 3); \
    } while(0)

#define TL_SIMPLE_LOOP(DEPTH_CODE, CHROMA_CODE) \
    do { \
        TL_OMP_FOR \
        for(int y = 0; y < h; y++) { \
            const int row = y * w; \
            for(int x = 0; x < w; x++) { \
                const int i = row + x; \
                const int sy = Y[i]; \
                int edge = 0; \
                (void)edge; \
                int d; \
                DEPTH_CODE(); \
                int ty = (sy >> 4) + light_lut[d] + TL_GLOW_SAMPLE(); \
                ty = (ty > 255) ? 255 : ty; \
                const int ry = res_blend_lut[(res_y[i] << 8) | ty]; \
                const int cy = (ty + ry + 1) >> 1; \
                res_y[i] = (uint8_t) ry; \
                Y[i] = y_amount_lut[(sy << 8) | ry]; \
                CHROMA_CODE(); \
            } \
        } \
    } while(0)

#define TL_DISPATCH_COLOR_EDGE(DEPTH_CODE, LIGHT_TY_CODE) \
    do { \
        if(color_mode == TL_COLOR_SOURCE) { \
            TL_EDGE_LOOP(DEPTH_CODE, LIGHT_TY_CODE, TL_STORE_CHROMA_SOURCE); \
        } else if(color_mode == TL_COLOR_INVERT) { \
            TL_EDGE_LOOP(DEPTH_CODE, LIGHT_TY_CODE, TL_STORE_CHROMA_INVERT); \
        } else { \
            TL_EDGE_LOOP(DEPTH_CODE, LIGHT_TY_CODE, TL_STORE_CHROMA_FIXED); \
        } \
    } while(0)

#define TL_DISPATCH_COLOR_SOFT(DEPTH_CODE, LIGHT_TY_CODE) \
    do { \
        if(color_mode == TL_COLOR_SOURCE) { \
            TL_SOFT_LOOP(DEPTH_CODE, LIGHT_TY_CODE, TL_STORE_CHROMA_SOURCE); \
        } else if(color_mode == TL_COLOR_INVERT) { \
            TL_SOFT_LOOP(DEPTH_CODE, LIGHT_TY_CODE, TL_STORE_CHROMA_INVERT); \
        } else { \
            TL_SOFT_LOOP(DEPTH_CODE, LIGHT_TY_CODE, TL_STORE_CHROMA_FIXED); \
        } \
    } while(0)

#define TL_DISPATCH_COLOR_SIMPLE(DEPTH_CODE) \
    do { \
        if(color_mode == TL_COLOR_SOURCE) { \
            TL_SIMPLE_LOOP(DEPTH_CODE, TL_STORE_CHROMA_SOURCE); \
        } else if(color_mode == TL_COLOR_INVERT) { \
            TL_SIMPLE_LOOP(DEPTH_CODE, TL_STORE_CHROMA_INVERT); \
        } else { \
            TL_SIMPLE_LOOP(DEPTH_CODE, TL_STORE_CHROMA_FIXED); \
        } \
    } while(0)

#define TL_DISPATCH_DEPTH_EDGE(LIGHT_TY_CODE) \
    do { \
        switch(depth_source) { \
            case TL_SRC_INV_LUMA:    TL_DISPATCH_COLOR_EDGE(TL_DEPTH_INV_LUMA, LIGHT_TY_CODE); break; \
            case TL_SRC_EDGE:        TL_DISPATCH_COLOR_EDGE(TL_DEPTH_EDGE, LIGHT_TY_CODE); break; \
            case TL_SRC_MOTION:      TL_DISPATCH_COLOR_EDGE(TL_DEPTH_MOTION, LIGHT_TY_CODE); break; \
            case TL_SRC_LUMA_MOTION: TL_DISPATCH_COLOR_EDGE(TL_DEPTH_LUMA_MOTION, LIGHT_TY_CODE); break; \
            case TL_SRC_LUMA: \
            default:                 TL_DISPATCH_COLOR_EDGE(TL_DEPTH_LUMA, LIGHT_TY_CODE); break; \
        } \
    } while(0)

#define TL_DISPATCH_DEPTH_SOFT(LIGHT_TY_CODE) \
    do { \
        switch(depth_source) { \
            case TL_SRC_INV_LUMA:    TL_DISPATCH_COLOR_SOFT(TL_DEPTH_INV_LUMA, LIGHT_TY_CODE); break; \
            case TL_SRC_EDGE:        TL_DISPATCH_COLOR_SOFT(TL_DEPTH_EDGE, LIGHT_TY_CODE); break; \
            case TL_SRC_MOTION:      TL_DISPATCH_COLOR_SOFT(TL_DEPTH_MOTION, LIGHT_TY_CODE); break; \
            case TL_SRC_LUMA_MOTION: TL_DISPATCH_COLOR_SOFT(TL_DEPTH_LUMA_MOTION, LIGHT_TY_CODE); break; \
            case TL_SRC_LUMA: \
            default:                 TL_DISPATCH_COLOR_SOFT(TL_DEPTH_LUMA, LIGHT_TY_CODE); break; \
        } \
    } while(0)

#define TL_DISPATCH_DEPTH_SIMPLE() \
    do { \
        switch(depth_source) { \
            case TL_SRC_INV_LUMA:    TL_DISPATCH_COLOR_SIMPLE(TL_DEPTH_INV_LUMA); break; \
            case TL_SRC_EDGE:        TL_DISPATCH_COLOR_SIMPLE(TL_DEPTH_EDGE); break; \
            case TL_SRC_MOTION:      TL_DISPATCH_COLOR_SIMPLE(TL_DEPTH_MOTION); break; \
            case TL_SRC_LUMA_MOTION: TL_DISPATCH_COLOR_SIMPLE(TL_DEPTH_LUMA_MOTION); break; \
            case TL_SRC_LUMA: \
            default:                 TL_DISPATCH_COLOR_SIMPLE(TL_DEPTH_LUMA); break; \
        } \
    } while(0)

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
    const int depth_arg = tl_clampi(args[P_DEPTH_SCALE], 0, 300);
    const int depth_scale_q8 = (depth_arg * 256 + 50) / 100;
    const int motion_react_q8 = ((tl_clampi(50 + (depth_arg / 5), 50, 110)) * 256 + 50) / 100;
    const int slices = tl_clampi(args[P_SLICES], 2, 64);
    const int render_mode = tl_clampi(args[P_RENDER_MODE], 0, 9);
    const int color_mode = tl_clampi(args[P_COLOR_MODE], 0, 8);
    const int fog_mode = (render_mode == TL_MODE_FOG);
    const int mode_radar = (render_mode == TL_MODE_RADAR);
    const int soft_depth_mode = fog_mode ||
                                (render_mode == TL_MODE_VOLUME) ||
                                mode_radar;
    const int scan_dependent_mode =
        (render_mode == TL_MODE_VOLUME) ||
        (render_mode == TL_MODE_SCAN) ||
        (render_mode == TL_MODE_XRAY) ||
        mode_radar ||
        (render_mode == TL_MODE_LIGHTSHEET) ||
        fog_mode;
    const int center = scan_dependent_mode ?
        tl_scan_center(frame_no, tl_clampi(args[P_SCAN_POS], 0, 1000),
                       tl_clampi(args[P_SCAN_MOTION], 0, 5)) :
        ((t->last_center >= 0) ? t->last_center : 128);
    int width = scan_dependent_mode ?
        ((tl_clampi(args[P_SCAN_WIDTH], 1, 100) * 255 + 50) / 100) :
        ((t->last_width >= 1) ? t->last_width : 46);
    width = (width < 1) ? 1 : width;

    const int true_edge =
        (depth_source == TL_SRC_EDGE) ||
        (render_mode == TL_MODE_XRAY) ||
        (render_mode == TL_MODE_CONTOUR) ||
        (render_mode == TL_MODE_SHELLS) ||
        (render_mode == TL_MODE_FOSSIL) ||
        (render_mode == TL_MODE_LIGHTSHEET);

    const int needs_src_copy = true_edge || soft_depth_mode;

    tl_rebuild_shape_luts(t, slices, center, width);
    tl_rebuild_depth_luts(t, depth_scale_q8, motion_react_q8);
    tl_rebuild_light_lut(t, render_mode);
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
    const uint8_t * restrict contour_relief_lut = t->contour_relief_lut;
    const uint8_t * restrict shell_body_lut = t->shell_body_lut;
    const uint8_t * restrict fossil_lut = t->fossil_lut;
    const uint8_t * restrict mri_lut = t->mri_lut;
    const uint8_t * restrict scan_lut = t->scan_lut;
    const uint8_t * restrict light_lut = t->light_lut;
    const uint8_t * restrict depth_luma_lut = t->depth_luma_lut;
    const uint8_t * restrict depth_inv_lut = t->depth_inv_lut;
    const uint8_t * restrict edge_depth_lut = t->edge_depth_lut;
    const uint8_t * restrict motion_depth_lut = t->motion_depth_lut;
    const uint8_t * restrict motion_add_lut = t->motion_add_lut;
    const uint8_t * restrict glow_lut = t->glow_lut;
    const uint8_t * restrict shadow_lut = t->shadow_lut;
    const uint8_t * restrict res_blend_lut = t->res_blend_lut;
    const uint8_t * restrict y_amount_lut = t->y_amount_lut;
    const uint8_t * restrict chroma_amount_lut = t->chroma_amount_lut;
    const uint8_t * restrict src_chroma_lut = t->src_chroma_lut;
    const uint8_t * restrict inv_chroma_lut = t->inv_chroma_lut;
    const uint8_t * restrict fixed_u_lut = t->fixed_u_lut;
    const uint8_t * restrict fixed_v_lut = t->fixed_v_lut;
    const uint8_t * restrict absdiff_lut = t->absdiff_lut;
    const uint8_t * restrict lightsheet_lut = t->lightsheet_lut;

    const uint16_t * restrict glow_x0 = t->glow_x0;
    const uint16_t * restrict glow_x1 = t->glow_x1;
    const uint8_t * restrict glow_fx = t->glow_fx;
    const uint8_t * restrict glow_wx0 = t->glow_wx0;
    const int * restrict glow_y0 = t->glow_y0;
    const int * restrict glow_y1 = t->glow_y1;
    const uint8_t * restrict glow_fy = t->glow_fy;
    const uint8_t * restrict glow_wy0 = t->glow_wy0;
    const uint8_t * restrict y_band_lut = t->y_band_lut;
    const uint16_t * restrict edge_xm = t->edge_xm;
    const uint16_t * restrict edge_xp = t->edge_xp;
    const int * restrict edge_rowm = t->edge_rowm;
    const int * restrict edge_rowp = t->edge_rowp;
    const uint16_t * restrict down_x0 = t->down_x0;
    const uint16_t * restrict down_x1 = t->down_x1;
    const uint16_t * restrict down_x2 = t->down_x2;
    const uint16_t * restrict blur_x_rm = t->blur_x_rm;
    const uint16_t * restrict blur_x_ap = t->blur_x_ap;
    const int * restrict down_row0 = t->down_row0;
    const int * restrict down_row1 = t->down_row1;
    const int * restrict down_row2 = t->down_row2;
    const int * restrict blur_y_rm = t->blur_y_rm;
    const int * restrict blur_y_ap = t->blur_y_ap;

    if(t->last_apply_render_mode != render_mode) {
        memset(res_y, 0, (size_t) len);
        memset(glow, 0, (size_t) t->glen);
        memset(glow_tmp, 0, (size_t) t->glen);
        memset(glow_next, 0, (size_t) t->glen);
        t->last_apply_render_mode = render_mode;
    }

    uint8_t radar_lut[256];
    if(mode_radar) {
        const int phase = (frame_no << 2) & 255;
        for(int i = 0; i < 256; i++) {
            const int sweep = absdiff_lut[((i + phase) & 255) + 127];
            const int v = 255 - (sweep << 1);
            radar_lut[i] = (uint8_t)((v < 0) ? 0 : v);
        }
    }

#pragma omp parallel num_threads(t->n_threads)
    {
        if(needs_src_copy) {
            TL_OMP_FOR
            for(int i = 0; i < len; i++)
                src_y[i] = Y[i];
        }

        if(true_edge) {
            switch(render_mode) {
                case TL_MODE_SCAN:       TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_SCAN); break;
                case TL_MODE_VOLUME:     TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_VOLUME); break;
                case TL_MODE_FOG:        TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_FOG); break;
                case TL_MODE_LIGHTSHEET: TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_LIGHTSHEET); break;
                case TL_MODE_RADAR:      TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_RADAR); break;
                case TL_MODE_XRAY:       TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_XRAY); break;
                case TL_MODE_MRI:        TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_MRI); break;
                case TL_MODE_CONTOUR:    TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_CONTOUR); break;
                case TL_MODE_SHELLS:     TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_SHELLS); break;
                case TL_MODE_FOSSIL:     TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_FOSSIL); break;
                default:                 TL_DISPATCH_DEPTH_EDGE(TL_LIGHT_TY_EDGE_SCAN); break;
            }
        } else if(soft_depth_mode) {
            if(mode_radar)
                TL_DISPATCH_DEPTH_SOFT(TL_LIGHT_TY_SOFT_RADAR);
            else if(fog_mode)
                TL_DISPATCH_DEPTH_SOFT(TL_LIGHT_TY_SOFT_FOG);
            else
                TL_DISPATCH_DEPTH_SOFT(TL_LIGHT_TY_SOFT_LUT);
        } else {
            TL_DISPATCH_DEPTH_SIMPLE();
        }

        TL_OMP_FOR
        for(int gy = 0; gy < gh; gy++) {
            const int row0 = down_row0[gy];
            const int row1 = down_row1[gy];
            const int row2 = down_row2[gy];
            const int grow = gy * gw;

            for(int gx = 0; gx < gw; gx++) {
                const int x0 = down_x0[gx];
                const int x1 = down_x1[gx];
                const int x2 = down_x2[gx];
                const int c  = res_y[row1 + x1];
                const int p0 = res_y[row0 + x0];
                const int p1 = res_y[row0 + x2];
                const int p2 = res_y[row2 + x0];
                const int p3 = res_y[row2 + x2];
                const int m01 = (p0 > p1) ? p0 : p1;
                const int m23 = (p2 > p3) ? p2 : p3;
                const int mx = (m01 > m23) ? m01 : m23;
                const int avg = TL_DIV6((c << 1) + p0 + p1 + p2 + p3);
                const int v = (avg + mx + 1) >> 1;
                glow_next[grow + gx] = (uint8_t)((v > 255) ? 255 : v);
            }
        }

        TL_OMP_FOR
        for(int gy = 0; gy < gh; gy++) {
            const int row = gy * gw;
            const int x1 = (gw > 1) ? 1 : 0;
            const int x2 = (gw > 2) ? 2 : gw - 1;
            int sum = ((int)glow_next[row] * 3) + glow_next[row + x1] + glow_next[row + x2];
            for(int gx = 0; gx < gw; gx++) {
                glow_tmp[row + gx] = (uint8_t)TL_DIV5(sum);
                sum += (int)glow_next[row + blur_x_ap[gx]] - (int)glow_next[row + blur_x_rm[gx]];
            }
        }

        TL_OMP_FOR
        for(int gx = 0; gx < gw; gx++) {
            const int y1 = (gh > 1) ? gw : 0;
            const int y2 = (gh > 2) ? (gw << 1) : ((gh - 1) * gw);
            int sum = ((int)glow_tmp[gx] * 3) + glow_tmp[y1 + gx] + glow_tmp[y2 + gx];
            for(int gy = 0; gy < gh; gy++) {
                const int idx = gy * gw + gx;
                const int blurred = TL_DIV5(sum);
                glow[idx] = (uint8_t)((((int)glow[idx] * 3) + blurred + 2) >> 2);
                sum += (int)glow_tmp[blur_y_ap[gy] + gx] - (int)glow_tmp[blur_y_rm[gy] + gx];
            }
        }
    }

    if(!use_motion)
        t->prev_valid = 0;

    t->frame++;
}
