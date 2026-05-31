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
#include <libveejay/vj-viewport.h>
#include "zoom.h"

#define ZOOM_PARAMS 8

#define P_CENTER_X      0
#define P_CENTER_Y      1
#define P_FACTOR        2
#define P_MODE          3
#define P_UPDATE_ALPHA  4
#define P_ZOOM_PUNCH    5
#define P_BEAT_PUSH     6
#define P_BEAT_SMOOTH   7

typedef struct {
    int zoom_[4];
    void *zoom_vp_;
    uint8_t *zoom_private_[4];
    size_t plane_size;
    int w;
    int h;
    float beat_env;
    float factor_env;
} zoom_t;

static inline int zoom_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int zoom_beat_shape(int beat_push)
{
    beat_push = zoom_clampi(beat_push, 0, 1000);

    const int sq = (beat_push * beat_push + 500) / 1000;
    return zoom_clampi((beat_push * 35 + sq * 65 + 50) / 100, 0, 1000);
}

static inline int zoom_lerp_i(int a, int b, int q)
{
    q = zoom_clampi(q, 0, 1000);
    return a + (((b - a) * q + ((b >= a) ? 500 : -500)) / 1000);
}

vj_effect *zoom_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = ZOOM_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    const int max_x = width  > 0 ? width  - 1 : 0;
    const int max_y = height > 0 ? height - 1 : 0;

    ve->defaults[P_CENTER_X]     = width  > 0 ? width  / 2 : 0;
    ve->defaults[P_CENTER_Y]     = height > 0 ? height / 2 : 0;
    ve->defaults[P_FACTOR]       = 50;
    ve->defaults[P_MODE]         = 1;
    ve->defaults[P_UPDATE_ALPHA] = 0;
    ve->defaults[P_ZOOM_PUNCH]   = 420;
    ve->defaults[P_BEAT_PUSH]    = 0;
    ve->defaults[P_BEAT_SMOOTH]  = 560;

    ve->limits[0][P_CENTER_X]     = 0;  ve->limits[1][P_CENTER_X]     = max_x;
    ve->limits[0][P_CENTER_Y]     = 0;  ve->limits[1][P_CENTER_Y]     = max_y;
    ve->limits[0][P_FACTOR]       = 10; ve->limits[1][P_FACTOR]       = 100;
    ve->limits[0][P_MODE]         = 0;  ve->limits[1][P_MODE]         = 1;
    ve->limits[0][P_UPDATE_ALPHA] = 0;  ve->limits[1][P_UPDATE_ALPHA] = 1;
    ve->limits[0][P_ZOOM_PUNCH]   = 0;  ve->limits[1][P_ZOOM_PUNCH]   = 1000;
    ve->limits[0][P_BEAT_PUSH]    = 0;  ve->limits[1][P_BEAT_PUSH]    = 1000;
    ve->limits[0][P_BEAT_SMOOTH]  = 0;  ve->limits[1][P_BEAT_SMOOTH]  = 1000;

    ve->description = "Zoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Center X",
        "Center Y",
        "Factor",
        "Mode",
        "Update Alpha",
        "Zoom Punch",
        "Beat Push",
        "Beat Smooth"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Forward",
        "Reverse"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_UPDATE_ALPHA],
        P_UPDATE_ALPHA,
        "Ignore Alpha",
        "Update Alpha"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_DRIFT,
        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE,
        max_x / 4, (max_x * 3) / 4,
        6, 20, 1800, 4200, 900, 22, /* Center X */

        VJ_BEAT_DRIFT,
        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE,
        max_y / 4, (max_y * 3) / 4,
        6, 20, 1800, 4200, 900, 22, /* Center Y */

        VJ_BEAT_WINDOW_RADIUS,
        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE,
        18, 86,
        6, 22, 1800, 4200, 900, 28, /* Factor */

        VJ_BEAT_SELECTOR,
        VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* Mode */

        VJ_BEAT_SELECTOR,
        VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* Update Alpha */

        VJ_BEAT_WINDOW_RADIUS,
        VJ_BEAT_F_REJECT,
        VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET,
        0, 0, 0, 0, 0, -1000, /* Zoom Punch */

        VJ_BEAT_KICK,
        VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,
        0, 720,
        18, 68, 90, 820, 0, 100, /* Beat Push */

        VJ_BEAT_MEMORY,
        VJ_BEAT_F_PHRASE_ONLY,
        260, 860,
        5, 18, 2200, 5200, 1200, 18 /* Beat Smooth */
    );

    return ve;
}

