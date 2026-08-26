/* 
 * Linux VeeJay
 *
 * Copyright(C)2008-2026 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vj-msg.h>
#include <veejaycore/vjmem.h>
#include <libsubsample/subsample.h>
#include "softblur.h"
#include "hist.h"
#include "bgsubtract.h"

typedef struct {
    uint8_t *static_bg__;
    uint8_t *bg_frame__[4];
    int bg_ssm;
    unsigned int bg_n;
    int auto_hist;

    int skip_processing;
} bgsubtract_t;

vj_effect *bgsubtract_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    

    ve->limits[0][0] = 0; ve->limits[1][0] = 255; ve->defaults[0] = 45;
    ve->limits[0][1] = 0; ve->limits[1][1] = 2;   ve->defaults[1] = 2;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1;   ve->defaults[2] = 0;
    ve->limits[0][3] = 0; ve->limits[1][3] = 2;   ve->defaults[3] = 0;

    ve->description = "Subtract Background";
    ve->extra_frame = 0;
    ve->sub_format = -1;
    ve->has_user = 1;
    ve->global = 1;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "BG Method", "Enable", "Output Mode");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][1], 1, "Static BG", "CMA BG", "Adaptive BG");
    vje_build_value_hint_list(ve->hints, ve->limits[1][2], 2, "Generate/Show BG", "Subtract Background");
    vje_build_value_hint_list(ve->hints, ve->limits[1][3], 3, "Create Alpha Mask", "Black Background", "Show B&W Mask");

    ve->alpha = FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;
    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_MOTION_REACT, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_OFFSET_BASE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 8, 128, 76, 100, 24, 360, 0, 1, 0, VJ_BEAT_COST_CHEAP, 96, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    (void)width;
    (void)height;

    return ve;
}

void *bgsubtract_malloc(int width, int height)
{
    bgsubtract_t *b = (bgsubtract_t*) vj_calloc(sizeof(bgsubtract_t));

    if(!b)
        return NULL;

    const int plane_size = width * height;

    b->static_bg__ = (uint8_t*) vj_malloc((size_t)plane_size * 4u);

    if(!b->static_bg__) {
        free(b);
        return NULL;
    }

    b->bg_frame__[0] = b->static_bg__;
    b->bg_frame__[1] = b->bg_frame__[0] + plane_size;
    b->bg_frame__[2] = b->bg_frame__[1] + plane_size;
    b->bg_frame__[3] = b->bg_frame__[2] + plane_size;

    const char *hist = getenv("VEEJAY_BG_AUTO_HISTOGRAM_EQ");

    if(hist)
        b->auto_hist = atoi(hist);

    veejay_msg(VEEJAY_MSG_INFO, "You can enable/disable the histogram equalizer by setting env var VEEJAY_BG_AUTO_HISTOGRAM_EQ");
    veejay_msg(VEEJAY_MSG_INFO, "Histogram equalization is %s", b->auto_hist ? "enabled" : "disabled");

    return b;
}

void bgsubtract_free(void *ptr)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    if(!b)
        return;

    if(b->static_bg__)
        free(b->static_bg__);

    free(b);
}

int bgsubtract_prepare(void *ptr, VJFrame *frame)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    if(!b || !frame || !frame->data[0])
        return 0;

    if(b->auto_hist)
        vje_histogram_auto_eq_serial(frame);

    veejay_memcpy(b->bg_frame__[0], frame->data[0], frame->len);

    if(frame->ssm) {
        veejay_memcpy(b->bg_frame__[1], frame->data[1], frame->len);
        veejay_memcpy(b->bg_frame__[2], frame->data[2], frame->len);
        b->bg_ssm = 1;
    }
    else {
        veejay_memcpy(b->bg_frame__[1], frame->data[1], frame->uv_len);
        veejay_memcpy(b->bg_frame__[2], frame->data[2], frame->uv_len);
        chroma_supersample(SSM_422_444, frame, b->bg_frame__);
        b->bg_ssm = 1;
    }

    b->bg_n = 0;

    veejay_msg(2, "Subtract background: Snapped background frame (4:4:4 = %d)", b->bg_ssm);
    
    return 1;
}

uint8_t* bgsubtract_get_bg_frame(void *ptr, unsigned int plane)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    if(!b || plane > 3)
        return NULL;

    return b->bg_frame__[plane];
}

static void bgsubtract_show_bg(bgsubtract_t *b, VJFrame *frame)
{
    const int len = frame->len;
    uint8_t *restrict Y = frame->data[0];
    const uint8_t *restrict bgY = b->bg_frame__[0];

    #pragma omp for schedule(static)
    for(int i = 0; i < len; i++) {
        Y[i] = bgY[i];
    }

    if(b->bg_ssm && frame->ssm) {
        uint8_t *restrict U = frame->data[1];
        uint8_t *restrict V = frame->data[2];
        const uint8_t *restrict bgU = b->bg_frame__[1];
        const uint8_t *restrict bgV = b->bg_frame__[2];

        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++) {
            U[i] = bgU[i];
            V[i] = bgV[i];
        }
    }
    else {
        const int actual_uv_len = frame->ssm ? frame->len : frame->uv_len;
        uint8_t *restrict U = frame->data[1];
        uint8_t *restrict V = frame->data[2];

        #pragma omp for schedule(static)
        for(int i = 0; i < actual_uv_len; i++) {
            U[i] = 128;
            V[i] = 128;
        }
    }   
}

void bgsubtract_apply(void *ptr, VJFrame *frame, int *args)
{
    bgsubtract_t *b = (bgsubtract_t*) ptr;

    if(!b || !frame || !frame->data[0])
        return;

    const int threshold = args[0];
    const int method = args[1];
    const int enabled = args[2];
    
    const int initial_out_mode = args[3];
    const int has_alpha = (frame->data[3] != NULL);
    const int out_mode = (enabled != 0 && initial_out_mode == 0 && !has_alpha) ? 2 : initial_out_mode;

    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict U = frame->data[1];
    uint8_t *restrict V = frame->data[2];
    uint8_t *restrict A = frame->data[3];
    uint8_t *restrict BG = b->bg_frame__[0];

    if(b->auto_hist)
        vje_histogram_auto_eq(frame);

    #pragma omp single
    {
        if(enabled == 0 && method == 0) {
            b->skip_processing = 1;
        } else {
            b->skip_processing = 0;
        }
    }

    const int skip_processing = b->skip_processing;

    if(skip_processing) {
        bgsubtract_show_bg(b, frame);
    } 
    else {
        if(enabled == 0) {
            const unsigned int current_bg_n = b->bg_n > 65535U ? 65535U : b->bg_n;
            const unsigned int bg_n1 = current_bg_n + 1U;

            #pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                if(method == 1) {
                    BG[i] = (uint8_t)(((unsigned int)Y[i] + (current_bg_n * (unsigned int)BG[i])) / bg_n1);
                } else {
                    if(Y[i] > BG[i])
                        BG[i]++;
                    else if(Y[i] < BG[i])
                        BG[i]--;
                }
            }

            #pragma omp single
            {
                if(method == 1 && b->bg_n < 65535U)
                    b->bg_n++;
            }

            bgsubtract_show_bg(b, frame);
        } 
        else {
            #pragma omp for schedule(static)
            for(int i = 0; i < len; i++) {
                int diff = (int)Y[i] - (int)BG[i];
                diff = (diff ^ (diff >> 31)) - (diff >> 31);
                
                const int is_fg = diff > threshold;

                if(!is_fg && method == 2) {
                    if(Y[i] > BG[i])
                        BG[i]++;
                    else if(Y[i] < BG[i])
                        BG[i]--;
                }

                if(out_mode == 0) {
                    if(A) A[i] = is_fg ? 255 : 0;
                } else if(out_mode == 1) {
                    if(!is_fg)
                        Y[i] = 16;
                } else {
                    Y[i] = is_fg ? 255 : 0;
                }
            }

            if(out_mode == 1 || out_mode == 2) {
                const int actual_uv_len = frame->ssm ? frame->len : frame->uv_len;
                #pragma omp for schedule(static)
                for(int i = 0; i < actual_uv_len; i++) {
                    U[i] = 128;
                    V[i] = 128;
                }
            }
        }
    }
}