/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2005 Niels Elburg <nwelburg@gmail.com>
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

#include <libvje/effects/common.h>
#include <veejaycore/vjmem.h>
#include "transop.h"

#include <math.h>
#include <stdint.h>

#define TRANSOP_PARAMS 8

#define P_WIDTH       0
#define P_HEIGHT      1
#define P_SRC_Y       2
#define P_SRC_X       3
#define P_DST_Y       4
#define P_DST_X       5
#define P_SLIDE_DRIVE 6
#define P_SIZE_DRIVE  7

#define TRANSOP_TWO_PI 6.28318530718f

typedef struct {
    float rect_w;
    float rect_h;
    float src_y;
    float src_x;
    float dst_y;
    float dst_x;
    float slide_drive;
    float size_drive;
    float phase;

    int initialized;
    int n_threads;
} transop_t;

static inline int transop_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float transop_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int transop_round_to_i(float v)
{
    return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}



static inline float transop_smooth(float oldv, float target, float coef)
{
    return oldv + (target - oldv) * coef;
}

vj_effect *transop_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRANSOP_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    int def_w = width / 3;
    int def_h = height / 3;

    ve->defaults[P_WIDTH]       = def_w;
    ve->defaults[P_HEIGHT]      = def_h;
    ve->defaults[P_SRC_Y]       = height / 4;
    ve->defaults[P_SRC_X]       = width / 4;
    ve->defaults[P_DST_Y]       = height / 3;
    ve->defaults[P_DST_X]       = width / 3;
    ve->defaults[P_SLIDE_DRIVE] = 0;
    ve->defaults[P_SIZE_DRIVE]  = 0;

    const int max_w = width;
    const int max_h = height;

    ve->limits[0][P_WIDTH]       = 0;    ve->limits[1][P_WIDTH]       = max_w;
    ve->limits[0][P_HEIGHT]      = 0;    ve->limits[1][P_HEIGHT]      = max_h;
    ve->limits[0][P_SRC_Y]       = 0;    ve->limits[1][P_SRC_Y]       = max_h;
    ve->limits[0][P_SRC_X]       = 0;    ve->limits[1][P_SRC_X]       = max_w;
    ve->limits[0][P_DST_Y]       = 0;    ve->limits[1][P_DST_Y]       = max_h;
    ve->limits[0][P_DST_X]       = 0;    ve->limits[1][P_DST_X]       = max_w;
    ve->limits[0][P_SLIDE_DRIVE] = 0;    ve->limits[1][P_SLIDE_DRIVE] = 1000;
    ve->limits[0][P_SIZE_DRIVE]  = 0;    ve->limits[1][P_SIZE_DRIVE]  = 1000;

    ve->description = "Frame Translate";
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Width",
        "Height",
        "Source Y",
        "Source X",
        "Dest Y",
        "Dest X",
        "Slide Drive",
        "Size Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 1,   max_w, 12, 46, 900, 3200, 0, 68,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 1,   max_h, 12, 46, 900, 3200, 0, 68,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE,                         0,   max_h, 10, 38, 1000,3600, 0, 44,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE,                         0,   max_w, 10, 38, 1000,3600, 0, 44,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE,                         0,   max_h, 10, 38, 1000,3600, 0, 44,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE,                         0,   max_w, 10, 38, 1000,3600, 0, 44,
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     120, 1000,  16, 62, 700, 2800, 0, 92,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                     120, 1000,  16, 62, 800, 3200, 0, 86
    );

    return ve;
}

void *transop_malloc(int w, int h)
{
    transop_t *t = (transop_t*) vj_calloc(sizeof(transop_t));
    if(!t)
        return NULL;

    t->n_threads = vje_advise_num_threads(w * h);

    return (void*) t;
}

void transop_free(void *ptr)
{
    free(ptr);
}



static void transop_copy_rect(VJFrame *frame,
                              VJFrame *frame2,
                              int rect_w,
                              int rect_h,
                              int sy,
                              int sx,
                              int dy,
                              int dx,
                              int n_threads)
{
    const int width = frame->width;

    uint8_t *restrict dY  = frame->data[0];
    uint8_t *restrict dCb = frame->data[1];
    uint8_t *restrict dCr = frame->data[2];

    const uint8_t *restrict sY  = frame2->data[0];
    const uint8_t *restrict sCb = frame2->data[1];
    const uint8_t *restrict sCr = frame2->data[2];

#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int y = 0; y < rect_h; y++) {
        const int src = (sy + y) * width + sx;
        const int dst = (dy + y) * width + dx;

        veejay_memcpy(dY  + dst, sY  + src, rect_w);
        veejay_memcpy(dCb + dst, sCb + src, rect_w);
        veejay_memcpy(dCr + dst, sCr + src, rect_w);
    }
}

