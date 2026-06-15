/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include "revtv.h"

#define REVTV_PARAMS 9

#define P_LINE_SPACING    0
#define P_VERTICAL_SCALE  1
#define P_LUMA_INTENSITY  2
#define P_COLOR_RANGE     3
#define P_MIX             4
#define P_CHROMA_AMOUNT   5
#define P_LINES_DRIVE     6
#define P_SCALE_DRIVE     7
#define P_INTENSITY_DRIVE 8

typedef struct {
    uint8_t *src[3];
    int n_threads;
    int initialized;

    float sm_linespace;
    float sm_vscale;
    float sm_luma;
    float sm_mix;
    float sm_chroma;
    float sm_lines_drive;
    float sm_scale_drive;
    float sm_intensity_drive;
} revtv_t;

static inline int revtv_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint8_t revtv_mix_u8(uint8_t a, uint8_t b, int q)
{
    return (uint8_t)((((int)a * (1000 - q)) + ((int)b * q) + 500) / 1000);
}



static inline void revtv_smooth_lane(float *v, float target, float a)
{
    *v += (target - *v) * a;
}

vj_effect *revtv_init(int max_width, int max_height)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = REVTV_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    ve->defaults[P_LINE_SPACING] = 2;
    ve->defaults[P_VERTICAL_SCALE] = 42;
    ve->defaults[P_LUMA_INTENSITY] = 201;
    ve->defaults[P_COLOR_RANGE] = 6;
    ve->defaults[P_MIX] = 1000;
    ve->defaults[P_CHROMA_AMOUNT] = 1000;
    ve->defaults[P_LINES_DRIVE] = 0;
    ve->defaults[P_SCALE_DRIVE] = 0;
    ve->defaults[P_INTENSITY_DRIVE] = 0;

    ve->limits[0][P_LINE_SPACING] = 1;    ve->limits[1][P_LINE_SPACING] = max_height;
    ve->limits[0][P_VERTICAL_SCALE] = 1;  ve->limits[1][P_VERTICAL_SCALE] = max_width;
    ve->limits[0][P_LUMA_INTENSITY] = 0;  ve->limits[1][P_LUMA_INTENSITY] = 255;
    ve->limits[0][P_COLOR_RANGE] = 0;     ve->limits[1][P_COLOR_RANGE] = 7;
    ve->limits[0][P_MIX] = 0;             ve->limits[1][P_MIX] = 1000;
    ve->limits[0][P_CHROMA_AMOUNT] = 0;   ve->limits[1][P_CHROMA_AMOUNT] = 1000;
    ve->limits[0][P_LINES_DRIVE] = 0;     ve->limits[1][P_LINES_DRIVE] = 1000;
    ve->limits[0][P_SCALE_DRIVE] = 0;     ve->limits[1][P_SCALE_DRIVE] = 1000;
    ve->limits[0][P_INTENSITY_DRIVE] = 0; ve->limits[1][P_INTENSITY_DRIVE] = 1000;

    ve->description = "RevTV (EffectTV)";
    ve->sub_format = 0;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Line spacing",
        "Vertical scale",
        "Luminance intensity",
        "Color range",
        "Mix",
        "Chroma Amount",
        "Lines Drive",
        "Scale Drive",
        "Intensity Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GRID_SIZE,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 1,                  24,                 16, 62,  700, 2800, 0,    88,
        VJ_BEAT_WARP,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,                       8,                  160,                16, 62,  700, 2800, 0,    86,
        VJ_BEAT_INTENSITY,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                             96,                 255,                14, 54,  800, 3000, 0,    78,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                                    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                             420,                1000,               12, 46, 1000, 3600, 0,    68,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                             320,                1000,               12, 46, 1000, 3600, 0,    64,
        VJ_BEAT_GRID_SIZE,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,                        120,                1000,               16, 62,  700, 2800, 0,    92,
        VJ_BEAT_WARP,             VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,                        120,                1000,               16, 62,  700, 2800, 0,    88,
        VJ_BEAT_INTENSITY,        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                             140,                1000,               16, 62,  700, 2800, 0,    90
    );
    return ve;
}

void *revtv_malloc(int w, int h)
{
    revtv_t *r = (revtv_t*)vj_calloc(sizeof(revtv_t));

    if(!r)
        return NULL;

    const int len = w * h;

    r->src[0] = (uint8_t*)vj_malloc((size_t)len * 3u);

    if(!r->src[0]) {
        free(r);
        return NULL;
    }

    r->src[1] = r->src[0] + len;
    r->src[2] = r->src[1] + len;
    r->n_threads = vje_advise_num_threads(len);

    return (void*)r;
}

void revtv_free(void *ptr)
{
    revtv_t *r = (revtv_t*)ptr;

    free(r->src[0]);
    free(r);
}

static void revtv_luma(uint8_t *restrict Y,
                       int width,
                       int height,
                       int linespace,
                       int vscale,
                       uint8_t color_y)
{
    for(int y = 0; y < height; y += linespace) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int yval = y - ((int)Y[row + x] / vscale);

            if(yval >= 0)
                Y[yval * width + x] = color_y;
        }
    }
}

static void revtv_chroma(uint8_t *restrict C,
                         int width,
                         int height,
                         int linespace,
                         int vscale,
                         uint8_t color_c)
{
    for(int y = 0; y < height; y += linespace) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            int chroma_delta = (int)C[row + x] - 128;

            if(chroma_delta < 0)
                chroma_delta = -chroma_delta;

            const int yval = y - (chroma_delta / vscale);

            if(yval >= 0)
                C[yval * width + x] = color_c;
        }
    }
}

