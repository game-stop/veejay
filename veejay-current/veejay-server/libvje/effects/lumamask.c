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
#include "lumamask.h"
#include "motionmap.h"

#define LUMAMASK_PARAMS 4

#define P_X_DISPLACE 0
#define P_Y_DISPLACE 1
#define P_BORDER     2
#define P_ALPHA      3

typedef struct {
    uint8_t *buf[4];
    void *motionmap;
    int n__;
    int N__;
    int n_threads;
} lumamask_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *lumamask_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = LUMAMASK_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
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

    ve->limits[0][P_X_DISPLACE] = -width;  ve->limits[1][P_X_DISPLACE] = width;  ve->defaults[P_X_DISPLACE] = width / 20;
    ve->limits[0][P_Y_DISPLACE] = -height; ve->limits[1][P_Y_DISPLACE] = height; ve->defaults[P_Y_DISPLACE] = height / 10;
    ve->limits[0][P_BORDER] = 0;           ve->limits[1][P_BORDER] = 1;          ve->defaults[P_BORDER] = 0;
    ve->limits[0][P_ALPHA] = 0;            ve->limits[1][P_ALPHA] = 1;           ve->defaults[P_ALPHA] = 0;

    ve->description = "Displacement Map";
    ve->motion = 1;
    ve->sub_format = 1;
    ve->extra_frame = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "X Displacement", "Y Displacement", "Border", "Update Alpha");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_BORDER], P_BORDER, "Clamp", "Blank Border");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_ALPHA], P_ALPHA, "No", "Yes");

    const int x_lo = -(width / 4);
    const int x_hi = width / 4;
    const int y_lo = -(height / 4);
    const int y_hi = height / 4;

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, x_lo,               x_hi,               8, 36,1200, 3400, 0,    48,
        VJ_BEAT_SIGNED_CURVE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_SIGN_LOCK | VJ_BEAT_F_NO_ZERO_CROSS, y_lo,               y_hi,               8, 34,1300, 3600, 0,    44,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_SELECTOR,     VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000
    );

    return ve;
}

int lumamask_requests_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void lumamask_set_motionmap(void *ptr, void *priv)
{
    lumamask_t *l = (lumamask_t*) ptr;
    l->motionmap = priv;
}

void *lumamask_malloc(int width, int height)
{
    lumamask_t *l = (lumamask_t*) vj_calloc(sizeof(lumamask_t));

    if(!l)
        return NULL;

    const int len = width * height;

    l->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 4);

    if(!l->buf[0]) {
        free(l);
        return NULL;
    }

    l->buf[1] = l->buf[0] + len;
    l->buf[2] = l->buf[1] + len;
    l->buf[3] = l->buf[2] + len;

    veejay_memset(l->buf[0], pixel_Y_lo_, len);
    veejay_memset(l->buf[1], 128, len);
    veejay_memset(l->buf[2], 128, len);
    veejay_memset(l->buf[3], 0, len);

    l->n_threads = vje_advise_num_threads(len);

    return (void*) l;
}

static void lumamask_noalpha_clamp(lumamask_t *l, VJFrame *frame, const uint8_t *restrict map, int x_mul_q8, int y_mul_q8)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const uint8_t *restrict sY = l->buf[0];
    const uint8_t *restrict sU = l->buf[1];
    const uint8_t *restrict sV = l->buf[2];

#pragma omp parallel for schedule(static) num_threads(l->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

#pragma omp simd
        for(int x = 0; x < width; x++) {
            const int p = row + x;
            const int m = (int)map[p] - 128;
            int nx = x + ((x_mul_q8 * m) >> 8);
            int ny = y + ((y_mul_q8 * m) >> 8);

            nx = nx < 0 ? 0 : (nx >= width ? width - 1 : nx);
            ny = ny < 0 ? 0 : (ny >= height ? height - 1 : ny);

            const int q = ny * width + nx;

            Y[p] = sY[q];
            U[p] = sU[q];
            V[p] = sV[q];
        }
    }
}

static void lumamask_noalpha_border(lumamask_t *l, VJFrame *frame, const uint8_t *restrict map, int x_mul_q8, int y_mul_q8)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    const uint8_t *restrict sY = l->buf[0];
    const uint8_t *restrict sU = l->buf[1];
    const uint8_t *restrict sV = l->buf[2];

#pragma omp parallel for schedule(static) num_threads(l->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

