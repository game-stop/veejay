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
#include "virtualcamera.h"
#include <math.h>
#include <stdint.h>

#define FP 16
#define FP_ONE (1 << FP)

#define VIRTUALCAMERA_PARAMS 11

#define P_TARGET_X     0
#define P_TARGET_Y     1
#define P_MOVE_SPEED   2
#define P_FOV_WIDTH    3
#define P_FOV_HEIGHT   4
#define P_ZOOM_PUNCH   5
#define P_PAN_IMPACT   6
#define P_SHAKE        7
#define P_BEAT_PUSH    8
#define P_LOCK_ASPECT  9
#define P_EDGE_MODE   10

typedef struct {
    uint8_t *buf[3];
    int *xmap;
    float current_x;
    float current_y;
    float current_fov_w;
    float current_fov_h;
    uint32_t frame_no;
    int is_initialized;
    int n_threads;
    int w;
    int h;
} virtualcam_t;

static inline int virtualcamera_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline float virtualcamera_clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int virtualcamera_percent_to_param1000(int v)
{
    v = virtualcamera_clampi(v, 0, 100);
    return (v * 1000 + 50) / 100;
}

static inline int virtualcamera_percent10_to_param(int v)
{
    v = virtualcamera_clampi(v, 1, 400);
    return v * 10;
}

static inline float virtualcamera_param1000_to_unit(int v)
{
    v = virtualcamera_clampi(v, 0, 1000);
    return (float)v * 0.001f;
}

static inline int virtualcamera_beat_shape_q8(int beat_push)
{
    int q;
    beat_push = virtualcamera_clampi(beat_push, 0, 1000);
    q = (beat_push * beat_push * 255 + 500000) / 1000000;
    return virtualcamera_clampi(q, 0, 255);
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
    if(max <= 1)
        return 0;

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
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
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
    ve->defaults[P_MOVE_SPEED] = virtualcamera_percent_to_param1000(15);

    ve->limits[0][P_FOV_WIDTH] = 10;
    ve->limits[1][P_FOV_WIDTH] = 4000;
    ve->defaults[P_FOV_WIDTH] = virtualcamera_percent10_to_param(100);

    ve->limits[0][P_FOV_HEIGHT] = 10;
    ve->limits[1][P_FOV_HEIGHT] = 4000;
    ve->defaults[P_FOV_HEIGHT] = virtualcamera_percent10_to_param(100);

    ve->limits[0][P_ZOOM_PUNCH] = 0;
    ve->limits[1][P_ZOOM_PUNCH] = 1000;
    ve->defaults[P_ZOOM_PUNCH] = 0;

    ve->limits[0][P_PAN_IMPACT] = 0;
    ve->limits[1][P_PAN_IMPACT] = 1000;
    ve->defaults[P_PAN_IMPACT] = 0;

    ve->limits[0][P_SHAKE] = 0;
    ve->limits[1][P_SHAKE] = 1000;
    ve->defaults[P_SHAKE] = 0;

    ve->limits[0][P_BEAT_PUSH] = 0;
    ve->limits[1][P_BEAT_PUSH] = 1000;
    ve->defaults[P_BEAT_PUSH] = 0;

    ve->limits[0][P_LOCK_ASPECT] = 0;
    ve->limits[1][P_LOCK_ASPECT] = 1;
    ve->defaults[P_LOCK_ASPECT] = 1;

    ve->limits[0][P_EDGE_MODE] = 0;
    ve->limits[1][P_EDGE_MODE] = 1;
    ve->defaults[P_EDGE_MODE] = 0;

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->description = "Beat Camera";
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
        "Beat Push",
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

        VJ_BEAT_DRIFT,         VJ_BEAT_F_PHRASE_ONLY,                         240,                760,                5,  18, 2200, 5200, 1800, 18,    /* Target X */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_PHRASE_ONLY,                         240,                760,                5,  18, 2200, 5200, 1800, 18,    /* Target Y */
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS,                          80,                 620,                8,  30, 1000, 2800, 0,    42,    /* Move Speed */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS,                          420,                1850,               8,  30, 1000, 2800, 0,    42,    /* FOV Width */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS,                          420,                1850,               8,  30, 1000, 2800, 0,    42,    /* FOV Height */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,       0,                  760,                16, 72, 80,   720,  0,    82,    /* Zoom Punch */
        VJ_BEAT_DRIFT,         VJ_BEAT_F_CONTINUOUS,                          0,                  640,                10, 42, 800,  2400, 0,    48,    /* Pan Impact */
        VJ_BEAT_TURBULENCE,    VJ_BEAT_F_CLIMAX_ONLY,                         0,                  520,                8,  32, 1200, 3600, 500,  32,    /* Shake */
        VJ_BEAT_INTENSITY,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_IMPULSE,       0,                  760,                18, 80, 60,   650,  0,    100,   /* Beat Push */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000, /* Lock Aspect */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,  0,    0,    0,    -1000  /* Edge Mode */
    );

    (void) w;
    (void) h;

    return ve;
}

