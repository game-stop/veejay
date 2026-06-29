/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "chromascratcher.h"
#include "chromamagick.h"

typedef struct {
    uint8_t *cframe[4];
    int cnframe;
    int cnreverse;
    int chroma_restart;
    int n_threads;
    VJFrame _tmp;
} chromascratcher_t;

static inline int chromascratcher_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *chromascratcher_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = MAX_SCRATCH_FRAMES - 1; ve->defaults[0] = 1;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255;                    ve->defaults[1] = 150;
    ve->limits[0][2] = 0; ve->limits[1][2] = 29;                     ve->defaults[2] = 8;
    ve->limits[0][3] = 0; ve->limits[1][3] = 1;                      ve->defaults[3] = 1;

    ve->description = "Matte Scratcher";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Frames", "Opacity", "Mode", "Pingpong");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][2], 2,
        "Appearing", "Dissapearing", "Appearing suppressed", "Dissappearing suppressed",
        "Add Subselect Luma", "Select Min", "Select Max", "Select Difference",
        "Select Difference Negate", "Add Luma", "Select Unfreeze", "Exclusive",
        "Difference Negate", "Additive", "Basecolor", "Freeze", "Unfreeze",
        "Hardlight", "Multiply", "Divide", "Subtract", "Add", "Screen",
        "Difference", "Softlight", "Dodge", "Reflect", "Difference Replace",
        "Darken", "Lighten", "Modulo Add"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_MEMORY,           VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 2,                  128,                4,  14, 3600, 9400, 2600, 22,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS,                                           48,                 255,                12, 48,  900, 3000, 0,    76,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                                                  VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR,         VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE,                       VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *chromascratcher_malloc(int w, int h)
{
    chromascratcher_t *c = (chromascratcher_t*) vj_calloc(sizeof(chromascratcher_t));

    if(!c)
        return NULL;

    const int len = w * h;
    const int plane_bank = len * MAX_SCRATCH_FRAMES;

    c->cframe[0] = (uint8_t *) vj_malloc(plane_bank * 3 * sizeof(uint8_t));

    if(!c->cframe[0]) {
        free(c);
        return NULL;
    }

    c->cframe[1] = c->cframe[0] + plane_bank;
    c->cframe[2] = c->cframe[1] + plane_bank;
    c->cframe[3] = NULL;
    c->n_threads = vje_advise_num_threads(len);

    vj_frame_clear1(c->cframe[0], pixel_Y_lo_, plane_bank);
    vj_frame_clear1(c->cframe[1], 128, plane_bank);
    vj_frame_clear1(c->cframe[2], 128, plane_bank);

    return c;
}

void chromascratcher_free(void *ptr)
{
    chromascratcher_t *c = (chromascratcher_t*) ptr;

    if(!c)
        return;

    if(c->cframe[0])
        free(c->cframe[0]);

    free(c);
}

static void chromastore_frame(chromascratcher_t *c, VJFrame *src, int n, int no_reverse)
{
    const int len = src->len;
    int cnframe = c->cnframe;
    int cnreverse = c->cnreverse;

    if(n <= 0)
        return;

    cnframe = chromascratcher_clampi(cnframe, 0, n - 1);

    uint8_t *dest[4] = {
        c->cframe[0] + (len * cnframe),
        c->cframe[1] + (len * cnframe),
        c->cframe[2] + (len * cnframe),
        NULL
    };

    int strides[4] = { len, len, len, 0 };

    if(!cnreverse)
        vj_frame_copy(src->data, dest, strides);
    else
        vj_frame_copy(dest, src->data, strides);

    if(cnreverse)
        cnframe--;
    else
        cnframe++;

    if(cnframe >= n) {
        if(no_reverse == 0) {
            cnreverse = 1;
            cnframe = n - 1;
        }
        else {
            cnframe = 0;
        }
    }

    if(cnframe <= 0) {
        cnframe = 0;
        cnreverse = 0;
    }

    c->cnreverse = cnreverse;
    c->cnframe = cnframe;
}

static void chromascratcher_apply_simple(chromascratcher_t *c, VJFrame *frame, int mode, int opacity, int offset)
{
    const int len = frame->len;
    const int n_threads = c->n_threads;
    const int op_a = opacity;
    const int op_b = 255 - op_a;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict SY = c->cframe[0] + offset;
    uint8_t *restrict SU = c->cframe[1] + offset;
    uint8_t *restrict SV = c->cframe[2] + offset;

#pragma omp parallel for num_threads(n_threads) schedule(static)
    for(int i = 0; i < len; i++)
    {
        int take = 0;

        switch(mode)
        {
            case 0:
                take = SY[i] < Y[i];
                break;
            case 1:
                take = SY[i] > Y[i];
                break;
            case 2:
                take = (SY[i] * op_a) < (Y[i] * op_b);
                break;
            case 3:
                take = (SY[i] * op_a) > (Y[i] * op_b);
                break;
        }

        if(take) {
            Y[i] = SY[i];
            Cb[i] = SU[i];
            Cr[i] = SV[i];
        }
    }
}

void chromascratcher_apply(void *ptr, VJFrame *frame, int *args)
{
    chromascratcher_t *c = (chromascratcher_t*) ptr;

    const int len = frame->len;


    int n = chromascratcher_clampi(args[0], 0, MAX_SCRATCH_FRAMES - 1);
    int opacity = args[1];
    int mode = chromascratcher_clampi(args[2], 0, 29);
    int no_reverse = args[3] == 0 ? 1 : 0;

    if(n <= 0) {
        c->cnframe = 0;
        c->cnreverse = 0;
        c->chroma_restart = no_reverse;
        return;
    }

    if(no_reverse != c->chroma_restart) {
        c->chroma_restart = no_reverse;
        c->cnreverse = 0;
        c->cnframe = 0;
    }

    c->cnframe = chromascratcher_clampi(c->cnframe, 0, n - 1);

    const int offset = len * c->cnframe;

    veejay_memcpy(&c->_tmp, frame, sizeof(VJFrame));
    c->_tmp.data[0] = c->cframe[0] + offset;
    c->_tmp.data[1] = c->cframe[1] + offset;
    c->_tmp.data[2] = c->cframe[2] + offset;
    c->_tmp.data[3] = NULL;
    c->_tmp.len = len;
    c->_tmp.uv_len = len;
    c->_tmp.ssm = 1;

    if(mode > 3) {
        int ch_args[2] = { mode - 3, opacity };
        chromamagick_apply(NULL, frame, &c->_tmp, ch_args);
    }
    else {
        chromascratcher_apply_simple(c, frame, mode, opacity, offset);
    }

    chromastore_frame(c, frame, n, no_reverse);
}
