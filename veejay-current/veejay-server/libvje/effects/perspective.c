/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
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
#include <veejaycore/vjmem.h>
#include <libveejay/vj-viewport.h>
#include "perspective.h"

#define PERSPECTIVE_PARAMS 9

#define P_X1      0
#define P_Y1      1
#define P_X2      2
#define P_Y2      3
#define P_X3      4
#define P_Y3      5
#define P_X4      6
#define P_Y4      7
#define P_REVERSE 8

typedef struct {
    int perspective_[PERSPECTIVE_PARAMS];
    void *perspective_vp_;
    uint8_t *perspective_private_[4];
} perspective_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *perspective_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PERSPECTIVE_PARAMS;
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

    ve->defaults[P_X1] = 30; ve->defaults[P_Y1] = 30;
    ve->defaults[P_X2] = 70; ve->defaults[P_Y2] = 30;
    ve->defaults[P_X3] = 70; ve->defaults[P_Y3] = 70;
    ve->defaults[P_X4] = 30; ve->defaults[P_Y4] = 70;
    ve->defaults[P_REVERSE] = 0;

    for(int i = 0; i < P_REVERSE; i++) {
        ve->limits[0][i] = -100;
        ve->limits[1][i] = 100;
    }

    ve->limits[0][P_REVERSE] = 0;
    ve->limits[1][P_REVERSE] = 1;

    ve->description = "Perspective Tool";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params, 
        "Point 1 (X)", "Point 1 (Y)", "Point 2 (X)", "Point 2 (Y)",
        "Point 3 (X)", "Point 3 (Y)", "Point 4 (X)", "Point 4 (Y)",
        "Reverse"
    );
    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DRIFT, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }

    return ve;
}

void perspective_free(void *ptr)
{
    perspective_t *p = (perspective_t*) ptr;

    free(p->perspective_private_[0]);

    if(p->perspective_vp_)
        viewport_destroy(p->perspective_vp_);

    free(p);
}

void *perspective_malloc(int width, int height)
{
    perspective_t *p = (perspective_t*) vj_calloc(sizeof(perspective_t));

    if(!p)
        return NULL;

    const size_t plane_len = (size_t)width * (size_t)height + (size_t)width;

    p->perspective_private_[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * plane_len * 3u);

    if(!p->perspective_private_[0]) {
        free(p);
        return NULL;
    }

    p->perspective_private_[1] = p->perspective_private_[0] + plane_len;
    p->perspective_private_[2] = p->perspective_private_[1] + plane_len;
    p->perspective_private_[3] = NULL;

    for(int i = 0; i < PERSPECTIVE_PARAMS; i++)
        p->perspective_[i] = 0x7fffffff;

    return (void*) p;
}

static int perspective_map_changed(const perspective_t *p, const int *v)
{
    for(int i = 0; i < PERSPECTIVE_PARAMS; i++) {
        if(p->perspective_[i] != v[i])
            return 1;
    }

    return 0;
}

static void perspective_store_map(perspective_t *p, const int *v)
{
    for(int i = 0; i < PERSPECTIVE_PARAMS; i++)
        p->perspective_[i] = v[i];
}

void perspective_apply(void *ptr, VJFrame *frame, int *args)
{
    perspective_t *p = (perspective_t*) ptr;
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    int v[PERSPECTIVE_PARAMS];

    for(int i = 0; i < P_REVERSE; i++)
        v[i] = args[i];

    v[P_REVERSE] = args[P_REVERSE];

    if(perspective_map_changed(p, v)) {
        void *new_vp = viewport_fx_init_map(
            width,
            height,
            v[P_X1],
            v[P_Y1],
            v[P_X2],
            v[P_Y2],
            v[P_X3],
            v[P_Y3],
            v[P_X4],
            v[P_Y4],
            v[P_REVERSE]
        );

        if(new_vp) {
            if(p->perspective_vp_)
                viewport_destroy(p->perspective_vp_);

            p->perspective_vp_ = new_vp;
            perspective_store_map(p, v);
        }
        else if(!p->perspective_vp_) {
            return;
        }
    }

    int strides[4] = { len, len, len, 0 };

    vj_frame_copy(frame->data, p->perspective_private_, strides);
    viewport_process_dynamic(p->perspective_vp_, p->perspective_private_, frame->data);
}