#pragma omp simd
        for(int x = 0; x < width; x++) {
            const int p = row + x;
            const int m = (int)map[p] - 128;
            const int nx = x + ((x_mul_q8 * m) >> 8);
            const int ny = y + ((y_mul_q8 * m) >> 8);
            const int out = (nx < 0) | (nx >= width) | (ny < 0) | (ny >= height);

            const int cx = nx < 0 ? 0 : (nx >= width ? width - 1 : nx);
            const int cy = ny < 0 ? 0 : (ny >= height ? height - 1 : ny);
            const int q = cy * width + cx;
            const int mask = -out;

            Y[p] = (uint8_t)((pixel_Y_lo_ & mask) | (sY[q] & ~mask));
            U[p] = (uint8_t)((128 & mask) | (sU[q] & ~mask));
            V[p] = (uint8_t)((128 & mask) | (sV[q] & ~mask));
        }
    }
}

static void lumamask_alpha_clamp(lumamask_t *l, VJFrame *frame, const uint8_t *restrict map, int x_mul_q8, int y_mul_q8)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    const uint8_t *restrict sY = l->buf[0];
    const uint8_t *restrict sU = l->buf[1];
    const uint8_t *restrict sV = l->buf[2];
    const uint8_t *restrict sA = l->buf[3];

#pragma omp parallel for schedule(static) num_threads(l->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

#pragma omp simd
        for(int x = 0; x < width; x++) {
            const int p = row + x;
            const int m = (int)map[p] - 128;
            int nx = x + ((x_mul_q8 * m) >> 8);
            int ny = y + ((y_mul_q8 * m) >> 8);

            nx = nx < 0 ? 0 : (nx >= width ? width - 1 : nx);
            ny = ny < 0 ? 0 : (ny >= height ? height - 1 : ny);

            const int q = ny * width + nx;

            Y[p] = sY[q];
            U[p] = sU[q];
            V[p] = sV[q];
            A[p] = sA[q];
        }
    }
}

static void lumamask_alpha_border(lumamask_t *l, VJFrame *frame, const uint8_t *restrict map, int x_mul_q8, int y_mul_q8)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    const uint8_t *restrict sY = l->buf[0];
    const uint8_t *restrict sU = l->buf[1];
    const uint8_t *restrict sV = l->buf[2];
    const uint8_t *restrict sA = l->buf[3];

#pragma omp parallel for schedule(static) num_threads(l->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;

#pragma omp simd
        for(int x = 0; x < width; x++) {
            const int p = row + x;
            const int m = (int)map[p] - 128;
            const int nx = x + ((x_mul_q8 * m) >> 8);
            const int ny = y + ((y_mul_q8 * m) >> 8);
            const int out = (nx < 0) | (nx >= width) | (ny < 0) | (ny >= height);

            const int cx = nx < 0 ? 0 : (nx >= width ? width - 1 : nx);
            const int cy = ny < 0 ? 0 : (ny >= height ? height - 1 : ny);
            const int q = cy * width + cx;
            const int mask = -out;

            Y[p] = (uint8_t)((pixel_Y_lo_ & mask) | (sY[q] & ~mask));
            U[p] = (uint8_t)((128 & mask) | (sU[q] & ~mask));
            V[p] = (uint8_t)((128 & mask) | (sV[q] & ~mask));
            A[p] = (uint8_t)(sA[q] & ~mask);
        }
    }
}

void lumamask_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    lumamask_t *l = (lumamask_t*) ptr;

    int x_scale = args[P_X_DISPLACE];
    int y_scale = args[P_Y_DISPLACE];
    const int border = args[P_BORDER];
    const int alpha = args[P_ALPHA];
    int interpolate = 1;
    int motion = 0;

    if(motionmap_active(l->motionmap)) {
        motionmap_scale_to(l->motionmap, frame->width, frame->height, 1, 1, &x_scale, &y_scale, &(l->n__), &(l->N__));
        motion = 1;
    }
    else {
        l->n__ = 0;
        l->N__ = 0;
    }

    if(l->n__ == l->N__ || l->n__ == 0)
        interpolate = 0;

    const int len = frame->len;
    int strides[4] = { len, len, len, alpha ? len : 0 };

    vj_frame_copy(frame->data, l->buf, strides);

    const int x_mul_q8 = -(x_scale * 2);
    const int y_mul_q8 = -(y_scale * 2);
    const uint8_t *restrict map = frame2->data[0];

    if(alpha) {
        if(border)
            lumamask_alpha_border(l, frame, map, x_mul_q8, y_mul_q8);
        else
            lumamask_alpha_clamp(l, frame, map, x_mul_q8, y_mul_q8);
    }
    else {
        if(border)
            lumamask_noalpha_border(l, frame, map, x_mul_q8, y_mul_q8);
        else
            lumamask_noalpha_clamp(l, frame, map, x_mul_q8, y_mul_q8);
    }

    if(interpolate)
        motionmap_interpolate_frame(l->motionmap, frame, l->N__, l->n__);

    if(motion)
        motionmap_store_frame(l->motionmap, frame);
}

void lumamask_free(void *ptr)
{
    lumamask_t *l = (lumamask_t*) ptr;

    free(l->buf[0]);
    free(l);
}
