/* 
 * Linux VeeJay
 *
 * Copyright(C)2004-2016 Niels Elburg <nwelburg@gmail.com>
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
#include "motionmap.h"
#include "magicmirror.h"

#define MAGICMIRROR_PARAMS 5

#define P_X_DISPLACE 0
#define P_Y_DISPLACE 1
#define P_X_WAVE     2
#define P_Y_WAVE     3
#define P_ALPHA      4

typedef struct {
    uint8_t *magicmirrorbuf[4];
    float *wave_x;
    float *wave_y;
    int *cache_x;
    int *cache_y;
    int last_x_wave;
    int last_y_wave;
    int cx1;
    int cx2;
    int n__;
    int N__;
    int n_threads;
    void *motionmap;
} magicmirror_t;

static inline int magicmirror_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *magicmirror_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MAGICMIRROR_PARAMS;
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

    ve->defaults[P_X_DISPLACE] = w / 4;
    ve->defaults[P_Y_DISPLACE] = h / 4;
    ve->defaults[P_X_WAVE] = 20;
    ve->defaults[P_Y_WAVE] = 20;
    ve->defaults[P_ALPHA] = 0;

    ve->limits[0][P_X_DISPLACE] = 0; ve->limits[1][P_X_DISPLACE] = w / 2;
    ve->limits[0][P_Y_DISPLACE] = 0; ve->limits[1][P_Y_DISPLACE] = h / 2;
    ve->limits[0][P_X_WAVE] = 0;     ve->limits[1][P_X_WAVE] = 100;
    ve->limits[0][P_Y_WAVE] = 0;     ve->limits[1][P_Y_WAVE] = 100;
    ve->limits[0][P_ALPHA] = 0;      ve->limits[1][P_ALPHA] = 2;

    ve->motion = 1;
    ve->sub_format = 1;
    ve->description = "Magic Mirror Surface";
    ve->has_user = 0;
    ve->extra_frame = 0;
    ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "X Displacement",
        "Y Displacement",
        "X Wave",
        "Y Wave",
        "Alpha"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_ALPHA], P_ALPHA, "Normal", "Alpha Mirror Mask", "Alpha Mirror Mask Only");

    int x_hi = w / 4;
    int y_hi = h / 4;

    if(x_hi < 1)
        x_hi = 1;
    if(y_hi < 1)
        y_hi = 1;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, x_hi, 86, 100, 10, 480, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, y_hi, 78, 100, 0, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 4, 72, 72, 96, 15, 620, 0, 1, 120, VJ_BEAT_COST_MODERATE, 74, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_FREQUENCY, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_BAND_BALANCE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_BIPOLAR, VJ_BEAT_CURVE_SMOOTHSTEP, 4, 72, 68, 94, 20, 720, 0, 1, 120, VJ_BEAT_COST_MODERATE, 68, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void *magicmirror_malloc(int w, int h)
{
    magicmirror_t *m = (magicmirror_t*) vj_calloc(sizeof(magicmirror_t));

    if(!m)
        return NULL;

    const int len = w * h;

    m->magicmirrorbuf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)len * 4);

    if(!m->magicmirrorbuf[0]) {
        free(m);
        return NULL;
    }

    m->magicmirrorbuf[1] = m->magicmirrorbuf[0] + len;
    m->magicmirrorbuf[2] = m->magicmirrorbuf[1] + len;
    m->magicmirrorbuf[3] = m->magicmirrorbuf[2] + len;

    m->wave_x = (float*) vj_malloc(sizeof(float) * (size_t)w);
    m->wave_y = (float*) vj_malloc(sizeof(float) * (size_t)h);
    m->cache_x = (int*) vj_malloc(sizeof(int) * (size_t)w);
    m->cache_y = (int*) vj_malloc(sizeof(int) * (size_t)h);

    if(!m->wave_x || !m->wave_y || !m->cache_x || !m->cache_y) {
        magicmirror_free(m);
        return NULL;
    }

    veejay_memset(m->magicmirrorbuf[0], pixel_Y_lo_, len);
    veejay_memset(m->magicmirrorbuf[1], 128, len);
    veejay_memset(m->magicmirrorbuf[2], 128, len);
    veejay_memset(m->magicmirrorbuf[3], 0, len);

    m->last_x_wave = -1;
    m->last_y_wave = -1;
    m->n_threads = vje_advise_num_threads(len);

    return (void*) m;
}

void magicmirror_free(void *ptr)
{
    magicmirror_t *m = (magicmirror_t*) ptr;

    if(m) {
        free(m->magicmirrorbuf[0]);
        free(m->wave_x);
        free(m->wave_y);
        free(m->cache_x);
        free(m->cache_y);
        free(m);
    }
}

static void magicmirror_update_wave_x(magicmirror_t *m, int width, int x_wave)
{
    const float scale = (float)x_wave * 0.001f;

#pragma omp for schedule(static)
    for(int x = 0; x < width; x++)
        fast_sin(m->wave_x[x], (float)x * scale);

#pragma omp single
    m->last_x_wave = x_wave;
}

static void magicmirror_update_wave_y(magicmirror_t *m, int height, int y_wave)
{
    const float scale = (float)y_wave * 0.001f;

#pragma omp for schedule(static)
    for(int y = 0; y < height; y++)
        fast_sin(m->wave_y[y], (float)y * scale);

#pragma omp single
    m->last_y_wave = y_wave;
}

static void magicmirror_update_cache_x(magicmirror_t *m, int width, int x_displace)
{
#pragma omp for schedule(static)
    for(int x = 0; x < width; x++) {
        int dx = x + (int)(m->wave_x[x] * (float)x_displace);

        if(dx < 0)
            dx += width;
        if(dx < 0)
            dx = 0;
        else if(dx >= width)
            dx = width - 1;

        m->cache_x[x] = dx;
    }
}

static void magicmirror_update_cache_y(magicmirror_t *m, int height, int y_displace)
{
#pragma omp for schedule(static)
    for(int y = 0; y < height; y++) {
        int dy = y + (int)(m->wave_y[y] * (float)y_displace);

        if(dy < 0)
            dy += height;
        if(dy < 0)
            dy = 0;
        else if(dy >= height)
            dy = height - 1;

        m->cache_y[y] = dy;
    }
}

static void magicmirror_apply_yuv(magicmirror_t *m, VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const uint8_t *restrict sY = m->magicmirrorbuf[0];
    const uint8_t *restrict sCb = m->magicmirrorbuf[1];
    const uint8_t *restrict sCr = m->magicmirrorbuf[2];

    const int *restrict cache_x = m->cache_x;
    const int *restrict cache_y = m->cache_y;

#pragma omp for schedule(static)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;
        const int src_row = cache_y[y] * width;

        for(int x = 1; x < width - 1; x++) {
            const int q = row + x;
            const int p = src_row + cache_x[x];

            Y[q] = sY[p];
            Cb[q] = sCb[p];
            Cr[q] = sCr[p];
        }
    }
}

static void magicmirror_apply_alpha_only(magicmirror_t *m, VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict A = frame->data[3];
    const uint8_t *restrict sA = m->magicmirrorbuf[3];

    const int *restrict cache_x = m->cache_x;
    const int *restrict cache_y = m->cache_y;

#pragma omp for schedule(static)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;
        const int src_row = cache_y[y] * width;

        for(int x = 1; x < width - 1; x++)
            A[row + x] = sA[src_row + cache_x[x]];
    }
}

static void magicmirror_apply_alpha_mask(magicmirror_t *m, VJFrame *frame)
{
    const int width = frame->width;
    const int height = frame->height;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    const uint8_t *restrict sY = m->magicmirrorbuf[0];
    const uint8_t *restrict sCb = m->magicmirrorbuf[1];
    const uint8_t *restrict sCr = m->magicmirrorbuf[2];
    const uint8_t *restrict sA = m->magicmirrorbuf[3];

    const int *restrict cache_x = m->cache_x;
    const int *restrict cache_y = m->cache_y;

#pragma omp for schedule(static)
    for(int y = 1; y < height - 1; y++) {
        const int row = y * width;
        const int src_row = cache_y[y] * width;

        for(int x = 1; x < width - 1; x++) {
            const int q = row + x;
            const int p = src_row + cache_x[x];
            const uint8_t a = sA[p];

            A[q] = a;

            if(a) {
                Y[q] = sY[p];
                Cb[q] = sCb[p];
                Cr[q] = sCr[p];
            }
        }
    }
}

void magicmirror_apply(void *ptr, VJFrame *frame, int *args)
{
    magicmirror_t *m = (magicmirror_t*) ptr;

    int vx = magicmirror_clampi(args[P_X_DISPLACE], 0, frame->width / 2);
    int vy = magicmirror_clampi(args[P_Y_DISPLACE], 0, frame->height / 2);
    int x_wave = magicmirror_clampi(args[P_X_WAVE], 0, 100);
    int y_wave = magicmirror_clampi(args[P_Y_WAVE], 0, 100);
    const int alpha = magicmirror_clampi(args[P_ALPHA], 0, 2);
    int motion = 0;
    int interpolate = 1;

    if(motionmap_active(m->motionmap)) {
        if(motionmap_is_locked(m->motionmap)) {
            x_wave = m->cx1;
            y_wave = m->cx2;
        }
        else {
            motionmap_scale_to(m->motionmap, 100, 100, 0, 0, &x_wave, &y_wave, &(m->n__), &(m->N__));
            m->cx1 = x_wave;
            m->cx2 = y_wave;
        }
        motion = 1;
    }
    else {
        m->n__ = 0;
        m->N__ = 0;
    }

    if(m->N__ == m->n__ || m->n__ == 0)
        interpolate = 0;

    const int update_x = x_wave != m->last_x_wave;
    const int update_y = y_wave != m->last_y_wave;
    const int len = frame->len;
    int strides[4] = { len, len, len, alpha ? len : 0 };

#pragma omp parallel num_threads(m->n_threads)
    {
        if(update_x)
            magicmirror_update_wave_x(m, frame->width, x_wave);

        if(update_y)
            magicmirror_update_wave_y(m, frame->height, y_wave);

        magicmirror_update_cache_x(m, frame->width, vx);
        magicmirror_update_cache_y(m, frame->height, vy);

#pragma omp single
        vj_frame_copy(frame->data, m->magicmirrorbuf, strides);

        if(alpha == 0) {
            magicmirror_apply_yuv(m, frame);
        }
        else if(alpha == 1) {
            magicmirror_apply_alpha_mask(m, frame);
        }
        else {
            magicmirror_apply_alpha_only(m, frame);
        }
    }

    if(interpolate)
        motionmap_interpolate_frame(m->motionmap, frame, m->N__, m->n__);

    if(motion)
        motionmap_store_frame(m->motionmap, frame);
}

int magicmirror_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void magicmirror_set_motionmap(void *ptr, void *priv)
{
    magicmirror_t *m = (magicmirror_t*) ptr;
    m->motionmap = priv;
}
