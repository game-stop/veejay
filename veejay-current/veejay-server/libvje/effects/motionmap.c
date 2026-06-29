/* 
 * Linux VeeJay
 *
 * Copyright(C)2007-2016 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or at your option) any later version.
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
#include <limits.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include "motionmap.h"

#define HIS_DEFAULT 6
#define HIS_LEN (8 * 25)

#define MOTIONMAP_PARAMS 8

#define P_DIFF_THRESHOLD 0
#define P_MAX_ENERGY     1
#define P_DRAW_MAP       2
#define P_HISTORY        3
#define P_DECAY          4
#define P_INTERPOLATE    5
#define P_ACTIVITY_MODE  6
#define P_ACTIVITY_DECAY 7

typedef struct {
    int32_t histogram_[HIS_LEN];

    uint8_t *region;
    uint8_t *bg_image;
    uint8_t *binary_img;
    uint8_t *diff_img;
    uint8_t *prev_img;
    uint8_t *interpolate_buf;

    int32_t max;
    int32_t global_max;
    uint32_t nframe_;

    int current_his_len;
    int current_decay;

    uint32_t key1_;
    uint32_t key2_;
    uint32_t keyv_;
    uint32_t keyp_;

    int have_bg;
    int running;
    int is_initialized;
    int do_interpolation;
    int stored_frame;
    int scale_lock;
    int activity_decay;
    int last_act_decay;
    int n_threads;
} motionmap_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int motionmap_absi(int v)
{
    const int m = v >> 31;
    return (v + m) ^ m;
}

static inline uint8_t motionmap_u8_sat(int v)
{
    return (uint8_t)clampi(v, 0, 255);
}

static inline uint8_t motionmap_lerp_u8(uint8_t prev, uint8_t cur, int q8)
{
    return (uint8_t)((int)prev + ((((int)cur - (int)prev) * q8 + (q8 >= 0 ? 128 : -128)) >> 8));
}

vj_effect *motionmap_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MOTIONMAP_PARAMS;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        if(ve->defaults)
            free(ve->defaults);
        if(ve->limits[0])
            free(ve->limits[0]);
        if(ve->limits[1])
            free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_DIFF_THRESHOLD] = 0; ve->limits[1][P_DIFF_THRESHOLD] = 255;        ve->defaults[P_DIFF_THRESHOLD] = 40;
    ve->limits[0][P_MAX_ENERGY] = 1;     ve->limits[1][P_MAX_ENERGY] = (w * h) / 20;  ve->defaults[P_MAX_ENERGY] = 1000;
    ve->limits[0][P_DRAW_MAP] = 0;       ve->limits[1][P_DRAW_MAP] = 1;               ve->defaults[P_DRAW_MAP] = 1;
    ve->limits[0][P_HISTORY] = 1;        ve->limits[1][P_HISTORY] = HIS_LEN;          ve->defaults[P_HISTORY] = HIS_DEFAULT;
    ve->limits[0][P_DECAY] = 1;          ve->limits[1][P_DECAY] = HIS_LEN;            ve->defaults[P_DECAY] = HIS_DEFAULT;
    ve->limits[0][P_INTERPOLATE] = 0;    ve->limits[1][P_INTERPOLATE] = 1;            ve->defaults[P_INTERPOLATE] = 0;
    ve->limits[0][P_ACTIVITY_MODE] = 0;  ve->limits[1][P_ACTIVITY_MODE] = 3;          ve->defaults[P_ACTIVITY_MODE] = 0;
    ve->limits[0][P_ACTIVITY_DECAY] = 0; ve->limits[1][P_ACTIVITY_DECAY] = 25 * 60;   ve->defaults[P_ACTIVITY_DECAY] = 0;

    ve->description = "Motion Mapping";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->global = 1;
    ve->n_out = 2;
    ve->static_bg = 1;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Difference Threshold",
        "Maximum Motion Energy",
        "Draw Motion Map",
        "History in frames",
        "Decay",
        "Interpolate frames",
        "Activity Mode",
        "Activity Decay"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_DRAW_MAP], P_DRAW_MAP, "Off", "On");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_INTERPOLATE], P_INTERPOLATE, "Off", "On");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_ACTIVITY_MODE], P_ACTIVITY_MODE, "Normal", "Local Max", "Global Max", "Hold Last");

    int energy_hi = (w * h) / 96;

    if(energy_hi < 500)
        energy_hi = 500;

    if(energy_hi > ve->limits[1][P_MAX_ENERGY])
        energy_hi = ve->limits[1][P_MAX_ENERGY];

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,    VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,              10,                 118,                14, 54,  800, 3000, 0,    82,
        VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS,              220,                energy_hi,          14, 54,  850, 3200, 0,    76,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,             4,                  72,                 4,  14, 3200, 8600, 2400, 20,
        VJ_BEAT_INERTIA,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS,             4,                  72,                 4,  14, 3200, 8600, 2400, 18,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                          VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_MEMORY,    VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 12, 360, 4, 14, 3600, 9200, 2600, 14
    );

    return ve;
}

void motionmap_free(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    if(!mm)
        return;

    free(mm->region);
    free(mm);
}

void *motionmap_malloc(int w, int h)
{
    motionmap_t *mm = (motionmap_t*) vj_calloc(sizeof(motionmap_t));

    if(!mm)
        return NULL;

    const int len = w * h;

    mm->region = (uint8_t*) vj_malloc((size_t)len * 8u);

    if(!mm->region) {
        free(mm);
        return NULL;
    }

    uint8_t *p = mm->region;

    mm->bg_image = p;        p += len;
    mm->binary_img = p;      p += len;
    mm->prev_img = p;        p += len;
    mm->diff_img = p;        p += len * 2;
    mm->interpolate_buf = p;

    mm->current_his_len = HIS_DEFAULT;
    mm->current_decay = HIS_DEFAULT;
    mm->last_act_decay = -1;
    mm->n_threads = vje_advise_num_threads(len);
    mm->is_initialized = 1;

    veejay_msg(2, "This is 'Motion Mapping'");
    veejay_msg(2, "This FX calculates motion energy activity levels over a period of time to scale FX parameters");
    veejay_msg(2, "Add any of the following to the FX chain (if not already present)");
    veejay_msg(2, "\tBathroom Window, Displacement Mapping, Multi Mirrors, Magic Mirror, Sinoids");
    veejay_msg(2, "\tSlice Window, Smear, ChameleonTV and TimeDistort TV");

    return (void*) mm;
}

uint8_t *motionmap_interpolate_buffer(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    return mm->interpolate_buf;
}

uint8_t *motionmap_bgmap(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    return mm->binary_img;
}

int motionmap_active(void *ptr)
{
    if(ptr == NULL)
        return 0;

    motionmap_t *mm = (motionmap_t*) ptr;

    return mm->running;
}

int motionmap_is_locked(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    return mm->scale_lock;
}

uint32_t motionmap_activity(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    return mm->keyv_;
}

int motionmap_instances(void *ptr)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    return mm->is_initialized;
}

void motionmap_scale_to(void *ptr,
                        int p1max,
                        int p2max,
                        int p1min,
                        int p2min,
                        int *p1val,
                        int *p2val,
                        int *pos,
                        int *len)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    if(mm->global_max == 0 || mm->scale_lock)
        return;

    if(mm->keyv_ > (uint32_t)mm->global_max)
        mm->keyv_ = (uint32_t)mm->global_max;

    const int n = (mm->nframe_ % mm->current_decay) + 1;
    const int64_t diff = (int64_t)mm->keyv_ - (int64_t)mm->keyp_;
    int64_t pu = (int64_t)mm->keyp_ + ((diff * n) / mm->current_decay);

    if(pu > mm->global_max)
        pu = mm->global_max;

    if(pu < 0)
        pu = 0;

    *p1val = p1min + (int)(((int64_t)(p1max - p1min) * pu + (mm->global_max >> 1)) / mm->global_max);
    *p2val = p2min + (int)(((int64_t)(p2max - p2min) * pu + (mm->global_max >> 1)) / mm->global_max);
    *len = mm->current_decay;
    *pos = n;
}

void motionmap_lerp_frame(void *ptr, VJFrame *cur, VJFrame *prev, int N, int n)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    if(mm->stored_frame == 0)
        return;

    const int n1 = ((n - 1) % N) + 1;
    const int q8 = (n1 * 256 + (N >> 1)) / N;
    const int len = cur->len;

    uint8_t *restrict Y0 = cur->data[0];
    uint8_t *restrict U0 = cur->data[1];
    uint8_t *restrict V0 = cur->data[2];

    const uint8_t *restrict Y1 = prev->data[0];
    const uint8_t *restrict U1 = prev->data[1];
    const uint8_t *restrict V1 = prev->data[2];

#pragma omp parallel for schedule(static) num_threads(mm->n_threads)
    for(int i = 0; i < len; i++) {
        Y0[i] = motionmap_lerp_u8(Y1[i], Y0[i], q8);
        U0[i] = motionmap_lerp_u8(U1[i], U0[i], q8);
        V0[i] = motionmap_lerp_u8(V1[i], V0[i], q8);
    }
}

void motionmap_store_frame(void *ptr, VJFrame *fx)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    if(mm->running == 0 || !mm->do_interpolation)
        return;

    const int len = fx->len;

    veejay_memcpy(mm->interpolate_buf, fx->data[0], len);
    veejay_memcpy(mm->interpolate_buf + len, fx->data[1], len);
    veejay_memcpy(mm->interpolate_buf + len + len, fx->data[2], len);

    mm->stored_frame = 1;
}

void motionmap_interpolate_frame(void *ptr, VJFrame *fx, int N, int n)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    if(mm->running == 0 || !mm->do_interpolation)
        return;

    VJFrame prev;

    veejay_memcpy(&prev, fx, sizeof(VJFrame));

    prev.data[0] = mm->interpolate_buf;
    prev.data[1] = mm->interpolate_buf + fx->len;
    prev.data[2] = mm->interpolate_buf + (2 * fx->len);

    motionmap_lerp_frame(ptr, fx, &prev, N, n);
}

static void motionmap_blur(uint8_t *restrict Y, int width, int height)
{
    const int len = width * height;

    for(int r = 0; r < len; r += width) {
        for(int c = 1; c < width - 1; c++)
            Y[r + c] = (uint8_t)((Y[r + c - 1] + Y[r + c] + Y[r + c + 1]) / 3);
    }

    (void)height;
}

static int32_t motionmap_activity_level(const uint8_t *restrict I, int len, int n_threads)
{
    int64_t level = 0;

#pragma omp parallel for reduction(+:level) schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++)
        level += I[i];

    return (int32_t)(level >> 8);
}

static void motionmap_calc_diff(const uint8_t *restrict bg,
                                uint8_t *restrict prev,
                                const uint8_t *restrict img,
                                uint8_t *restrict tmp1,
                                uint8_t *restrict tmp2,
                                uint8_t *restrict dst,
                                int len,
                                int threshold,
                                int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads)
    for(int i = 0; i < len; i++) {
        int a = motionmap_absi((int)bg[i] - (int)img[i]);
        int b = motionmap_absi((int)bg[i] - (int)prev[i]);
        int edge;
        int old;

        a = a < threshold ? 0 : 255;
        b = b < threshold ? 0 : 255;

        edge = motionmap_absi(a - b);
        old = dst[i] >> 1;

        tmp1[i] = (uint8_t)edge;
        tmp2[i] = (uint8_t)old;
        dst[i] = motionmap_u8_sat(edge + old);
        prev[i] = img[i];
    }
}

int motionmap_prepare(void *ptr, VJFrame *frame)
{
    const int len = frame->width * frame->height;
    motionmap_t *mm = (motionmap_t*) ptr;

    vj_frame_copy1(frame->data[0], mm->bg_image, len);
    motionmap_blur(mm->bg_image, frame->width, frame->height);
    veejay_memcpy(mm->prev_img, mm->bg_image, len);

    mm->have_bg = 1;
    mm->nframe_ = 0;
    mm->running = 0;
    mm->stored_frame = 0;
    mm->do_interpolation = 0;
    mm->scale_lock = 0;
    mm->key1_ = 0;
    mm->key2_ = 0;
    mm->keyv_ = 0;
    mm->keyp_ = 0;
    mm->max = 0;
    mm->global_max = 0;

    veejay_memset(mm->histogram_, 0, sizeof(mm->histogram_));
    veejay_memset(mm->binary_img, 0, len);

    veejay_msg(2, "Motion Mapping: Snapped background frame");

    return 1;
}

void motionmap_apply(void *ptr, VJFrame *frame, int *args)
{
    motionmap_t *mm = (motionmap_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;

    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int threshold = args[P_DIFF_THRESHOLD];
    const int limit = args[P_MAX_ENERGY] * 10;
    const int draw = args[P_DRAW_MAP];
    const int history = args[P_HISTORY];
    const int decay = args[P_DECAY];
    const int interpol = args[P_INTERPOLATE];
    const int activity_mode = args[P_ACTIVITY_MODE];
    const int act_decay = args[P_ACTIVITY_DECAY];

    if(!mm->have_bg) {
        veejay_msg(VEEJAY_MSG_ERROR, "Motion Mapping: Snap the background frame with VIMS 339 or mask button in reloaded");
        return;
    }

    if(act_decay != mm->last_act_decay) {
        mm->last_act_decay = act_decay;
        mm->activity_decay = act_decay;
    }

    motionmap_calc_diff(
        mm->bg_image,
        mm->prev_img,
        frame->data[0],
        mm->diff_img,
        mm->diff_img + len,
        mm->binary_img,
        len,
        threshold,
        mm->n_threads
    );

    if(draw) {
        vj_frame_clear1(Cb, 128, uv_len);
        vj_frame_clear1(Cr, 128, uv_len);
        vj_frame_copy1(mm->binary_img, frame->data[0], len);

        mm->running = 0;
        mm->stored_frame = 0;
        mm->scale_lock = 0;
        return;
    }

    int32_t activity_level = motionmap_activity_level(mm->binary_img, len, mm->n_threads);
    int32_t avg_actlvl = 0;
    int32_t min = INT_MAX;
    int32_t local_max = 0;

    mm->current_his_len = history;
    mm->current_decay = decay;
    mm->histogram_[mm->nframe_ % mm->current_his_len] = activity_level;

    for(int i = 0; i < mm->current_his_len; i++) {
        const int32_t v = mm->histogram_[i];

        avg_actlvl += v;

        if(v > mm->max)
            mm->max = v;
        if(v < min)
            min = v;
        if(v > local_max)
            local_max = v;
    }

    avg_actlvl /= mm->current_his_len;

    if(avg_actlvl < limit)
        avg_actlvl = 0;

    mm->nframe_++;

    switch(activity_mode) {
        case 0:
            if((mm->nframe_ % mm->current_his_len) == 0) {
                mm->key1_ = min;
                mm->key2_ = mm->max;
                mm->keyp_ = mm->keyv_;
                mm->keyv_ = avg_actlvl;
                mm->global_max = mm->max;
            }
            break;

        case 1:
            mm->key1_ = min;
            mm->key2_ = mm->max;
            mm->keyv_ = local_max;
            mm->global_max = local_max;
            break;

        case 2:
            mm->key1_ = min;
            mm->key2_ = mm->max;
            mm->keyp_ = mm->keyv_;
            mm->keyv_ = avg_actlvl;
            mm->global_max = mm->max;
            break;

        case 3:
            if((mm->nframe_ % mm->current_his_len) == 0) {
                mm->key1_ = min;
                mm->key2_ = mm->max;
                mm->keyp_ = mm->keyv_;
                mm->keyv_ = avg_actlvl;
                mm->global_max = mm->max;
            }

            mm->scale_lock = avg_actlvl == 0;

            if(mm->scale_lock && act_decay > 0) {
                mm->activity_decay--;

                if(mm->activity_decay == 0) {
                    mm->last_act_decay = 0;
                    mm->scale_lock = 0;
                }
            }
            break;
    }

    mm->running = 1;
    mm->do_interpolation = interpol;
}