void transop_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    transop_t *t = (transop_t*) ptr;

    const int width  = frame->width;
    const int height = frame->height;

    const int rect_w_arg = args[P_WIDTH];
    const int rect_h_arg = args[P_HEIGHT];
    const int sy_arg = args[P_SRC_Y];
    const int sx_arg = args[P_SRC_X];
    const int dy_arg = args[P_DST_Y];
    const int dx_arg = args[P_DST_X];
    const int slide_drive_arg = args[P_SLIDE_DRIVE];
    const int size_drive_arg = args[P_SIZE_DRIVE];

    if(!t->initialized) {
        t->rect_w = (float)rect_w_arg;
        t->rect_h = (float)rect_h_arg;
        t->src_y = (float)sy_arg;
        t->src_x = (float)sx_arg;
        t->dst_y = (float)dy_arg;
        t->dst_x = (float)dx_arg;
        t->slide_drive = (float)slide_drive_arg;
        t->size_drive = (float)size_drive_arg;
        t->phase = 0.0f;
        t->initialized = 1;
    } else {
        const float user_coef = 0.24f;
        const float drive_coef = 0.28f;

        t->rect_w = transop_smooth(t->rect_w, (float)rect_w_arg, user_coef);
        t->rect_h = transop_smooth(t->rect_h, (float)rect_h_arg, user_coef);
        t->src_y = transop_smooth(t->src_y, (float)sy_arg, user_coef);
        t->src_x = transop_smooth(t->src_x, (float)sx_arg, user_coef);
        t->dst_y = transop_smooth(t->dst_y, (float)dy_arg, user_coef);
        t->dst_x = transop_smooth(t->dst_x, (float)dx_arg, user_coef);
        t->slide_drive = transop_smooth(t->slide_drive, (float)slide_drive_arg, drive_coef);
        t->size_drive = transop_smooth(t->size_drive, (float)size_drive_arg, drive_coef * 0.82f);
    }

    const float slide_t = transop_clampf(t->slide_drive * 0.001f, 0.0f, 1.0f);
    const float size_t = transop_clampf(t->size_drive * 0.001f, 0.0f, 1.0f);

    t->phase += 0.010f + slide_t * 0.105f;
    if(t->phase >= TRANSOP_TWO_PI)
        t->phase -= TRANSOP_TWO_PI;

    const float s0 = sinf(t->phase);
    const float s1 = sinf(t->phase * 0.73f + 1.91f);
    const float c0 = cosf(t->phase * 0.61f + 0.37f);

    const float slide_amp_x = (float)width  * 0.25f * slide_t;
    const float slide_amp_y = (float)height * 0.25f * slide_t;
    const float size_add_w = (float)width  * 0.42f * size_t;
    const float size_add_h = (float)height * 0.42f * size_t;

    int rect_w = transop_clampi(transop_round_to_i(t->rect_w + size_add_w * (0.35f + 0.65f * fabsf(s1))), 0, width);
    int rect_h = transop_clampi(transop_round_to_i(t->rect_h + size_add_h * (0.35f + 0.65f * fabsf(c0))), 0, height);

    int sx = transop_clampi(transop_round_to_i(t->src_x - slide_amp_x * s0 * 0.42f), 0, width);
    int sy = transop_clampi(transop_round_to_i(t->src_y - slide_amp_y * c0 * 0.42f), 0, height);
    int dx = transop_clampi(transop_round_to_i(t->dst_x + slide_amp_x * s0), 0, width);
    int dy = transop_clampi(transop_round_to_i(t->dst_y + slide_amp_y * s1), 0, height);

    if(rect_w <= 0 || rect_h <= 0)
        return;

    if(sx >= width || sy >= height || dx >= width || dy >= height)
        return;

    if(sx + rect_w > width)
        rect_w = width - sx;
    if(dx + rect_w > width)
        rect_w = width - dx;

    if(sy + rect_h > height)
        rect_h = height - sy;
    if(dy + rect_h > height)
        rect_h = height - dy;

    if(rect_w <= 0 || rect_h <= 0)
        return;

    transop_copy_rect(frame, frame2, rect_w, rect_h, sy, sx, dy, dx, t->n_threads);
}
