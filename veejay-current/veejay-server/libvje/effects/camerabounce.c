/* 
 * Linux VeeJay
 *
 * Copyright(C)2023 Niels Elburg <nwelburg@gmail.com>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include "camerabounce.h"


#define CB_PARAMS 8

#define P_TEMPO      0
#define P_IMPACT     1
#define P_BLUR       2
#define P_DEPTH      3
#define P_CENTER_X   4
#define P_CENTER_Y   5
#define P_DIRECTION  6
#define P_PHASE      7

#define CB_COPY_RADIUS 255
#define CB_INV_AREA_SIZE 256
#define CB_FP_SHIFT 8
#define CB_FP_ONE 256
#define CB_FP_ROUND 32768

#define CB_MODE_PUSH          0
#define CB_MODE_PULL          1
#define CB_MODE_PUSH_PULL     2
#define CB_MODE_PULL_PUSH     3
#define CB_MODE_ALTERNATE     4

vj_effect *camerabounce_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = CB_PARAMS;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][P_TEMPO] = 1;
    ve->limits[1][P_TEMPO] = 300;
    ve->defaults[P_TEMPO] = 30;

    ve->limits[0][P_IMPACT] = 1;
    ve->limits[1][P_IMPACT] = 100;
    ve->defaults[P_IMPACT] = 50;

    ve->limits[0][P_BLUR] = 0;
    ve->limits[1][P_BLUR] = 100;
    ve->defaults[P_BLUR] = 20;

    ve->limits[0][P_DEPTH] = 0;
    ve->limits[1][P_DEPTH] = 100;
    ve->defaults[P_DEPTH] = 40;

    ve->limits[0][P_CENTER_X] = 0;
    ve->limits[1][P_CENTER_X] = 100;
    ve->defaults[P_CENTER_X] = 50;

    ve->limits[0][P_CENTER_Y] = 0;
    ve->limits[1][P_CENTER_Y] = 100;
    ve->defaults[P_CENTER_Y] = 50;

    ve->limits[0][P_DIRECTION] = 0;
    ve->limits[1][P_DIRECTION] = 4;
    ve->defaults[P_DIRECTION] = 0;

    ve->limits[0][P_PHASE] = 0;
    ve->limits[1][P_PHASE] = 100;
    ve->defaults[P_PHASE] = 0;

    ve->sub_format = 1;

    ve->description = "Camera Bounce";
    ve->param_description = vje_build_param_list(ve->num_params,
        "Tempo (Frames)",
        "Impact (Percentage)",
        "Motion Blur",
        "Zoom Depth",
        "Center X",
        "Center Y",
        "Direction Mode",
        "Phase"
    );

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_INERTIA, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_ENVELOPE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 18, 92, 74, 98, 100, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_HIGH_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 0, 92, 90, 100, 4, 520, 24, 1, 120, VJ_BEAT_COST_EXPENSIVE, 86, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_KICK_PULSE, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 12, 100, 94, 100, 0, 520, 24, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

typedef struct {
    uint8_t *buf[3];
    uint32_t *sat[3];
    uint8_t *radius_map;
    int *x0map;
    int *x1map;
    int *y0map;
    int *y1map;
    uint16_t *xwmap;
    uint16_t *ywmap;
    float inv_area[CB_INV_AREA_SIZE];
    int frameNumber;
    int n_threads;
    int last_blur_arg;
    int last_center_x_arg;
    int last_center_y_arg;
    int last_blur_energy_q;
    int w, h;
} camera_t;

static inline float cb_center_pos(int size, int arg)
{
    if (size <= 1)
        return 0.0f;

    if (arg <= 0)
        return 0.0f;

    if (arg >= 100)
        return (float)(size - 1);

    return ((float)(size - 1) * (float)arg) * 0.01f;
}

static inline float cb_old_triangle(int currentFrame, int zoomDuration)
{
    int halfDuration = zoomDuration >> 1;
    if (halfDuration == 0)
        halfDuration = 1;

    if (currentFrame <= halfDuration)
        return (float) currentFrame / (float) halfDuration;

    return (float)(zoomDuration - currentFrame) / (float) halfDuration;
}

static inline float cb_triangle01(float p)
{
    if (p <= 0.0f)
        return 0.0f;
    if (p >= 1.0f)
        return 0.0f;
    if (p <= 0.5f)
        return p * 2.0f;
    return (1.0f - p) * 2.0f;
}

static inline float cb_push_zoom(float env, float delta)
{
    return 1.0f + (env * delta);
}

static inline float cb_pull_zoom(float env, float delta)
{
    return 1.0f / (1.0f + (env * delta));
}

static inline float cb_smooth01(float v)
{
    if (v <= 0.0f)
        return 0.0f;
    if (v >= 1.0f)
        return 1.0f;
    return v * v * (3.0f - (2.0f * v));
}

static inline float cb_zoom_for_mode(int mode, int pulse_index, int currentFrame, int zoomDuration, float delta, float *energy)
{
    float env = cb_old_triangle(currentFrame, zoomDuration);
    float zenv;

    if (mode == CB_MODE_PULL) {
        zenv = cb_smooth01(env);
        *energy = zenv;
        return cb_pull_zoom(zenv, delta);
    }

    if (mode == CB_MODE_PUSH_PULL || mode == CB_MODE_PULL_PUSH) {
        const float p = (zoomDuration > 0) ? ((float) currentFrame / (float) zoomDuration) : 0.0f;

        if (p < 0.5f) {
            env = cb_triangle01(p * 2.0f);
            zenv = cb_smooth01(env);
            *energy = zenv;
            return (mode == CB_MODE_PUSH_PULL) ? cb_push_zoom(zenv, delta) : cb_pull_zoom(zenv, delta);
        }

        env = cb_triangle01((p - 0.5f) * 2.0f);
        zenv = cb_smooth01(env);
        *energy = zenv;
        return (mode == CB_MODE_PUSH_PULL) ? cb_pull_zoom(zenv, delta) : cb_push_zoom(zenv, delta);
    }

    if (mode == CB_MODE_ALTERNATE) {
        zenv = cb_smooth01(env);
        *energy = zenv;
        if (pulse_index & 1)
            return cb_pull_zoom(zenv, delta);
        return cb_push_zoom(zenv, delta);
    }

    zenv = cb_smooth01(env);
    *energy = zenv;
    return cb_push_zoom(zenv, delta);
}

static void camerabounce_build_radius_map(camera_t *c, int blur_arg, float blurEnergy, float centerX, float centerY)
{
    const int w = c->w;
    const int h = c->h;
    const float radiusEnergy = blurEnergy * blurEnergy;
    const float blurAmount = (blur_arg / 10.0f) * radiusEnergy;
    const float left = centerX;
    const float right = (float)(w - 1) - centerX;
    const float top = centerY;
    const float bottom = (float)(h - 1) - centerY;
    const float maxDx = (left > right) ? left : right;
    const float maxDy = (top > bottom) ? top : bottom;
    float maxDistSq = (maxDx * maxDx) + (maxDy * maxDy);

    if (maxDistSq <= 0.0f)
        maxDistSq = 1.0f;

    for (int y = 0; y < h; y++) {
        const float dy = centerY - (float)y;
        const int row = y * w;

        for (int x = 0; x < w; x++) {
            const float dx = centerX - (float)x;
            const float distSq = (dx * dx) + (dy * dy);
            float norm = distSq / maxDistSq;
            if (norm > 1.0f)
                norm = 1.0f;

            float radius = blurAmount * norm * norm * 100.0f;
            if (radius > 6.0f)
                radius = 6.0f;

            c->radius_map[row + x] = (radius < 0.5f) ? CB_COPY_RADIUS : (uint8_t)((int) radius);
        }
    }

    c->last_blur_arg = blur_arg;
}

static inline void cb_build_axis_map(int len, float center, float invZoom, int *m0, int *m1, uint16_t *mw)
{
    for (int i = 0; i < len; i++) {
        float src = center + (((float)i - center) * invZoom);

        if (src <= 0.0f) {
            m0[i] = 0;
            m1[i] = 0;
            mw[i] = 0;
        } else if (src >= (float)(len - 1)) {
            m0[i] = len - 1;
            m1[i] = len - 1;
            mw[i] = 0;
        } else {
            const int base = (int) src;
            int frac = (int)((src - (float)base) * (float)CB_FP_ONE + 0.5f);

            if (frac < 0)
                frac = 0;
            else if (frac > CB_FP_ONE)
                frac = CB_FP_ONE;

            m0[i] = base;
            m1[i] = base + 1;
            mw[i] = (uint16_t) frac;
        }
    }
}

static void camerabounce_build_maps(camera_t *c, int w, int h, float centerX, float centerY, float invZoom)
{
    cb_build_axis_map(w, centerX, invZoom, c->x0map, c->x1map, c->xwmap);
    cb_build_axis_map(h, centerY, invZoom, c->y0map, c->y1map, c->ywmap);
}

static inline uint8_t cb_bilerp(const uint8_t *row0, const uint8_t *row1, int x0, int x1, int wx, int wy)
{
    const int iw = CB_FP_ONE - wx;
    const int ih = CB_FP_ONE - wy;
    const int top = ((int)row0[x0] * iw) + ((int)row0[x1] * wx);
    const int bot = ((int)row1[x0] * iw) + ((int)row1[x1] * wx);
    const int v = (top * ih) + (bot * wy) + CB_FP_ROUND;
    return (uint8_t)(v >> (CB_FP_SHIFT * 2));
}

static void camerabounce_build_sat(camera_t *c, int w, int h)
{
    const uint8_t *srcY = c->buf[0];
    const uint8_t *srcU = c->buf[1];
    const uint8_t *srcV = c->buf[2];
    uint32_t *satY = c->sat[0];
    uint32_t *satU = c->sat[1];
    uint32_t *satV = c->sat[2];

    satY[0] = srcY[0];
    satU[0] = srcU[0];
    satV[0] = srcV[0];

    for (int x = 1; x < w; x++) {
        satY[x] = satY[x - 1] + srcY[x];
        satU[x] = satU[x - 1] + srcU[x];
        satV[x] = satV[x - 1] + srcV[x];
    }

    for (int y = 1; y < h; y++) {
        uint32_t rowSumY = 0;
        uint32_t rowSumU = 0;
        uint32_t rowSumV = 0;
        const int cur = y * w;
        const int pre = (y - 1) * w;

        for (int x = 0; x < w; x++) {
            const int i = cur + x;
            rowSumY += srcY[i];
            rowSumU += srcU[i];
            rowSumV += srcV[i];
            satY[i] = satY[pre + x] + rowSumY;
            satU[i] = satU[pre + x] + rowSumU;
            satV[i] = satV[pre + x] + rowSumV;
        }
    }
}

void camerabounce_free(void *ptr)
{
    camera_t *c = (camera_t*) ptr;
    if (!c)
        return;

    free(c->buf[0]);
    free(c->sat[0]);
    free(c->radius_map);
    free(c->x0map);
    free(c->xwmap);
    free(c);
}

void *camerabounce_malloc(int w, int h)
{
    camera_t *c = (camera_t*) vj_calloc(sizeof(camera_t));
    if (!c)
        return NULL;

    const size_t plane_size = (size_t) w * (size_t) h;

    c->buf[0] = (uint8_t*) vj_malloc(plane_size * 3);
    c->sat[0] = (uint32_t*) vj_malloc(plane_size * 3 * sizeof(uint32_t));
    c->radius_map = (uint8_t*) vj_malloc(plane_size);
    c->x0map = (int*) vj_malloc(((size_t)w * 2 + (size_t)h * 2) * sizeof(int));
    c->xwmap = (uint16_t*) vj_malloc(((size_t)w + (size_t)h) * sizeof(uint16_t));

    if (!c->buf[0] || !c->sat[0] || !c->radius_map || !c->x0map || !c->xwmap) {
        camerabounce_free(c);
        return NULL;
    }

    c->buf[1] = c->buf[0] + plane_size;
    c->buf[2] = c->buf[1] + plane_size;
    c->sat[1] = c->sat[0] + plane_size;
    c->sat[2] = c->sat[1] + plane_size;

    c->x1map = c->x0map + w;
    c->y0map = c->x1map + w;
    c->y1map = c->y0map + h;
    c->ywmap = c->xwmap + w;

    c->inv_area[0] = 0.0f;
    for (int i = 1; i < CB_INV_AREA_SIZE; i++)
        c->inv_area[i] = 1.0f / (float)i;

    c->w = w;
    c->h = h;
    c->last_blur_arg = -1;
    c->last_center_x_arg = -1;
    c->last_center_y_arg = -1;
    c->last_blur_energy_q = -1;
    c->n_threads = vje_advise_num_threads(plane_size);

    return (void*) c;
}

void camerabounce_apply(void *ptr, VJFrame* frame, int *args)
{
    camera_t *c = (camera_t*) ptr;
    const int w = frame->width;
    const int h = frame->height;
    const int plane_size = w * h;

    const int zoomInterval = args[P_TEMPO];
    const int zoomDuration = (args[P_IMPACT] * zoomInterval) / 100;
    const float depthDelta = args[P_DEPTH] / 50.0f;
    const int phaseFrames = (args[P_PHASE] * zoomInterval) / 100;
    const int frameWithPhase = c->frameNumber + phaseFrames;
    const int currentFrame = frameWithPhase % zoomInterval;
    const int pulseIndex = frameWithPhase / zoomInterval;

    c->frameNumber++;

    if (currentFrame > zoomDuration || zoomDuration <= 0)
        return;

    int mode = args[P_DIRECTION];
    if (mode < CB_MODE_PUSH)
        mode = CB_MODE_PUSH;
    else if (mode > CB_MODE_ALTERNATE)
        mode = CB_MODE_ALTERNATE;

    float bounceEnergy = 0.0f;
    const float zoomFactor = cb_zoom_for_mode(mode, pulseIndex, currentFrame, zoomDuration, depthDelta, &bounceEnergy);
    const int blur_mix = (args[P_BLUR] <= 0) ? 0 : (int)(bounceEnergy * 256.0f + 0.5f);
    const int blur_energy_q = (args[P_BLUR] <= 0) ? 0 : (int)(bounceEnergy * 1024.0f + 0.5f);
    const int no_blur = (blur_mix <= 0 || blur_energy_q <= 0);

    if (no_blur && zoomFactor == 1.0f)
        return;

    const float invZoom = 1.0f / zoomFactor;
    const float centerX = cb_center_pos(w, args[P_CENTER_X]);
    const float centerY = cb_center_pos(h, args[P_CENTER_Y]);

    camerabounce_build_maps(c, w, h, centerX, centerY, invZoom);

    if (!no_blur &&
        (c->last_blur_arg != args[P_BLUR] ||
         c->last_center_x_arg != args[P_CENTER_X] ||
         c->last_center_y_arg != args[P_CENTER_Y] ||
         c->last_blur_energy_q != blur_energy_q)) {
        camerabounce_build_radius_map(c, args[P_BLUR], bounceEnergy, centerX, centerY);
        c->last_center_x_arg = args[P_CENTER_X];
        c->last_center_y_arg = args[P_CENTER_Y];
        c->last_blur_energy_q = blur_energy_q;
    }

    uint8_t *frameY = frame->data[0];
    uint8_t *frameU = frame->data[1];
    uint8_t *frameV = frame->data[2];
    uint8_t *bufY = c->buf[0];
    uint8_t *bufU = c->buf[1];
    uint8_t *bufV = c->buf[2];

#pragma omp parallel num_threads(c->n_threads)
    {
#pragma omp for schedule(static)
        for (int y = 0; y < h; y++) {
            const int y0 = c->y0map[y];
            const int y1 = c->y1map[y];
            const int wy = c->ywmap[y];
            const uint8_t *row0Y = frameY + y0 * w;
            const uint8_t *row1Y = frameY + y1 * w;
            const uint8_t *row0U = frameU + y0 * w;
            const uint8_t *row1U = frameU + y1 * w;
            const uint8_t *row0V = frameV + y0 * w;
            const uint8_t *row1V = frameV + y1 * w;
            const int row_idx = y * w;

            for (int x = 0; x < w; x++) {
                const int x0 = c->x0map[x];
                const int x1 = c->x1map[x];
                const int wx = c->xwmap[x];
                const int idx = row_idx + x;

                bufY[idx] = cb_bilerp(row0Y, row1Y, x0, x1, wx, wy);
                bufU[idx] = cb_bilerp(row0U, row1U, x0, x1, wx, wy);
                bufV[idx] = cb_bilerp(row0V, row1V, x0, x1, wx, wy);
            }
        }

        if (no_blur) {
#pragma omp for schedule(static)
            for (int i = 0; i < plane_size; i++) {
                frameY[i] = bufY[i];
                frameU[i] = bufU[i];
                frameV[i] = bufV[i];
            }
        } else {
#pragma omp single
            camerabounce_build_sat(c, w, h);

            const uint32_t *satY = c->sat[0];
            const uint32_t *satU = c->sat[1];
            const uint32_t *satV = c->sat[2];
            const uint8_t *radius_map = c->radius_map;
            const float *inv_area = c->inv_area;
            const int bm = (blur_mix > 256) ? 256 : blur_mix;

#pragma omp for schedule(static)
            for (int y = 0; y < h; y++) {
                const int row_off = y * w;

                for (int x = 0; x < w; x++) {
                    const int idx = row_off + x;
                    const uint8_t r_u8 = radius_map[idx];

                    if (r_u8 == CB_COPY_RADIUS) {
                        frameY[idx] = bufY[idx];
                        frameU[idx] = bufU[idx];
                        frameV[idx] = bufV[idx];
                        continue;
                    }

                    const int r = (int) r_u8;
                    const int x1 = (x - r < 1) ? 0 : x - r - 1;
                    const int y1 = (y - r < 1) ? 0 : y - r - 1;
                    const int x2 = (x + r >= w) ? w - 1 : x + r;
                    const int y2 = (y + r >= h) ? h - 1 : y + r;
                    const int area = (x2 - x1) * (y2 - y1);
                    const float areaInv = inv_area[area];
                    const int y2w = y2 * w;
                    const int y1w = y1 * w;
                    const int a = y2w + x2;
                    const int b = y1w + x2;
                    const int d = y1w + x1;
                    const int e = y2w + x1;

                    const uint32_t sumY = satY[a] - satY[b] - satY[e] + satY[d];
                    const uint32_t sumU = satU[a] - satU[b] - satU[e] + satU[d];
                    const uint32_t sumV = satV[a] - satV[b] - satV[e] + satV[d];

                    const int by = (int)(sumY * areaInv + 0.5f);
                    const int bu = (int)(sumU * areaInv + 0.5f);
                    const int bv = (int)(sumV * areaInv + 0.5f);

                    if (bm >= 256) {
                        frameY[idx] = (uint8_t)by;
                        frameU[idx] = (uint8_t)bu;
                        frameV[idx] = (uint8_t)bv;
                    } else {
                        const int sy = bufY[idx];
                        const int su = bufU[idx];
                        const int sv = bufV[idx];
                        frameY[idx] = (uint8_t)(sy + (((by - sy) * bm + 128) >> 8));
                        frameU[idx] = (uint8_t)(su + (((bu - su) * bm + 128) >> 8));
                        frameV[idx] = (uint8_t)(sv + (((bv - sv) * bm + 128) >> 8));
                    }
                }
            }
        }
    }
}
