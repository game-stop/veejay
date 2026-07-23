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
#include "charcoalsketch.h"

typedef struct {
    uint8_t *temp_Y;
    uint8_t *blur_Y;
    int n_threads;
} charcoal_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *charcoalsketch_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 3;
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 1; ve->limits[1][0] = 50;  ve->defaults[0] = 7;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 180;
    ve->limits[0][2] = 0; ve->limits[1][2] = 64;  ve->defaults[2] = 10;

    ve->param_description = vje_build_param_list(ve->num_params, "Stroke Thickness", "Intensity", "Grain Level");
    ve->description = "Charcoal Sketch";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 0;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 3, 36, 82, 100, 8, 520, 0, 1, 80, VJ_BEAT_COST_MODERATE, 82, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_CONTRAST, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 96, 255, 94, 100, 10, 520, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_TURBULENCE, VJ_BEAT_F_CONTINUOUS, VJ_BEAT_SRC_HIGH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 0, 56, 72, 98, 80, 900, 0, 1, 0, VJ_BEAT_COST_CHEAP, 76, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *charcoalsketch_malloc(int w, int h)
{
    charcoal_t *c = (charcoal_t*) vj_calloc(sizeof(charcoal_t));

    if(!c)
        return NULL;

    c->temp_Y = (uint8_t*) vj_malloc(w * h * sizeof(uint8_t));

    if(!c->temp_Y) {
        free(c);
        return NULL;
    }

    c->blur_Y = (uint8_t*) vj_malloc(w * h * sizeof(uint8_t));

    if(!c->blur_Y) {
        free(c->temp_Y);
        free(c);
        return NULL;
    }

    c->n_threads = vje_advise_num_threads(w * h);

    return c;
}

void charcoalsketch_free(void *ptr)
{
    charcoal_t *c = (charcoal_t*) ptr;

    if(!c)
        return;

    if(c->temp_Y)
        free(c->temp_Y);

    if(c->blur_Y)
        free(c->blur_Y);

    free(c);
}

void charcoalsketch_apply(void *ptr, VJFrame *frame, int *args)
{
    charcoal_t *c = (charcoal_t*) ptr;

    const int radius = args[0];
    const int intensity = args[1];
    const int grain = args[2];
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict temp_Y = c->temp_Y;
    uint8_t *restrict blur_Y = c->blur_Y;

    const int diameter = radius * 2 + 1;

#pragma omp parallel num_threads(c->n_threads)
    {
#pragma omp for schedule(static)
        for(int y = 0; y < height; y++)
        {
            int sum = 0;
            const int row_offset = y * width;

            for(int x = -radius; x <= radius; x++)
            {
                const int nx = x < 0 ? 0 : (x >= width ? width - 1 : x);
                sum += Y[row_offset + nx];
            }

            for(int x = 0; x < width; x++)
            {
                const int t_x = x - radius;
                const int h_x = x + radius + 1;

                temp_Y[row_offset + x] = (uint8_t)(sum / diameter);
                sum += Y[row_offset + (h_x >= width ? width - 1 : h_x)] - Y[row_offset + (t_x < 0 ? 0 : t_x)];
            }
        }

#pragma omp for schedule(static)
        for(int x = 0; x < width; x++)
        {
            int sum = 0;

            for(int y = -radius; y <= radius; y++)
            {
                const int ny = y < 0 ? 0 : (y >= height ? height - 1 : y);
                sum += temp_Y[ny * width + x];
            }

            for(int y = 0; y < height; y++)
            {
                const int t_y = y - radius;
                const int h_y = y + radius + 1;

                blur_Y[y * width + x] = (uint8_t)(sum / diameter);
                sum += temp_Y[(h_y >= height ? height - 1 : h_y) * width + x] - temp_Y[(t_y < 0 ? 0 : t_y) * width + x];
            }
        }

#pragma omp for schedule(static)
        for(int y = 0; y < height; y++)
        {
            uint32_t seed = (uint32_t)y * 0x9E3779B9u + (uint32_t)grain;

            for(int x = 0; x < width; x++)
            {
                const int i = y * width + x;
                const int orig = Y[i];
                const int blur = blur_Y[i];

                int sketch = (orig << 8) / (blur + 1);
                sketch = (sketch * intensity) >> 8;

                if(grain > 0)
                {
                    seed = (seed * 1103515245u + 12345u) & 0x7fffffffu;
                    sketch += (int)(seed % ((uint32_t)grain + 1u)) - (grain >> 1);
                }

                Y[i] = (uint8_t)clampi(sketch, 0, 255);
            }
        }
    }

    if(frame->data[1] && frame->data[2])
    {
        const int uv_len = frame->uv_len;

        veejay_memset(frame->data[1], 128, uv_len);
        veejay_memset(frame->data[2], 128, uv_len);
    }
}