void *zoom_malloc(int width, int height)
{
    if(width <= 0 || height <= 0)
        return NULL;

    zoom_t *z = (zoom_t*) vj_calloc(sizeof(zoom_t));
    if(!z)
        return NULL;

    z->plane_size = (size_t)(width * height + width);

    z->zoom_private_[0] = (uint8_t*) vj_malloc(z->plane_size * 4u);
    if(!z->zoom_private_[0]) {
        free(z);
        return NULL;
    }

    z->zoom_private_[1] = z->zoom_private_[0] + z->plane_size;
    z->zoom_private_[2] = z->zoom_private_[1] + z->plane_size;
    z->zoom_private_[3] = z->zoom_private_[2] + z->plane_size;

    z->zoom_[0] = -1;
    z->zoom_[1] = -1;
    z->zoom_[2] = -1;
    z->zoom_[3] = -1;

    z->zoom_vp_ = NULL;
    z->w = width;
    z->h = height;
    z->beat_env = 0.0f;
    z->factor_env = 0.0f;

    return (void*) z;
}

void zoom_free(void *ptr)
{
    zoom_t *z = (zoom_t*) ptr;

    if(!z)
        return;

    if(z->zoom_private_[0])
        free(z->zoom_private_[0]);

    if(z->zoom_vp_)
        viewport_destroy(z->zoom_vp_);

    free(z);
}

void zoom_apply(void *ptr, VJFrame *frame, int *args)
{
    zoom_t *z = (zoom_t*) ptr;

    if(!z || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    if(width != z->w || height != z->h)
        return;

    const int x = zoom_clampi(args[P_CENTER_X], 0, width - 1);
    const int y = zoom_clampi(args[P_CENTER_Y], 0, height - 1);
    const int factor_base = zoom_clampi(args[P_FACTOR], 10, 100);
    const int dir = args[P_MODE] ? 1 : 0;
    const int alpha = args[P_UPDATE_ALPHA] ? 1 : 0;
    const int use_alpha = alpha && frame->data[3];
    const int punch = zoom_clampi(args[P_ZOOM_PUNCH], 0, 1000);
    const int beat_push = zoom_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int smooth = zoom_clampi(args[P_BEAT_SMOOTH], 0, 1000);

    const int beat_shaped = zoom_beat_shape(beat_push);
    const float beat_target = (float)beat_shaped * 0.001f;
    const float smooth_f = (float)smooth * 0.001f;
    const float attack = 0.18f + (1.0f - smooth_f) * 0.32f;
    const float release = 0.035f + (1.0f - smooth_f) * 0.080f;

    if(beat_target > z->beat_env)
        z->beat_env += (beat_target - z->beat_env) * attack;
    else
        z->beat_env += (beat_target - z->beat_env) * release;

    if(z->beat_env < 0.0001f)
        z->beat_env = 0.0f;
    else if(z->beat_env > 1.0f)
        z->beat_env = 1.0f;

    const int beat_q = zoom_clampi((int)(z->beat_env * z->beat_env * 1000.0f + 0.5f), 0, 1000);

    int target_factor = factor_base;
    if(beat_q > 0 && punch > 0) {
        const int headroom = 100 - factor_base;
        const int lift = (int)(((int64_t)headroom * (int64_t)punch * (int64_t)beat_q + 500000LL) / 1000000LL);
        target_factor = zoom_clampi(factor_base + lift, 10, 100);
    }

    const int smooth_q = 180 + ((1000 - smooth) * 240 + 500) / 1000;
    const int factor_env_i = zoom_clampi((int)(z->factor_env + 0.5f), 10, 100);
    const int next_factor_i = zoom_lerp_i(factor_env_i, target_factor, smooth_q);

    z->factor_env += ((float)next_factor_i - z->factor_env) * 0.72f;

    int factor = zoom_clampi((int)(z->factor_env + 0.5f), 10, 100);

    if(z->zoom_[0] != x ||
       z->zoom_[1] != y ||
       z->zoom_[2] != factor ||
       z->zoom_[3] != dir ||
       !z->zoom_vp_)
    {
        if(z->zoom_vp_) {
            viewport_destroy(z->zoom_vp_);
            z->zoom_vp_ = NULL;
        }

        z->zoom_vp_ = viewport_fx_zoom_init(
            VP_QUADZOOM,
            width,
            height,
            x,
            y,
            factor,
            dir
        );

        if(!z->zoom_vp_)
            return;

        z->zoom_[0] = x;
        z->zoom_[1] = y;
        z->zoom_[2] = factor;
        z->zoom_[3] = dir;
    }

    int strides[4] = {
        len,
        len,
        len,
        use_alpha ? len : 0
    };

    vj_frame_copy(frame->data, z->zoom_private_, strides);

    if(use_alpha)
        viewport_process_dynamic_alpha(z->zoom_vp_, z->zoom_private_, frame->data);
    else
        viewport_process_dynamic(z->zoom_vp_, z->zoom_private_, frame->data);
}
