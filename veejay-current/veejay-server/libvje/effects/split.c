/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "split.h"

#define SPLIT_PARAMS 6

#define P_MODE        0
#define P_SWITCH      1
#define P_SPLIT_POS   2
#define P_EDGE_GLOW   3
#define P_SLIDE_DRIVE 4
#define P_MIX_DRIVE   5

typedef struct {
    uint8_t *tmp[3];

    float pos_state;
    float glow_state;
    float slide_state;
    float mix_state;

    int frame;
    int state_ready;
    int n_threads;
} split_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t split_u8(int v)
{
    return (uint8_t) clampi(v, 0, 255);
}

static inline uint8_t split_mix_u8(uint8_t a, uint8_t b, int q8)
{
    q8 = clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}


static inline int split_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;
    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static inline int split_tri_centered(int phase)
{
    int p = phase & 1023;
    int t = (p < 512) ? p : (1023 - p);

    return t - 256;
}

vj_effect *split_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SPLIT_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults) free(ve->defaults);
        if(ve->limits[0]) free(ve->limits[0]);
        if(ve->limits[1]) free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->defaults[P_MODE]        = 8;
    ve->defaults[P_SWITCH]      = 1;
    ve->defaults[P_SPLIT_POS]   = 500;
    ve->defaults[P_EDGE_GLOW]   = 0;
    ve->defaults[P_SLIDE_DRIVE] = 0;
    ve->defaults[P_MIX_DRIVE]   = 0;

    ve->limits[0][P_MODE]        = 0;    ve->limits[1][P_MODE]        = 8;
    ve->limits[0][P_SWITCH]      = 0;    ve->limits[1][P_SWITCH]      = 1;
    ve->limits[0][P_SPLIT_POS]   = 0;    ve->limits[1][P_SPLIT_POS]   = 1000;
    ve->limits[0][P_EDGE_GLOW]   = 0;    ve->limits[1][P_EDGE_GLOW]   = 1000;
    ve->limits[0][P_SLIDE_DRIVE] = 0;    ve->limits[1][P_SLIDE_DRIVE] = 1000;
    ve->limits[0][P_MIX_DRIVE]   = 0;    ve->limits[1][P_MIX_DRIVE]   = 1000;

    ve->description = "Splitted Screens";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Switch",
        "Split Position",
        "Edge Glow",
        "Slide Drive",
        "Mix Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Right Half",
        "Right Mirror",
        "Left Mirror",
        "Upper Left",
        "Upper Right",
        "Lower Right",
        "Lower Left",
        "Dual Squeeze",
        "Upper Half"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_SWITCH],
        P_SWITCH,
        "Direct",
        "Fit Source"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 0, 1000, 82, 100, 0, 320, 0, 2, 0, VJ_BEAT_COST_CHEAP, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GLOW, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 86, 100, 4, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 88, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 92, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 98, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 1000, 90, 100, 6, 440, 24, 5, 0, VJ_BEAT_COST_CHEAP, 94, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    (void) width;
    (void) height;

    return ve;
}

void *split_malloc(int width, int height)
{
    split_t *s = (split_t*) vj_calloc(sizeof(split_t));
    if(!s)
        return NULL;

    const int len = width * height;

    s->tmp[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->tmp[0]) {
        free(s);
        return NULL;
    }

    s->tmp[1] = s->tmp[0] + len;
    s->tmp[2] = s->tmp[1] + len;

    s->pos_state = 500.0f;
    s->glow_state = 0.0f;
    s->slide_state = 0.0f;
    s->mix_state = 0.0f;
    s->frame = 0;
    s->state_ready = 0;

    s->n_threads = vje_advise_num_threads(len);

    return (void*) s;
}

void split_free(void *ptr)
{
    split_t *s = (split_t*) ptr;

    free(s->tmp[0]);
    free(s);
}

static void split_snapshot(split_t *s, VJFrame *frame)
{
    const int len = frame->len;
    const int uv_len = frame->uv_len;

    veejay_memcpy(s->tmp[0], frame->data[0], len);
    veejay_memcpy(s->tmp[1], frame->data[1], uv_len);
    veejay_memcpy(s->tmp[2], frame->data[2], uv_len);
}