void *virtualcamera_malloc(int w, int h)
{
    if(w <= 0 || h <= 0)
        return NULL;

    virtualcam_t *c = (virtualcam_t*) vj_calloc(sizeof(virtualcam_t));
    if(!c)
        return NULL;

    const size_t plane_size = (size_t)w * (size_t)h;

    c->buf[0] = (uint8_t*) vj_malloc(plane_size * 3u);
    if(!c->buf[0]) {
        free(c);
        return NULL;
    }

    c->xmap = (int*) vj_malloc(sizeof(int) * (size_t)w);
    if(!c->xmap) {
        free(c->buf[0]);
        free(c);
        return NULL;
    }

    c->buf[1] = c->buf[0] + plane_size;
    c->buf[2] = c->buf[1] + plane_size;

    c->current_x = 0.0f;
    c->current_y = 0.0f;
    c->current_fov_w = 0.0f;
    c->current_fov_h = 0.0f;
    c->frame_no = 0;
    c->is_initialized = 0;
    c->w = w;
    c->h = h;

    c->n_threads = vje_advise_num_threads((int)plane_size);
    if(c->n_threads <= 0)
        c->n_threads = 1;

    return (void*) c;
}

void virtualcamera_free(void *ptr)
{
    virtualcam_t *c = (virtualcam_t*) ptr;

    if(!c)
        return;

    if(c->buf[0])
        free(c->buf[0]);

    if(c->xmap)
        free(c->xmap);

    free(c);
}

static void virtualcamera_build_xmap(virtualcam_t *c,
                                     int w,
                                     int edge_black,
                                     int start_x_fp,
                                     int step_x_fp)
{
    int sx_fp = start_x_fp;

    if(edge_black) {
        for(int x = 0; x < w; x++, sx_fp += step_x_fp)
            c->xmap[x] = sx_fp >> FP;
    } else {
        for(int x = 0; x < w; x++, sx_fp += step_x_fp)
            c->xmap[x] = virtualcamera_mirror_coord(sx_fp >> FP, w);
    }
}