static void revtv_blend_plane(uint8_t *restrict dst,
                              const uint8_t *restrict src,
                              int len,
                              int q,
                              int n_threads)
{
    if(q >= 1000)
        return;

    if(q <= 0) {
        veejay_memcpy(dst, src, len);
        return;
    }

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        dst[i] = revtv_mix_u8(src[i], dst[i], q);
}

void revtv_apply(void *ptr, VJFrame *frame, int *args)
{
    revtv_t *r = (revtv_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int uv_width = frame->ssm ? width : frame->uv_width;
    const int uv_height = frame->ssm ? height : frame->uv_height;
    const int uv_len = frame->ssm ? len : frame->uv_len;

    int linespace_arg = args[P_LINE_SPACING];
    int vscale_arg = args[P_VERTICAL_SCALE];
    int color_y_arg = args[P_LUMA_INTENSITY];
    const int color_num = args[P_COLOR_RANGE];
    int mix_arg = args[P_MIX];
    int chroma_arg = args[P_CHROMA_AMOUNT];
    int lines_drive = args[P_LINES_DRIVE];
    int scale_drive = args[P_SCALE_DRIVE];
    int intensity_drive = args[P_INTENSITY_DRIVE];

    const float lane_a = 0.28f;

    if(!r->initialized) {
        r->sm_linespace = (float)linespace_arg;
        r->sm_vscale = (float)vscale_arg;
        r->sm_luma = (float)color_y_arg;
        r->sm_mix = (float)mix_arg;
        r->sm_chroma = (float)chroma_arg;
        r->sm_lines_drive = (float)lines_drive;
        r->sm_scale_drive = (float)scale_drive;
        r->sm_intensity_drive = (float)intensity_drive;
        r->initialized = 1;
    }
    else {
        revtv_smooth_lane(&r->sm_linespace, (float)linespace_arg, lane_a);
        revtv_smooth_lane(&r->sm_vscale, (float)vscale_arg, lane_a);
        revtv_smooth_lane(&r->sm_luma, (float)color_y_arg, lane_a);
        revtv_smooth_lane(&r->sm_mix, (float)mix_arg, lane_a);
        revtv_smooth_lane(&r->sm_chroma, (float)chroma_arg, lane_a);
        revtv_smooth_lane(&r->sm_lines_drive, (float)lines_drive, lane_a);
        revtv_smooth_lane(&r->sm_scale_drive, (float)scale_drive, lane_a);
        revtv_smooth_lane(&r->sm_intensity_drive, (float)intensity_drive, lane_a);
    }

    const int line_q = revtv_clampi((int)(r->sm_lines_drive + 0.5f), 0, 1000);
    const int scale_q = revtv_clampi((int)(r->sm_scale_drive + 0.5f), 0, 1000);
    const int inten_q = revtv_clampi((int)(r->sm_intensity_drive + 0.5f), 0, 1000);

    int linespace = revtv_clampi((int)(r->sm_linespace + 0.5f), 1, height);
    int vscale = revtv_clampi((int)(r->sm_vscale + 0.5f), 1, width);
    int color_y = revtv_clampi((int)(r->sm_luma + 0.5f), 0, 255);
    const int mix_q = revtv_clampi((int)(r->sm_mix + 0.5f), 0, 1000);
    const int chroma_q = revtv_clampi((int)(r->sm_chroma + 0.5f), 0, 1000);

    if(linespace > 1)
        linespace -= ((linespace - 1) * line_q + 500) / 1000;

    if(linespace < 1)
        linespace = 1;

    if(vscale > 1) {
        const int pull = ((vscale - 1) * scale_q * 82 + 50000) / 100000;

        vscale -= pull;

        if(vscale < 1)
            vscale = 1;
    }

    color_y += ((255 - color_y) * inten_q + 500) / 1000;
    color_y = revtv_clampi(color_y, 0, 255);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    veejay_memcpy(r->src[0], Y, len);
    veejay_memcpy(r->src[1], Cb, uv_len);
    veejay_memcpy(r->src[2], Cr, uv_len);

    const uint8_t color_cb = (uint8_t)bl_pix_get_color_cb(color_num);
    const uint8_t color_cr = (uint8_t)bl_pix_get_color_cr(color_num);

    revtv_luma(Y, width, height, linespace, vscale, (uint8_t)color_y);

    if(color_num > 0 && chroma_q > 0) {
        int uv_linespace = linespace >> frame->shift_v;
        int uv_vscale = vscale >> frame->shift_v;

        if(uv_linespace < 1)
            uv_linespace = 1;
        if(uv_vscale < 1)
            uv_vscale = 1;

        revtv_chroma(Cb, uv_width, uv_height, uv_linespace, uv_vscale, color_cb);
        revtv_chroma(Cr, uv_width, uv_height, uv_linespace, uv_vscale, color_cr);
    }

    revtv_blend_plane(Y, r->src[0], len, mix_q, r->n_threads);

    const int chroma_mix_q = (mix_q * chroma_q + 500) / 1000;

    revtv_blend_plane(Cb, r->src[1], uv_len, chroma_mix_q, r->n_threads);
    revtv_blend_plane(Cr, r->src[2], uv_len, chroma_mix_q, r->n_threads);
}
