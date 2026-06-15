/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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

/*
 * This effect overlays 2 images.
 * It allows the user to set transparency per channel.
 * Result will vary over different color spaces.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "tripplicity.h"

#define TRIPPLICITY_PARAMS 5

#define P_OPACITY_Y      0
#define P_OPACITY_CB     1
#define P_OPACITY_CR     2
#define P_MIX_DRIVE      3
#define P_CHROMA_DRIVE   4

typedef struct {
    float op_y_env;
    float op_cb_env;
    float op_cr_env;
    float mix_drive_env;
    float chroma_drive_env;
    int initialized;

    int n_threads;
} tripplicity_t;

static inline int tripplicity_clampi(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

static inline uint8_t tripplicity_mix_u8(uint8_t a, uint8_t b, int q8)
{
    q8 = tripplicity_clampi(q8, 0, 256);
    return (uint8_t)((((int)a * (256 - q8)) + ((int)b * q8) + 128) >> 8);
}

static inline int tripplicity_to_q8(int v)
{
    v = tripplicity_clampi(v, 0, 255);
    return (v * 256 + 127) / 255;
}





static inline float tripplicity_slew(float oldv, float target, float coeff)
{
    return oldv + (target - oldv) * coeff;
}

static inline int tripplicity_add_q8(int q8, int add)
{
    return tripplicity_clampi(q8 + add, 0, 256);
}



vj_effect *tripplicity_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));
    if(!ve)
        return NULL;

    ve->num_params = TRIPPLICITY_PARAMS;

    ve->defaults  = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    if(!ve->defaults || !ve->limits[0] || !ve->limits[1]) {
        free(ve->defaults);
        free(ve->limits[0]);
        free(ve->limits[1]);
        free(ve);
        return NULL;
    }

    ve->limits[0][P_OPACITY_Y]    = 0; ve->limits[1][P_OPACITY_Y]    = 255;  ve->defaults[P_OPACITY_Y]    = 150;
    ve->limits[0][P_OPACITY_CB]   = 0; ve->limits[1][P_OPACITY_CB]   = 255;  ve->defaults[P_OPACITY_CB]   = 150;
    ve->limits[0][P_OPACITY_CR]   = 0; ve->limits[1][P_OPACITY_CR]   = 255;  ve->defaults[P_OPACITY_CR]   = 150;
    ve->limits[0][P_MIX_DRIVE]    = 0; ve->limits[1][P_MIX_DRIVE]    = 1000; ve->defaults[P_MIX_DRIVE]    = 260;
    ve->limits[0][P_CHROMA_DRIVE] = 0; ve->limits[1][P_CHROMA_DRIVE] = 1000; ve->defaults[P_CHROMA_DRIVE] = 180;

    ve->description = "Normal Overlay (per Channel)";
    ve->sub_format = -1;
    ve->extra_frame = 1;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Opacity Y",
        "Opacity Cb",
        "Opacity Cr",
        "Mix Drive",
        "Chroma Drive"
    );

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 48,  255,  12, 64, 120, 1600, 0, 82,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 32,  255,  12, 58, 120, 1600, 0, 72,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 32,  255,  12, 58, 120, 1600, 0, 72,
        VJ_BEAT_ALPHA_OR_OPACITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120, 1000, 18, 72, 80,  1300, 0, 94,
        VJ_BEAT_COLOR_AMOUNT,     VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, 120, 1000, 18, 72, 80,  1300, 0, 90
    );

    return ve;
}

void *tripplicity_malloc(int w, int h)
{
    tripplicity_t *t = (tripplicity_t*) vj_calloc(sizeof(tripplicity_t));
    if(!t)
        return NULL;

    t->op_y_env = 0.0f;
    t->op_cb_env = 0.0f;
    t->op_cr_env = 0.0f;
    t->mix_drive_env = 0.0f;
    t->chroma_drive_env = 0.0f;
    t->initialized = 0;

    t->n_threads = vje_advise_num_threads(w * h);

    return (void*) t;
}

void tripplicity_free(void *ptr)
{
    free(ptr);
}

void tripplicity_apply(void *ptr, VJFrame *frame, VJFrame *frame2, int *args)
{
    tripplicity_t *t = (tripplicity_t*) ptr;

    const int len = frame->len;
    const int uv_len = frame->ssm ? frame->len : frame->uv_len;

    const int op_y_arg = args[P_OPACITY_Y];
    const int op_cb_arg = args[P_OPACITY_CB];
    const int op_cr_arg = args[P_OPACITY_CR];
    const int mix_drive_arg = args[P_MIX_DRIVE];
    const int chroma_drive_arg = args[P_CHROMA_DRIVE];

    if(!t->initialized) {
        t->op_y_env = (float)op_y_arg;
        t->op_cb_env = (float)op_cb_arg;
        t->op_cr_env = (float)op_cr_arg;
        t->mix_drive_env = (float)mix_drive_arg;
        t->chroma_drive_env = (float)chroma_drive_arg;
        t->initialized = 1;
    }

    const float op_fast = 0.30f;
    const float drive_fast = 0.24f;

    t->op_y_env = tripplicity_slew(t->op_y_env, (float)op_y_arg, op_fast);
    t->op_cb_env = tripplicity_slew(t->op_cb_env, (float)op_cb_arg, op_fast * 0.90f);
    t->op_cr_env = tripplicity_slew(t->op_cr_env, (float)op_cr_arg, op_fast * 0.90f);
    t->mix_drive_env = tripplicity_slew(t->mix_drive_env, (float)mix_drive_arg, drive_fast);
    t->chroma_drive_env = tripplicity_slew(t->chroma_drive_env, (float)chroma_drive_arg, drive_fast);

    const int mix_drive = tripplicity_clampi((int)(t->mix_drive_env + 0.5f), 0, 1000);
    const int chroma_drive = tripplicity_clampi((int)(t->chroma_drive_env + 0.5f), 0, 1000);

    int qY  = tripplicity_to_q8((int)(t->op_y_env + 0.5f));
    int qCb = tripplicity_to_q8((int)(t->op_cb_env + 0.5f));
    int qCr = tripplicity_to_q8((int)(t->op_cr_env + 0.5f));

    qY  += ((256 - qY) * mix_drive + 500) / 1000;
    qCb += ((256 - qCb) * chroma_drive + 500) / 1000;
    qCr += ((256 - qCr) * chroma_drive + 500) / 1000;

    qY = tripplicity_clampi(qY, 0, 256);
    qCb = tripplicity_clampi(qCb, 0, 256);
    qCr = tripplicity_clampi(qCr, 0, 256);

    uint8_t *restrict Y1  = frame->data[0];
    uint8_t *restrict Cb1 = frame->data[1];
    uint8_t *restrict Cr1 = frame->data[2];

    const uint8_t *restrict Y2  = frame2->data[0];
    const uint8_t *restrict Cb2 = frame2->data[1];
    const uint8_t *restrict Cr2 = frame2->data[2];

#pragma omp parallel num_threads(t->n_threads)
    {
#pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
            Y1[i] = tripplicity_mix_u8(Y1[i], Y2[i], qY);

#pragma omp for schedule(static)
        for(int i = 0; i < uv_len; i++) {
            Cb1[i] = tripplicity_mix_u8(Cb1[i], Cb2[i], qCb);
            Cr1[i] = tripplicity_mix_u8(Cr1[i], Cr2[i], qCr);
        }
    }
}
