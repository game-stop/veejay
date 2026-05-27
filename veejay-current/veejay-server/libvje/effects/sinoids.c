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
#include "sinoids.h"
#include "motionmap.h"

#define DEFAULT_SINOIDS 70
#define SINOIDS_PI 3.14159265358979323846

typedef struct {
    int *sinoids_X;
    uint8_t *sinoid_frame[3];
    int current_sinoids;
    int n__;
    int N__;
    int n_threads;
    void *motionmap;
} sinoids_t;

vj_effect *sinoids_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 1;
    ve->defaults[1] = DEFAULT_SINOIDS;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 1;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1000;

    ve->description = "Sinoids";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->motion = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Sinoids"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "Inplace",
        "On Copy"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Mode */
        VJ_BEAT_WARP,     VJ_BEAT_F_CONTINUOUS,                    0,                  220,                8, 30, 1200, 3000, 0,   55     /* Sinoids */
    );

    (void) width;
    (void) height;

    return ve;
}

void *sinoids_malloc(int width, int height)
{
    sinoids_t *s = (sinoids_t*) vj_calloc(sizeof(sinoids_t));
    if(!s)
        return NULL;

    const int len = width * height;

    s->sinoids_X = (int*) vj_malloc(sizeof(int) * width);
    if(!s->sinoids_X) {
        free(s);
        return NULL;
    }

    s->sinoid_frame[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->sinoid_frame[0]) {
        free(s->sinoids_X);
        free(s);
        return NULL;
    }

    s->sinoid_frame[1] = s->sinoid_frame[0] + len;
    s->sinoid_frame[2] = s->sinoid_frame[1] + len;

    s->current_sinoids = -1;
    s->n__ = 0;
    s->N__ = 0;
    s->motionmap = NULL;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void sinoids_free(void *ptr)
{
    sinoids_t *s = (sinoids_t*) ptr;
    if(!s)
        return;

    if(s->sinoids_X)
        free(s->sinoids_X);

    if(s->sinoid_frame[0])
        free(s->sinoid_frame[0]);

    free(s);
}

static inline int sinoids_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int sinoids_reflect_x(int x, int width)
{
    if(width <= 1)
        return 0;

    const int max = width - 1;
    const int period = max << 1;

    x %= period;

    if(x < 0)
        x += period;

    return (x <= max) ? x : period - x;
}

static void sinoids_recalc(sinoids_t *s, int width, int z)
{
    const double zoom = (double)z * 0.1;
    int *restrict sinoids_X = s->sinoids_X;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int i = 0; i < width; i++) {
        const double phase = ((double)i / (double)width) * 2.0 * SINOIDS_PI;
        sinoids_X[i] = (int)(a_sin(phase) * zoom * 4.0);
    }

    s->current_sinoids = z;
}

static void sinoids_apply_inplace(sinoids_t *s, VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    int *restrict offset = s->sinoids_X;

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int sx = sinoids_reflect_x(x + offset[x], width);
            const int dst = row + x;
            const int src = row + sx;

            Y[dst]  = Y[src];
            Cb[dst] = Cb[src];
            Cr[dst] = Cr[src];
        }
    }
}

static void sinoids_apply_copy(sinoids_t *s, VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY  = s->sinoid_frame[0];
    uint8_t *restrict srcCb = s->sinoid_frame[1];
    uint8_t *restrict srcCr = s->sinoid_frame[2];

    int *restrict offset = s->sinoids_X;

    veejay_memcpy(srcY,  Y,  len);
    veejay_memcpy(srcCb, Cb, len);
    veejay_memcpy(srcCr, Cr, len);

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int sx = sinoids_reflect_x(x + offset[x], width);
            const int dst = row + x;
            const int src = row + sx;

            Y[dst]  = srcY[src];
            Cb[dst] = srcCb[src];
            Cr[dst] = srcCr[src];
        }
    }
}

void sinoids_apply(void *ptr, VJFrame *frame, int *args)
{
    sinoids_t *s = (sinoids_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 1 || height <= 2 || len <= 0)
        return;

    int mode = sinoids_clampi(args[0], 0, 1);
    int ns = sinoids_clampi(args[1], 0, 1000);

    int tmp1 = mode;
    int tmp2 = ns;
    int interpolate = 0;
    int motion = 0;

    if(s->motionmap && motionmap_active(s->motionmap)) {
        motionmap_scale_to(
            s->motionmap,
            1,
            1000,
            0,
            0,
            &tmp1,
            &tmp2,
            &(s->n__),
            &(s->N__)
        );

        mode = sinoids_clampi(tmp1, 0, 1);
        ns = sinoids_clampi(tmp2, 0, 1000);

        motion = 1;
        interpolate = !(s->n__ == s->N__ || s->n__ == 0);
    } else {
        s->n__ = 0;
        s->N__ = 0;
    }

    if(ns != s->current_sinoids)
        sinoids_recalc(s, width, ns);

    if(mode == 0)
        sinoids_apply_inplace(s, frame);
    else
        sinoids_apply_copy(s, frame);

    if(interpolate)
        motionmap_interpolate_frame(s->motionmap, frame, s->N__, s->n__);

    if(motion)
        motionmap_store_frame(s->motionmap, frame);
}

int sinoids_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void sinoids_set_motionmap(void *ptr, void *priv)
{
    sinoids_t *s = (sinoids_t*) ptr;

    if(s)
        s->motionmap = priv;
}