void virtualcamera_apply(void *ptr, VJFrame *frame, int *args)
{
    virtualcam_t *c = (virtualcam_t*) ptr;

    if(!c || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int w = frame->width;
    const int h = frame->height;
    const int len = frame->len;

    if(w <= 0 || h <= 0 || len <= 0)
        return;

    if(w != c->w || h != c->h)
        return;

    const int target_x_arg = virtualcamera_clampi(args[P_TARGET_X], 0, 1000);
    const int target_y_arg = virtualcamera_clampi(args[P_TARGET_Y], 0, 1000);
    const int speed_arg    = virtualcamera_clampi(args[P_MOVE_SPEED], 0, 1000);
    const int fov_w_arg    = virtualcamera_clampi(args[P_FOV_WIDTH], 10, 4000);
    const int fov_h_arg    = virtualcamera_clampi(args[P_FOV_HEIGHT], 10, 4000);
    const int zoom_arg     = virtualcamera_clampi(args[P_ZOOM_PUNCH], 0, 1000);
    const int pan_arg      = virtualcamera_clampi(args[P_PAN_IMPACT], 0, 1000);
    const int shake_arg    = virtualcamera_clampi(args[P_SHAKE], 0, 1000);
    const int beat_push    = virtualcamera_clampi(args[P_BEAT_PUSH], 0, 1000);
    const int lock_aspect  = args[P_LOCK_ASPECT] ? 1 : 0;
    const int edge_black   = args[P_EDGE_MODE] ? 1 : 0;

    const float target_x_base = ((float)target_x_arg * (float)w) * 0.001f;
    const float target_y_base = ((float)target_y_arg * (float)h) * 0.001f;
    const float speed = virtualcamera_param1000_to_unit(speed_arg);

    const int beat_drive_q8 = virtualcamera_beat_shape_q8(beat_push);
    const float beat_t = (float)beat_drive_q8 * (1.0f / 255.0f);
    const float zoom_t = virtualcamera_param1000_to_unit(zoom_arg);
    const float pan_t = virtualcamera_param1000_to_unit(pan_arg);
    const float shake_t = virtualcamera_param1000_to_unit(shake_arg);

    const float fov_w_base = ((float)fov_w_arg * (float)w) * 0.001f;
    const float fov_h_base = lock_aspect
        ? fov_w_base * ((float)h / (float)w)
        : ((float)fov_h_arg * (float)h) * 0.001f;

    const float phase = (float)(c->frame_no & 4095U) * 0.013671875f;
    const float pan_wave_x = sinf(phase * 1.37f + 0.71f);
    const float pan_wave_y = cosf(phase * 1.11f + 1.37f);
    const float beat_pan = (0.012f + pan_t * 0.060f) * beat_t;
    const float manual_pan = pan_t * 0.012f;
    const float target_x = target_x_base + pan_wave_x * (float)w * (beat_pan + manual_pan);
    const float target_y = target_y_base + pan_wave_y * (float)h * (beat_pan + manual_pan);

    const float zoom_curve = zoom_t * zoom_t;
    float zoom_punch = zoom_curve * 0.32f + beat_t * 0.26f;
    if(zoom_punch > 0.48f)
        zoom_punch = 0.48f;

    const float fov_w_target = virtualcamera_clampf(fov_w_base * (1.0f - zoom_punch), 1.0f, (float)w * 4.0f);
    const float fov_h_target = virtualcamera_clampf(fov_h_base * (1.0f - zoom_punch), 1.0f, (float)h * 4.0f);

    const float fov_speed = virtualcamera_clampf(0.18f + speed * 0.62f + beat_t * 0.16f, 0.02f, 0.94f);

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

    if(shake_t > 0.0f || beat_t > 0.0f) {
        const float shake_amp_x = (float)w * (0.002f + shake_t * 0.028f) * beat_t;
        const float shake_amp_y = (float)h * (0.002f + shake_t * 0.028f) * beat_t;
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

    virtualcamera_build_xmap(c, w, edge_black, start_x_fp, step_x_fp);

    int *restrict xmap = c->xmap;

    if(edge_black) {
#pragma omp parallel for schedule(static) num_threads(c->n_threads)
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
#pragma omp parallel for schedule(static) num_threads(c->n_threads)
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

    const size_t plane_size = (size_t)w * (size_t)h;

    veejay_memcpy(frame->data[0], c->buf[0], plane_size);
    veejay_memcpy(frame->data[1], c->buf[1], plane_size);
    veejay_memcpy(frame->data[2], c->buf[2], plane_size);

    c->frame_no++;
}
