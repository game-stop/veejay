/* * Linux VeeJay
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
#include "boxfit.h"

typedef struct
{
    uint8_t *buf[3];
    uint32_t *integralY;
    uint32_t *integralU;
    uint32_t *integralV;
    int n_threads;
} boxfit_t;

static inline int boxfit_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int boxfit_maxi(int a, int b)
{
    return a > b ? a : b;
}

vj_effect *boxfit_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    const int min_hi = boxfit_maxi(2, w / 8);
    const int max_hi = boxfit_maxi(4, w / 4);

    ve->limits[0][0] = 2; ve->limits[1][0] = min_hi; ve->defaults[0] = boxfit_clampi(8, 2, min_hi);
    ve->limits[0][1] = 4; ve->limits[1][1] = max_hi; ve->defaults[1] = boxfit_clampi(40, 4, max_hi);
    ve->limits[0][2] = 1; ve->limits[1][2] = 255;    ve->defaults[2] = 128;
    ve->limits[0][3] = 0; ve->limits[1][3] = 1;      ve->defaults[3] = 1;

    ve->description = "Box Accumulator";
    ve->sub_format = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Min Size", "Max Size", "Sensitivity", "Borders");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, min_hi, 72, 96, 40, 520, 0, 2, 90, VJ_BEAT_COST_MODERATE, 78, 1, 0, VJ_BEAT_GROUP_ASCENDING, 2),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 4, max_hi, 78, 100, 28, 620, 0, 4, 90, VJ_BEAT_COST_MODERATE, 88, 1, 1, VJ_BEAT_GROUP_ASCENDING, 2),
            VJ_BEAT_HINT_V2(VJ_BEAT_DETAIL, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_SMOOTHSTEP, 32, 240, 84, 100, 22, 420, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

void *boxfit_malloc(int w, int h)
{
    boxfit_t *s = (boxfit_t *)vj_calloc(sizeof(boxfit_t));

    if(!s)
        return NULL;

    const int plane_size = w * h;
    const int integral_size = (w + 1) * (h + 1);

    s->buf[0] = (uint8_t *)vj_malloc(plane_size * 3);
    s->integralY = (uint32_t *)vj_malloc(sizeof(uint32_t) * integral_size * 3);

    if(!s->buf[0] || !s->integralY) {
        if(s->buf[0])
            free(s->buf[0]);
        if(s->integralY)
            free(s->integralY);
        free(s);
        return NULL;
    }

    s->buf[1] = s->buf[0] + plane_size;
    s->buf[2] = s->buf[1] + plane_size;
    s->integralU = s->integralY + integral_size;
    s->integralV = s->integralU + integral_size;
    s->n_threads = vje_advise_num_threads(plane_size);

    return s;
}

void boxfit_free(void *ptr)
{
    boxfit_t *s = (boxfit_t *)ptr;

    if(!s)
        return;

    if(s->buf[0])
        free(s->buf[0]);

    if(s->integralY)
        free(s->integralY);

    free(s);
}

static inline uint32_t get_rect_sum(const uint32_t *intC, int stride, int x, int y, int rw, int rh)
{
    const int x2 = x + rw;
    const int y2 = y + rh;

    return intC[y2 * stride + x2] - intC[y * stride + x2] - intC[y2 * stride + x] + intC[y * stride + x];
}

void boxfit_apply(void *ptr, VJFrame *frame, int *args)
{
    boxfit_t *s = (boxfit_t *)ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    int min_s = boxfit_clampi(args[0], 2, boxfit_maxi(2, width / 8));
    int max_s = boxfit_clampi(args[1], 4, boxfit_maxi(4, width / 4));
    const int sensitivity = args[2];
    const int show_borders = args[3];

    if(max_s < min_s)
        max_s = min_s;

    const int stride = width + 1;

    int size_lut[256];

    for(int i = 0; i < 256; i++)
    {
        const float avg = (float)i + 0.001f;
        const float s_f = (float)sensitivity;
        const float detail = avg > s_f ? (s_f / avg) : (avg / s_f);
        const int sz = (((int)((float)max_s * detail) + 2) >> 2) << 2;

        size_lut[i] = boxfit_clampi(sz, min_s, max_s);
    }

    #pragma omp parallel for num_threads(s->n_threads) schedule(static)
    for(int c = 0; c < 3; c++)
    {
        const uint8_t *restrict src = frame->data[c];
        uint32_t *restrict intC = c == 0 ? s->integralY : (c == 1 ? s->integralU : s->integralV);

        for(int x = 0; x <= width; x++)
            intC[x] = 0;

        for(int y = 0; y < height; y++)
        {
            uint32_t row_sum = 0;
            uint32_t *restrict curr = intC + (y + 1) * stride;
            const uint32_t *restrict prev = intC + y * stride;

            curr[0] = 0;

            for(int x = 0; x < width; x++)
            {
                row_sum += src[y * width + x];
                curr[x + 1] = prev[x + 1] + row_sum;
            }
        }
    }

    int i = 0;

    while(i < height)
    {
        const int rem_h = height - i;
        const int sh = min_s < rem_h ? min_s : rem_h;
        const int sw0 = min_s < width ? min_s : width;
        const int r_area = sw0 * sh;
        int r_avg = get_rect_sum(s->integralY, stride, 0, i, sw0, sh) / r_area;
        int row_h = size_lut[r_avg];

        if(row_h > rem_h)
            row_h = rem_h;

        int j = 0;

        while(j < width)
        {
            const int rem_w = width - j;
            const int sw = min_s < rem_w ? min_s : rem_w;
            const int b_area = sw * row_h;
            int b_avg = get_rect_sum(s->integralY, stride, j, i, sw, row_h) / b_area;
            int box_w = size_lut[b_avg];

            if(box_w > rem_w)
                box_w = rem_w;

            const uint32_t area = (uint32_t)box_w * (uint32_t)row_h;
            const uint8_t valY = (uint8_t)(get_rect_sum(s->integralY, stride, j, i, box_w, row_h) / area);
            const uint8_t valU = (uint8_t)(get_rect_sum(s->integralU, stride, j, i, box_w, row_h) / area);
            const uint8_t valV = (uint8_t)(get_rect_sum(s->integralV, stride, j, i, box_w, row_h) / area);
            const uint8_t borderY = valY >> 1;

            for(int bi = 0; bi < row_h; bi++)
            {
                const int row_off = (i + bi) * width + j;

                uint8_t *restrict pY = frame->data[0] + row_off;
                uint8_t *restrict pU = frame->data[1] + row_off;
                uint8_t *restrict pV = frame->data[2] + row_off;

                if(show_borders)
                {
                    if(bi == 0 || bi == row_h - 1 || box_w <= 2)
                    {
                        for(int bk = 0; bk < box_w; bk++)
                            pY[bk] = borderY;
                    }
                    else
                    {
                        pY[0] = borderY;

                        for(int bk = 1; bk < box_w - 1; bk++)
                            pY[bk] = valY;

                        pY[box_w - 1] = borderY;
                    }
                }
                else
                {
                    for(int bk = 0; bk < box_w; bk++)
                        pY[bk] = valY;
                }

                for(int bk = 0; bk < box_w; bk++)
                {
                    pU[bk] = valU;
                    pV[bk] = valV;
                }
            }

            j += box_w;
        }

        i += row_h;
    }
}
