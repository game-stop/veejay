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
#include "rotozoom.h"

typedef struct {
    uint8_t *rotobuffer[3];
    float sin_lut[360];
    float cos_lut[360];
    double zoom;
    double rotate;
    int frameCount;
    int direction;
    int n_threads;
} rotozoom_t;

vj_effect *rotozoom_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 30;
    ve->defaults[1] = 2;
    ve->defaults[2] = 1;
    ve->defaults[3] = 100;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 360;

    ve->limits[0][1] = -1000;
    ve->limits[1][1] = 1000;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 1;

    ve->limits[0][3] = 1;
    ve->limits[1][3] = 1500;

    ve->description = "Rotozoom";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Rotate",
        "Zoom",
        "Automatic",
        "Duration"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][2],
        2,
        "Manual",
        "Automatic"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,          VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_WRAP,      0,                  360,                8, 30, 1200, 3000, 0,   45,    /* Rotate */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS,                       -220,               420,                8, 30, 1200, 3000, 0,   50,    /* Zoom */
        VJ_BEAT_SELECTOR,      VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0,  0,    0,    0,   -1000, /* Automatic */
        VJ_BEAT_SPEED,         VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 24,                 360,                6, 22, 1800, 4200, 900, 30     /* Duration */
    );

    (void) width;
    (void) height;

    return ve;
}

void *rotozoom_malloc(int width, int height)
{
    rotozoom_t *r = (rotozoom_t*) vj_calloc(sizeof(rotozoom_t));
    if(!r)
        return NULL;

    const int len = width * height;

    r->rotobuffer[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
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

    for(int i = 0; i < 360; i++) {
        const double rad = (double)i * M_PI / 180.0;
        r->sin_lut[i] = a_sin(rad);
        r->cos_lut[i] = a_cos(rad);
    }

    r->n_threads = vje_advise_num_threads(len);
    if(r->n_threads < 1)
        r->n_threads = 1;

    return (void*) r;
}

void rotozoom_free(void *ptr)
{
    rotozoom_t *r = (rotozoom_t*) ptr;
    if(!r)
        return;

    if(r->rotobuffer[0])
        free(r->rotobuffer[0]);

    free(r);
}

static inline int rotozoom_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline int rotozoom_wrap360(double v)
{
    int a = (int)v % 360;

    if(a < 0)
        a += 360;

    return a;
}

static inline double rotozoom_scale_from_arg(double zoom_arg)
{
    if(zoom_arg > 0.0)
        return 1.0 / (1.0 + zoom_arg / 100.0);

    if(zoom_arg < 0.0)
        return pow(2.0, -zoom_arg / 200.0);

    return 1.0;
}

void rotozoom_apply(void *ptr, VJFrame *frame, int *args)
{
    rotozoom_t *r = (rotozoom_t*) ptr;
    if(!r || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    double rotate = (double)rotozoom_clampi(args[0], 0, 360);
    double zoom_arg = (double)rotozoom_clampi(args[1], -1000, 1000);
    int autom = args[2] ? 1 : 0;
    int maxFrames = rotozoom_clampi(args[3], 1, 1500);

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
    } else {
        r->zoom = zoom_arg;
        r->rotate = rotate;
        r->frameCount = 0;
        r->direction = 1;
    }

    const double zoom = rotozoom_scale_from_arg(zoom_arg);

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    uint8_t *restrict srcY = r->rotobuffer[0];
    uint8_t *restrict srcU = r->rotobuffer[1];
    uint8_t *restrict srcV = r->rotobuffer[2];

    veejay_memcpy(srcY, dstY, len);
    veejay_memcpy(srcU, dstU, len);
    veejay_memcpy(srcV, dstV, len);

    const float centerX = ((float)width  - 1.0f) * 0.5f;
    const float centerY = ((float)height - 1.0f) * 0.5f;

    const int angle = rotozoom_wrap360(rotate);
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

            newX = rotozoom_clampi(newX, 0, width - 1);
            newY = rotozoom_clampi(newY, 0, height - 1);

            const int srcIndex = newY * width + newX;
            const int dstIndex = row + x;

            dstY[dstIndex] = srcY[srcIndex];
            dstU[dstIndex] = srcU[srcIndex];
            dstV[dstIndex] = srcV[srcIndex];
        }
    }
}