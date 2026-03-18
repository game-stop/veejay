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
#include <veejaycore/vjmem.h>
#include "spectralmotion.h"

vj_effect *spectralmotion_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->defaults[0] = 150;
    ve->defaults[1] = 10;
    ve->defaults[2] = 150;
    ve->defaults[3] = 0;
    ve->defaults[4] = 8;
    ve->defaults[5] = 200;
    ve->defaults[6] = 180;
    ve->defaults[7] = 256;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 255;
    ve->limits[0][1] = 32;
    ve->limits[1][1] = 224;
    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;
    ve->limits[1][3] = 2;
    ve->limits[0][4] = 1;
    ve->limits[1][4] = 120;
    ve->limits[0][5] = 0;
    ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;
    ve->limits[1][6] = 255;
    ve->limits[0][7] = 0;
    ve->limits[1][7] = 1024;

    ve->description = "Spectral Motion Trail";
    
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user =0;
    ve->parallel = 0;
    
    ve->param_description = vje_build_param_list( ve->num_params, 
        "Trigger", "Cycle speed", "Opacity", "Mode", "Trail Decay", "Persistence", "Motion Boost", "Motion Gain" );

    return ve;
}

typedef struct {
    uint8_t *buf[5];
    uint8_t rainbow[256][3];
    int timestamp;
    int n_threads;
    float smooth_threshold;
    float phase;
} spectralmotion_t;

static void spectralmotion_build_rainbow(uint8_t lut[256][3])
{
    for(int i = 0; i < 256; i++)
    {
        float h = i * (6.2831853f / 256.0f);

        int Y = 140 + (int)(40.0f * sinf(h * 0.5f));
        int U = 128 + (int)(90.0f * sinf(h));
        int V = 128 + (int)(90.0f * cosf(h));

        lut[i][0] = CLAMP_Y(Y);
        lut[i][1] = CLAMP_UV(U);
        lut[i][2] = CLAMP_UV(V);
    }
}

void *spectralmotion_malloc(int w, int h) {
    spectralmotion_t *s = (spectralmotion_t*) vj_calloc(sizeof(spectralmotion_t));
    if(!s) return NULL;
    
    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * w * h * 5);
    if(!s->buf[0]) {
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + (w * h);
    s->buf[2] = s->buf[1] + (w * h);
    s->buf[3] = s->buf[2] + (w * h);
    s->buf[4] = s->buf[3] + (w * h);

    veejay_memset( s->buf[0], 0, (w*h * 2));
    veejay_memset( s->buf[2], 128, (w*h * 2));
    veejay_memset( s->buf[4], 0, (w*h));

    s->n_threads = vje_advise_num_threads(w*h);
    spectralmotion_build_rainbow(s->rainbow);
    return (void*) s;
}

void spectralmotion_free(void *ptr) {
    spectralmotion_t *s = (spectralmotion_t*) ptr;
    if(s) {
        if(s->buf[0]) free(s->buf[0]);
        free(s);
    }
}

static inline void spectralmotion_output_full(
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    uint8_t *restrict vY,
    uint8_t *restrict vU,
    uint8_t *restrict vV,
    int len,
    int n_threads)
{
#pragma omp parallel for simd num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        Y[i] = vY[i];
        U[i] = vU[i];
        V[i] = vV[i];
    }
}

static inline void spectralmotion_output_overlay(
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    uint8_t *restrict vY,
    uint8_t *restrict vU,
    uint8_t *restrict vV,
    int opacity,
    int len,
    int n_threads)
{
    const int inv_o = 255 - opacity;

#pragma omp parallel for simd num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        Y[i] = (uint8_t)((Y[i] * inv_o + vY[i] * opacity) >> 8);

        U[i] = (uint8_t)(((((int)U[i]-128) * inv_o + ((int)vU[i]-128) * opacity) >> 8) + 128);

        V[i] = (uint8_t)(((((int)V[i]-128) * inv_o + ((int)vV[i]-128) * opacity) >> 8) + 128);
    }
}

