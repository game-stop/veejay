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
#include "smear.h"
#include "motionmap.h"

typedef struct {
    uint8_t *tmp[3];
    int n__;
    int N__;
    int n_threads;
    void *motionmap;
} smear_t;

static inline int smear_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *smear_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->defaults[0] = 0;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 255;
    ve->defaults[1] = 1;

    ve->description = "Pixel Smear";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->motion = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Mode",
        "Value"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][0],
        0,
        "Horizontal",
        "Horizontal Average",
        "Vertical",
        "Vertical Average"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,   -1000, /* Mode */
        VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS,                    4,                  180,                10, 38, 1000, 2600, 0,   60     /* Value */
    );

    (void) w;
    (void) h;

    return ve;
}

void *smear_malloc(int w, int h)
{
    smear_t *s = (smear_t*) vj_calloc(sizeof(smear_t));
    if(!s)
        return NULL;

    const int len = w * h;

    s->tmp[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!s->tmp[0]) {
        free(s);
        return NULL;
    }

    s->tmp[1] = s->tmp[0] + len;
    s->tmp[2] = s->tmp[1] + len;

    s->n__ = 0;
    s->N__ = 0;
    s->motionmap = NULL;

    s->n_threads = vje_advise_num_threads(len);
    if(s->n_threads < 1)
        s->n_threads = 1;

    return (void*) s;
}

void smear_free(void *ptr)
{
    smear_t *s = (smear_t*) ptr;
    if(!s)
        return;

    if(s->tmp[0])
        free(s->tmp[0]);

    free(s);
}

static void smear_snapshot(smear_t *s, VJFrame *frame)
{
    const int len = frame->len;

    veejay_memcpy(s->tmp[0], frame->data[0], len);
    veejay_memcpy(s->tmp[1], frame->data[1], len);
    veejay_memcpy(s->tmp[2], frame->data[2], len);
}

static void smear_apply_x(smear_t *s, VJFrame *frame, int val)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict sY  = s->tmp[0];
    const uint8_t *restrict sCb = s->tmp[1];
    const uint8_t *restrict sCr = s->tmp[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int idx = row + x;
            const int j = sY[idx];

            if(j >= val) {
                int sx = x + j;
                if(sx >= width)
                    sx = width - 1;

                const int src = row + sx;

                Y[idx]  = sY[src];
                Cb[idx] = sCb[src];
                Cr[idx] = sCr[src];
            }
        }
    }
}

static void smear_apply_x_avg(smear_t *s, VJFrame *frame, int val)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict sY  = s->tmp[0];
    const uint8_t *restrict sCb = s->tmp[1];
    const uint8_t *restrict sCr = s->tmp[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int idx = row + x;
            const int j = sY[idx];

            if(j >= val) {
                int sx = x + j;
                if(sx >= width)
                    sx = width - 1;

                const int src = row + sx;

                Y[idx]  = (uint8_t)(((int)sY[src]  + (int)sY[idx])  >> 1);
                Cb[idx] = (uint8_t)((((int)sCb[src] - 128 + (int)sCb[idx] - 128) >> 1) + 128);
                Cr[idx] = (uint8_t)((((int)sCr[src] - 128 + (int)sCr[idx] - 128) >> 1) + 128);
            }
        }
    }
}

static void smear_apply_y(smear_t *s, VJFrame *frame, int val)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict sY  = s->tmp[0];
    const uint8_t *restrict sCb = s->tmp[1];
    const uint8_t *restrict sCr = s->tmp[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int idx = row + x;
            const int j = sY[idx];

            if(j >= val) {
                int sy = y + j;
                if(sy >= height)
                    sy = height - 1;

                const int src = sy * width + x;

                Y[idx]  = sY[src];
                Cb[idx] = sCb[src];
                Cr[idx] = sCr[src];
            }
        }
    }
}

static void smear_apply_y_avg(smear_t *s, VJFrame *frame, int val)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict sY  = s->tmp[0];
    const uint8_t *restrict sCb = s->tmp[1];
    const uint8_t *restrict sCr = s->tmp[2];

#pragma omp parallel for schedule(static) num_threads(s->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const int idx = row + x;
            const int j = sY[idx];

            if(j >= val) {
                int sy = y + j;
                if(sy >= height)
                    sy = height - 1;

                const int src = sy * width + x;

                Y[idx]  = (uint8_t)(((int)sY[src]  + (int)sY[idx])  >> 1);
                Cb[idx] = (uint8_t)((((int)sCb[src] - 128 + (int)sCb[idx] - 128) >> 1) + 128);
                Cr[idx] = (uint8_t)((((int)sCr[src] - 128 + (int)sCr[idx] - 128) >> 1) + 128);
            }
        }
    }
}

int smear_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void smear_set_motionmap(void *ptr, void *priv)
{
    smear_t *s = (smear_t*) ptr;

    if(s)
        s->motionmap = priv;
}

void smear_apply(void *ptr, VJFrame *frame, int *args)
{
    smear_t *s = (smear_t*) ptr;

    if(!s || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int mode = smear_clampi(args[0], 0, 3);
    int val = smear_clampi(args[1], 0, 255);

    int tmp1 = mode;
    int tmp2 = val;
    int motion = 0;
    int interpolate = 0;

    if(s->motionmap && motionmap_active(s->motionmap)) {
        motionmap_scale_to(
            s->motionmap,
            255,
            3,
            0,
            0,
            &tmp2,
            &tmp1,
            &(s->n__),
            &(s->N__)
        );

        val = smear_clampi(tmp2, 0, 255);
        mode = smear_clampi(tmp1, 0, 3);

        motion = 1;
        interpolate = !(s->n__ == s->N__ || s->n__ == 0);
    } else {
        s->N__ = 0;
        s->n__ = 0;
    }

    smear_snapshot(s, frame);

    switch(mode) {
        case 0:
            smear_apply_x(s, frame, val);
            break;
        case 1:
            smear_apply_x_avg(s, frame, val);
            break;
        case 2:
            smear_apply_y(s, frame, val);
            break;
        case 3:
            smear_apply_y_avg(s, frame, val);
            break;
        default:
            break;
    }

    if(interpolate)
        motionmap_interpolate_frame(s->motionmap, frame, s->N__, s->n__);

    if(motion)
        motionmap_store_frame(s->motionmap, frame);
}