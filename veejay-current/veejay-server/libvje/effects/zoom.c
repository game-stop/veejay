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

#define ZOOM_PARAMS 6

#define P_CENTER_X      0
#define P_CENTER_Y      1
#define P_FACTOR        2
#define P_MODE          3
#define P_UPDATE_ALPHA  4
#define P_ZOOM_PUNCH    5

typedef struct {
    int zoom_[4];
    void *zoom_vp_;
    uint8_t *zoom_private_[4];
    size_t plane_size;
    int w;
    int h;
    int env_ready;
    float center_x_env;
    float center_y_env;
    float factor_env;
} zoom_t;

static inline int zoom_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
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
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    const int max_x = width - 1;
    const int max_y = height - 1;

    ve->defaults[P_CENTER_X]     = width / 2;
    ve->defaults[P_CENTER_Y]     = height / 2;
    ve->defaults[P_FACTOR]       = 50;
    ve->defaults[P_MODE]         = 1;
    ve->defaults[P_UPDATE_ALPHA] = 0;
    ve->defaults[P_ZOOM_PUNCH]   = 420;

    ve->limits[0][P_CENTER_X]     = 0;  ve->limits[1][P_CENTER_X]     = max_x;
    ve->limits[0][P_CENTER_Y]     = 0;  ve->limits[1][P_CENTER_Y]     = max_y;
    ve->limits[0][P_FACTOR]       = 10; ve->limits[1][P_FACTOR]       = 100;
    ve->limits[0][P_MODE]         = 0;  ve->limits[1][P_MODE]         = 1;
    ve->limits[0][P_UPDATE_ALPHA] = 0;  ve->limits[1][P_UPDATE_ALPHA] = 1;
    ve->limits[0][P_ZOOM_PUNCH]   = 0;  ve->limits[1][P_ZOOM_PUNCH]   = 1000;

    ve->description = "Zoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Center X",
        "Center Y",
        "Factor",
        "Mode",
        "Update Alpha",
        "Zoom Punch"
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
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE,                          0,                  max_x,              14, 58, 420, 1900, 0,  48,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE,                          0,                  max_y,              14, 58, 420, 1900, 0,  48,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, 10,                 100,                20, 76, 260, 1250, 0,  78,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,   0,    0,  -1000,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,   0,   0,    0,  -1000,
        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                          120,                1000,               22, 86, 220, 1450, 0,  94
    );

    return ve;
}

void *zoom_malloc(int width, int height)
{
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
    z->env_ready = 0;
    z->center_x_env = (float)(width >> 1);
    z->center_y_env = (float)(height >> 1);
    z->factor_env = 50.0f;

    return (void*) z;
}

void zoom_free(void *ptr)
{
    zoom_t *z = (zoom_t*) ptr;

    free(z->zoom_private_[0]);

    if(z->zoom_vp_)
        viewport_destroy(z->zoom_vp_);

    free(z);
}

void zoom_apply(void *ptr, VJFrame *frame, int *args)
{
    zoom_t *z = (zoom_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int target_x = args[P_CENTER_X];
    const int target_y = args[P_CENTER_Y];
    const int factor_base = args[P_FACTOR];
    const int dir = args[P_MODE] ? 1 : 0;
    const int alpha = args[P_UPDATE_ALPHA] ? 1 : 0;
    const int use_alpha = alpha && frame->data[3];
    const int punch = args[P_ZOOM_PUNCH];

    if(!z->env_ready) {
        z->center_x_env = (float)target_x;
        z->center_y_env = (float)target_y;
        z->factor_env = (float)factor_base;
        z->env_ready = 1;
    }

    int target_factor = factor_base;
    if(punch > 0) {
        const int headroom = 100 - factor_base;
        const int lift = (headroom > 0)
            ? (int)(((int64_t)headroom * (int64_t)punch + 500LL) / 1000LL)
            : 0;
        target_factor = factor_base + lift;
    }

    z->center_x_env += ((float)target_x - z->center_x_env) * 0.092f;
    z->center_y_env += ((float)target_y - z->center_y_env) * 0.092f;
    z->factor_env += ((float)target_factor - z->factor_env) * 0.285f;

    const int x = zoom_clampi((int)(z->center_x_env + 0.5f), 0, width - 1);
    const int y = zoom_clampi((int)(z->center_y_env + 0.5f), 0, height - 1);
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
