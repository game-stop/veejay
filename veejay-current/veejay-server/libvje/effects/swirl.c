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
#include "swirl.h"
#include <math.h>

#define SWIRL_PARAMS 3

#define P_DEGREES     0
#define P_MODE        1
#define P_SWIRL_DRIVE 2

typedef struct {
    uint8_t *region;

    double *polar_map;
    double *fish_angle;

    int *cached_coords;
    int *drive_coords;

    uint8_t *buf[3];

    int v;
    int mode;
    int drive_v;
    int drive_swirl;

    float eff_degrees;
    float eff_swirl_drive;
    int eff_ready;

    int n_threads;
    int w;
    int h;
} swirl_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t mix_u8(uint8_t a, uint8_t b, int q8)
{
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}



static inline int swirl_smooth_i(float *state, int target, float attack, float release)
{
    const float cur = *state;
    const float diff = (float)target - cur;
    const float step = (diff > 0.0f) ? attack : release;
    const float out = cur + diff * step;

    *state = out;

    return (int)(out + (out >= 0.0f ? 0.5f : -0.5f));
}

static inline int swirl_drive_degrees(int degrees, int swirl_drive)
{
    swirl_drive = clampi(swirl_drive, 0, 1000);

    int delta = (swirl_drive * 220 + 500) / 1000;

    if(delta < 1 && swirl_drive > 0)
        delta = 1;

    return clampi(degrees - delta, 1, 360);
}

vj_effect *swirl_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = SWIRL_PARAMS;

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

    ve->limits[0][P_DEGREES] = 1;
    ve->limits[1][P_DEGREES] = 360;
    ve->defaults[P_DEGREES] = 250;

    ve->limits[0][P_MODE] = 0;
    ve->limits[1][P_MODE] = 1;
    ve->defaults[P_MODE] = 0;

    ve->limits[0][P_SWIRL_DRIVE] = 0;
    ve->limits[1][P_SWIRL_DRIVE] = 1000;
    ve->defaults[P_SWIRL_DRIVE] = 0;

    ve->description = "Swirl";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Degrees",
        "Mode",
        "Swirl Drive"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Normal",
        "Mirrored"
    );

    
    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_WARP,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 24,                 360,                12, 46, 1000, 3600, 0,    72,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_WARP,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                       140,                1000,               16, 62,  700, 2800, 0,    94
    );


    return ve;
}

void *swirl_malloc(int w, int h)
{
    swirl_t *s = (swirl_t*) vj_calloc(sizeof(swirl_t));
    if(!s)
        return NULL;

    const int len = w * h;
    const int w2 = w >> 1;
    const int h2 = h >> 1;
    const size_t dbytes = sizeof(double) * (size_t)len;
    const size_t ibytes = sizeof(int) * (size_t)len;
    const size_t fbytes = (size_t)len * 3u;
    const size_t total = dbytes + dbytes + ibytes + ibytes + fbytes + 96u;

    s->region = (uint8_t*) vj_malloc(total);
    if(!s->region) {
        free(s);
        return NULL;
    }

    uint8_t *p = s->region;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->polar_map = (double*)p;
    p += dbytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->fish_angle = (double*)p;
    p += dbytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->cached_coords = (int*)p;
    p += ibytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->drive_coords = (int*)p;
    p += ibytes;

    p = (uint8_t*)(((uintptr_t)p + 15u) & ~(uintptr_t)15u);
    s->buf[0] = p;
    s->buf[1] = s->buf[0] + len;
    s->buf[2] = s->buf[1] + len;

    for(int y = 0; y < h; y++) {
        const int dy = y - h2;
        const int row = y * w;

        for(int x = 0; x < w; x++) {
            const int dx = x - w2;
            const int i = row + x;

            s->polar_map[i] = sqrt((double)(dy * dy + dx * dx));
            s->fish_angle[i] = atan2((double)dy, (double)dx);
            s->cached_coords[i] = i;
            s->drive_coords[i] = i;
        }
    }

    s->v = -1;
    s->mode = -1;
    s->drive_v = -1;
    s->drive_swirl = -1;
    s->eff_degrees = 250.0f;
    s->eff_swirl_drive = 0.0f;
    s->eff_ready = 0;
    s->w = w;
    s->h = h;

    s->n_threads = vje_advise_num_threads(len);

    return (void*) s;
}

void swirl_free(void *ptr)
{
    swirl_t *s = (swirl_t*) ptr;

    free(s->region);
    free(s);
}

