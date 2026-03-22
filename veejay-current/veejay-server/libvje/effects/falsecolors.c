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
#include "falsecolors.h"
#include <math.h>

vj_effect *falsecolors_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    ve->num_params = 8;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->defaults[0] = 128;   // motion sensitivity
    ve->defaults[1] = 1;     // cycle speed (for dynamic LUT rotation)
    ve->defaults[2] = 200;   // opacity
    ve->defaults[3] = 0;     // mode (unused, reserved)
    ve->defaults[4] = 16;    // trail decay (frames)
    ve->defaults[5] = 192;   // persistence
    ve->defaults[6] = 128;   // motion boost
    ve->defaults[7] = 256;   // motion gain

    ve->limits[0][0] = 0;    ve->limits[1][0] = 255;
    ve->limits[0][1] = 0;    ve->limits[1][1] = 64;
    ve->limits[0][2] = 0;    ve->limits[1][2] = 255;
    ve->limits[0][3] = 0;    ve->limits[1][3] = 2;
    ve->limits[0][4] = 1;    ve->limits[1][4] = 128;
    ve->limits[0][5] = 0;    ve->limits[1][5] = 255;
    ve->limits[0][6] = 0;    ve->limits[1][6] = 255;
    ve->limits[0][7] = 0;    ve->limits[1][7] = 1024;

    ve->description = "False Color Map";

    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->parallel = 0;

    ve->param_description = vje_build_param_list( ve->num_params,
        "Motion Sensitivity", "Cycle Speed", "Opacity", "Mode",
        "Trail Decay", "Persistence", "Motion Boost", "Motion Gain" );

    return ve;
}

typedef struct {
    uint8_t *buf[6];
    uint8_t rainbow[256][3];
    int timestamp;
    int n_threads;
    float phase;
} thermal_t;

static void thermal_build_palette(uint8_t lut[256][3], float gamma)
{
    // 7 color points
    const float t_points[7] = {0.0f,0.14f,0.28f,0.42f,0.57f,0.71f,1.0f};
    const float r_points[7] = {1.0f,1.0f,1.0f,0.0f,0.0f,0.0f,1.0f};
    const float g_points[7] = {0.0f,0.3f,0.7f,1.0f,1.0f,0.0f,0.0f};
    const float b_points[7] = {0.0f,0.0f,0.0f,0.0f,0.5f,1.0f,1.0f};

    for(int i=0;i<256;i++){
        float t = i/255.0f;
        int seg = 0;
        while(seg<6 && t > t_points[seg+1]) seg++;

        float f = (t - t_points[seg])/(t_points[seg+1]-t_points[seg]);
        float r = r_points[seg] + f*(r_points[seg+1]-r_points[seg]);
        float g = g_points[seg] + f*(g_points[seg+1]-g_points[seg]);
        float b = b_points[seg] + f*(b_points[seg+1]-b_points[seg]);

        // gamma correction
        r = powf(r, gamma);
        g = powf(g, gamma);
        b = powf(b, gamma);

        // RGB -> YUV (BT.601)
        float Yf = 0.299f*r + 0.587f*g + 0.114f*b;
        float Uf = -0.169f*r -0.331f*g + 0.5f*b + 0.5f;
        float Vf = 0.5f*r -0.419f*g -0.081f*b + 0.5f;

        lut[i][0] = (uint8_t)(fminf(fmaxf(Yf,0.0f),1.0f)*255.0f);
        lut[i][1] = (uint8_t)(fminf(fmaxf(Uf,0.0f),1.0f)*255.0f);
        lut[i][2] = (uint8_t)(fminf(fmaxf(Vf,0.0f),1.0f)*255.0f);
    }
}

