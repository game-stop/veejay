/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2026 Niels Elburg <nwelburg@gmail.com>
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
#include "emboss.h"
#include <string.h>
#include <stdlib.h>
#include <veejaycore/vjmem.h>

typedef struct {
    uint8_t *tmp_buffer;
    int width;
    int height;
    int size;
} emboss_data_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}


void *emboss_malloc(int wid, int hei)
{
    emboss_data_t *data = (emboss_data_t *)vj_calloc(sizeof(emboss_data_t));
    if (!data)
        return NULL;

    data->width = wid;
    data->height = hei;
    data->size = wid * hei;
    data->tmp_buffer = (uint8_t *)vj_calloc((size_t)data->size * sizeof(uint8_t));

    if (!data->tmp_buffer) {
        free(data);
        return NULL;
    }

    return (void *)data;
}

void emboss_free(void *ptr)
{
    if (ptr) {
        emboss_data_t *data = (emboss_data_t *)ptr;
        if (data->tmp_buffer) {
            free(data->tmp_buffer); 
        }
        free(data);
    }
}

vj_effect *emboss_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = 1;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 8;
    ve->limits[0][0] = 0;
    ve->limits[1][0] = 9;

    ve->description = "Various Weird Effects";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode");
    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0,
                              "Blurry Dark", "Xtreme Emboss",
                              "Lines White Balance", "Gray Emboss",
                              "Aggressive Emboss", "Dark Emboss",
                              "Grayish Emboss", "Edged", "Emboss Expi",
                              "Another Expi");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
                      
    return ve;
}

static void simpleedge_framedata(const uint8_t *src, uint8_t *dst, int width, int height)
{
#pragma omp for schedule(static)
    for (int y = 1; y < height - 1; y++) {
        size_t row_above = (y - 1) * width;
        size_t row = y * width;
        size_t row_below = (y + 1) * width;

        for (int x = 1; x < width - 1; x++) {
            uint8_t a1 = src[row_above + (x - 1)];
            uint8_t a2 = src[row_above + x];
            uint8_t a3 = src[row_above + (x + 1)];

            uint8_t b1 = src[row + (x - 1)];
            uint8_t b2 = src[row + x];
            uint8_t b3 = src[row + (x + 1)];

            uint8_t c1 = src[row_below + (x - 1)];
            uint8_t c2 = src[row_below + x];
            uint8_t c3 = src[row_below + (x + 1)];

            if (b2 > a1 && b2 > a2 && b2 > a3 &&
                b2 > b1 && b2 > b3 &&
                b2 > c1 && b2 > c2 && b2 > c3)
            {
                dst[row + x] = pixel_Y_hi_;
            } else {
                dst[row + x] = pixel_Y_lo_;
            }
        }
    }
}

static void xtreme_emboss_framedata(const uint8_t *src, uint8_t *dst, int width, int height)
{
#pragma omp for schedule(static)
    for (int r = 1; r < height - 1; r++)
    {
        size_t row_above = (r - 1) * width;
        size_t row = r * width;
        size_t row_below = (r + 1) * width;

        for (int c = 1; c < width - 1; c++)
        {
            int val = 0;

            val +=  src[row_above + (c - 1)];
            val -=  src[row_above + c];
            val -=  src[row_above + (c + 1)];

            val +=  src[row + (c - 1)];
            val -=  src[row + c];
            val +=  src[row + (c + 1)];

            val +=  src[row_below + (c - 1)];
            val +=  src[row_below + c];
            val -=  src[row_below + (c + 1)];

            val /= 9;

            int tmp = val & ~(val >> 31);
            val = 255 + ((tmp - 255) & ((tmp - 255) >> 31));

            dst[row + c] = (uint8_t) val;
        }
    }
}

static void another_try_edge(const uint8_t *src, uint8_t *dst, int w, int len_total)
{
    const int len = len_total - w;
    
#pragma omp for schedule(static)
    for(int r = w; r < len; r += w)
    {
        for(int c = 1; c < w - 1; c++)
        {
            int p = ((src[r+c-w] * -1) + (src[r+c-w-1] * -1) +
                     (src[r+c-w+1] * -1) + (src[r+c-1] * -1) +
                     (src[r+c] * -8) + (src[r+c+1] * -1) +
                     (src[r+c+w] * -1) + (src[r+c+w-1] * -1) +
                     (src[r+c+w+1] * -1)) / 9;
            dst[r+c] = CLAMP_Y(p);
        }
    }
}

static void lines_white_balance_framedata(const uint8_t *src, uint8_t *dst, int width, int len_total)
{
    const int len = len_total - width;
    
#pragma omp for schedule(static)
    for (int r = width; r < len; r += width)
    {
        for (int c = 1; c < (width - 1); c++)
        {
            int val = (src[r - 1 + c - 1] -
                   src[r - 1 + c] -
                   src[r - 1 + c + 1] +
                   src[r + c - 1] -
                   src[r + c] +
                   src[r + c + 1] +
                   src[r + 1 + c - 1] -
                   src[r + 1 + c] - src[r + 1 + c + 1]
                   ) / 9;
            dst[c + r] = CLAMP_Y(val);
        }
    }
}

