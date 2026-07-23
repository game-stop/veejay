/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
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
#include "bathroom.h"
#include "motionmap.h"

typedef struct {
    uint8_t *bathroom_frame[4];
    void *motionmap;
    int n__;
    int N__;
} bathroom_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int bathroom_mini(int a, int b)
{
    return a < b ? a : b;
}

static inline int bathroom_maxi(int a, int b)
{
    return a > b ? a : b;
}

vj_effect *bathroom_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = 4;
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

    ve->limits[0][0] = 0; ve->limits[1][0] = 3;     ve->defaults[0] = 1;
    ve->limits[0][1] = 1; ve->limits[1][1] = 64;    ve->defaults[1] = 32;
    ve->limits[0][2] = 0; ve->limits[1][2] = width; ve->defaults[2] = 0;
    ve->limits[0][3] = 0; ve->limits[1][3] = width; ve->defaults[3] = width;

    ve->description = "Bathroom Window";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->motion = 1;
    ve->parallel = 0;
    ve->alpha = FLAG_ALPHA_SRC_A | FLAG_ALPHA_OUT | FLAG_ALPHA_OPTIONAL;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Distance", "X start position", "X end position");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][0], 0, "Horizontal", "Vertical", "Horizontal (Alpha)", "Vertical (Alpha)");

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_WINDOW_RADIUS, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_FLUX, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_PUNCH, 2, 64, 92, 100, 0, 240, 0, 1, 0, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_GEOMETRY_PHASE, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void *bathroom_malloc(int width, int height)
{
    bathroom_t *b = (bathroom_t*) vj_calloc(sizeof(bathroom_t));

    if(!b)
        return NULL;

    const int len = width * height;

    b->bathroom_frame[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len * 4);

    if(!b->bathroom_frame[0]) {
        free(b);
        return NULL;
    }

    b->bathroom_frame[1] = b->bathroom_frame[0] + len;
    b->bathroom_frame[2] = b->bathroom_frame[1] + len;
    b->bathroom_frame[3] = b->bathroom_frame[2] + len;

    return b;
}

void bathroom_free(void *ptr)
{
    bathroom_t *b = (bathroom_t*) ptr;

    if(b) {
        if(b->bathroom_frame[0])
            free(b->bathroom_frame[0]);
        free(b);
    }
}

static void bathroom_apply_noalpha(bathroom_t *b, VJFrame *frame, int val, int x0, int x1, int horiz)
{
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t **restrict bf = b->bathroom_frame;
    int strides[4] = { len, len, len, 0 };
    int mod_table[64];

    const int half_val = val >> 1;

    vj_frame_copy(frame->data, bf, strides);

    for(int i = 0; i < val; i++)
        mod_table[i] = i - half_val;

    if(horiz) {
        int y_mod = 0;

        for(int y = 0; y < height; y++) {
            const int y_offset = mod_table[y_mod];
            const int y_base = y * width;

            y_mod++;
            if(y_mod == val)
                y_mod = 0;

            for(int x = x0; x < x1; x++) {
                const int sx = clampi(x + y_offset, 0, width - 1);
                const int src_idx = y_base + sx;
                const int dst_idx = y_base + x;

                Y[dst_idx] = bf[0][src_idx];
                Cb[dst_idx] = bf[1][src_idx];
                Cr[dst_idx] = bf[2][src_idx];
            }
        }
    }
    else {
        for(int y = 0; y < height; y++) {
            const int y_base = y * width;
            int x_mod = 0;

            for(int x = x0; x < x1; x++) {
                const int y_offset = mod_table[x_mod];
                const int sy = clampi(y + y_offset, 0, height - 1);
                const int src_idx = sy * width + x;
                const int dst_idx = y_base + x;

                Y[dst_idx] = bf[0][src_idx];
                Cb[dst_idx] = bf[1][src_idx];
                Cr[dst_idx] = bf[2][src_idx];

                x_mod++;
                if(x_mod == val)
                    x_mod = 0;
            }
        }
    }
}

