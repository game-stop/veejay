/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2010 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License , or
 * (at your option) any later version.
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
#include <stdlib.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/vj-msg.h>
#include "cali.h"

#define CALI_MODE_CORRECT 0
#define CALI_MODE_DARK    1
#define CALI_MODE_LIGHT   2
#define CALI_MODE_FLAT    3

#define CALI_CHROMA          127
#define CALI_MIN_GAIN        0.25
#define CALI_MAX_GAIN        4.00
#define CALI_MAX_CHROMA_BIAS 64.0

typedef struct
{
    uint8_t *b[3];
    uint8_t *l[3];
    uint8_t *m[3];
    double mean[3];
    int len;
    int uv_len;
    int flood;
} calidata_t;

static inline uint8_t cali_clip_u8_int(int v)
{
    return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v);
}

static inline uint8_t cali_clip_u8_double(double v)
{
    int iv = (int) (v + (v >= 0.0 ? 0.5 : -0.5));
    return cali_clip_u8_int(iv);
}

static inline double cali_clip_double(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static inline double cali_gain_from_flat(uint8_t flat, double mean)
{
    if(mean <= 0.0 || flat <= 1)
        return 1.0;
    return cali_clip_double(mean / (double) flat, CALI_MIN_GAIN, CALI_MAX_GAIN);
}

vj_effect *cali_init(int width, int height)
{
    (void) width;
    (void) height;

    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0;
    ve->limits[1][0] = 3;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 1;
    ve->defaults[0] = CALI_MODE_CORRECT;
    ve->defaults[1] = 1;

    ve->description = "Image calibration";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 1;

    ve->param_description = vje_build_param_list(ve->num_params,
                                                 "Mode",
                                                 "Use Flat Field");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0,
                              "Corrected Output",
                              "Show Dark Frame",
                              "Show Light Frame",
                              "Show Flat Frame");
    vje_build_value_hint_list(ve->hints, ve->limits[1][1], 1,
                              "Dark Current Only",
                              "Dark + Flat Field");

    return ve;
}

int cali_prepare(void *ed, double meanY, double meanU, double meanV, uint8_t *data, int len, int uv_len)
{
    if(!ed || !data || len <= 0 || uv_len <= 0)
        return 0;

    const int fl = len + (2 * uv_len);
    calidata_t *c = (calidata_t*) ed;

    c->b[0] = data;
    c->b[1] = c->b[0] + len;
    c->b[2] = c->b[1] + uv_len;

    c->l[0] = data + fl;
    c->l[1] = c->l[0] + len;
    c->l[2] = c->l[1] + uv_len;

    c->m[0] = c->l[0] + fl;
    c->m[1] = c->m[0] + len;
    c->m[2] = c->m[1] + uv_len;

    c->mean[0] = meanY;
    c->mean[1] = meanU;
    c->mean[2] = meanV;
    c->len = len;
    c->uv_len = uv_len;
    c->flood = 0;

    return 1;
}

void *cali_malloc(int width, int height)
{
    (void) width;
    (void) height;
    return vj_calloc(sizeof(calidata_t));
}

void cali_free(void *ptr)
{
    free(ptr);
}

void cali_apply(void *ptr, VJFrame *frame, int *args)
{
    calidata_t *c = (calidata_t*) ptr;
    if(!c || !frame || !args)
        return;

    if(!c->b[0] || !c->l[0] || !c->m[0] || c->mean[0] <= 0.0) {
        if(c->flood == 0) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "Please select a calibration source for the Image calibration FX");
        }
        c->flood = (c->flood + 1) % 25;
        return;
    }

    const int len = frame->len;
    const int uv_len = frame->uv_len;
    if(c->len > 0 && c->uv_len > 0 && (c->len != len || c->uv_len != uv_len)) {
        if(c->flood == 0) {
            veejay_msg(VEEJAY_MSG_ERROR,
                       "Calibration frame geometry does not match the current video frame");
        }
        c->flood = (c->flood + 1) % 25;
        return;
    }

    uint8_t *Y = frame->data[0];
    uint8_t *U = frame->data[1];
    uint8_t *V = frame->data[2];

    const int mode = args[0];
    const int use_flat = args[1];

    if(mode == CALI_MODE_DARK) {
        veejay_memcpy(Y, c->b[0], len);
        veejay_memcpy(U, c->b[1], uv_len);
        veejay_memcpy(V, c->b[2], uv_len);
        return;
    }

    if(mode == CALI_MODE_LIGHT) {
        veejay_memcpy(Y, c->l[0], len);
        veejay_memcpy(U, c->l[1], uv_len);
        veejay_memcpy(V, c->l[2], uv_len);
        return;
    }

    if(mode == CALI_MODE_FLAT) {
        veejay_memcpy(Y, c->m[0], len);
        veejay_memcpy(U, c->m[1], uv_len);
        veejay_memcpy(V, c->m[2], uv_len);
        return;
    }

    uint8_t *by = c->b[0];
    uint8_t *bu = c->b[1];
    uint8_t *bv = c->b[2];
    uint8_t *fy = c->m[0];
    uint8_t *fu = c->m[1];
    uint8_t *fv = c->m[2];

    if(use_flat) {
        const double cy = c->mean[0] > 0.0 ? c->mean[0] : 1.0;
        const double cu = c->mean[1] > 0.0 ? c->mean[1] : CALI_CHROMA;
        const double cv = c->mean[2] > 0.0 ? c->mean[2] : CALI_CHROMA;

        for(int i = 0; i < len; i++) {
            int signal = (int) Y[i] - (int) by[i];
            if(signal < 0)
                signal = 0;
            Y[i] = cali_clip_u8_double((double) signal * cali_gain_from_flat(fy[i], cy));
        }

        for(int i = 0; i < uv_len; i++) {
            double bias_u = cali_clip_double((double) fu[i] - cu, -CALI_MAX_CHROMA_BIAS, CALI_MAX_CHROMA_BIAS);
            double bias_v = cali_clip_double((double) fv[i] - cv, -CALI_MAX_CHROMA_BIAS, CALI_MAX_CHROMA_BIAS);
            int du = ((int) U[i] - CALI_CHROMA) - ((int) bu[i] - CALI_CHROMA);
            int dv = ((int) V[i] - CALI_CHROMA) - ((int) bv[i] - CALI_CHROMA);
            U[i] = cali_clip_u8_double((double) CALI_CHROMA + (double) du - bias_u);
            V[i] = cali_clip_u8_double((double) CALI_CHROMA + (double) dv - bias_v);
        }
    }
    else {
#pragma omp simd
        for(int i = 0; i < len; i++) {
            int p = (int) Y[i] - (int) by[i];
            Y[i] = cali_clip_u8_int(p < 0 ? pixel_Y_lo_ : p);
        }
#pragma omp simd
        for(int i = 0; i < uv_len; i++) {
            int du = CALI_CHROMA + ((int) U[i] - CALI_CHROMA) - ((int) bu[i] - CALI_CHROMA);
            int dv = CALI_CHROMA + ((int) V[i] - CALI_CHROMA) - ((int) bv[i] - CALI_CHROMA);
            U[i] = cali_clip_u8_int(du);
            V[i] = cali_clip_u8_int(dv);
        }
    }
}
