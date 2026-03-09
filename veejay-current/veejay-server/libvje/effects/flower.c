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
#include "flower.h"
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FP_SHIFT 16
#define FP_MULT (1 << FP_SHIFT)
#define LUT_SIZE 8192
typedef struct 
{
    uint8_t *buf[3];
    uint16_t *atan2_idx;
    uint16_t *dist_idx;
    int *offset_map;     
    
    int32_t cos_lut_1d[LUT_SIZE];
    int32_t exp_lut_1d[LUT_SIZE];
    
    int last_petal_count;
    int last_petal_length;
} flower_t;

vj_effect *flower_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 2;

    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    
    ve->limits[0][0] = 1;
    ve->limits[1][0] = 100;
    ve->defaults[0] = 8;

    ve->limits[0][1] = 1;
    ve->limits[1][1] = w/2;
    ve->defaults[1] = h/2;
    
    ve->description = "Flower";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->parallel = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Petal Count", "Petal Length");
    return ve;
}

void *flower_malloc(int w, int h) {
    flower_t *s = (flower_t*) vj_calloc(sizeof(flower_t));
    if(!s) return NULL;
    
    s->buf[0] = (uint8_t*) vj_malloc(w * h * 3);
    if(!s->buf[0]) { free(s); return NULL; }
    s->buf[1] = s->buf[0] + (w * h);
    s->buf[2] = s->buf[1] + (w * h);

    size_t len = w * h;
    void *mem = vj_malloc(len * (sizeof(uint16_t) * 2 + sizeof(int)));
    if(!mem) { free(s->buf[0]); free(s); return NULL; }

    s->atan2_idx = (uint16_t *)mem;
    s->dist_idx = (uint16_t *)(s->atan2_idx + len);
    s->offset_map = (int *)(s->dist_idx + len);

    int cx = w >> 1;
    int cy = h >> 1;
    
    for (int y = 0, i = 0; y < h; ++y) {
        int dy = y - cy;
        for (int x = 0; x < w; ++x, ++i) {
            int dx = x - cx;
            
            float angle = atan2f((float)dy, (float)dx);
            int a_idx = (int)(((angle + M_PI) / (2.0f * M_PI)) * (LUT_SIZE - 1));
            s->atan2_idx[i] = (a_idx < 0) ? 0 : (a_idx >= LUT_SIZE ? LUT_SIZE - 1 : a_idx);
            
            float dist = sqrtf((float)(dx*dx + dy*dy));
            int d_idx = (int)(dist + 0.5f);
            s->dist_idx[i] = (d_idx >= LUT_SIZE) ? LUT_SIZE - 1 : d_idx;
        }
    }
    
    s->last_petal_count = -1;
    s->last_petal_length = -1;
    return (void*) s;
}
void flower_free(void *ptr) {
    flower_t *s = (flower_t*) ptr;
    if (s) {
        free(s->buf[0]);
        free(s->atan2_idx);
        free(s);
    }
}

static inline int fast_clamp(int x, int max_val) {
    x &= ~(x >> 31); 
    int diff = max_val - x;
    return x + (diff & (diff >> 31));
}

void flower_apply(void *ptr, VJFrame *frame, int *args) {
    flower_t *s = (flower_t*)ptr;
    const int petalCount = args[0];
    const int petalLength = args[1];
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int w_limit = width - 1;
    const int h_limit = height - 1;
    int needs_update = 0;

    uint8_t *restrict srcY = frame->data[0];
    uint8_t *restrict srcU = frame->data[1];
    uint8_t *restrict srcV = frame->data[2];
    uint8_t *restrict bufY = s->buf[0];
    uint8_t *restrict bufU = s->buf[1];
    uint8_t *restrict bufV = s->buf[2];

    veejay_memcpy(bufY, srcY, len);
    veejay_memcpy(bufU, srcU, len);
    veejay_memcpy(bufV, srcV, len);

    if (petalCount != s->last_petal_count) {
        for (int i = 0; i < LUT_SIZE; i++) {
            float angle = ((float)i / (LUT_SIZE - 1)) * 2.0f * M_PI - M_PI;
            s->cos_lut_1d[i] = (int32_t)((1.0f + cosf(petalCount * angle)) * FP_MULT);
        }
        s->last_petal_count = petalCount;
        needs_update = 1;
    }
    if (petalLength != s->last_petal_length) {
        float inv_len = 1.0f / (float)petalLength;
        for (int i = 0; i < LUT_SIZE; i++) {
            s->exp_lut_1d[i] = (int32_t)(expf(-(float)i * inv_len) * FP_MULT);
        }
        s->last_petal_length = petalLength;
        needs_update = 1;
    }

    if (needs_update) {
        const int cx = width >> 1;
        const int cy = height >> 1;
        
        for (int dy_val = -cy; dy_val < (height - cy); dy_val++) {
            const int y_curr = cy + dy_val;
            const int row_base = y_curr * width;

            for (int dx_val = -cx; dx_val < (width - cx); dx_val++) {
                const int x_curr = cx + dx_val;
                const int i = row_base + x_curr;

                const uint16_t a_idx = s->atan2_idx[i];
                const uint16_t d_idx = s->dist_idx[i];

                int64_t combined = (int64_t)s->cos_lut_1d[a_idx] * s->exp_lut_1d[d_idx];
                int32_t pVal = (int32_t)(combined >> FP_SHIFT);

                int mx = cx + ((dx_val * pVal) >> FP_SHIFT);
                int my = cy + ((dy_val * pVal) >> FP_SHIFT);

                mx = (mx < 0) ? 0 : (mx > w_limit ? w_limit : mx);
                my = (my < 0) ? 0 : (my > h_limit ? h_limit : my);

                s->offset_map[i] = my * width + mx;
            }
        }
    }

    int *restrict map = s->offset_map;
    for (int i = 0; i < len; i++) {
        const int src_idx = map[i];
        srcY[i] = bufY[src_idx];
        srcU[i] = bufU[src_idx];
        srcV[i] = bufV[src_idx];
    }
}