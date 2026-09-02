/* 
 * Linux VeeJay
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * ChameleonTV - Vanishing into the wall!!
 * Copyright (C) 2003 FUKUCHI Kentaro
 *
 * Ported to veejay by Niels Elburg 
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
#include "softblur.h"
#include "chameleon.h"
#include "motionmap.h"

#define CHAMELEON_MAX_PLANES 64

typedef struct {
    int last_mode_;
    int N__;
    int n__;
    void *motionmap;
    int has_bg;
    int32_t *sum;
    uint8_t *timebuffer;
    uint8_t *tmpimage[4];
    int plane;
    int planes;
    int planes_depth;
    int plane_mask;
    uint8_t *bgimage[4];
} chameleon_t;

static inline int chameleon_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void chameleon_reset_history(chameleon_t *c, int len)
{
    veejay_memset(c->sum, 0, sizeof(int32_t) * (size_t)len);
    veejay_memset(c->timebuffer, 0, (size_t)len * (size_t)c->planes);
    c->plane = 0;
}

vj_effect *chameleon_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 1;  ve->defaults[0] = 0;
    ve->limits[0][1] = 1; ve->limits[1][1] = 32; ve->defaults[1] = 8;

    ve->description = "ChameleonTV (EffectTV)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->motion = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Sensitivity");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,         VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 3,                  30,                 10, 42, 1000, 3200, 0,    64
    );

    (void) w;
    (void) h;

    return ve;
}

int chameleon_prepare(void *ptr, VJFrame *frame)
{
    chameleon_t *c = (chameleon_t*) ptr;

    veejay_memcpy(c->bgimage[0], frame->data[0], frame->len);
    veejay_memcpy(c->bgimage[1], frame->data[1], frame->len);
    veejay_memcpy(c->bgimage[2], frame->data[2], frame->len);

    VJFrame tmp;
    veejay_memset(&tmp, 0, sizeof(VJFrame));
    tmp.data[0] = c->bgimage[0];
    tmp.width = frame->width;
    tmp.height = frame->height;
    tmp.len = frame->len;

    softblur_apply_internal(&tmp);

    c->has_bg = 1;
    chameleon_reset_history(c, frame->len);

    veejay_msg(2, "ChameleonTV: Snapped background frame");

    return 1;
}

void *chameleon_malloc(int w, int h)
{
    if(w <= 0 || h <= 0 || w > INT_MAX / 2 ||
       (size_t)w > (size_t)INT_MAX / (size_t)h)
        return NULL;

    chameleon_t *c = (chameleon_t*) vj_calloc(sizeof(chameleon_t));

    if(!c)
        return NULL;

    const int len = w * h;
    const int safe_zone = w * 2;
    const size_t plane_bytes = (size_t)len;
    const size_t safe_zone_bytes = (size_t)safe_zone;
    if(safe_zone_bytes > SIZE_MAX / 3u ||
       plane_bytes > (SIZE_MAX - (safe_zone_bytes * 3u)) / 10u) {
        free(c);
        return NULL;
    }
    const size_t fixed_bytes = plane_bytes * 10u +
                               safe_zone_bytes * 3u;
    const size_t bg_plane_bytes = plane_bytes + safe_zone_bytes;
    const int planes = vje_history_capacity("ChameleonTV",
                                             fixed_bytes,
                                             plane_bytes,
                                             2,
                                             CHAMELEON_MAX_PLANES,
                                             1);
    if(planes == 0) {
        free(c);
        return NULL;
    }

    c->planes = planes;
    c->plane_mask = planes - 1;
    for(int value = planes; value > 1; value >>= 1)
        c->planes_depth++;

    c->bgimage[0] = (uint8_t*) vj_malloc(bg_plane_bytes * 3u);

    if(!c->bgimage[0]) {
        free(c);
        return NULL;
    }

    c->tmpimage[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 3u);

    if(!c->tmpimage[0]) {
        free(c->bgimage[0]);
        free(c);
        return NULL;
    }

    c->sum = (int32_t*) vj_calloc((size_t)len * sizeof(int32_t));

    if(!c->sum) {
        free(c->bgimage[0]);
        free(c->tmpimage[0]);
        free(c);
        return NULL;
    }

    c->timebuffer = (uint8_t*) vj_calloc((size_t)len * (size_t)planes);

    if(!c->timebuffer) {
        free(c->bgimage[0]);
        free(c->tmpimage[0]);
        free(c->sum);
        free(c);
        return NULL;
    }

    c->bgimage[1] = c->bgimage[0] + bg_plane_bytes;
    c->bgimage[2] = c->bgimage[1] + bg_plane_bytes;
    c->tmpimage[1] = c->tmpimage[0] + len;
    c->tmpimage[2] = c->tmpimage[1] + len;

    veejay_memset(c->bgimage[0], pixel_Y_lo_, bg_plane_bytes);
    veejay_memset(c->tmpimage[0], pixel_Y_lo_, len);

    for(int i = 1; i < 3; i++) {
        veejay_memset(c->bgimage[i], 128, bg_plane_bytes);
        veejay_memset(c->tmpimage[i], 128, len);
    }

    c->last_mode_ = -1;

    return c;
}

void chameleon_free(void *ptr)
{
    chameleon_t *c = (chameleon_t*) ptr;

    free(c->bgimage[0]);
    free(c->tmpimage[0]);
    free(c->timebuffer);
    free(c->sum);
    free(c);
}

static void drawChameleon(chameleon_t *cb, VJFrame *src, VJFrame *dest, int sensitivity, int appearing)
{
    const int video_area = src->len;
    uint8_t *restrict time_p = cb->timebuffer + ((size_t)cb->plane * (size_t)video_area);
    int32_t *restrict sum_s = cb->sum;

    uint8_t *restrict bgY = cb->bgimage[0];
    uint8_t *restrict bgU = cb->bgimage[1];
    uint8_t *restrict bgV = cb->bgimage[2];
    uint8_t *restrict srcY = src->data[0];
    uint8_t *restrict srcU = src->data[1];
    uint8_t *restrict srcV = src->data[2];
    uint8_t *restrict dstY = dest->data[0];
    uint8_t *restrict dstU = dest->data[1];
    uint8_t *restrict dstV = dest->data[2];

#pragma omp for schedule(static)
    for(int i = 0; i < video_area; i++) {
        const int y = srcY[i];
        const int current_sum = sum_s[i] - time_p[i] + y;

        sum_s[i] = current_sum;
        time_p[i] = (uint8_t)y;

        int diff = (y << cb->planes_depth) - current_sum;

        if(diff < 0)
            diff = -diff;

        const uint32_t dist = (uint32_t)diff * (uint32_t)sensitivity;
        const uint32_t a_calc = dist >> cb->planes_depth;
        const int alpha = a_calc > 255 ? 255 : (int)a_calc;

        if(appearing) {
            dstY[i] = (uint8_t)(y + ((((int)bgY[i] - y) * alpha) >> 8));
            dstU[i] = (uint8_t)((int)srcU[i] + ((((int)bgU[i] - (int)srcU[i]) * alpha) >> 8));
            dstV[i] = (uint8_t)((int)srcV[i] + ((((int)bgV[i] - (int)srcV[i]) * alpha) >> 8));
        } else {
            dstY[i] = (uint8_t)((int)bgY[i] + (((y - (int)bgY[i]) * alpha) >> 8));
            dstU[i] = (uint8_t)((int)bgU[i] + ((((int)srcU[i] - (int)bgU[i]) * alpha) >> 8));
            dstV[i] = (uint8_t)((int)bgV[i] + ((((int)srcV[i] - (int)bgV[i]) * alpha) >> 8));
        }
    }

#pragma omp single
    cb->plane = (cb->plane + 1) & cb->plane_mask;
}

void chameleon_apply(void *ptr, VJFrame *frame, int *args)
{
    chameleon_t *c = (chameleon_t*) ptr;
    const int mode = args[0];
    const int sensitivity = chameleon_clampi(args[1], 1, 32);
    const int len = frame->len;

#pragma omp single
    {
        if(!c->has_bg)
            chameleon_prepare(c, frame);

        if(c->last_mode_ != mode) {
            chameleon_reset_history(c, len);
            c->last_mode_ = mode;
        }
    }

    VJFrame source;
#pragma omp sections
    {
#pragma omp section
        {
            veejay_memcpy(c->tmpimage[0], frame->data[0], (size_t) len);
        }
#pragma omp section
        {
            veejay_memcpy(c->tmpimage[1], frame->data[1], (size_t) len);
        }
#pragma omp section
        {
            veejay_memcpy(c->tmpimage[2], frame->data[2], (size_t) len);
        }
    }

    veejay_memset(&source, 0, sizeof(VJFrame));
    source.data[0] = c->tmpimage[0];
    source.data[1] = c->tmpimage[1];
    source.data[2] = c->tmpimage[2];
    source.len = len;

    uint32_t activity = 0;
    int auto_switch = 0;
    int tmp1 = 0;
    int tmp2 = 0;

    if(c->motionmap && motionmap_active(c->motionmap)) {
#pragma omp single
        motionmap_scale_to(c->motionmap, 32, 32, 1, 1, &tmp1, &tmp2, &(c->n__), &(c->N__));
        auto_switch = 1;
        activity = motionmap_activity(c->motionmap);
    } else {
#pragma omp single
        {
        c->N__ = 0;
        c->n__ = 0;
        }
    }

    if(c->n__ == c->N__ || c->n__ == 0)
        auto_switch = 0;

    int appearing = mode ? 1 : 0;

    if(auto_switch)
        appearing = activity > 40 ? 1 : 0;

    drawChameleon(c, &source, frame, sensitivity, appearing);
}

int chameleon_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void chameleon_set_motionmap(void *ptr, void *priv)
{
    chameleon_t *c = (chameleon_t*) ptr;

    c->motionmap = priv;
}