static void split_copy_region_plane(uint8_t *restrict dst,
                                    const uint8_t *restrict src,
                                    int w,
                                    int h,
                                    int x0,
                                    int y0,
                                    int x1,
                                    int y1,
                                    int mirror_x,
                                    int fit_source,
                                    int n_threads)
{
    x0 = clampi(x0, 0, w);
    x1 = clampi(x1, 0, w);
    y0 = clampi(y0, 0, h);
    y1 = clampi(y1, 0, h);

    if(x1 <= x0 || y1 <= y0)
        return;

    const int rw = x1 - x0;
    const int rh = y1 - y0;

(void)n_threads;

#pragma omp for schedule(static)
    for(int y = y0; y < y1; y++) {
        const int dst_row = y * w;

        for(int x = x0; x < x1; x++) {
            int sx;
            int sy;

            if(fit_source) {
                sx = ((x - x0) * w) / rw;
                sy = ((y - y0) * h) / rh;
            } else {
                sx = x;
                sy = y;
            }

            if(mirror_x)
                sx = (w - 1) - sx;

            sx = clampi(sx, 0, w - 1);
            sy = clampi(sy, 0, h - 1);

            dst[dst_row + x] = src[sy * w + sx];
        }
    }
}

static void split_copy_region_xy(VJFrame *dst_frame,
                                 VJFrame *src_frame,
                                 int x0,
                                 int y0,
                                 int x1,
                                 int y1,
                                 int mirror_x,
                                 int fit_source,
                                 int n_threads)
{
    const int w = dst_frame->width;
    const int h = dst_frame->height;

    split_copy_region_plane(
        dst_frame->data[0],
        src_frame->data[0],
        w,
        h,
        x0,
        y0,
        x1,
        y1,
        mirror_x,
        fit_source,
        n_threads
    );

    const int uw = dst_frame->ssm ? w : dst_frame->uv_width;
    const int uh = dst_frame->ssm ? h : dst_frame->uv_height;

    const int ux0 = (x0 * uw + (w >> 1)) / w;
    const int uy0 = (y0 * uh + (h >> 1)) / h;
    const int ux1 = (x1 * uw + (w >> 1)) / w;
    const int uy1 = (y1 * uh + (h >> 1)) / h;

    split_copy_region_plane(
        dst_frame->data[1],
        src_frame->data[1],
        uw,
        uh,
        ux0,
        uy0,
        ux1,
        uy1,
        mirror_x,
        fit_source,
        n_threads
    );

    split_copy_region_plane(
        dst_frame->data[2],
        src_frame->data[2],
        uw,
        uh,
        ux0,
        uy0,
        ux1,
        uy1,
        mirror_x,
        fit_source,
        n_threads
    );
}

static void split_squeeze_plane(uint8_t *restrict dst,
                                const uint8_t *restrict src,
                                int w,
                                int h,
                                int x0,
                                int x1,
                                int n_threads)
{
    x0 = clampi(x0, 0, w);
    x1 = clampi(x1, 0, w);

    if(x1 <= x0)
        return;

    const int rw = x1 - x0;

(void)n_threads;

#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const int row = y * w;

        for(int x = x0; x < x1; x++) {
            const int sx = ((x - x0) * w) / rw;
            dst[row + x] = src[row + sx];
        }
    }
}



static void split_mix_frame2(VJFrame *frame, VJFrame *frame2, int q8, int n_threads)
{
    if(q8 <= 0)
        return;

    q8 = clampi(q8, 0, 256);

    const int len = frame->len;
    const int uv_len = frame->uv_len;

(void)n_threads;

#pragma omp for schedule(static)
    for(int i = 0; i < len; i++)
        frame->data[0][i] = split_mix_u8(frame->data[0][i], frame2->data[0][i], q8);

#pragma omp for schedule(static)
    for(int i = 0; i < uv_len; i++) {
        frame->data[1][i] = split_mix_u8(frame->data[1][i], frame2->data[1][i], q8);
        frame->data[2][i] = split_mix_u8(frame->data[2][i], frame2->data[2][i], q8);
    }
}

static void split_apply_edge_glow(VJFrame *frame,
                                  int split_x,
                                  int split_y,
                                  int use_x,
                                  int use_y,
                                  int glow,
                                  int n_threads)
{
    if(glow <= 0 || (!use_x && !use_y))
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int radius = 1 + ((glow * 31 + 500) / 1000);
    const int lift = (glow * 110 + 500) / 1000;

    uint8_t *restrict Y = frame->data[0];

(void)n_threads;

#pragma omp for schedule(static)
    for(int y = 0; y < h; y++) {
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            int dist = 999999;

            if(use_x) {
                int d = x - split_x;
                if(d < 0)
                    d = -d;
                if(d < dist)
                    dist = d;
            }

            if(use_y) {
                int d = y - split_y;
                if(d < 0)
                    d = -d;
                if(d < dist)
                    dist = d;
            }

            if(dist < radius) {
                const int add = (lift * (radius - dist) + (radius >> 1)) / radius;
                const int i = row + x;

                Y[i] = split_u8((int)Y[i] + add);
            }
        }
    }
}

