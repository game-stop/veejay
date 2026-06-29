/* 
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
#include <math.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include "virtualcamera.h"

#define FP 16
#define FP_ONE (1 << FP)

#define VIRTUALCAMERA_PARAMS 10

#define P_TARGET_X     0
#define P_TARGET_Y     1
#define P_MOVE_SPEED   2
#define P_FOV_WIDTH    3
#define P_FOV_HEIGHT   4
#define P_ZOOM_PUNCH   5
#define P_PAN_IMPACT   6
#define P_SHAKE        7
#define P_LOCK_ASPECT  8
#define P_EDGE_MODE    9

typedef struct {
    uint8_t *region;
    uint8_t *buf[3];
    int *xmap;
    float current_x;
    float current_y;
    float current_fov_w;
    float current_fov_h;
    float speed_env;
    float zoom_env;
    float pan_env;
    float shake_env;
    uint32_t frame_no;
    int is_initialized;
    int n_threads;
    int w;
    int h;
} virtualcam_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int virtualcamera_percent_to_param1000(int v)
{
    v = clampi(v, 0, 100);
    return (v * 1000 + 50) / 100;
}

static inline int virtualcamera_percent10_to_param(int v)
{
    v = clampi(v, 1, 400);
    return v * 10;
}

static inline float virtualcamera_param1000_to_unit(int v)
{
    v = clampi(v, 0, 1000);
    return (float)v * 0.001f;
}

static inline float virtualcamera_smoothf(float oldv, float target, float attack, float release)
{
    return target > oldv
        ? oldv + (target - oldv) * attack
        : oldv + (target - oldv) * release;
}

static inline uint32_t virtualcamera_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static inline float virtualcamera_hash_signed(uint32_t x)
{
    return ((float)(virtualcamera_hash_u32(x) & 0xffffU) * (1.0f / 65535.0f)) * 2.0f - 1.0f;
}

static inline int virtualcamera_mirror_coord(int v, int max)
{
    const int period = max << 1;

    v %= period;

    if(v < 0)
        v += period;

    return (v < max) ? v : (period - 1 - v);
}

vj_effect *virtualcamera_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = VIRTUALCAMERA_PARAMS;

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

    ve->limits[0][P_TARGET_X] = 0;
    ve->limits[1][P_TARGET_X] = 1000;
    ve->defaults[P_TARGET_X] = 500;

    ve->limits[0][P_TARGET_Y] = 0;
    ve->limits[1][P_TARGET_Y] = 1000;
    ve->defaults[P_TARGET_Y] = 500;

    ve->limits[0][P_MOVE_SPEED] = 0;
    ve->limits[1][P_MOVE_SPEED] = 1000;
    ve->defaults[P_MOVE_SPEED] = virtualcamera_percent_to_param1000(18);

    ve->limits[0][P_FOV_WIDTH] = 10;
    ve->limits[1][P_FOV_WIDTH] = 4000;
    ve->defaults[P_FOV_WIDTH] = virtualcamera_percent10_to_param(92);

    ve->limits[0][P_FOV_HEIGHT] = 10;
    ve->limits[1][P_FOV_HEIGHT] = 4000;
    ve->defaults[P_FOV_HEIGHT] = virtualcamera_percent10_to_param(92);

    ve->limits[0][P_ZOOM_PUNCH] = 0;
    ve->limits[1][P_ZOOM_PUNCH] = 1000;
    ve->defaults[P_ZOOM_PUNCH] = 0;

    ve->limits[0][P_PAN_IMPACT] = 0;
    ve->limits[1][P_PAN_IMPACT] = 1000;
    ve->defaults[P_PAN_IMPACT] = 0;

    ve->limits[0][P_SHAKE] = 0;
    ve->limits[1][P_SHAKE] = 1000;
    ve->defaults[P_SHAKE] = 0;

    ve->limits[0][P_LOCK_ASPECT] = 0;
    ve->limits[1][P_LOCK_ASPECT] = 1;
    ve->defaults[P_LOCK_ASPECT] = 0;

    ve->limits[0][P_EDGE_MODE] = 0;
    ve->limits[1][P_EDGE_MODE] = 1;
    ve->defaults[P_EDGE_MODE] = 0;

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->description = "Camera";
    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Target X",
        "Target Y",
        "Move Speed",
        "FOV Width",
        "FOV Height",
        "Zoom Punch",
        "Pan Impact",
        "Shake",
        "Lock Aspect",
        "Edge Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_LOCK_ASPECT],
        P_LOCK_ASPECT,
        "Free Aspect",
        "Lock Aspect"
    );

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_EDGE_MODE],
        P_EDGE_MODE,
        "Mirror",
        "Black"
    );
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SPEED,         VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 80, 360, 4, 14, 2200, 7600, 1800, 22,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,       0,                  420,                18, 58,  180, 1400, 0,    86,
        VJ_BEAT_DRIFT,         VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 0, 220, 4, 18, 1800, 6200, 1200, 26,
        VJ_BEAT_TURBULENCE,    VJ_BEAT_F_REJECT,                                     VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *virtualcamera_malloc(int w, int h)
{
    virtualcam_t *c = (virtualcam_t*) vj_calloc(sizeof(virtualcam_t));
    if(!c)
        return NULL;

    const size_t plane_size = (size_t)w * (size_t)h;
    const size_t frame_bytes = plane_size * 3u;
    const size_t map_bytes = sizeof(int) * (size_t)w;
    const size_t total = frame_bytes + map_bytes + 16u;

    c->region = (uint8_t*) vj_malloc(total);
    if(!c->region) {
        free(c);
        return NULL;
    }

    c->buf[0] = c->region;
    c->buf[1] = c->buf[0] + plane_size;
    c->buf[2] = c->buf[1] + plane_size;

    uint8_t *p = c->buf[2] + plane_size;
    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    c->xmap = (int*)p;

    c->current_x = 0.0f;
    c->current_y = 0.0f;
    c->current_fov_w = 0.0f;
    c->current_fov_h = 0.0f;
    c->speed_env = 0.0f;
    c->zoom_env = 0.0f;
    c->pan_env = 0.0f;
    c->shake_env = 0.0f;
    c->frame_no = 0;
    c->is_initialized = 0;
    c->w = w;
    c->h = h;

    c->n_threads = vje_advise_num_threads((int)plane_size);

    return (void*) c;
}

void virtualcamera_free(void *ptr)
{
    virtualcam_t *c = (virtualcam_t*) ptr;

    free(c->region);
    free(c);
}

static void virtualcamera_build_xmap(virtualcam_t *c,
                                     int w,
                                     int edge_black,
                                     int start_x_fp,
                                     int step_x_fp)
{
    if(edge_black) {
#pragma omp for schedule(static)
        for(int x = 0; x < w; x++)
            c->xmap[x] = (start_x_fp + x * step_x_fp) >> FP;
    } else {
#pragma omp for schedule(static)
        for(int x = 0; x < w; x++)
            c->xmap[x] = virtualcamera_mirror_coord((start_x_fp + x * step_x_fp) >> FP, w);
    }
}

void virtualcamera_apply(void *ptr, VJFrame *frame, int *args)
{
    virtualcam_t *c = (virtualcam_t*) ptr;

    const int w = frame->width;
    const int h = frame->height;

    const int target_x_arg = args[P_TARGET_X];
    const int target_y_arg = args[P_TARGET_Y];
    const int speed_arg    = args[P_MOVE_SPEED];
    const int fov_w_arg    = args[P_FOV_WIDTH];
    const int fov_h_arg    = args[P_FOV_HEIGHT];
    const int zoom_arg     = args[P_ZOOM_PUNCH];
    const int pan_arg      = args[P_PAN_IMPACT];
    const int shake_arg    = args[P_SHAKE];
    const int lock_aspect  = args[P_LOCK_ASPECT] ? 1 : 0;
    const int edge_black   = args[P_EDGE_MODE] ? 1 : 0;

    if(!c->is_initialized) {
        c->speed_env = (float)speed_arg;
        c->zoom_env = (float)zoom_arg;
        c->pan_env = (float)pan_arg;
        c->shake_env = (float)shake_arg;
    } else {
        c->speed_env = virtualcamera_smoothf(c->speed_env, (float)speed_arg, 0.16f, 0.10f);
        c->zoom_env = virtualcamera_smoothf(c->zoom_env, (float)zoom_arg, 0.28f, 0.090f);
        c->pan_env = virtualcamera_smoothf(c->pan_env, (float)pan_arg, 0.18f, 0.075f);
        c->shake_env = virtualcamera_smoothf(c->shake_env, (float)shake_arg, 0.42f, 0.120f);
    }

    const float target_x_base = ((float)target_x_arg * (float)w) * 0.001f;
    const float target_y_base = ((float)target_y_arg * (float)h) * 0.001f;
    const float speed = virtualcamera_param1000_to_unit((int)(c->speed_env + 0.5f));
    const float zoom_t = virtualcamera_param1000_to_unit((int)(c->zoom_env + 0.5f));
    const float pan_t = virtualcamera_param1000_to_unit((int)(c->pan_env + 0.5f));
    const float shake_t = virtualcamera_param1000_to_unit((int)(c->shake_env + 0.5f));

    const float fov_w_base = ((float)fov_w_arg * (float)w) * 0.001f;
    const float fov_h_base = lock_aspect
        ? fov_w_base * ((float)h / (float)w)
        : ((float)fov_h_arg * (float)h) * 0.001f;

    const float phase = (float)(c->frame_no & 4095U) *
        (0.010f + speed * 0.035f + pan_t * 0.024f);
    const float pan_wave_x = sinf(phase * 1.37f + 0.71f);
    const float pan_wave_y = cosf(phase * 1.11f + 1.37f);

    const float manual_pan = pan_t * 0.105f;
    const float target_x = target_x_base + pan_wave_x * (float)w * manual_pan;
    const float target_y = target_y_base + pan_wave_y * (float)h * manual_pan;

    float zoom_punch = zoom_t * 0.58f;
    if(zoom_punch > 0.72f)
        zoom_punch = 0.72f;

    const float fov_w_target = clampf(fov_w_base * (1.0f - zoom_punch), 1.0f, (float)w * 4.0f);
    const float fov_h_target = clampf(fov_h_base * (1.0f - zoom_punch), 1.0f, (float)h * 4.0f);

    const float fov_speed = clampf(0.12f + speed * 0.58f + zoom_t * 0.24f, 0.015f, 0.94f);

    if(!c->is_initialized) {
        c->current_x = target_x;
        c->current_y = target_y;
        c->current_fov_w = fov_w_target;
        c->current_fov_h = fov_h_target;
        c->is_initialized = 1;
    } else {
        c->current_x += (target_x - c->current_x) * speed;
        c->current_y += (target_y - c->current_y) * speed;
        c->current_fov_w += (fov_w_target - c->current_fov_w) * fov_speed;
        c->current_fov_h += (fov_h_target - c->current_fov_h) * fov_speed;
    }

    float sample_x = c->current_x;
    float sample_y = c->current_y;

    if(shake_t > 0.0f) {
        const float shake_drive = shake_t * 0.032f;
        const float shake_amp_x = (float)w * shake_drive;
        const float shake_amp_y = (float)h * shake_drive;
        const float sx = virtualcamera_hash_signed(c->frame_no * 2U + 0x1337U);
        const float sy = virtualcamera_hash_signed(c->frame_no * 2U + 0x8331U);

        sample_x += sx * shake_amp_x;
        sample_y += sy * shake_amp_y;
    }

    const float start_x = sample_x - (c->current_fov_w * 0.5f);
    const float start_y = sample_y - (c->current_fov_h * 0.5f);

    const float step_x = c->current_fov_w / (float)w;
    const float step_y = c->current_fov_h / (float)h;

    const int start_x_fp = (int)(start_x * (float)FP_ONE);
    const int start_y_fp = (int)(start_y * (float)FP_ONE);
    const int step_x_fp  = (int)(step_x  * (float)FP_ONE);
    const int step_y_fp  = (int)(step_y  * (float)FP_ONE);

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];

    uint8_t *restrict dstY = c->buf[0];
    uint8_t *restrict dstU = c->buf[1];
    uint8_t *restrict dstV = c->buf[2];

    int *restrict xmap = c->xmap;

#pragma omp parallel num_threads(c->n_threads)
    {
        virtualcamera_build_xmap(c, w, edge_black, start_x_fp, step_x_fp);

        if(edge_black) {
#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                const int sy = (start_y_fp + y * step_y_fp) >> FP;
                const int dst_row = y * w;

                if(sy < 0 || sy >= h) {
                    for(int x = 0; x < w; x++) {
                        const int d = dst_row + x;

                        dstY[d] = pixel_Y_lo_;
                        dstU[d] = 128;
                        dstV[d] = 128;
                    }

                    continue;
                }

                const int src_row = sy * w;

                for(int x = 0; x < w; x++) {
                    const int sx = xmap[x];
                    const int d = dst_row + x;

                    if(sx < 0 || sx >= w) {
                        dstY[d] = pixel_Y_lo_;
                        dstU[d] = 128;
                        dstV[d] = 128;
                    } else {
                        const int s = src_row + sx;

                        dstY[d] = srcY[s];
                        dstU[d] = srcU[s];
                        dstV[d] = srcV[s];
                    }
                }
            }
        } else {
#pragma omp for schedule(static)
            for(int y = 0; y < h; y++) {
                const int sy = virtualcamera_mirror_coord((start_y_fp + y * step_y_fp) >> FP, h);
                const int src_row = sy * w;
                const int dst_row = y * w;

                for(int x = 0; x < w; x++) {
                    const int s = src_row + xmap[x];
                    const int d = dst_row + x;

                    dstY[d] = srcY[s];
                    dstU[d] = srcU[s];
                    dstV[d] = srcV[s];
                }
            }
        }
    }

    const size_t plane_size = (size_t)w * (size_t)h;

    veejay_memcpy(frame->data[0], c->buf[0], plane_size);
    veejay_memcpy(frame->data[1], c->buf[1], plane_size);
    veejay_memcpy(frame->data[2], c->buf[2], plane_size);

    c->frame_no++;
}