static void bathroom_apply_alpha(bathroom_t *b, VJFrame *frame, int val, int x0, int x1, int horiz)
{
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];
    uint8_t *restrict A = frame->data[3];

    uint8_t **restrict bf = b->bathroom_frame;
    int strides[4] = { len, len, len, len };
    int mod_table[64];

    const int half_val = val >> 1;

    vj_frame_copy(frame->data, bf, strides);

    for(int i = 0; i < val; i++)
        mod_table[i] = i - half_val;

    if(horiz) {
        int y_mod = 0;

        for(int y = 0; y < height; y++) {
            const int y_offset = mod_table[y_mod];
            const int y_base = y * width;

            y_mod++;
            if(y_mod == val)
                y_mod = 0;

            for(int x = x0; x < x1; x++) {
                const int sx = clampi(x + y_offset, 0, width - 1);
                const int src_idx = y_base + sx;
                const int dst_idx = y_base + x;

                Y[dst_idx] = bf[0][src_idx];
                Cb[dst_idx] = bf[1][src_idx];
                Cr[dst_idx] = bf[2][src_idx];
                A[dst_idx] = bf[3][src_idx];
            }
        }
    }
    else {
        for(int y = 0; y < height; y++) {
            const int y_base = y * width;
            int x_mod = 0;

            for(int x = x0; x < x1; x++) {
                const int y_offset = mod_table[x_mod];
                const int sy = clampi(y + y_offset, 0, height - 1);
                const int src_idx = sy * width + x;
                const int dst_idx = y_base + x;

                Y[dst_idx] = bf[0][src_idx];
                Cb[dst_idx] = bf[1][src_idx];
                Cr[dst_idx] = bf[2][src_idx];
                A[dst_idx] = bf[3][src_idx];

                x_mod++;
                if(x_mod == val)
                    x_mod = 0;
            }
        }
    }
}

void bathroom_apply(void *ptr, VJFrame *frame, int *args)
{
    bathroom_t *b = (bathroom_t*) ptr;

    int mode = args[0];
    int val = args[1];
    int x0 = clampi(args[2], 0, frame->width);
    int x1 = clampi(args[3], 0, frame->width);

    if(x0 > x1) {
        const int tmp = x0;
        x0 = x1;
        x1 = tmp;
    }

    if(x0 == x1)
        return;

    if((mode == 2 || mode == 3) && !frame->data[3])
        mode = mode == 2 ? 0 : 1;

    int tmp1 = val;
    int tmp2 = 0;
    int motion = 0;
    int interpolate = 1;

    if(b->motionmap && motionmap_active(b->motionmap)) {
        motionmap_scale_to(b->motionmap, 64, 64, 1, 1, &tmp1, &tmp2, &(b->n__), &(b->N__));
        motion = 1;
    }
    else {
        b->N__ = 0;
        b->n__ = 0;
    }

    tmp1 = clampi(tmp1, 1, 64);

    if(b->n__ == b->N__ || b->n__ == 0)
        interpolate = 0;

    switch(mode) {
        case 0:
            bathroom_apply_noalpha(b, frame, tmp1, x0, x1, 1);
            break;
        case 1:
            bathroom_apply_noalpha(b, frame, tmp1, x0, x1, 0);
            break;
        case 2:
            bathroom_apply_alpha(b, frame, tmp1, x0, x1, 1);
            break;
        case 3:
            bathroom_apply_alpha(b, frame, tmp1, x0, x1, 0);
            break;
    }

    if(b->motionmap && interpolate)
        motionmap_interpolate_frame(b->motionmap, frame, b->N__, b->n__);

    if(b->motionmap && motion)
        motionmap_store_frame(b->motionmap, frame);
}

int bathroom_request_fx(void)
{
    return VJ_IMAGE_EFFECT_MOTIONMAP_ID;
}

void bathroom_set_motionmap(void *ptr, void *priv)
{
    bathroom_t *b = (bathroom_t*) ptr;

    if(b)
        b->motionmap = priv;
}
