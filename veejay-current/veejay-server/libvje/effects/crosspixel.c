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
 */
#include "common.h"
#include <veejaycore/vjmem.h>
#include "crosspixel.h"

#define CROSSPIXEL_PARAMS 2
#define P_MODE      0
#define P_THRESHOLD 1

typedef struct {
    int state;
} crosspixel_t;

static inline int clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

vj_effect *crosspixel_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;
    
    ve->num_params = CROSSPIXEL_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    

    
    ve->limits[0][P_MODE] = 0;      ve->limits[1][P_MODE] = 1;      ve->defaults[P_MODE] = 0;
    ve->limits[0][P_THRESHOLD] = 0; ve->limits[1][P_THRESHOLD] = 255; ve->defaults[P_THRESHOLD] = 128;
    
    ve->description = "Cross Pixel";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Threshold");
    
    (void)w;
    (void)h;

    return ve;
}

void *crosspixel_malloc(int w, int h)
{
    crosspixel_t *c = (crosspixel_t *) vj_calloc(sizeof(crosspixel_t));
    if(!c)
        return NULL;
    
    c->state = 0;
    
    (void)w;
    (void)h;

    return (void*) c;
}

void crosspixel_free(void *ptr)
{
    if(!ptr)
        return;
    free(ptr);
}

void crosspixel_apply(void *ptr, VJFrame *frame, int *args)
{
    crosspixel_t *c = (crosspixel_t *) ptr;
    const int len = frame->len;
    const int mode = args[P_MODE];
    const int threshold = args[P_THRESHOLD];
    
    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    
    #pragma omp single
    {
        c->state = (c->state + 1) & 1;
    }
    
    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        int y = Y[i];
        int cb = Cb[i];
        int cr = Cr[i];
        
        if(mode == 0) {
            if(y > threshold) {
                Y[i] = 255 - y;
            }
        } else {
            if(y < threshold) {
                Y[i] = 255 - y;
            }
        }
        
        Cb[i] = 128 + ((cb - 128) >> 1);
        Cr[i] = 128 + ((cr - 128) >> 1);
    }
}