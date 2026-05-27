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

/* This effect recalculates the coordinate map when waves, amplitude,
 * or attenuation changes. Frame data is copied to a scratch buffer,
 * then sampled through the cached ripple map.
 */

#include "common.h"
#include "ripple.h"

#define RIPPLE_DEGREES 360
#define RIPPLE_PI 3.14159265358979323846

typedef struct {
    int *ripple_table;
    uint8_t *ripple_data[3];
    float *ripple_sin;
    float *ripple_cos;
    int ripple_waves;
    int ripple_ampli;
    int ripple_attn;
    int n_threads;
} ripple_t;

vj_effect *ripple_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 3;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1;
    ve->limits[1][0] = 3600;
    ve->defaults[0] = 132;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = 80;
    ve->defaults[1] = 47;

    ve->limits[0][2] = 1;
    ve->limits[1][2] = 360;
    ve->defaults[2] = 7;

    ve->description = "Ripple";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Waves",
        "Amplitude",
        "Attenuation"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,

        VJ_BEAT_WARP,          VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE, 40,  760, 6, 22, 1800, 4200, 900, 30, /* Waves */
        VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE, 8,   64,  6, 22, 1800, 4200, 900, 30, /* Amplitude */
        VJ_BEAT_DETAIL,        VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE, 4,   80,  6, 22, 1600, 3400, 700, 30  /* Attenuation */
    );

    (void) width;
    (void) height;

    return ve;
}

void *ripple_malloc(int width, int height)
{
    ripple_t *r = (ripple_t*) vj_calloc(sizeof(ripple_t));
    if(!r)
        return NULL;

    const int len = width * height;

    r->ripple_table = (int*) vj_malloc(sizeof(int) * len);
    if(!r->ripple_table) {
        free(r);
        return NULL;
    }

    r->ripple_data[0] = (uint8_t*) vj_malloc((size_t)len * 3u);
    if(!r->ripple_data[0]) {
        free(r->ripple_table);
        free(r);
        return NULL;
    }

    r->ripple_data[1] = r->ripple_data[0] + len;
    r->ripple_data[2] = r->ripple_data[1] + len;

    r->ripple_sin = (float*) vj_malloc(sizeof(float) * RIPPLE_DEGREES);
    if(!r->ripple_sin) {
        free(r->ripple_data[0]);
        free(r->ripple_table);
        free(r);
        return NULL;
    }

    r->ripple_cos = (float*) vj_malloc(sizeof(float) * RIPPLE_DEGREES);
    if(!r->ripple_cos) {
        free(r->ripple_sin);
        free(r->ripple_data[0]);
        free(r->ripple_table);
        free(r);
        return NULL;
    }

    for(int i = 0; i < RIPPLE_DEGREES; i++) {
        const float rad = (float)((2.0 * RIPPLE_PI * (double)i) / (double)RIPPLE_DEGREES);
        r->ripple_sin[i] = sinf(rad);
        r->ripple_cos[i] = cosf(rad);
    }

    for(int i = 0; i < len; i++)
        r->ripple_table[i] = i;

    veejay_memset(r->ripple_data[0], pixel_Y_lo_, len);
    veejay_memset(r->ripple_data[1], 128, len);
    veejay_memset(r->ripple_data[2], 128, len);

    r->ripple_waves = -1;
    r->ripple_ampli = -1;
    r->ripple_attn = -1;

    r->n_threads = vje_advise_num_threads(len);
    if(r->n_threads < 1)
        r->n_threads = 1;

    return (void*) r;
}

void ripple_free(void *ptr)
{
    ripple_t *r = (ripple_t*) ptr;
    if(!r)
        return;

    if(r->ripple_table)
        free(r->ripple_table);
    if(r->ripple_sin)
        free(r->ripple_sin);
    if(r->ripple_cos)
        free(r->ripple_cos);
    if(r->ripple_data[0])
        free(r->ripple_data[0]);

    free(r);
}

static inline int ripple_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static void ripple_build_table(ripple_t *r, int width, int height, int waves_arg, int amplitude_arg, int attenuation_arg)
{
    const float cx = ((float)width - 1.0f) * 0.5f;
    const float cy = ((float)height - 1.0f) * 0.5f;
    const float maxradius = sqrtf(cx * cx + cy * cy);

    const float waves = (float)waves_arg * 0.1f;
    const float ampli = (float)amplitude_arg * 0.1f;
    const float attenuation = (float)attenuation_arg * 0.1f;

    const float frequency = (maxradius > 0.0f) ? ((float)RIPPLE_DEGREES * waves / maxradius) : 0.0f;
    const float amplitude = (ampli > 0.0001f) ? (maxradius / ampli) : 0.0f;

    int *restrict table = r->ripple_table;
    float *restrict sin_lut = r->ripple_sin;
    float *restrict cos_lut = r->ripple_cos;

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int y = 0; y < height; y++) {
        for(int x = 0; x < width; x++) {
            const int idx = y * width + x;

            const float dx = (float)x - cx;
            const float dy = (float)y - cy;
            const float dist2 = dx * dx + dy * dy;

            if(dist2 <= 0.000001f) {
                table[idx] = idx;
                continue;
            }

            const float radius = sqrtf(dist2);
            int angle = (int)((atan2f(dy, dx) * 180.0f) / (float)RIPPLE_PI);

            if(angle < 0)
                angle += RIPPLE_DEGREES;

            angle %= RIPPLE_DEGREES;

            const int wave_index = ((int)(frequency * radius)) % RIPPLE_DEGREES;
            const float denom = powf(radius, attenuation);
            const float z = (denom > 0.000001f) ? ((amplitude / denom) * sin_lut[wave_index]) : 0.0f;

            int sx = (int)((float)x + z * cos_lut[angle]);
            int sy = (int)((float)y + z * sin_lut[angle]);

            sx = ripple_clampi(sx, 0, width - 1);
            sy = ripple_clampi(sy, 0, height - 1);

            table[idx] = sy * width + sx;
        }
    }

    r->ripple_waves = waves_arg;
    r->ripple_ampli = amplitude_arg;
    r->ripple_attn = attenuation_arg;
}

void ripple_apply(void *ptr, VJFrame *frame, int *args)
{
    ripple_t *r = (ripple_t*) ptr;
    if(!r || !frame || !args || !frame->data[0] || !frame->data[1] || !frame->data[2])
        return;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if(width <= 0 || height <= 0 || len <= 0)
        return;

    int waves_arg = ripple_clampi(args[0], 1, 3600);
    int amplitude_arg = ripple_clampi(args[1], 1, 80);
    int attenuation_arg = ripple_clampi(args[2], 1, 360);

    uint8_t *restrict Y  = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    veejay_memcpy(r->ripple_data[0], Y, len);
    veejay_memcpy(r->ripple_data[1], Cb, len);
    veejay_memcpy(r->ripple_data[2], Cr, len);

    if(r->ripple_waves != waves_arg ||
       r->ripple_ampli != amplitude_arg ||
       r->ripple_attn != attenuation_arg)
    {
        ripple_build_table(r, width, height, waves_arg, amplitude_arg, attenuation_arg);
    }

    int *restrict table = r->ripple_table;
    uint8_t *restrict srcY  = r->ripple_data[0];
    uint8_t *restrict srcCb = r->ripple_data[1];
    uint8_t *restrict srcCr = r->ripple_data[2];

#pragma omp parallel for schedule(static) num_threads(r->n_threads)
    for(int i = 0; i < len; i++) {
        const int src = table[i];

        Y[i]  = srcY[src];
        Cb[i] = srcCb[src];
        Cr[i] = srcCr[src];
    }
}