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
#include <veejaycore/vjmem.h>
#include "rotozoom.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ROTOZOOM_PARAMS 8

#define P_ROTATE      0
#define P_ZOOM        1
#define P_AUTOMATIC   2
#define P_DURATION    3
#define P_MIX         4
#define P_CHROMA      5
#define P_ZOOM_DRIVE  6
#define P_SPIN_DRIVE  7

typedef struct {
    uint8_t *rotobuffer[3];
    float sin_lut[360];
    float cos_lut[360];

    double zoom;
    double rotate;
    int frameCount;
    int direction;
    int n_threads;

    float sm_rotate;
    float sm_zoom;
    float sm_duration;
    float sm_mix;
    float sm_chroma;
    float sm_zoom_drive;
    float sm_spin_drive;
    int smoothing_ready;
} rotozoom_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int rotozoom_wrap360(double v)
{
    int a = (int)v % 360;

    if(a < 0)
        a += 360;

    return a;
}

static inline uint8_t rotozoom_blend_y(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline uint8_t rotozoom_blend_uv(uint8_t a, uint8_t b, int q8)
{
    const int ac = (int)a - 128;
    const int bc = (int)b - 128;
    const int v = (((ac * (256 - q8)) + (bc * q8) + 128) >> 8) + 128;

    return (uint8_t)CLAMP_UV(v);
}

static inline int rotozoom_to_q8(int v, int max)
{
    v = clampi(v, 0, max);

    return (v * 256 + (max >> 1)) / max;
}



static inline float rotozoom_follow(float current, float target, float amount)
{
    return current + (target - current) * amount;
}

static inline float rotozoom_follow_angle(float current, float target, float amount)
{
    float d = target - current;

    while(d > 180.0f)
        d -= 360.0f;
    while(d < -180.0f)
        d += 360.0f;

    current += d * amount;

    while(current < 0.0f)
        current += 360.0f;
    while(current >= 360.0f)
        current -= 360.0f;

    return current;
}

static inline double rotozoom_scale_from_arg(double zoom_arg)
{
    if(zoom_arg > 0.0)
        return 1.0 / (1.0 + zoom_arg / 100.0);

    if(zoom_arg < 0.0)
        return pow(2.0, -zoom_arg / 200.0);

    return 1.0;
}

vj_effect *rotozoom_init(int width, int height)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = ROTOZOOM_PARAMS;
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

    ve->defaults[P_ROTATE] = 30;
    ve->defaults[P_ZOOM] = 2;
    ve->defaults[P_AUTOMATIC] = 1;
    ve->defaults[P_DURATION] = 100;
    ve->defaults[P_MIX] = 1000;
    ve->defaults[P_CHROMA] = 1000;
    ve->defaults[P_ZOOM_DRIVE] = 0;
    ve->defaults[P_SPIN_DRIVE] = 0;

    ve->limits[0][P_ROTATE] = 0;     ve->limits[1][P_ROTATE] = 360;
    ve->limits[0][P_ZOOM] = -1000;   ve->limits[1][P_ZOOM] = 1000;
    ve->limits[0][P_AUTOMATIC] = 0;  ve->limits[1][P_AUTOMATIC] = 1;
    ve->limits[0][P_DURATION] = 1;   ve->limits[1][P_DURATION] = 1500;
    ve->limits[0][P_MIX] = 0;        ve->limits[1][P_MIX] = 1000;
    ve->limits[0][P_CHROMA] = 0;     ve->limits[1][P_CHROMA] = 1000;
    ve->limits[0][P_ZOOM_DRIVE] = 0; ve->limits[1][P_ZOOM_DRIVE] = 1000;
    ve->limits[0][P_SPIN_DRIVE] = 0; ve->limits[1][P_SPIN_DRIVE] = 1000;

    ve->description = "Rotozoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Rotate",
        "Zoom",
        "Automatic",
        "Duration",
        "Mix",
        "Chroma Amount",
        "Zoom Drive",
        "Spin Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_AUTOMATIC], P_AUTOMATIC, "Manual", "Automatic");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_RATE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_LINEAR, 1, 360, 72, 100, 0, 220, 0, 1, 0, VJ_BEAT_COST_CHEAP, 58, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_SIGNED, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_SOURCE_SIGN, VJ_BEAT_CURVE_EASE_OUT, -420, 620, 78, 100, 8, 520, 0, 2, 0, VJ_BEAT_COST_CHEAP, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 24, 700, 82, 100, 8, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 70, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SOURCE_MIX, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 420, 1000, 86, 100, 8, 520, 0, 5, 0, VJ_BEAT_COST_CHEAP, 80, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_COLOR_AMOUNT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 360, 1000, 70, 96, 120, 900, 0, 5, 0, VJ_BEAT_COST_CHEAP, 66, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WARP, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 90, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 0, 1000, 88, 100, 8, 420, 0, 5, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *rotozoom_malloc(int width, int height)
{
    rotozoom_t *r = (rotozoom_t*)vj_calloc(sizeof(rotozoom_t));

    if(!r)
        return NULL;

    const int len = width * height;

    r->rotobuffer[0] = (uint8_t*)vj_malloc((size_t)len * 3u);

    if(!r->rotobuffer[0]) {
        free(r);
        return NULL;
    }

    r->rotobuffer[1] = r->rotobuffer[0] + len;
    r->rotobuffer[2] = r->rotobuffer[1] + len;

    r->zoom = 0.0;
    r->rotate = 0.0;
    r->frameCount = 0;
    r->direction = 1;
    r->smoothing_ready = 0;

    for(int i = 0; i < 360; i++) {
        const double rad = (double)i * M_PI / 180.0;

        r->sin_lut[i] = a_sin(rad);
        r->cos_lut[i] = a_cos(rad);
    }

    r->n_threads = vje_advise_num_threads(len);

    return (void*)r;
}

void rotozoom_free(void *ptr)
{
    rotozoom_t *r = (rotozoom_t*)ptr;

    free(r->rotobuffer[0]);
    free(r);
}

void rotozoom_apply(void *ptr, VJFrame *frame, int *args)
{
    rotozoom_t *r = (rotozoom_t*)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    double rotate = (double)args[P_ROTATE];
    double zoom_arg = (double)args[P_ZOOM];
    const int autom = args[P_AUTOMATIC] ? 1 : 0;
    int maxFrames = args[P_DURATION];

    const int mix_arg = args[P_MIX];
    const int chroma_arg = args[P_CHROMA];
    const int zoom_drive_arg = args[P_ZOOM_DRIVE];
    const int spin_drive_arg = args[P_SPIN_DRIVE];

    if(autom) {
        zoom_arg = r->zoom;
        rotate = r->rotate;

        r->zoom += (double)r->direction * (2000.0 / (double)maxFrames);
        r->rotate += (double)r->direction * (360.0 / (double)maxFrames);
        r->frameCount++;

        if(r->frameCount >= maxFrames || r->rotate <= 0.0 || r->rotate >= 360.0) {
            r->direction *= -1;
            r->frameCount = 0;

            if(r->rotate < 0.0)
                r->rotate = 0.0;
            else if(r->rotate > 360.0)
                r->rotate = 360.0;
        }

        if(r->zoom < -1000.0)
            r->zoom = -1000.0;
        else if(r->zoom > 1000.0)
            r->zoom = 1000.0;
    }
    else {
        r->zoom = zoom_arg;
        r->rotate = rotate;
        r->frameCount = 0;
        r->direction = 1;
    }

    const float follow = 0.28f;

    if(!r->smoothing_ready) {
        r->sm_rotate = (float)rotate;
        r->sm_zoom = (float)zoom_arg;
        r->sm_duration = (float)maxFrames;
        r->sm_mix = (float)mix_arg;
        r->sm_chroma = (float)chroma_arg;
        r->sm_zoom_drive = (float)zoom_drive_arg;
        r->sm_spin_drive = (float)spin_drive_arg;
        r->smoothing_ready = 1;
    }
    else {
        r->sm_rotate = rotozoom_follow_angle(r->sm_rotate, (float)rotate, follow);
        r->sm_zoom = rotozoom_follow(r->sm_zoom, (float)zoom_arg, follow);
        r->sm_duration = rotozoom_follow(r->sm_duration, (float)maxFrames, follow);
        r->sm_mix = rotozoom_follow(r->sm_mix, (float)mix_arg, follow);
        r->sm_chroma = rotozoom_follow(r->sm_chroma, (float)chroma_arg, follow);
        r->sm_zoom_drive = rotozoom_follow(r->sm_zoom_drive, (float)zoom_drive_arg, follow);
        r->sm_spin_drive = rotozoom_follow(r->sm_spin_drive, (float)spin_drive_arg, follow);
    }

    const float zoom_drive = clampf(r->sm_zoom_drive * 0.001f, 0.0f, 1.0f);
    const float spin_drive = clampf(r->sm_spin_drive * 0.001f, 0.0f, 1.0f);

    double effective_zoom_arg = (double)r->sm_zoom;

    effective_zoom_arg += (double)(zoom_drive * 840.0f);

    if(effective_zoom_arg > 1000.0)
        effective_zoom_arg = 1000.0;
    else if(effective_zoom_arg < -1000.0)
        effective_zoom_arg = -1000.0;

    double effective_rotate = (double)r->sm_rotate;

    effective_rotate += (double)(spin_drive * 360.0f);

    const double zoom = rotozoom_scale_from_arg(effective_zoom_arg);

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict srcY = r->rotobuffer[0];
    uint8_t *restrict srcU = r->rotobuffer[1];
    uint8_t *restrict srcV = r->rotobuffer[2];

    veejay_memcpy(srcY, dstY, len);
    veejay_memcpy(srcU, dstU, len);
    veejay_memcpy(srcV, dstV, len);

    int mix_q8 = rotozoom_to_q8((int)(r->sm_mix + 0.5f), 1000);
    int chroma_q8 = rotozoom_to_q8((int)(r->sm_chroma + 0.5f), 1000);

    if(zoom_drive > 0.0f || spin_drive > 0.0f) {
        const float drive = clampf((zoom_drive * 0.60f) + (spin_drive * 0.40f), 0.0f, 1.0f);
        const int lift = (int)((float)(256 - mix_q8) * drive * 0.48f + 0.5f);
        const int clift = (int)((float)(256 - chroma_q8) * drive * 0.42f + 0.5f);

        mix_q8 = clampi(mix_q8 + lift, 0, 256);
        chroma_q8 = clampi(chroma_q8 + clift, 0, 256);
    }

    const float centerX = ((float)width - 1.0f) * 0.5f;
    const float centerY = ((float)height - 1.0f) * 0.5f;
    const int angle = rotozoom_wrap360(effective_rotate);
    const float cos_val = r->cos_lut[angle];
    const float sin_val = r->sin_lut[angle];
    const float z = (float)zoom;

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int y = 0; y < height; y++) {
        const float dy = (float)y - centerY;
        const int row = y * width;

        for(int x = 0; x < width; x++) {
            const float dx = (float)x - centerX;
            const float rotatedX = dx * cos_val - dy * sin_val;
            const float rotatedY = dx * sin_val + dy * cos_val;

            int newX = (int)(rotatedX * z + centerX);
            int newY = (int)(rotatedY * z + centerY);

            newX = clampi(newX, 0, width - 1);
            newY = clampi(newY, 0, height - 1);

            const int srcIndex = newY * width + newX;
            const int dstIndex = row + x;

            const uint8_t oy = srcY[dstIndex];
            const uint8_t ou = srcU[dstIndex];
            const uint8_t ov = srcV[dstIndex];
            const uint8_t wy = srcY[srcIndex];
            const uint8_t wu = srcU[srcIndex];
            const uint8_t wv = srcV[srcIndex];
            const uint8_t cu = rotozoom_blend_uv(ou, wu, chroma_q8);
            const uint8_t cv = rotozoom_blend_uv(ov, wv, chroma_q8);

            dstY[dstIndex] = rotozoom_blend_y(oy, wy, mix_q8);
            dstU[dstIndex] = rotozoom_blend_uv(ou, cu, mix_q8);
            dstV[dstIndex] = rotozoom_blend_uv(ov, cv, mix_q8);
        }
    }
}