static inline void spectralmotion_output_debug(
    uint8_t *restrict Y,
    uint8_t *restrict U,
    uint8_t *restrict V,
    uint8_t *restrict mY,
    int len)
{
#pragma omp parallel for simd schedule(static)
    for(int i = 0; i < len; i++)
    {
        int diff = abs((int)Y[i] - (int)mY[i]);

        Y[i] = CLAMP_Y(diff * 2);
        U[i] = 128;
        V[i] = 128;
    }
}

void spectralmotion_apply(void *ptr, VJFrame *frame, int *args)
{
    spectralmotion_t *s = (spectralmotion_t*) ptr;

    const int sensitivity   = args[0]; 
    const int duration      = (args[1] > 0) ? args[1] : 1;
    const int opacity       = args[2];
    const int mode          = args[3]; 
    const int strobe_rate   = (args[4] > 0) ? args[4] : 1;
    const int persistence   = args[5];
    const int energy_persist= args[6];
    const int motion_gain   = args[7];
    const int adaptation = 256 - sensitivity;

    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict vY = s->buf[0];
    uint8_t *restrict mY = s->buf[1];
    uint8_t *restrict vU = s->buf[2];
    uint8_t *restrict vV = s->buf[3];
    uint8_t *restrict exc= s->buf[4];
    
    uint32_t Histogram[256] = {0};
    for(int i = 0; i < len; i += 16)
        Histogram[abs((int)Y[i] - (int)mY[i])]++;

    uint32_t raw_threshold = otsu_method(Histogram);

    s->smooth_threshold = (s->smooth_threshold * 0.85f) + ((float)raw_threshold * 0.15f);
    const int cutoff = (int)s->smooth_threshold + (128 - sensitivity);

    const int is_flash_frame = (s->timestamp % strobe_rate == 0);

    //float cycle_speed = powf(2.0f, (duration - 128) / 32.0f);
    float cycle_speed = powf(2.0f, (duration - 128) / 64.0f);

    int color_idx = (int)s->phase & 255;

    s->phase += cycle_speed;
    if (s->phase >= 256.0f)
        s->phase = fmodf(s->phase, 256.0f);

    uint8_t strobe_Y = s->rainbow[color_idx][0];
    int strobe_U = (int)s->rainbow[color_idx][1];
    int strobe_V = (int)s->rainbow[color_idx][2];

    if (s->timestamp == 0)
    {
        veejay_memcpy(mY, Y, len);
        veejay_memset(exc, 64, len);
    }

    #pragma omp parallel for simd num_threads(s->n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        int input_y = Y[i];
        int diff = abs(input_y - (int)mY[i]);

        // trail decay (visual buffer)
        int vy = (vY[i] * persistence) >> 8;
        int vu = (((int)vU[i] - 128) * persistence >> 8) + 128;
        int vv = (((int)vV[i] - 128) * persistence >> 8) + 128;

        // motion detection
        int over = diff - cutoff;
        over = (over > 0) ? over : 0;

        int excitation_raw = (over * motion_gain) >> 8;
        if (excitation_raw > 255) excitation_raw = 255;

        // temporal smoothing (energy buffer)
        int prev_exc = exc[i];
        int excitation = (prev_exc * energy_persist + excitation_raw * (256 - energy_persist)) >> 8;

        // soft strobe modulation
        int strobe = is_flash_frame ? 255 : 192; // keep baseline energy
        excitation = (excitation * strobe) >> 8;

        exc[i] = excitation;

        int inv_exc = 255 - excitation;

        int newY = (vy * inv_exc + strobe_Y * excitation) >> 8;

        int vu_c = vu - 128;
        int vv_c = vv - 128;

        int newU = ((vu_c * inv_exc + (strobe_U - 128) * excitation) >> 8) + 128;
        int newV = ((vv_c * inv_exc + (strobe_V - 128) * excitation) >> 8) + 128;

        vY[i] = (uint8_t)newY;
        vU[i] = (uint8_t)CLAMP_UV(newU);
        vV[i] = (uint8_t)CLAMP_UV(newV);

        // background adaptation
        mY[i] = ((mY[i] * (256 - adaptation)) + (input_y * adaptation)) >> 8;
    }

    switch(mode)
    {
        case 2:
            spectralmotion_output_debug(Y, U, V, mY, len);
            break;
        case 1:
            spectralmotion_output_overlay(Y, U, V, vY, vU, vV, opacity, len, s->n_threads);
            break;
        default:
            spectralmotion_output_full(Y, U, V, vY, vU, vV, len, s->n_threads);
            break;
    }

    s->timestamp++;
}

