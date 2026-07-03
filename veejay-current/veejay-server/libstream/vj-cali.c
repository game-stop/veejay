/* 
 * veejay  
 *
 * Copyright (C) 2000-2026 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/defs.h>
#include <libvje/vje.h>
#include <libvje/ctmf/ctmf.h>
#include <libvje/effects/cali.h>
#include <libstream/vj-cali.h>

typedef struct
{
    uint8_t *data;
    uint8_t *bf;
    uint8_t *lf;
    uint8_t *mf;
    int uv_len;
    int len;
    double mean[3];
} vj_cali_tag_t;

typedef struct
{
    int len;
    int uv_len;
    uint8_t *y_min;
    uint8_t *y_max;
    int16_t *u_min;
    int16_t *u_max;
    int16_t *v_min;
    int16_t *v_max;
} vj_cali_accum_t;

#define VJ_CALI_MEMSIZE        (512UL * 1024UL)
#define VJ_CALI_CHROMA         127
#define VJ_CALI_MAX_GAIN       4.0
#define VJ_CALI_MIN_GAIN       0.25
#define VJ_CALI_MAX_CHROMA_BIAS 64.0

static vj_cali_tag_t *cali_streams[VJ_TAG_MAX_STREAM_IN];
static vj_cali_accum_t capture_accum[SAMPLE_MAX_SAMPLES];
static void *live_cali_state[SAMPLE_MAX_SAMPLES];

static inline size_t vj_cali_frame_size(int len, int uv_len)
{
    return (size_t) len + (2u * (size_t) uv_len);
}

static inline uint8_t vj_cali_clip_u8_int(int v)
{
    if(v < 0)
        return 0;
    if(v > 255)
        return 255;
    return (uint8_t) v;
}

static inline uint8_t vj_cali_clip_u8_double(double v)
{
    int iv = (int) (v >= 0.0 ? v + 0.5 : v - 0.5);
    return vj_cali_clip_u8_int(iv);
}

static inline double vj_cali_clip_double(double v, double lo, double hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static inline double vj_cali_gain_from_flat(uint8_t flat, double mean)
{
    if(mean <= 0.000001)
        return 1.0;

    if(flat == 0)
        return VJ_CALI_MAX_GAIN;

    return vj_cali_clip_double(mean / (double) flat, VJ_CALI_MIN_GAIN, VJ_CALI_MAX_GAIN);
}

static inline int vj_cali_tag_slot(vj_tag *tag)
{
    if(!tag || tag->id <= 0 || tag->id >= SAMPLE_MAX_SAMPLES)
        return -1;
    return tag->id;
}

static void vj_cali_accum_free(vj_tag *tag)
{
    int slot = vj_cali_tag_slot(tag);
    if(slot < 0)
        return;

    vj_cali_accum_t *a = &capture_accum[slot];

    if(a->y_min) free(a->y_min);
    if(a->y_max) free(a->y_max);
    if(a->u_min) free(a->u_min);
    if(a->u_max) free(a->u_max);
    if(a->v_min) free(a->v_min);
    if(a->v_max) free(a->v_max);

    veejay_memset(a, 0, sizeof(vj_cali_accum_t));
}

static vj_cali_accum_t *vj_cali_accum_new(vj_tag *tag, int len, int uv_len)
{
    int slot = vj_cali_tag_slot(tag);
    if(slot < 0)
        return NULL;

    vj_cali_accum_free(tag);

    vj_cali_accum_t *a = &capture_accum[slot];
    a->len = len;
    a->uv_len = uv_len;
    a->y_min = (uint8_t*) vj_malloc((size_t) len);
    a->y_max = (uint8_t*) vj_malloc((size_t) len);
    a->u_min = (int16_t*) vj_malloc(sizeof(int16_t) * (size_t) uv_len);
    a->u_max = (int16_t*) vj_malloc(sizeof(int16_t) * (size_t) uv_len);
    a->v_min = (int16_t*) vj_malloc(sizeof(int16_t) * (size_t) uv_len);
    a->v_max = (int16_t*) vj_malloc(sizeof(int16_t) * (size_t) uv_len);

    if(!a->y_min || !a->y_max || !a->u_min || !a->u_max || !a->v_min || !a->v_max) {
        veejay_msg(VEEJAY_MSG_WARNING, "Insufficient memory to initialize robust calibration accumulator; falling back to plain temporal averaging");
        vj_cali_accum_free(tag);
        return NULL;
    }

    return a;
}

static vj_cali_accum_t *vj_cali_accum_get(vj_tag *tag, int len, int uv_len)
{
    int slot = vj_cali_tag_slot(tag);
    if(slot < 0)
        return NULL;

    vj_cali_accum_t *a = &capture_accum[slot];
    if(a->len != len || a->uv_len != uv_len || !a->y_min || !a->y_max || !a->u_min || !a->u_max || !a->v_min || !a->v_max)
        return NULL;

    return a;
}

static inline double vj_cali_temporal_value(double sum, double minv, double maxv, int duration)
{
    if(duration >= 3)
        return (sum - minv - maxv) / (double) (duration - 2);

    return sum / (double) duration;
}

static inline void vj_cali_update_u8_minmax(uint8_t v, uint8_t *minv, uint8_t *maxv)
{
    if(v < *minv)
        *minv = v;
    if(v > *maxv)
        *maxv = v;
}

static inline void vj_cali_update_i16_minmax(int16_t v, int16_t *minv, int16_t *maxv)
{
    if(v < *minv)
        *minv = v;
    if(v > *maxv)
        *maxv = v;
}

void vj_cali_default_args(int *args)
{
    if(!args)
        return;

    args[0] = VJ_CALI_MODE_CORRECT;
    args[1] = 1;
}

static inline int vj_cali_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void vj_cali_args_sanitize(const int *src, int *dst)
{
    vj_cali_default_args(dst);
    if(!src)
        return;

    dst[0] = vj_cali_clampi(src[0], VJ_CALI_MODE_CORRECT, VJ_CALI_MODE_FLAT);
    dst[1] = src[1] ? 1 : 0;
}

static int vj_cali_tag_has_data(vj_tag *tag)
{
    if(!tag)
        return 0;

    if(tag->source_type == VJ_TAG_TYPE_CALI) {
        if(tag->index < 0 || tag->index >= VJ_TAG_MAX_STREAM_IN)
            return 0;
        return cali_streams[tag->index] != NULL;
    }

    return tag->blackframe && tag->noise_suppression == V4L_BLACKFRAME_PROCESS;
}

static int vj_cali_tag_accepts_args(vj_tag *tag)
{
    if(!tag)
        return 0;

    if(vj_cali_tag_has_data(tag))
        return 1;

    switch(tag->source_type) {
        case VJ_TAG_TYPE_V4L:
        case VJ_TAG_TYPE_VLOOPBACK:
        case VJ_TAG_TYPE_CALI:
        case VJ_TAG_TYPE_NET:
        case VJ_TAG_TYPE_MCAST:
        case VJ_TAG_TYPE_AVFORMAT:
        case VJ_TAG_TYPE_YUV4MPEG:
        case VJ_TAG_TYPE_DV1394:
        case VJ_TAG_TYPE_PICTURE:
            return 1;
        default:
            break;
    }

    return 0;
}

int vj_cali_set_args(vj_tag *tag, const int *args)
{
    if(!tag || !args || !vj_cali_tag_accepts_args(tag))
        return 0;

    int tmp[VJ_CALI_PARAM_COUNT];
    vj_cali_args_sanitize(args, tmp);
    tag->genargs[0] = tmp[0];
    tag->genargs[1] = tmp[1];
    return 1;
}

int vj_cali_get_args(vj_tag *tag, int *args, int *n_args, int *fx_id)
{
    if(!tag || !args || !n_args || !fx_id)
        return 0;

    if(!vj_cali_tag_accepts_args(tag))
        return 0;

    int tmp[VJ_CALI_PARAM_COUNT];
    vj_cali_args_sanitize(tag->genargs, tmp);
    args[0] = tmp[0];
    args[1] = tmp[1];
    *n_args = VJ_CALI_PARAM_COUNT;
    *fx_id = VJ_CALI_STREAM_EFFECT_ID;
    return 1;
}

uint8_t *vj_cali_get(vj_tag *tag, int type, int len, int uv_len)
{
    if(!tag || !tag->blackframe)
        return NULL;

    if(type < VJ_CALI_DARK || type > VJ_CALI_BUF)
        return NULL;

    return tag->blackframe + ((size_t) type * vj_cali_frame_size(len, uv_len));
}

static void blackframe_subtract(vj_tag *tag, uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int use_light, const double mean_y, const double mean_u, const double mean_v);

static void vj_cali_live_state_free(vj_tag *tag)
{
    int slot = vj_cali_tag_slot(tag);
    if(slot < 0)
        return;

    if(live_cali_state[slot]) {
        cali_free(live_cali_state[slot]);
        live_cali_state[slot] = NULL;
    }
}

static void *vj_cali_live_state_get(vj_tag *tag)
{
    int slot = vj_cali_tag_slot(tag);
    if(slot < 0)
        return NULL;

    if(!live_cali_state[slot])
        live_cali_state[slot] = cali_malloc(0, 0);

    return live_cali_state[slot];
}

static void vj_cali_copy_frame_to_dst(VJFrame *dst, const uint8_t *src, int len, int uv_len)
{
    veejay_memcpy(dst->data[0], src, len);
    veejay_memcpy(dst->data[1], src + len, uv_len);
    veejay_memcpy(dst->data[2], src + len + uv_len, uv_len);
}

static void vj_cali_apply_live(vj_tag *tag, uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int *args)
{
    const int len = w * h;
    void *state = vj_cali_live_state_get(tag);

    if(!state) {
        blackframe_subtract(tag, Y, U, V, w, h, uv_len, tag->has_white, tag->mean[0], tag->mean[1], tag->mean[2]);
        return;
    }

    if(!cali_prepare(state, tag->mean[0], tag->mean[1], tag->mean[2], tag->blackframe, len, uv_len))
        return;

    int cali_args[VJ_CALI_PARAM_COUNT];
    vj_cali_args_sanitize(args ? args : tag->genargs, cali_args);

    VJFrame frame;
    veejay_memset(&frame, 0, sizeof(VJFrame));
    frame.width = w;
    frame.height = h;
    frame.len = len;
    frame.uv_len = uv_len;
    frame.data[0] = Y;
    frame.data[1] = U;
    frame.data[2] = V;

    cali_apply(state, &frame, cali_args);
}

static void vj_cali_free_capture_buffers(vj_tag *tag)
{
    if(!tag)
        return;

    vj_cali_accum_free(tag);
    vj_cali_live_state_free(tag);

    if(tag->blackframe)
        free(tag->blackframe);
    if(tag->bf)
        free(tag->bf);
    if(tag->bfu)
        free(tag->bfu);
    if(tag->bfv)
        free(tag->bfv);
    if(tag->lf)
        free(tag->lf);
    if(tag->lfu)
        free(tag->lfu);
    if(tag->lfv)
        free(tag->lfv);

    tag->blackframe = NULL;
    tag->bf = NULL;
    tag->bfu = NULL;
    tag->bfv = NULL;
    tag->lf = NULL;
    tag->lfu = NULL;
    tag->lfv = NULL;
    tag->mean[0] = 0.0;
    tag->mean[1] = 0.0;
    tag->mean[2] = 0.0;
}

void vj_cali_free_capture(vj_tag *tag)
{
    if(!tag)
        return;

    vj_cali_free_capture_buffers(tag);
    tag->bf_index = 0;
    tag->median_radius = 0;
    tag->has_white = 0;
    tag->cali_duration = 0;
}

static int vj_cali_radius_for_plane(int width, int height, int radius)
{
    if(radius <= 0)
        return 0;

    if(width < 3 || height < 3)
        return 0;

    int max_radius = (width < height ? width : height) / 2;
    if(max_radius > 0)
        max_radius--;

    if(max_radius < 1)
        return 0;

    if(max_radius > 127)
        max_radius = 127;

    if(radius > max_radius)
        radius = max_radius;

    return radius;
}

static void vj_cali_uv_geometry(int w, int h, int uv_len, int *uw, int *uh)
{
    const int len = w * h;

    if(uv_len == len) {
        *uw = w;
        *uh = h;
    }
    else if(uv_len == (len / 2) && (w % 2) == 0) {
        *uw = w / 2;
        *uh = h;
    }
    else if(uv_len == (len / 4) && (w % 2) == 0 && (h % 2) == 0) {
        *uw = w / 2;
        *uh = h / 2;
    }
    else if(h > 0 && (uv_len % h) == 0) {
        *uw = uv_len / h;
        *uh = h;
    }
    else if(w > 0 && (uv_len % w) == 0) {
        *uw = w;
        *uh = uv_len / w;
    }
    else {
        *uw = uv_len;
        *uh = 1;
    }

    if(*uw < 1)
        *uw = 1;
    if(*uh < 1)
        *uh = 1;
}

static void vj_cali_filter_plane(uint8_t *src, uint8_t *dst, int width, int height, int radius)
{
    int r = vj_cali_radius_for_plane(width, height, radius);
    size_t len = (size_t) width * (size_t) height;

    if(r <= 0) {
        if(src != dst)
            veejay_memcpy(dst, src, len);
        return;
    }

    ctmf(src, dst, width, height, width, width, r, 1, VJ_CALI_MEMSIZE);
}

static void vj_cali_filter_frame(uint8_t *Y, uint8_t *U, uint8_t *V, uint8_t *dY, uint8_t *dU, uint8_t *dV, int w, int h, int uv_len, int radius)
{
    int uw = 0;
    int uh = 0;

    vj_cali_filter_plane(Y, dY, w, h, radius);
    vj_cali_uv_geometry(w, h, uv_len, &uw, &uh);
    vj_cali_filter_plane(U, dU, uw, uh, radius);
    vj_cali_filter_plane(V, dV, uw, uh, radius);
}

static uint8_t *blackframe_new(int w, int h, int uv_len, uint8_t *Y, uint8_t *U, uint8_t *V, int median_radius, vj_tag *tag)
{
    int i;
    const int len = w * h;
    const size_t frame_size = vj_cali_frame_size(len, uv_len);

    vj_cali_free_capture_buffers(tag);

    uint8_t *buf = (uint8_t*) vj_malloc(5u * frame_size);
    if(!buf) {
        veejay_msg(0, "Insufficient memory to initialize calibration");
        return NULL;
    }

    veejay_memset(buf, 0, 5u * frame_size);
    tag->blackframe = buf;

    tag->lf  = (double*) vj_malloc(sizeof(double) * (size_t) len);
    tag->lfu = (double*) vj_malloc(sizeof(double) * (size_t) uv_len);
    tag->lfv = (double*) vj_malloc(sizeof(double) * (size_t) uv_len);
    tag->bf  = (double*) vj_malloc(sizeof(double) * (size_t) len);
    tag->bfu = (double*) vj_malloc(sizeof(double) * (size_t) uv_len);
    tag->bfv = (double*) vj_malloc(sizeof(double) * (size_t) uv_len);

    if(!tag->lf || !tag->lfu || !tag->lfv || !tag->bf || !tag->bfu || !tag->bfv) {
        veejay_msg(0, "Insufficient memory to initialize calibration planes");
        vj_cali_free_capture(tag);
        return NULL;
    }

    uint8_t *srcY = Y;
    uint8_t *srcU = U;
    uint8_t *srcV = V;

    if(median_radius > 0) {
        uint8_t *ptr = vj_cali_get(tag, VJ_CALI_BUF, len, uv_len);
        uint8_t *ptru = ptr + len;
        uint8_t *ptrv = ptru + uv_len;

        vj_cali_filter_frame(Y, U, V, ptr, ptru, ptrv, w, h, uv_len, median_radius);
        srcY = ptr;
        srcU = ptru;
        srcV = ptrv;
    }

    vj_cali_accum_t *a = vj_cali_accum_new(tag, len, uv_len);

#pragma omp simd
    for(i = 0; i < len; i++) {
        tag->lf[i] = 0.0;
        tag->bf[i] = (double) srcY[i];
        if(a) {
            a->y_min[i] = srcY[i];
            a->y_max[i] = srcY[i];
        }
    }

#pragma omp simd
    for(i = 0; i < uv_len; i++) {
        int16_t su = (int16_t) ((int) srcU[i] - VJ_CALI_CHROMA);
        int16_t sv = (int16_t) ((int) srcV[i] - VJ_CALI_CHROMA);
        tag->lfu[i] = 0.0;
        tag->lfv[i] = 0.0;
        tag->bfu[i] = (double) su;
        tag->bfv[i] = (double) sv;
        if(a) {
            a->u_min[i] = su;
            a->u_max[i] = su;
            a->v_min[i] = sv;
            a->v_max[i] = sv;
        }
    }

    return buf;
}

static void blackframe_process(uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int median_radius, vj_tag *tag)
{
    int i;
    const int len = w * h;
    double *blackframe = tag->bf;
    double *blackframeu = tag->bfu;
    double *blackframev = tag->bfv;
    uint8_t *srcY = Y;
    uint8_t *srcU = U;
    uint8_t *srcV = V;

    if(median_radius > 0) {
        uint8_t *bf = vj_cali_get(tag, VJ_CALI_BUF, len, uv_len);
        uint8_t *bu = bf + len;
        uint8_t *bv = bu + uv_len;

        vj_cali_filter_frame(Y, U, V, bf, bu, bv, w, h, uv_len, median_radius);
        srcY = bf;
        srcU = bu;
        srcV = bv;
    }

    vj_cali_accum_t *a = vj_cali_accum_get(tag, len, uv_len);

#pragma omp simd
    for(i = 0; i < len; i++) {
        blackframe[i] += srcY[i];
        if(a) {
            if(srcY[i] < a->y_min[i])
                a->y_min[i] = srcY[i];
            if(srcY[i] > a->y_max[i])
                a->y_max[i] = srcY[i];
        }
    }

#pragma omp simd
    for(i = 0; i < uv_len; i++) {
        int16_t su = (int16_t) ((int) srcU[i] - VJ_CALI_CHROMA);
        int16_t sv = (int16_t) ((int) srcV[i] - VJ_CALI_CHROMA);
        blackframeu[i] += (double) su;
        blackframev[i] += (double) sv;
        if(a) {
            if(su < a->u_min[i])
                a->u_min[i] = su;
            if(su > a->u_max[i])
                a->u_max[i] = su;
            if(sv < a->v_min[i])
                a->v_min[i] = sv;
            if(sv > a->v_max[i])
                a->v_max[i] = sv;
        }
    }
}

static int whiteframe_new(int w, int h, int uv_len, uint8_t *Y, uint8_t *U, uint8_t *V, int median_radius, vj_tag *tag)
{
    int i;
    const int len = w * h;
    uint8_t *bf = vj_cali_get(tag, VJ_CALI_DARK, len, uv_len);
    uint8_t *srcY = Y;
    uint8_t *srcU = U;
    uint8_t *srcV = V;

    if(!bf || !tag->lf || !tag->lfu || !tag->lfv)
        return 0;

    uint8_t *bu = bf + len;
    uint8_t *bv = bu + uv_len;

    if(median_radius > 0) {
        uint8_t *ptr = vj_cali_get(tag, VJ_CALI_BUF, len, uv_len);
        uint8_t *ptru = ptr + len;
        uint8_t *ptrv = ptru + uv_len;

        vj_cali_filter_frame(Y, U, V, ptr, ptru, ptrv, w, h, uv_len, median_radius);
        srcY = ptr;
        srcU = ptru;
        srcV = ptrv;
    }

    vj_cali_accum_t *a = vj_cali_accum_new(tag, len, uv_len);

#pragma omp simd
    for(i = 0; i < len; i++) {
        int p = (int) srcY[i] - (int) bf[i];
        uint8_t v = (uint8_t) (p > 0 ? p : 0);
        tag->lf[i] = (double) v;
        if(a) {
            a->y_min[i] = v;
            a->y_max[i] = v;
        }
    }

#pragma omp simd
    for(i = 0; i < uv_len; i++) {
        int16_t su = (int16_t) (((int) srcU[i] - VJ_CALI_CHROMA) - ((int) bu[i] - VJ_CALI_CHROMA));
        int16_t sv = (int16_t) (((int) srcV[i] - VJ_CALI_CHROMA) - ((int) bv[i] - VJ_CALI_CHROMA));
        tag->lfu[i] = (double) su;
        tag->lfv[i] = (double) sv;
        if(a) {
            a->u_min[i] = su;
            a->u_max[i] = su;
            a->v_min[i] = sv;
            a->v_max[i] = sv;
        }
    }

    return 1;
}

static int whiteframe_process(uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int median_radius, vj_tag *tag)
{
    int i;
    const int len = w * h;
    uint8_t *bf = vj_cali_get(tag, VJ_CALI_DARK, len, uv_len);
    uint8_t *srcY = Y;
    uint8_t *srcU = U;
    uint8_t *srcV = V;

    if(!bf || !tag->lf || !tag->lfu || !tag->lfv)
        return 0;

    uint8_t *bu = bf + len;
    uint8_t *bv = bu + uv_len;

    if(median_radius > 0) {
        uint8_t *dbf = vj_cali_get(tag, VJ_CALI_BUF, len, uv_len);
        uint8_t *dbu = dbf + len;
        uint8_t *dbv = dbu + uv_len;

        vj_cali_filter_frame(Y, U, V, dbf, dbu, dbv, w, h, uv_len, median_radius);
        srcY = dbf;
        srcU = dbu;
        srcV = dbv;
    }

    vj_cali_accum_t *a = vj_cali_accum_get(tag, len, uv_len);

#pragma omp simd
    for(i = 0; i < len; i++) {
        int p = (int) srcY[i] - (int) bf[i];
        uint8_t v = (uint8_t) (p > 0 ? p : 0);
        tag->lf[i] += (double) v;
        if(a) {
            if(v < a->y_min[i])
                a->y_min[i] = v;
            if(v > a->y_max[i])
                a->y_max[i] = v;
        }
    }

#pragma omp simd
    for(i = 0; i < uv_len; i++) {
        int16_t su = (int16_t) (((int) srcU[i] - VJ_CALI_CHROMA) - ((int) bu[i] - VJ_CALI_CHROMA));
        int16_t sv = (int16_t) (((int) srcV[i] - VJ_CALI_CHROMA) - ((int) bv[i] - VJ_CALI_CHROMA));
        tag->lfu[i] += (double) su;
        tag->lfv[i] += (double) sv;
        if(a) {
            if(su < a->u_min[i])
                a->u_min[i] = su;
            if(su > a->u_max[i])
                a->u_max[i] = su;
            if(sv < a->v_min[i])
                a->v_min[i] = sv;
            if(sv > a->v_max[i])
                a->v_max[i] = sv;
        }
    }

    return 1;
}

static void master_blackframe(int w, int h, int uv_len, vj_tag *tag)
{
    int i;
    const int len = w * h;
    const int duration = tag->cali_duration > 0 ? tag->cali_duration : 1;
    uint8_t *bf = vj_cali_get(tag, VJ_CALI_DARK, len, uv_len);
    uint8_t *bu = bf + len;
    uint8_t *bv = bu + uv_len;
    vj_cali_accum_t *a = vj_cali_accum_get(tag, len, uv_len);

#pragma omp simd
    for(i = 0; i < len; i++) {
        double v = a ? vj_cali_temporal_value(tag->bf[i], (double) a->y_min[i], (double) a->y_max[i], duration) : tag->bf[i] / duration;
        bf[i] = vj_cali_clip_u8_double(v);
    }

#pragma omp simd
    for(i = 0; i < uv_len; i++) {
        double u = a ? vj_cali_temporal_value(tag->bfu[i], (double) a->u_min[i], (double) a->u_max[i], duration) : tag->bfu[i] / duration;
        double v = a ? vj_cali_temporal_value(tag->bfv[i], (double) a->v_min[i], (double) a->v_max[i], duration) : tag->bfv[i] / duration;
        bu[i] = vj_cali_clip_u8_double((double) VJ_CALI_CHROMA + u);
        bv[i] = vj_cali_clip_u8_double((double) VJ_CALI_CHROMA + v);
    }

    vj_cali_accum_free(tag);
}

static void master_lightframe(int w, int h, int uv_len, vj_tag *tag)
{
    int i;
    const int len = w * h;
    const int duration = tag->cali_duration > 0 ? tag->cali_duration : 1;
    uint8_t *lf = vj_cali_get(tag, VJ_CALI_LIGHT, len, uv_len);
    uint8_t *lu = lf + len;
    uint8_t *lv = lu + uv_len;
    vj_cali_accum_t *a = vj_cali_accum_get(tag, len, uv_len);

#pragma omp simd
    for(i = 0; i < len; i++) {
        double v = a ? vj_cali_temporal_value(tag->lf[i], (double) a->y_min[i], (double) a->y_max[i], duration) : tag->lf[i] / duration;
        lf[i] = vj_cali_clip_u8_double(v);
    }

#pragma omp simd
    for(i = 0; i < uv_len; i++) {
        double u = a ? vj_cali_temporal_value(tag->lfu[i], (double) a->u_min[i], (double) a->u_max[i], duration) : tag->lfu[i] / duration;
        double v = a ? vj_cali_temporal_value(tag->lfv[i], (double) a->v_min[i], (double) a->v_max[i], duration) : tag->lfv[i] / duration;
        lu[i] = vj_cali_clip_u8_double((double) VJ_CALI_CHROMA + u);
        lv[i] = vj_cali_clip_u8_double((double) VJ_CALI_CHROMA + v);
    }

    vj_cali_accum_free(tag);
}

static void master_flatframe(int w, int h, int uv_len, vj_tag *tag)
{
    int i;
    const int len = w * h;
    uint8_t *ly = vj_cali_get(tag, VJ_CALI_LIGHT, len, uv_len);
    uint8_t *lu = ly + len;
    uint8_t *lv = lu + uv_len;
    uint8_t *fy = vj_cali_get(tag, VJ_CALI_FLAT, len, uv_len);
    uint8_t *fu = fy + len;
    uint8_t *fv = fu + uv_len;
    uint8_t *sy = vj_cali_get(tag, VJ_CALI_MFLAT, len, uv_len);
    uint8_t *su = sy + len;
    uint8_t *sv = su + uv_len;
    double sum_y = 0.0;
    double sum_u = 0.0;
    double sum_v = 0.0;
    int valid_y = 0;

#pragma omp simd reduction(+:sum_y,valid_y)
    for(i = 0; i < len; i++) {
        fy[i] = ly[i];
        sy[i] = ly[i];
        if(ly[i] > 0) {
            sum_y += ly[i];
            valid_y++;
        }
    }

#pragma omp simd reduction(+:sum_u,sum_v)
    for(i = 0; i < uv_len; i++) {
        fu[i] = lu[i];
        fv[i] = lv[i];
        su[i] = lu[i];
        sv[i] = lv[i];
        sum_u += fu[i];
        sum_v += fv[i];
    }

    tag->mean[0] = valid_y > 0 ? sum_y / valid_y : 1.0;
    tag->mean[1] = uv_len > 0 ? sum_u / uv_len : (double) VJ_CALI_CHROMA;
    tag->mean[2] = uv_len > 0 ? sum_v / uv_len : (double) VJ_CALI_CHROMA;

    if(tag->mean[0] < 1.0)
        tag->mean[0] = 1.0;

    if(tag->mean[0] < 16.0)
        veejay_msg(VEEJAY_MSG_WARNING, "Calibration light frame is very dark; flat-field correction may amplify noise");
}

static void blackframe_subtract(vj_tag *tag, uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int use_light, const double mean_y, const double mean_u, const double mean_v)
{
    int i;
    const int len = w * h;
    uint8_t *bf = vj_cali_get(tag, VJ_CALI_DARK, len, uv_len);

    if(!bf)
        return;

    uint8_t *bu = bf + len;
    uint8_t *bv = bu + uv_len;

    if(use_light) {
        uint8_t *fy = vj_cali_get(tag, VJ_CALI_FLAT, len, uv_len);
        uint8_t *fu = fy + len;
        uint8_t *fv = fu + uv_len;
        double cy = mean_y > 1.0 ? mean_y : 1.0;
        double cu = mean_u > 0.000001 ? mean_u : (double) VJ_CALI_CHROMA;
        double cv = mean_v > 0.000001 ? mean_v : (double) VJ_CALI_CHROMA;

#pragma omp simd
        for(i = 0; i < len; i++) {
            int signal = (int) Y[i] - (int) bf[i];
            if(signal <= 0) {
                Y[i] = 0;
            }
            else {
                double gain = vj_cali_gain_from_flat(fy[i], cy);
                Y[i] = vj_cali_clip_u8_double((double) signal * gain);
            }
        }

#pragma omp simd
        for(i = 0; i < uv_len; i++) {
            double bias_u = vj_cali_clip_double((double) fu[i] - cu, -VJ_CALI_MAX_CHROMA_BIAS, VJ_CALI_MAX_CHROMA_BIAS);
            double bias_v = vj_cali_clip_double((double) fv[i] - cv, -VJ_CALI_MAX_CHROMA_BIAS, VJ_CALI_MAX_CHROMA_BIAS);
            int du = (int) U[i] - (int) bu[i];
            int dv = (int) V[i] - (int) bv[i];
            U[i] = vj_cali_clip_u8_double((double) VJ_CALI_CHROMA + (double) du - bias_u);
            V[i] = vj_cali_clip_u8_double((double) VJ_CALI_CHROMA + (double) dv - bias_v);
        }
    }
    else {
#pragma omp simd
        for(i = 0; i < len; i++) {
            int d = (int) Y[i] - (int) bf[i];
            Y[i] = vj_cali_clip_u8_int(d);
        }

#pragma omp simd
        for(i = 0; i < uv_len; i++) {
            int du = VJ_CALI_CHROMA + ((int) U[i] - (int) bu[i]);
            int dv = VJ_CALI_CHROMA + ((int) V[i] - (int) bv[i]);
            U[i] = vj_cali_clip_u8_int(du);
            V[i] = vj_cali_clip_u8_int(dv);
        }
    }
}

static int cali_write_exact(FILE *f, const void *data, size_t len)
{
    return fwrite(data, 1, len, f) == len;
}

static int cali_read_exact(FILE *f, void *data, size_t len)
{
    return fread(data, 1, len, f) == len;
}

static int cali_write_file(char *file, vj_tag *tag, editlist *el)
{
    (void) el;

    if(!file || !tag)
        return 0;

    int w = vj_tag_get_width();
    int h = vj_tag_get_height();
    int len = w * h;
    int uv_len = vj_tag_get_uvlen();
    size_t frame_size = vj_cali_frame_size(len, uv_len);
    uint8_t *darkframe = vj_cali_get(tag, VJ_CALI_DARK, len, uv_len);
    uint8_t *lightframe = vj_cali_get(tag, VJ_CALI_LIGHT, len, uv_len);
    uint8_t *flatframe = vj_cali_get(tag, VJ_CALI_FLAT, len, uv_len);

    if(!darkframe || !lightframe || !flatframe)
        return 0;

    FILE *f = fopen(file, "wb");
    if(!f) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to open '%s' for writing", file);
        return 0;
    }

    char header[384];
    char fileheader[512];

    snprintf(header, sizeof(header), "%08d %08d %08d %08d %.17g %.17g %.17g", w, h, len, uv_len, tag->mean[0], tag->mean[1], tag->mean[2]);

    int offset = 4 + (int) strlen(header);
    snprintf(fileheader, sizeof(fileheader), "%03d %s", offset, header);

    if((int) strlen(fileheader) != offset ||
       !cali_write_exact(f, fileheader, (size_t) offset) ||
       !cali_write_exact(f, darkframe, frame_size) ||
       !cali_write_exact(f, lightframe, frame_size) ||
       !cali_write_exact(f, flatframe, frame_size)) {
        fclose(f);
        veejay_msg(0, "File write error");
        return 0;
    }

    fclose(f);
    return 1;
}

static int cali_read_file(vj_cali_tag_t *p, char *file, int expected_w, int expected_h)
{
    if(!p || !file)
        return 0;

    FILE *f = fopen(file, "rb");
    if(!f)
        return 0;

    char prefix[5];
    int offset = 0;

    if(!cali_read_exact(f, prefix, 4)) {
        fclose(f);
        return 0;
    }

    prefix[4] = '\0';
    if(sscanf(prefix, "%3d", &offset) != 1 || offset < 5 || offset > 511) {
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid calibration header");
        fclose(f);
        return 0;
    }

    char header[512];
    int header_len = offset - 4;
    if(!cali_read_exact(f, header, (size_t) header_len)) {
        fclose(f);
        return 0;
    }
    header[header_len] = '\0';

    int file_w = 0;
    int file_h = 0;
    int len = 0;
    int uv_len = 0;
    double mean[3] = {0.0, 0.0, 0.0};

    if(sscanf(header, "%8d %8d %8d %8d %lf %lf %lf", &file_w, &file_h, &len, &uv_len, &mean[0], &mean[1], &mean[2]) != 7) {
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid calibration header");
        fclose(f);
        return 0;
    }

    if(file_w != expected_w || file_h != expected_h) {
        veejay_msg(VEEJAY_MSG_ERROR, "Calibration size %dx%d does not match stream size %dx%d", file_w, file_h, expected_w, expected_h);
        fclose(f);
        return 0;
    }

    if(len != (expected_w * expected_h)) {
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid length for plane Y");
        fclose(f);
        return 0;
    }

    if(uv_len != vj_tag_get_uvlen()) {
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid length for planes UV");
        fclose(f);
        return 0;
    }

    size_t frame_size = vj_cali_frame_size(len, uv_len);
    p->data = (uint8_t*) vj_malloc(3u * frame_size);
    if(!p->data) {
        fclose(f);
        return 0;
    }

    p->bf = p->data;
    p->lf = p->data + frame_size;
    p->mf = p->lf + frame_size;
    p->uv_len = uv_len;
    p->len = len;
    p->mean[0] = mean[0];
    p->mean[1] = mean[1];
    p->mean[2] = mean[2];

    veejay_memset(p->data, 0, 3u * frame_size);

    if(!cali_read_exact(f, p->bf, frame_size) ||
       !cali_read_exact(f, p->lf, frame_size) ||
       !cali_read_exact(f, p->mf, frame_size)) {
        veejay_msg(VEEJAY_MSG_ERROR, "Calibration file is truncated");
        fclose(f);
        free(p->data);
        p->data = NULL;
        p->bf = NULL;
        p->lf = NULL;
        p->mf = NULL;
        return 0;
    }

    fclose(f);
    veejay_msg(VEEJAY_MSG_INFO, "Image calibration data loaded");
    return 1;
}

int vj_cali_tag_new(vj_tag *tag, int stream_nr, int w, int h)
{
    if(!tag || !tag->source_name || stream_nr < 0 || stream_nr >= VJ_TAG_MAX_STREAM_IN)
        return 0;

    vj_cali_tag_free(stream_nr);

    vj_cali_tag_t *p = (vj_cali_tag_t*) vj_calloc(sizeof(vj_cali_tag_t));
    if(!p)
        return 0;

    if(!cali_read_file(p, tag->source_name, w, h)) {
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to load calibration frame '%s'", tag->source_name);
        free(p);
        return 0;
    }

    cali_streams[stream_nr] = p;
    veejay_msg(VEEJAY_MSG_INFO, "Image Calibration files ready");
    return 1;
}

void vj_cali_tag_free(int stream_nr)
{
    if(stream_nr < 0 || stream_nr >= VJ_TAG_MAX_STREAM_IN)
        return;

    vj_cali_tag_t *p = cali_streams[stream_nr];
    if(!p)
        return;

    if(p->data)
        free(p->data);
    free(p);
    cali_streams[stream_nr] = NULL;
}

int vj_cali_get_frame(vj_tag *tag, VJFrame *dst, int *args)
{
    if(!tag || !dst || tag->index < 0 || tag->index >= VJ_TAG_MAX_STREAM_IN)
        return 0;

    vj_cali_tag_t *p = cali_streams[tag->index];
    if(!p || !p->bf || !p->lf || !p->mf)
        return 0;

    int cali_args[VJ_CALI_PARAM_COUNT];
    vj_cali_args_sanitize(args ? args : tag->genargs, cali_args);

    switch(cali_args[0]) {
        case VJ_CALI_MODE_DARK:
            vj_cali_copy_frame_to_dst(dst, p->bf, dst->len, dst->uv_len);
            break;
        case VJ_CALI_MODE_LIGHT:
            vj_cali_copy_frame_to_dst(dst, p->lf, dst->len, dst->uv_len);
            break;
        case VJ_CALI_MODE_FLAT:
        case VJ_CALI_MODE_CORRECT:
        default:
            vj_cali_copy_frame_to_dst(dst, p->mf, dst->len, dst->uv_len);
            break;
    }

    return 1;
}

void vj_tag_cali_prepare_now(vj_tag *tag)
{
    (void) tag;
}

void vj_tag_cali_prepare(int t1, int pos, int cali_tag)
{
    (void) t1;
    (void) pos;
    (void) cali_tag;
}

uint8_t *vj_tag_get_cali_buffer(int t1, int type, int *total, int *plane, int *planeuv)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return NULL;

    int len = vj_tag_get_width() * vj_tag_get_height();
    int uv_len = vj_tag_get_uvlen();

    if(total)
        *total = len + (2 * uv_len);
    if(plane)
        *plane = len;
    if(planeuv)
        *planeuv = uv_len;

    if(tag->source_type == VJ_TAG_TYPE_CALI && tag->index >= 0 && tag->index < VJ_TAG_MAX_STREAM_IN) {
        vj_cali_tag_t *p = cali_streams[tag->index];
        if(!p)
            return NULL;
        switch(type) {
            case VJ_CALI_DARK:  return p->bf;
            case VJ_CALI_LIGHT: return p->lf;
            case VJ_CALI_FLAT:  return p->mf;
            default: return NULL;
        }
    }

    return vj_cali_get(tag, type, len, uv_len);
}

uint8_t *vj_tag_get_cali_data(int t1, int what)
{
    int total = 0;
    int plane = 0;
    int planeuv = 0;
    return vj_tag_get_cali_buffer(t1, what, &total, &plane, &planeuv);
}

int vj_tag_cali_write_file(int t1, char *name, editlist *el)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return 0;


    if(!tag->blackframe || tag->noise_suppression != V4L_BLACKFRAME_PROCESS) {
        veejay_msg(0, "Please finish calibration first");
        return 0;
    }

    return cali_write_file(name, tag, el);
}

int vj_tag_has_cali_fx(int t1)
{
    (void) t1;
    return -1;
}

int vj_tag_grab_blackframe(int t1, int duration, int median_radius, int mode)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return 0;

    if(tag->source_type != VJ_TAG_TYPE_V4L)
        veejay_msg(VEEJAY_MSG_WARNING, "Calibration source is not a video device");

    if(duration < 1)
        return 0;

    if(median_radius < 0)
        median_radius = 0;

    if(mode != 0 && mode != 1)
        mode = 0;

    if(mode == 1) {
        if(!tag->blackframe) {
            veejay_msg(0, "Please start with a dark frame first (Put cap on lens)");
            return 0;
        }
        if(tag->noise_suppression == V4L_BLACKFRAME || tag->noise_suppression == V4L_BLACKFRAME_NEXT ||
           tag->noise_suppression == V4L_WHITEFRAME || tag->noise_suppression == V4L_WHITEFRAME_NEXT) {
            veejay_msg(0, "Calibration capture is already in progress");
            return 0;
        }
    }

    if(mode == 0)
        vj_cali_free_capture(tag);
    else
        vj_cali_accum_free(tag);

    tag->cali_duration = duration;
    tag->median_radius = median_radius;
    tag->bf_index = 0;
    tag->has_white = mode == 1 ? 1 : 0;
    tag->noise_suppression = mode == 0 ? V4L_BLACKFRAME : V4L_WHITEFRAME;

    veejay_msg(VEEJAY_MSG_INFO, "Setup per-pixel camera calibration:");
    veejay_msg(VEEJAY_MSG_INFO, "  This method corrects dark current, flat-field brightness, dust shadows and local chroma bias.");
    veejay_msg(VEEJAY_MSG_INFO, "  CTMF is used as an optional spatial hot-pixel scrubber; temporal averaging remains the master-frame estimator.");
    veejay_msg(VEEJAY_MSG_INFO, "  Take the dark frame while having the cap on the lens.");
    veejay_msg(VEEJAY_MSG_INFO, "  Take the light frame with an evenly illuminated white target, below clipping.");
    veejay_msg(VEEJAY_MSG_INFO, "  You can save the dark, light and flat calibration frames using reloaded.");
    veejay_msg(VEEJAY_MSG_INFO, "\tMode: %s", mode == 0 ? "Darkframe" : "Lightframe");
    veejay_msg(VEEJAY_MSG_INFO, "\tMedian radius: %d", median_radius);
    veejay_msg(VEEJAY_MSG_INFO, "\tDuration: %d", duration);

    return 1;
}

int vj_tag_drop_blackframe(int t1)
{
    vj_tag *tag = vj_tag_get(t1);
    if(!tag)
        return 0;

    vj_cali_free_capture(tag);
    tag->noise_suppression = 0;
    veejay_msg(VEEJAY_MSG_INFO, "Calibration data cleaned up");
    return 1;
}

void vj_cali_process_frame(vj_tag *tag, uint8_t *Y, uint8_t *U, uint8_t *V, int w, int h, int uv_len, int *args)
{
    if(!tag)
        return;

    switch(tag->noise_suppression) {
        case V4L_BLACKFRAME_PROCESS:
            vj_cali_apply_live(tag, Y, U, V, w, h, uv_len, args);
            break;

        case V4L_BLACKFRAME:
            if(!blackframe_new(w, h, uv_len, Y, U, V, tag->median_radius, tag)) {
                tag->noise_suppression = 0;
                break;
            }
            tag->bf_index++;
            veejay_msg(VEEJAY_MSG_INFO, "Processed dark frame %d/%d", tag->bf_index, tag->cali_duration);
            if(tag->bf_index >= tag->cali_duration) {
                master_blackframe(w, h, uv_len, tag);
                tag->noise_suppression = 0;
                veejay_msg(VEEJAY_MSG_INFO, "Please create a lightframe now");
            }
            else {
                tag->noise_suppression = V4L_BLACKFRAME_NEXT;
            }
            break;

        case V4L_BLACKFRAME_NEXT:
            blackframe_process(Y, U, V, w, h, uv_len, tag->median_radius, tag);
            tag->bf_index++;
            veejay_msg(VEEJAY_MSG_INFO, "Processed dark frame %d/%d", tag->bf_index, tag->cali_duration);
            if(tag->bf_index >= tag->cali_duration) {
                master_blackframe(w, h, uv_len, tag);
                tag->noise_suppression = 0;
                veejay_msg(VEEJAY_MSG_INFO, "Please create a lightframe now");
            }
            break;

        case V4L_WHITEFRAME:
            if(!tag->blackframe) {
                veejay_msg(0, "Please start with a dark frame first (Put cap on lens)");
                tag->noise_suppression = 0;
                break;
            }
            if(!whiteframe_new(w, h, uv_len, Y, U, V, tag->median_radius, tag)) {
                veejay_msg(0, "Unable to initialize light-frame calibration");
                tag->noise_suppression = V4L_BLACKFRAME_PROCESS;
                break;
            }
            tag->bf_index++;
            veejay_msg(VEEJAY_MSG_INFO, "Processed light frame %d/%d", tag->bf_index, tag->cali_duration);
            if(tag->bf_index >= tag->cali_duration) {
                master_lightframe(w, h, uv_len, tag);
                master_flatframe(w, h, uv_len, tag);
                tag->noise_suppression = V4L_BLACKFRAME_PROCESS;
                tag->has_white = 1;
                veejay_msg(VEEJAY_MSG_DEBUG, "Mastered flat frame. Ready for processing. Mean is %g,%g,%g", tag->mean[0], tag->mean[1], tag->mean[2]);
            }
            else {
                tag->noise_suppression = V4L_WHITEFRAME_NEXT;
            }
            break;

        case V4L_WHITEFRAME_NEXT:
            if(!whiteframe_process(Y, U, V, w, h, uv_len, tag->median_radius, tag)) {
                veejay_msg(0, "Unable to process light-frame calibration");
                tag->noise_suppression = V4L_BLACKFRAME_PROCESS;
                break;
            }
            tag->bf_index++;
            veejay_msg(VEEJAY_MSG_INFO, "Processed light frame %d/%d", tag->bf_index, tag->cali_duration);
            if(tag->bf_index >= tag->cali_duration) {
                master_lightframe(w, h, uv_len, tag);
                master_flatframe(w, h, uv_len, tag);
                tag->noise_suppression = V4L_BLACKFRAME_PROCESS;
                tag->has_white = 1;
                veejay_msg(VEEJAY_MSG_DEBUG, "Mastered flat frame. Ready for processing. Mean is %g,%g,%g", tag->mean[0], tag->mean[1], tag->mean[2]);
            }
            break;

        case -1:
            vj_cali_free_capture(tag);
            tag->noise_suppression = 0;
            veejay_msg(VEEJAY_MSG_INFO, "Calibration data cleaned up");
            break;

        default:
            break;
    }
}