static void swirl_rebuild_map(swirl_t *s, int width, int height, int degrees, int mode, int *restrict coords)
{
    const int len = width * height;
    const int w2 = width >> 1;
    const int h2 = height >> 1;
    const double coeff = (double)clampi(degrees, 1, 360);

    double *restrict polar_map = s->polar_map;
    double *restrict fish_angle = s->fish_angle;

    if(mode == 0) {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            double co;
            double si;

            const double r = polar_map[i];
            const double a = fish_angle[i];

            sin_cos(co, si, a + (r / coeff));

            int px = (int)(r * co) + w2;
            int py = (int)(r * si) + h2;

            px = clampi(px, 0, width - 1);
            py = clampi(py, 0, height - 1);

            coords[i] = py * width + px;
        }
    } else {
#pragma omp for schedule(static)
        for(int y = 0; y < height; y++) {
            const int row = y * width;
            const int my = (y <= h2) ? y : (height - 1 - y);

            for(int x = 0; x < width; x++) {
                double co;
                double si;

                const int mx = (x <= w2) ? x : (width - 1 - x);
                const int qidx = my * width + mx;
                const int idx = row + x;

                const double r = polar_map[qidx];
                const double a = fish_angle[qidx];

                sin_cos(co, si, a + (r / coeff));

                int px = (int)(r * co) + w2;
                int py = (int)(r * si) + h2;

                px = clampi(px, 0, width - 1);
                py = clampi(py, 0, height - 1);

                coords[idx] = py * width + px;
            }
        }
    }
}



void swirl_apply(void *ptr, VJFrame *frame, int *args)
{
    swirl_t *s = (swirl_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int degrees_arg = args[P_DEGREES];
    const int mode = args[P_MODE];
    const int swirl_drive_arg = args[P_SWIRL_DRIVE];


    const float param_attack = 0.28f;
    const float param_release = 0.095f;

    if(!s->eff_ready) {
        s->eff_degrees = (float)degrees_arg;
        s->eff_swirl_drive = (float)swirl_drive_arg;
        s->eff_ready = 1;
    } else {
        swirl_smooth_i(&s->eff_degrees, degrees_arg, param_attack, param_release);
        swirl_smooth_i(&s->eff_swirl_drive, swirl_drive_arg, param_attack * 1.16f, param_release);
    }

    const int degrees = clampi((int)(s->eff_degrees + 0.5f), 1, 360);
    const int swirl_drive = clampi((int)(s->eff_swirl_drive + 0.5f), 0, 1000);
    const int drive_degrees = swirl_drive_degrees(degrees, swirl_drive);

    const int rebuild_base = (s->v != degrees || s->mode != mode);
    const int rebuild_drive = (s->drive_v != drive_degrees || s->mode != mode || s->drive_swirl != swirl_drive);

    int drive_q8 = (swirl_drive * 256 + 500) / 1000;
    drive_q8 = clampi(drive_q8, 0, 256);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict srcY  = s->buf[0];
    uint8_t *restrict srcCb = s->buf[1];
    uint8_t *restrict srcCr = s->buf[2];

    veejay_memcpy(srcY,  Y,  len);
    veejay_memcpy(srcCb, Cb, len);
    veejay_memcpy(srcCr, Cr, len);

    int *restrict base_coords = s->cached_coords;
    int *restrict drive_coords = s->drive_coords;

#pragma omp parallel num_threads(s->n_threads)
    {
        if(rebuild_base)
            swirl_rebuild_map(s, width, height, degrees, mode, s->cached_coords);

        if(rebuild_drive)
            swirl_rebuild_map(s, width, height, drive_degrees, mode, s->drive_coords);

#pragma omp single
        {
            s->v = degrees;
            s->mode = mode;
            s->drive_v = drive_degrees;
            s->drive_swirl = swirl_drive;
        }

        if(drive_q8 <= 0 || drive_degrees == degrees) {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int idx = base_coords[i];

                Y[i]  = srcY[idx];
                Cb[i] = srcCb[idx];
                Cr[i] = srcCr[idx];
            }
        } else {
#pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                const int a = base_coords[i];
                const int b = drive_coords[i];

                Y[i]  = mix_u8(srcY[a],  srcY[b],  drive_q8);
                Cb[i] = mix_u8(srcCb[a], srcCb[b], drive_q8);
                Cr[i] = mix_u8(srcCr[a], srcCr[b], drive_q8);
            }
        }
    }
}
