/* 
 * Linux VeeJay
 *
 * Copyright(C)2004 Niels Elburg <nwelburg@gmail.com>
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
#include "ripplewave.h"

vj_effect *ripplewave_init(int w, int h)
{
    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 10;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 100;
    ve->defaults[1] = 15;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 45;
    ve->defaults[2] = 30;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 100;
    ve->defaults[3] = 10;

    ve->description = "Wave Patterns (H/V)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Frequency X",
        "Frequency Y",
        "Amplitude",
        "Speed"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,          VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 0, 72, 6, 22, 1800, 4200, 900, 30, /* Frequency X */
        VJ_BEAT_WARP,          VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 1, 72, 6, 22, 1800, 4200, 900, 30, /* Frequency Y */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS,                       0, 38, 8, 30, 1200, 3000, 0,   45, /* Amplitude */
        VJ_BEAT_SPEED,         VJ_BEAT_F_CONTINUOUS,                       0, 64, 8, 30, 1200, 3000, 0,   45  /* Speed */
    );

    (void) w;
    (void) h;

    return ve;
}

typedef struct {
    uint8_t *buf[3];
    float *lut_x;
    float *lut_y;
    int width;
    int height;
    float phase;
    int n_threads;
} ripplewave_t;

void *ripplewave_malloc(int w, int h)
{
    ripplewave_t *data = (ripplewave_t*) vj_calloc(sizeof(ripplewave_t));
    if(!data)
        return NULL;

    const int len = w * h;

    data->buf[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!data->buf[0]) {
        free(data);
        return NULL;
    }

    data->lut_x = (float*) vj_malloc(sizeof(float) * w);
    if(!data->lut_x) {
        free(data->buf[0]);
        free(data);
        return NULL;
    }

    data->lut_y = (float*) vj_malloc(sizeof(float) * h);
    if(!data->lut_y) {
        free(data->lut_x);
        free(data->buf[0]);
        free(data);
        return NULL;
    }

    data->buf[1] = data->buf[0] + len;
    data->buf[2] = data->buf[1] + len;

    data->width = w;
    data->height = h;
    data->phase = 0.0f;

    data->n_threads = vje_advise_num_threads(len);
    if(data->n_threads < 1)
        data->n_threads = 1;

    return (void*) data;
}

void ripplewave_free(void *ptr)
{
    ripplewave_t *data = (ripplewave_t*) ptr;
    if(!data)
        return;

    if(data->buf[0])
        free(data->buf[0]);
    if(data->lut_x)
        free(data->lut_x);
    if(data->lut_y)
        free(data->lut_y);

    free(data);
}

static inline int ripplewave_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

void ripplewave_apply(void *ptr, VJFrame *frame, int *args)
{
    ripplewave_t *data = (ripplewave_t*)ptr;

    if(!data || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int freq_x_arg = ripplewave_clampi(args[0], 0, 100);
    int freq_y_arg = ripplewave_clampi(args[1], 1, 100);
    int amp_arg    = ripplewave_clampi(args[2], 0, 45);
    int speed_arg  = ripplewave_clampi(args[3], 0, 100);

    const float frequency_x = (float)freq_x_arg * 0.01f;
    const float frequency_y = (float)freq_y_arg * 0.01f;
    const float amplitude = (float)amp_arg;

    if(speed_arg > 0) {
        data->phase += (float)speed_arg * 0.01f;
        if(data->phase > 628.3185f)
            data->phase -= 628.3185f;
    }

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict dstY = data->buf[0];
    uint8_t *restrict dstU = data->buf[1];
    uint8_t *restrict dstV = data->buf[2];

    float *restrict lut_x = data->lut_x;
    float *restrict lut_y = data->lut_y;

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int y = 0; y < height; y++) {
        lut_y[y] = a_sin(frequency_y * (float)y + data->phase);
    }

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int x = 0; x < width; x++) {
        lut_x[x] = a_cos(frequency_x * (float)x + data->phase);
    }

#pragma omp parallel for schedule(static) num_threads(data->n_threads)
    for(int y = 0; y < height; y++) {
        const int row = y * width;
        const int offset_y = (int)(amplitude * lut_y[y]);

        for(int x = 0; x < width; x++) {
            const int offset_x = (int)(amplitude * lut_x[x]);

            int sx = x + offset_x;
            int sy = y + offset_y;

            sx = ripplewave_clampi(sx, 0, width - 1);
            sy = ripplewave_clampi(sy, 0, height - 1);

            const int src = sy * width + sx;
            const int dst = row + x;

            dstY[dst] = Y[src];
            dstU[dst] = U[src];
            dstV[dst] = V[src];
        }
    }

    veejay_memcpy(Y, dstY, len);
    veejay_memcpy(U, dstU, len);
    veejay_memcpy(V, dstV, len);
}