void split_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    split_t *s = (split_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;

    const int mode = args[P_MODE];
    const int fit_source = args[P_SWITCH] ? 1 : 0;
    const int split_pos_arg = args[P_SPLIT_POS];
    const int edge_glow_arg = args[P_EDGE_GLOW];
    const int slide_drive_arg = args[P_SLIDE_DRIVE];
    const int mix_drive_arg = args[P_MIX_DRIVE];

    if(!s->state_ready) {
        s->pos_state = (float)split_pos_arg;
        s->glow_state = (float)edge_glow_arg;
        s->slide_state = (float)slide_drive_arg;
        s->mix_state = (float)mix_drive_arg;
        s->state_ready = 1;
    }

    const int slide_drive = split_smooth_i(&s->slide_state, slide_drive_arg, 0.18f, 0.070f);
    const int mix_drive = split_smooth_i(&s->mix_state, mix_drive_arg, 0.16f, 0.065f);
    const int base_glow = split_smooth_i(&s->glow_state, edge_glow_arg, 0.15f, 0.060f);

    const int tri = split_tri_centered(s->frame * 7);
    const int slide_offset = (tri * slide_drive) / 1024;

    int target_pos = split_pos_arg + slide_offset;
    target_pos = clampi(target_pos, 0, 1000);

    const int pos_q = clampi(split_smooth_i(&s->pos_state, target_pos, 0.16f, 0.080f), 0, 1000);
    const int split_x = clampi((w * pos_q + 500) / 1000, 1, w - 1);
    const int split_y = clampi((h * pos_q + 500) / 1000, 1, h - 1);

    const int glow = clampi(base_glow + ((slide_drive * 180 + 500) / 1000), 0, 1000);

    const int mix_q8 = clampi((mix_drive * 220 + 500) / 1000, 0, 256);

    int use_x_glow = 0;
    int use_y_glow = 0;

    switch(mode) {
        case 0:
        case 1:
        case 2:
        case 7:
            use_x_glow = 1;
            break;
        case 3:
        case 4:
        case 5:
        case 6:
            use_x_glow = 1;
            use_y_glow = 1;
            break;
        case 8:
            use_y_glow = 1;
            break;
        default:
            break;
    }

    if(mode == 7)
        split_snapshot(s, frame);

#pragma omp parallel num_threads(s->n_threads)
    {
        switch(mode) {
            case 0:
                split_copy_region_xy(frame, frame2, split_x, 0, w, h, 0, fit_source, s->n_threads);
                break;

            case 1:
                split_copy_region_xy(frame, frame2, split_x, 0, w, h, 1, fit_source, s->n_threads);
                break;

            case 2:
                split_copy_region_xy(frame, frame2, 0, 0, split_x, h, 1, fit_source, s->n_threads);
                break;

            case 3:
                split_copy_region_xy(frame, frame2, 0, 0, split_x, split_y, 0, fit_source, s->n_threads);
                break;

            case 4:
                split_copy_region_xy(frame, frame2, split_x, 0, w, split_y, 0, fit_source, s->n_threads);
                break;

            case 5:
                split_copy_region_xy(frame, frame2, split_x, split_y, w, h, 0, fit_source, s->n_threads);
                break;

            case 6:
                split_copy_region_xy(frame, frame2, 0, split_y, split_x, h, 0, fit_source, s->n_threads);
                break;

            case 7: {
                const int uw = frame->ssm ? w : frame->uv_width;
                const int uh = frame->ssm ? h : frame->uv_height;
                const int usplit = (split_x * uw + (w >> 1)) / w;

                split_squeeze_plane(frame->data[0], frame2->data[0], w, h, 0, split_x, s->n_threads);
                split_squeeze_plane(frame->data[0], s->tmp[0],       w, h, split_x, w, s->n_threads);
                split_squeeze_plane(frame->data[1], frame2->data[1], uw, uh, 0, usplit, s->n_threads);
                split_squeeze_plane(frame->data[2], frame2->data[2], uw, uh, 0, usplit, s->n_threads);
                split_squeeze_plane(frame->data[1], s->tmp[1], uw, uh, usplit, uw, s->n_threads);
                split_squeeze_plane(frame->data[2], s->tmp[2], uw, uh, usplit, uw, s->n_threads);
                break;
            }

            case 8:
                split_copy_region_xy(frame, frame2, 0, 0, w, split_y, 0, fit_source, s->n_threads);
                break;

            default:
                break;
        }

        split_mix_frame2(frame, frame2, mix_q8, s->n_threads);
        split_apply_edge_glow(frame, split_x, split_y, use_x_glow, use_y_glow, glow, s->n_threads);
    }

    s->frame++;
}