static void thermal_build_palette1(uint8_t lut[256][3])
{
    for(int i=0;i<256;i++){
        float t = i/255.0f;

        float r,g,b;
        if(t < 0.25f) { // red -> orange
            float f = t/0.25f;
            r = 1.0f;
            g = 0.3f + 0.4f*f;
            b = 0.0f;
        } else if(t < 0.5f) { // orange -> yellow
            float f = (t-0.25f)/0.25f;
            r = 1.0f;
            g = 0.7f + 0.3f*f;
            b = 0.0f;
        } else if(t < 0.75f) { // yellow -> cyan
            float f = (t-0.5f)/0.25f;
            r = 1.0f - f;
            g = 1.0f - 0.5f*f;
            b = 0.5f*f;
        } else {// cyan -> blue
            float f = (t-0.75f)/0.25f;
            r = 0.25f - 0.25f*f;
            g = 0.5f - 0.5f*f;
            b = 0.5f + 0.5f*f;
        }

        // wip
        float Yf = 0.299f*r + 0.587f*g + 0.114f*b;
        float Uf = -0.169f*r - 0.331f*g + 0.5f*b + 0.5f;
        float Vf = 0.5f*r - 0.419f*g - 0.081f*b + 0.5f;

        lut[i][0] = (uint8_t)(fminf(fmaxf(Yf,0.0f),1.0f)*255.0f);
        lut[i][1] = (uint8_t)(fminf(fmaxf(Uf,0.0f),1.0f)*255.0f);
        lut[i][2] = (uint8_t)(fminf(fmaxf(Vf,0.0f),1.0f)*255.0f);
    }
}

static inline void box_blur(uint8_t *dst, uint8_t *src, int w, int h, int radius)
{
    uint8_t temp[2][4096];
    uint8_t *a = temp[0], *b = temp[1];

    for(int y = 0; y < h; y++){
        blur2(a, src + y*w, w, radius, 2, 1, 1);
        memcpy(dst + y*w, a, w);
    }

    for(int x = 0; x < w; x++){
        for(int y = 0; y < h; y++) a[y] = dst[y*w + x];
        blur2(b, a, h, radius, 2, 1, 1);
        for(int y = 0; y < h; y++) dst[y*w + x] = b[y];
    }
}

void *falsecolors_malloc(int w, int h) {
    thermal_t *s = (thermal_t*) vj_calloc(sizeof(thermal_t));
    if(!s) return NULL;

    s->buf[0] = (uint8_t*) vj_malloc(sizeof(uint8_t)*w*h*3); 
    if(!s->buf[0]){ free(s); return NULL; }
    s->buf[1] = s->buf[0] + (w*h);
    s->buf[2] = s->buf[1] + (w*h);

    veejay_memset(s->buf[0],0,w*h);
    veejay_memset(s->buf[1],0,w*h);
    veejay_memset(s->buf[2],0,w*h);

    s->n_threads = vje_advise_num_threads(w*h);
    thermal_build_palette(s->rainbow, 0.8f);

    return s;
}

void falsecolors_free(void *ptr){
    thermal_t *s = (thermal_t*) ptr;
    if(s){
        if(s->buf[0]) free(s->buf[0]);
        free(s);
    }
}


void falsecolors_apply(void *ptr, VJFrame *frame, int *args){
    thermal_t *s = (thermal_t*) ptr;
    const int w = frame->width;
    const int h = frame->height;

    const int opacity       = args[2];
    const int trail_decay   = args[4];
    const int persistence   = args[5];
    const int motion_gain   = args[7];
    const int motion_boost  = args[6];
    const int cycle_speed   = args[1];
    const float gamma       = args[3] ? args[3]/32.0f : 0.8f; // wip

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    veejay_memcpy(s->buf[1], Y, w*h);       
    box_blur(s->buf[2], Y, w, h, 2);

    int lut_offset = (s->timestamp * cycle_speed) & 0xFF;

    for(int i=0;i<w*h;i++){
        int lum_fine   = s->buf[1][i];
        int lum_coarse = s->buf[2][i];
        int motion     = abs(lum_fine - s->buf[0][i]);

        int val = (lum_fine*motion_boost + lum_coarse*(256-motion_boost))/256;
        val += motion * motion_gain / 256;
        if(val>255) val=255;

        int prev = s->buf[1][i];
        val = (val*opacity + prev*(256-opacity))/256;

        float vn = val / 255.0f;
        vn = 1.0f / (1.0f + expf(-6*(vn-0.5f))); // wip
        int val_mapped = (int)(vn*255.0f);

        int lut_idx = (val_mapped + lut_offset + (i%16)) & 0xFF; //wip
        if(motion > 24) lut_idx = (lut_idx + 64) & 0xFF; // wip

        Y[i] = s->rainbow[lut_idx][0];
        U[i] = s->rainbow[lut_idx][1];
        V[i] = s->rainbow[lut_idx][2];

        s->buf[0][i] = lum_fine;
        s->buf[1][i] = val;
    }

    s->timestamp++;
}