void spectralmotion_apply3(void *ptr, VJFrame *frame, int *args)
{
    spectralmotion_t *s = (spectralmotion_t*) ptr;

    const int sensitivity   = args[0]; 
    const int duration      = (args[1] > 0) ? args[1] : 1;
    const int opacity       = args[2];
    const int mode          = args[3]; 
    const int strobe_rate   = (args[4] > 0) ? args[4] : 1;
    const int persistence   = args[5];
    const int adaptation    = args[6];
    const int motion_gain   = args[7];

    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];

    uint8_t *restrict vY = s->buf[0];
    uint8_t *restrict mY = s->buf[1];
    uint8_t *restrict vU = s->buf[2];
    uint8_t *restrict vV = s->buf[3];

    uint32_t Histogram[256] = {0};
    for(int i = 0; i < len; i += 16)
        Histogram[abs((int)Y[i] - (int)mY[i])]++;

    uint32_t raw_threshold = otsu_method(Histogram);

    s->smooth_threshold = (s->smooth_threshold * 0.85f) + ((float)raw_threshold * 0.15f);
    //const int cutoff = (int)s->smooth_threshold + (255 - sensitivity);
    const int cutoff = (int)s->smooth_threshold + (128 - sensitivity);

    const int is_flash_frame = (s->timestamp % strobe_rate == 0);
    float cycle_speed = powf(2.0f, (duration - 128) / 32.0f);

    int color_idx = (int)s->phase & 255;

    s->phase += cycle_speed;
    if (s->phase >= 256.0f)
        s->phase = fmodf(s->phase, 256.0f);

    uint8_t strobe_Y = s->rainbow[color_idx][0];
    int strobe_U = (int)s->rainbow[color_idx][1];
    int strobe_V = (int)s->rainbow[color_idx][2];

    #pragma omp parallel for simd num_threads(s->n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        int input_y = Y[i];
        int diff = abs(input_y - (int)mY[i]);

        int vy = (vY[i] * persistence) >> 8;
        int vu = (((int)vU[i] - 128) * persistence >> 8) + 128;
        int vv = (((int)vV[i] - 128) * persistence >> 8) + 128;

        int over = diff - cutoff;
        over = (over > 0) ? over : 0;
        
        int excitation = (over * motion_gain) >> 4;
        if (excitation > 255) excitation = 255;

        int mask = -(is_flash_frame & (diff > cutoff));

        excitation = (excitation & mask);

        int inv_exc = 255 - excitation;

        int newY = (vy * inv_exc + strobe_Y * excitation) >> 8;

        int vu_c = vu - 128;
        int vv_c = vv - 128;

        int newU = ((vu_c * inv_exc + (strobe_U - 128) * excitation) >> 8) + 128;
        int newV = ((vv_c * inv_exc + (strobe_V - 128) * excitation) >> 8) + 128;

        vY[i] = (uint8_t)((newY & mask) | (vy & ~mask));
        vU[i] = (uint8_t)((CLAMP_UV(newU) & mask) | (vu & ~mask));
        vV[i] = (uint8_t)((CLAMP_UV(newV) & mask) | (vv & ~mask));

        mY[i] = ((mY[i] * (256 - adaptation)) + (input_y * adaptation)) >> 8;
    }

    switch(mode)
    {
        case 2:
            spectralmotion_output_debug(Y, U, V, mY, len);
            break;
        case 1:
            spectralmotion_output_overlay(Y, U, V, vY, vU, vV, opacity, len, s->n_threads);
            break;
        default:
            spectralmotion_output_full(Y, U, V, vY, vU, vV, len, s->n_threads);
            break;
    }

    s->timestamp++;

}
