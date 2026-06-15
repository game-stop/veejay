/* 
 * EffecTV - Realtime Digital Video Effector
 * RadioacTV - motion-enlightment effect.
 * I referred to "DUNE!" by QuoVadis for this effect.
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * Veejay FX 'RadioActiveVJ'
 * (C) 2007 Niels Elburg
 *   This effect was ported from EffecTV.
 *   Differences:
 *    - difference frame over 2 frame interval intsead of bg substraction
 *    - several mask methods
 *    - more parameters
 *    - no palette (but mixing source)
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
#include <stdint.h>
#include "radioactive.h"

#define RADIOACTIVE_PARAMS 4

#define P_MODE      0
#define P_ZOOM      1
#define P_STRENGTH  2
#define P_THRESHOLD 3

typedef struct {
    uint8_t *block;
    uint8_t *diffbuf;
    uint8_t *blurzoombuf;
    int *zoom_x;
    int *zoom_y;
    int buf_width;
    int buf_height;
    int buf_area;
    int buf_margin_left;
    int first_frame;
    int last_mode;
    float ratio_;
    int n_threads;
} radioactive_t;

static inline int radioactive_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int radioactive_abs(int v)
{
    int mask = v >> (sizeof(int) * 8 - 1);
    return (v ^ mask) - mask;
}

static inline uint8_t radioactive_u8(int v)
{
    return (uint8_t)radioactive_clampi(v, 0, 255);
}

static inline uint8_t radioactive_blend_u8(uint8_t dst, uint8_t src, int a)
{
    const int d = (int)src - (int)dst;

    return radioactive_u8((int)dst + ((a * d) >> 8));
}

vj_effect *radioactivetv_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RADIOACTIVE_PARAMS;
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

    ve->limits[0][P_MODE] = 0;      ve->limits[1][P_MODE] = 6;      ve->defaults[P_MODE] = 0;
    ve->limits[0][P_ZOOM] = 50;     ve->limits[1][P_ZOOM] = 100;    ve->defaults[P_ZOOM] = 95;
    ve->limits[0][P_STRENGTH] = 0;  ve->limits[1][P_STRENGTH] = 255; ve->defaults[P_STRENGTH] = 200;
    ve->limits[0][P_THRESHOLD] = 0; ve->limits[1][P_THRESHOLD] = 255;ve->defaults[P_THRESHOLD] = 30;

    ve->description = "RadioActive EffecTV";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Zoom ratio",
        "Strength",
        "Difference Threshold"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Average",
        "Normal",
        "Strobe",
        "Spill (greyscale)",
        "Flood (greyscale)",
        "Frontal (greyscale)",
        "Low (greyscale)"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WARP,      VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 76, 99, 12, 46, 1000, 3600, 0, 72,
        VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                64,                 245,                16, 62,  700, 2800, 0,    88,
        VJ_BEAT_DETAIL,    VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,            8,                  96,                 12, 46, 1000, 3600, 0,    68
    );

    return ve;
}

static void radioactive_set_table(radioactive_t *r)
{
    const int w = r->buf_width;
    const int h = r->buf_height;
    const float ratio = r->ratio_;
    const float hw = 0.5f * (float)(w - 1);
    const float hh = 0.5f * (float)(h - 1);

    for(int x = 0; x < w; x++) {
        const int sx = (int)(0.5f + ratio * ((float)x - hw) + hw);
        r->zoom_x[x] = radioactive_clampi(sx, 0, w - 1);
    }

    for(int y = 0; y < h; y++) {
        const int sy = (int)(0.5f + ratio * ((float)y - hh) + hh);
        r->zoom_y[y] = radioactive_clampi(sy, 0, h - 1);
    }
}

void *radioactivetv_malloc(int w, int h)
{
    radioactive_t *r = (radioactive_t*)vj_calloc(sizeof(radioactive_t));

    if(!r)
        return NULL;

    r->ratio_ = 0.95f;
    r->last_mode = -1;
    r->first_frame = 1;

    r->buf_width = (w >= 32) ? ((w / 32) * 32) : w;
    r->buf_height = h;
    r->buf_area = r->buf_width * r->buf_height;
    r->buf_margin_left = (w - r->buf_width) >> 1;

    const size_t blur_bytes = (size_t)r->buf_area * 2u;
    const size_t diff_bytes = (size_t)w * (size_t)h;
    const size_t zoom_x_bytes = sizeof(int) * (size_t)r->buf_width;
    const size_t zoom_y_bytes = sizeof(int) * (size_t)r->buf_height;
    const size_t total = blur_bytes + diff_bytes + zoom_x_bytes + zoom_y_bytes + 32u;

    r->block = (uint8_t*)vj_calloc(total);

    if(!r->block) {
        free(r);
        return NULL;
    }

    uint8_t *p = r->block;

    r->blurzoombuf = p;
    p += blur_bytes;

    r->diffbuf = p;
    p += diff_bytes;

    p = (uint8_t*)(((uintptr_t)p + 15U) & ~(uintptr_t)15U);

    r->zoom_x = (int*)p;
    p += zoom_x_bytes;

    r->zoom_y = (int*)p;

    r->n_threads = vje_advise_num_threads(w * h);

    radioactive_set_table(r);

    return (void*)r;
}

void radioactivetv_free(void *ptr)
{
    radioactive_t *r = (radioactive_t*)ptr;

    free(r->block);
    free(r);
}

static void radioactive_blur(radioactive_t *r)
{
    const int width = r->buf_width;
    const int height = r->buf_height;

    uint8_t *restrict src = r->blurzoombuf;
    uint8_t *restrict dst = r->blurzoombuf + r->buf_area;

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int y = 0; y < height; y++) {
        const int ym = y > 0 ? y - 1 : y;
        const int yp = y < height - 1 ? y + 1 : y;

        const uint8_t *restrict top = src + ym * width;
        const uint8_t *restrict mid = src + y * width;
        const uint8_t *restrict bot = src + yp * width;
        uint8_t *restrict out = dst + y * width;

        for(int x = 0; x < width; x++) {
            const int xl = x > 0 ? x - 1 : x;
            const int xr = x < width - 1 ? x + 1 : x;
            const int v = (top[x] + mid[xl] + mid[xr] + bot[x]) >> 2;

            out[x] = (uint8_t)(v > 0 ? v - 1 : 0);
        }
    }
}

static void radioactive_zoom(radioactive_t *r)
{
    const int width = r->buf_width;
    const int height = r->buf_height;

    uint8_t *restrict base_in = r->blurzoombuf + r->buf_area;
    uint8_t *restrict base_out = r->blurzoombuf;

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int y = 0; y < height; y++) {
        const uint8_t *restrict src_row = base_in + r->zoom_y[y] * width;
        uint8_t *restrict dst_row = base_out + y * width;

        for(int x = 0; x < width; x++)
            dst_row[x] = src_row[r->zoom_x[x]];
    }
}

static inline void radioactive_inject_core(radioactive_t *r,
                                           uint8_t *restrict lum,
                                           uint8_t *restrict prev,
                                           int width,
                                           int threshold,
                                           int strength,
                                           int mode)
{
    const int buf_width = r->buf_width;
    const int buf_height = r->buf_height;
    const int margin = r->buf_margin_left;

    if(mode == 0 || mode == 3) {
#pragma omp parallel for schedule(static) num_threads(r->n_threads)
        for(int y = 0; y < buf_height; y++) {
            const int offset = y * width + margin;
            uint8_t *restrict l_ptr = lum + offset;
            uint8_t *restrict p_ptr = prev + offset;
            uint8_t *restrict b_ptr = r->blurzoombuf + y * buf_width;

            for(int x = 0; x < buf_width; x++) {
                const int d_val = (p_ptr[x] + (l_ptr[x] * 3)) >> 2;
                const int motion_val = d_val * (d_val >= threshold);
                const int inj = (motion_val * strength) >> 7;
                const uint8_t existing = b_ptr[x] - (b_ptr[x] != 0);

                b_ptr[x] = existing | radioactive_u8(inj);
            }
        }
    }
    else if(mode == 1 || mode == 4) {
#pragma omp parallel for schedule(static) num_threads(r->n_threads)
        for(int y = 0; y < buf_height; y++) {
            const int offset = y * width + margin;
            uint8_t *restrict l_ptr = lum + offset;
            uint8_t *restrict p_ptr = prev + offset;
            uint8_t *restrict b_ptr = r->blurzoombuf + y * buf_width;

            for(int x = 0; x < buf_width; x++) {
                const int diff = (int)l_ptr[x] - (int)p_ptr[x];
                const int d_val = diff > 0 ? (diff >> 1) : 0;
                const int motion_val = d_val * (d_val >= threshold);
                const int inj = (motion_val * strength) >> 7;
                const uint8_t existing = b_ptr[x] - (b_ptr[x] != 0);

                b_ptr[x] = existing | radioactive_u8(inj);
            }
        }
    }
    else if(mode == 6) {
#pragma omp parallel for schedule(static) num_threads(r->n_threads)
        for(int y = 0; y < buf_height; y++) {
            const int offset = y * width + margin;
            uint8_t *restrict l_ptr = lum + offset;
            uint8_t *restrict p_ptr = prev + offset;
            uint8_t *restrict b_ptr = r->blurzoombuf + y * buf_width;

            for(int x = 0; x < buf_width; x++) {
                const int delta = radioactive_abs((int)l_ptr[x] - (int)p_ptr[x]);
                const int d_val = delta > threshold ? (l_ptr[x] >> 2) : 0;
                const int motion_val = d_val * (d_val >= threshold);
                const int inj = (motion_val * strength) >> 7;
                const uint8_t existing = b_ptr[x] - (b_ptr[x] != 0);

                b_ptr[x] = existing | radioactive_u8(inj);
            }
        }
    }
    else {
#pragma omp parallel for schedule(static) num_threads(r->n_threads)
        for(int y = 0; y < buf_height; y++) {
            const int offset = y * width + margin;
            uint8_t *restrict l_ptr = lum + offset;
            uint8_t *restrict p_ptr = prev + offset;
            uint8_t *restrict b_ptr = r->blurzoombuf + y * buf_width;

            for(int x = 0; x < buf_width; x++) {
                const int d_val = radioactive_abs((int)l_ptr[x] - (int)p_ptr[x]);
                const int motion_val = d_val * (d_val >= threshold);
                const int inj = (motion_val * strength) >> 7;
                const uint8_t existing = b_ptr[x] - (b_ptr[x] != 0);

                b_ptr[x] = existing | radioactive_u8(inj);
            }
        }
    }
}

void radioactivetv_apply(void *ptr, VJFrame *frame, VJFrame *blue, int *args)
{
    radioactive_t *r = (radioactive_t*)ptr;

    const int width = frame->width;
    const int len = frame->len;

    const int mode = args[P_MODE];
    const int zoom_ratio = args[P_ZOOM];
    const int strength = args[P_STRENGTH];
    const int threshold = args[P_THRESHOLD];

    uint8_t *restrict lum = frame->data[0];
    uint8_t *restrict prev = r->diffbuf;

    const float snap_ratio = (float)zoom_ratio * 0.01f;

    if(r->ratio_ != snap_ratio) {
        r->ratio_ = snap_ratio;
        radioactive_set_table(r);
    }

    if(r->first_frame) {
        veejay_memcpy(prev, lum, len);
        r->first_frame = 0;
    }

    if(r->last_mode != mode || strength == 0) {
        veejay_memset(r->blurzoombuf, 0, (size_t)r->buf_area * 2u);
        r->last_mode = mode;
    }

    if(strength > 0)
        radioactive_inject_core(r, lum, prev, width, threshold, strength, mode);

    veejay_memcpy(prev, lum, len);

    radioactive_blur(r);
    radioactive_zoom(r);

    if(mode >= 3) {
#pragma omp parallel num_threads(r->n_threads)
        {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                frame->data[1][i] = 128;
                frame->data[2][i] = 128;
            }

#pragma omp for schedule(static)
            for(int y = 0; y < r->buf_height; y++) {
                uint8_t *restrict src_row = r->blurzoombuf + y * r->buf_width;
                uint8_t *restrict dst_row = lum + y * width + r->buf_margin_left;

                for(int x = 0; x < r->buf_width; x++)
                    dst_row[x] = src_row[x];
            }
        }
    }
    else {
#pragma omp parallel for schedule(static) num_threads(r->n_threads)
        for(int y = 0; y < r->buf_height; y++) {
            const int frame_offset = y * width + r->buf_margin_left;

            uint8_t *restrict mask_row = r->blurzoombuf + y * r->buf_width;

            uint8_t *restrict y_plane = frame->data[0] + frame_offset;
            uint8_t *restrict u_plane = frame->data[1] + frame_offset;
            uint8_t *restrict v_plane = frame->data[2] + frame_offset;

            uint8_t *restrict src_y = blue->data[0] + frame_offset;
            uint8_t *restrict src_u = blue->data[1] + frame_offset;
            uint8_t *restrict src_v = blue->data[2] + frame_offset;

            for(int x = 0; x < r->buf_width; x++) {
                const uint8_t a = mask_row[x];

                if(a > 0) {
                    y_plane[x] = radioactive_blend_u8(y_plane[x], src_y[x], a);
                    u_plane[x] = radioactive_blend_u8(u_plane[x], src_u[x], a);
                    v_plane[x] = radioactive_blend_u8(v_plane[x], src_v[x], a);
                }
            }
        }
    }
}
