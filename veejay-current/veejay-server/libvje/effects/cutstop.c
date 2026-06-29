/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2016 Niels Elburg <nwelburg@gmail.com>
 *
 * vvCutStop - ported from vvFFPP_basic
 * Copyright(C)2005 Maciek Szczesniak <maciek@visualvinyl.net>
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
#include "cutstop.h"

typedef struct {
    uint8_t *vvcutstop_buffer[4];
    unsigned int frq_cnt;
    int n_threads;
} cutstop_t;

vj_effect *cutstop_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 4;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    ve->limits[0][0] = 0; ve->limits[1][0] = 255; ve->defaults[0] = 40;
    ve->limits[0][1] = 0; ve->limits[1][1] = 255; ve->defaults[1] = 50;
    ve->limits[0][2] = 0; ve->limits[1][2] = 1;   ve->defaults[2] = 0;
    ve->limits[0][3] = 0; ve->limits[1][3] = 1;   ve->defaults[3] = 0;

    ve->description = "vvCutStop";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Threshold", "Frame freq", "Cut mode", "Hold front/back");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_DETAIL,   VJ_BEAT_F_CONTINUOUS,                                                8,                  185,                10, 38, 1100, 3800, 0,    56,
        VJ_BEAT_SPEED,    VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_INVERTED | VJ_BEAT_F_NO_ZERO_CROSS, 3,                  190,                12, 50,  800, 3000, 0,    78,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,                             VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *cutstop_malloc(int width, int height)
{
    cutstop_t *c = (cutstop_t*) vj_calloc(sizeof(cutstop_t));

    if(!c)
        return NULL;

    const int len = width * height;

    if(len <= 0) {
        free(c);
        return NULL;
    }

    c->vvcutstop_buffer[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len * 3);

    if(!c->vvcutstop_buffer[0]) {
        free(c);
        return NULL;
    }

    c->vvcutstop_buffer[1] = c->vvcutstop_buffer[0] + len;
    c->vvcutstop_buffer[2] = c->vvcutstop_buffer[1] + len;
    c->vvcutstop_buffer[3] = NULL;

    veejay_memset(c->vvcutstop_buffer[0], pixel_Y_lo_, len);
    veejay_memset(c->vvcutstop_buffer[1], 128, len * 2);

    c->frq_cnt = 256;
    c->n_threads = vje_advise_num_threads(len);

    return c;
}

void cutstop_free(void *ptr)
{
    cutstop_t *c = (cutstop_t*) ptr;

    if(!c)
        return;

    if(c->vvcutstop_buffer[0])
        free(c->vvcutstop_buffer[0]);

    free(c);
}

static void cutstop_copy_held_444(uint8_t *restrict Yd,
                                  uint8_t *restrict Ud,
                                  uint8_t *restrict Vd,
                                  const uint8_t *restrict Yb,
                                  const uint8_t *restrict Ub,
                                  const uint8_t *restrict Vb,
                                  int len,
                                  int threshold,
                                  int cutmode,
                                  int holdmode)
{
    if(cutmode && !holdmode)
    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
        {
            if(threshold > Yb[i])
            {
                Yd[i] = Yb[i];
                Ud[i] = Ub[i];
                Vd[i] = Vb[i];
            }
        }
    }
    else if(cutmode && holdmode)
    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
        {
            if(threshold > Yd[i])
            {
                Yd[i] = Yb[i];
                Ud[i] = Ub[i];
                Vd[i] = Vb[i];
            }
        }
    }
    else if(!cutmode && holdmode)
    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
        {
            if(threshold < Yd[i])
            {
                Yd[i] = Yb[i];
                Ud[i] = Ub[i];
                Vd[i] = Vb[i];
            }
        }
    }
    else
    {
        #pragma omp for schedule(static)
        for(int i = 0; i < len; i++)
        {
            if(threshold < Yb[i])
            {
                Yd[i] = Yb[i];
                Ud[i] = Ub[i];
                Vd[i] = Vb[i];
            }
        }
    }
}

void cutstop_apply(void *ptr, VJFrame *frame, int *args)
{
    cutstop_t *c = (cutstop_t*) ptr;

    const int threshold = args[0];
    const int freq = args[1];
    const int cutmode = args[2];
    const int holdmode = args[3];
    const int len = frame->len;
    const int uv_len = frame->ssm ? frame->len : frame->uv_len;

    uint8_t *restrict Yb = c->vvcutstop_buffer[0];
    uint8_t *restrict Ub = c->vvcutstop_buffer[1];
    uint8_t *restrict Vb = c->vvcutstop_buffer[2];

    uint8_t *restrict Yd = frame->data[0];
    uint8_t *restrict Ud = frame->data[1];
    uint8_t *restrict Vd = frame->data[2];

    c->frq_cnt += (unsigned int)freq;

    if(freq == 255 || c->frq_cnt > 255)
    {
        veejay_memcpy(Yb, Yd, len);
        veejay_memcpy(Ub, Ud, uv_len);
        veejay_memcpy(Vb, Vd, uv_len);
        c->frq_cnt = 0;
    }

#pragma omp parallel num_threads(c->n_threads)
    {
        if(uv_len == len)
        {
            cutstop_copy_held_444(Yd, Ud, Vd, Yb, Ub, Vb, len, threshold, cutmode, holdmode);
        }
        else
        {
            if(cutmode && !holdmode)
            {
#pragma omp for schedule(static)
                for(int i = 0; i < len; i++)
                    if(threshold > Yb[i])
                        Yd[i] = Yb[i];
            }
            else if(cutmode && holdmode)
            {
#pragma omp for schedule(static)
                for(int i = 0; i < len; i++)
                    if(threshold > Yd[i])
                        Yd[i] = Yb[i];
            }
            else if(!cutmode && holdmode)
            {
#pragma omp for schedule(static)
                for(int i = 0; i < len; i++)
                    if(threshold < Yd[i])
                        Yd[i] = Yb[i];
            }
            else
            {
#pragma omp for schedule(static)
                for(int i = 0; i < len; i++)
                    if(threshold < Yb[i])
                        Yd[i] = Yb[i];
            }

#pragma omp for schedule(static)
            for(int i = 0; i < uv_len; i++)
            {
                Ud[i] = Ub[i];
                Vd[i] = Vb[i];
            }
        }
    }

}
