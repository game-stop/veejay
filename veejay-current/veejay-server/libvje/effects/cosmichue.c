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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "cosmichue.h"

vj_effect *cosmichue_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 4;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params); /* default values */
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* min */
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);    /* max */
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 2000;
    ve->defaults[0] = 100;

    ve->limits[0][1] = 0;
    ve->limits[1][1] = 2000;
    ve->defaults[1] = 100;

    ve->limits[0][2] = 0;
    ve->limits[1][2] = 255;
    ve->defaults[2] = 100;

    ve->limits[0][3] = 0;
    ve->limits[1][3] = 3600;
    ve->defaults[3] = 0;

    ve->description = "Cosmic Hue";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 1;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list( ve->num_params, "Amplitude", "Frequency", "Opacity", "Hue Shift" );
    return ve;
}
  

void cosmichue_apply(void *ptr, VJFrame *frame, int *args)
{
    const int opacity = args[2];
    const int len = frame->len;

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    const float amplitude = args[0] * 0.1f;
    const float frequency = args[1] * 0.1f;

    float hue_shift = args[3] * 0.1f;
    hue_shift *= (float)(M_PI / 180.0);
    hue_shift = fmodf(hue_shift, (float)(2.0 * M_PI));
    if (hue_shift < 0.0f)
        hue_shift += (float)(2.0 * M_PI);

    float sin_lut[256];
    float cos_lut[256];
    float hsin_lut[256];
    float hcos_lut[256];

    const float angle_step = (float)(2.0 * M_PI / 256.0);

    for (int i = 0; i < 256; i++) {
        float luminance = i / 255.0f;
        float angle = i * angle_step;

        sin_lut[i]  = amplitude * a_sin(frequency * luminance);
        cos_lut[i]  = amplitude * a_cos(frequency * luminance);
        hsin_lut[i] = a_sin(angle);
        hcos_lut[i] = a_cos(angle);
    }

    const int angle_index =
        (int)((hue_shift / (float)(2.0 * M_PI)) * 256.0f) & 0xFF;

    const float cos_val = hcos_lut[angle_index];
    const float sin_val = hsin_lut[angle_index];

    for (int i = 0; i < len; i++) {
        int u = (int)U[i] - 128;
        int v = (int)V[i] - 128;

        float u_offset = sin_lut[Y[i]];
        float v_offset = cos_lut[Y[i]];

        u = (int)(u + u_offset);
        v = (int)(v + v_offset);

        int u_rot = 128 + (int)(u * cos_val - v * sin_val);
        int v_rot = 128 + (int)(u * sin_val + v * cos_val);

        u_rot = (u_rot + (u_rot >> 31)) ^ (u_rot >> 31);
        v_rot = (v_rot + (v_rot >> 31)) ^ (v_rot >> 31);

        U[i] = (uint8_t)((opacity * u_rot + (255 - opacity) * U[i]) >> 8);
        V[i] = (uint8_t)((opacity * v_rot + (255 - opacity) * V[i]) >> 8);
    }
}