static void emboss_test_framedata(const uint8_t *src, uint8_t *dst, int len)
{
#pragma omp for schedule(static)
    for (int i = 0; i < len; i++)
    {
        int a = src[i];
        int b = (a + 235) >> 1;
        int c = (b + 235) >> 1;
        dst[i] = (uint8_t)c;
    }
}

static void gray_emboss_framedata(const uint8_t *src, uint8_t *dst, int width, int height)
{
#pragma omp for schedule(static)
    for (int y = 1; y < height - 1; y++)
    {
        const int row_above = (y - 1) * width;
        const int row = y * width;
        const int row_below = (y + 1) * width;

        for (int c = 1; c < width - 1; c++)
        {
            int val = (src[row_above + c - 1] -
                   src[row_above + c] -
                   src[row_above + c + 1] +
                   src[row + c - 1] -
                   src[row + c] -
                   src[row + c + 1] -
                   src[row_below + c - 1] -
                   src[row_below + c] - src[row_below + c + 1]
                   ) / 9;
            dst[row + c] = CLAMP_Y(val);
        }
    }
}

static void aggressive_emboss_framedata(const uint8_t *src, uint8_t *dst, int width, int height)
{
#pragma omp for schedule(static)
    for (int y = 1; y < height - 1; y++)
    {
        const int row_above = (y - 1) * width;
        const int row = y * width;
        const int row_below = (y + 1) * width;

        for (int c = 1; c < width - 1; c++)
        {
            int val = (src[row_above + c - 1] -
                   src[row_above + c] -
                   src[row_above + c + 1] +
                   src[row + c - 1] -
                   src[row + c] -
                   src[row + c + 1] -
                   src[row_below + c - 1] +
                   src[row_below + c] + src[row_below + c + 1]
                   ) / 9;
            dst[row + c] = CLAMP_Y(val);
        }
    }
}

static void dark_emboss_framedata(const uint8_t *src, uint8_t *dst, int width, int height)
{
#pragma omp for schedule(static)
    for (int y = 1; y < height - 1; y++)
    {
        const int row_above = (y - 1) * width;
        const int row = y * width;
        const int row_below = (y + 1) * width;

        for (int c = 1; c < width - 1; c++)
        {
            int val = (src[row_above + c - 1] -
                       src[row_above + c] -
                       src[row_above + c + 1] +
                       src[row + c - 1] +
                       src[row + c] -
                       src[row + c + 1] -
                       src[row_below + c - 1] +
                       src[row_below + c] + src[row_below + c + 1]
                       ) / 9;
            dst[row + c] = (uint8_t)clampi(val, 0, 255);
        }
    }
}

static void grayish_mood_framedata(const uint8_t *src, uint8_t *dst, int width, int height)
{
#pragma omp for schedule(static)
    for (int y = 1; y < height - 1; y++)
    {
        const int row_above = (y - 1) * width;
        const int row = y * width;
        const int row_below = (y + 1) * width;

        for (int c = 1; c < width - 1; c++)
        {
            int val = (src[row_above + c - 1] -
                       src[row_above + c] -
                       src[row_above + c + 1] -
                       src[row + c - 1] -
                       src[row + c] -
                       src[row + c + 1] -
                       src[row_below + c - 1] -
                       src[row_below + c] - src[row_below + c + 1]
                       ) / 9;
            dst[row + c] = (uint8_t)clampi(val, 0, 255);
        }
    }
}

static void blur_dark_framedata(const uint8_t *src, uint8_t *dst, int width, int len_total)
{
    const int len = len_total - width;
    
#pragma omp for schedule(static)
    for (int r = width; r < len; r += width)
    {
        for (int c = 1; c < width - 1; c++)
        {
            int val = (src[r - 1 + c - 1] -
                       src[r - 1 + c] -
                       src[r - 1 + c + 1] +
                       src[r + c - 1] +
                       src[r + c] +
                       src[r + c + 1] +
                       src[r + 1 + c - 1] -
                       src[r + 1 + c] + src[r + 1 + c + 1]
                       ) / 9;
            dst[c + r] = (uint8_t)clampi(val, 0, 255);
        }
    }
}

void emboss_apply(void *ptr, VJFrame *frame, int *args)
{
    emboss_data_t *data = (emboss_data_t *)ptr;
    
    int n = args[0];
    if(n < 0) n = 0;
    else if(n > 9) n = 9;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    
    uint8_t *src = frame->data[0];
    uint8_t *tmp = data->tmp_buffer;

    #pragma omp single
    {
        veejay_memcpy(tmp, src, len);
    }

    switch (n)
    {
        case 1:
            xtreme_emboss_framedata(tmp, src, width, height);
            break;
        case 2:
            lines_white_balance_framedata(tmp, src, width, len);
            break;
        case 3:
            gray_emboss_framedata(tmp, src, width, height);
            break;
        case 4:
            aggressive_emboss_framedata(tmp, src, width, height);
            break;
        case 5:
            dark_emboss_framedata(tmp, src, width, height);
            break;
        case 6:
            grayish_mood_framedata(tmp, src, width, height);
            break;
        case 7:
            simpleedge_framedata(tmp, src, width, height);
            break;
        case 8:
            emboss_test_framedata(tmp, src, len);
            break;
        case 9:
            another_try_edge(tmp, src, width, len);
            break;
        default:
            blur_dark_framedata(tmp, src, width, len);
            break;
    }
}