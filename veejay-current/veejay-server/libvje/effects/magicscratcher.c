/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
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
#include "internal.h"
#include "magicscratcher.h"

#define MAGICSCRATCHER_PARAMS 4

#define P_MODE           0
#define P_SCRATCH_FRAMES 1
#define P_PINGPONG       2
#define P_GRAYSCALE      3

typedef struct {
    uint8_t *mframe;
    int write_pos;
    int read_pos;
    int reverse;
    int seeded;
    int n_threads;
} magicscratcher_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

vj_effect *magicscratcher_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = MAGICSCRATCHER_PARAMS;
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

    const int scratch_soft_hi = (MAX_SCRATCH_FRAMES - 1) < 28 ? (MAX_SCRATCH_FRAMES - 1) : 28;

    ve->limits[0][P_MODE] = 0;           ve->limits[1][P_MODE] = VJ_EFFECT_BLEND_COUNT;          ve->defaults[P_MODE] = 1;
    ve->limits[0][P_SCRATCH_FRAMES] = 1; ve->limits[1][P_SCRATCH_FRAMES] = MAX_SCRATCH_FRAMES - 1; ve->defaults[P_SCRATCH_FRAMES] = 7;
    ve->limits[0][P_PINGPONG] = 0;       ve->limits[1][P_PINGPONG] = 1;                           ve->defaults[P_PINGPONG] = 1;
    ve->limits[0][P_GRAYSCALE] = 0;      ve->limits[1][P_GRAYSCALE] = 1;                          ve->defaults[P_GRAYSCALE] = 1;

    ve->description = "Magic Overlay Scratcher";
    ve->sub_format = -1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Mode", "Scratch frames", "PingPong", "Grayscale");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, VJ_EFFECT_BLEND_STRINGS);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_PINGPONG], P_PINGPONG, "Enabled", "Disabled");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_GRAYSCALE], P_GRAYSCALE, "Colorful", "Grayscale");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_MEMORY,   VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_DISCRETE, 2,                  scratch_soft_hi,      4, 16,2600, 7600, 1800, 16,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000,
        VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL,    VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0,    0,    0,   -1000
    );

    (void) w;
    (void) h;

    return ve;
}

void *magicscratcher_malloc(int w, int h)
{
    magicscratcher_t *m = (magicscratcher_t*) vj_calloc(sizeof(magicscratcher_t));

    if(!m)
        return NULL;

    m->mframe = (uint8_t*) vj_malloc((size_t)w * (size_t)h * (size_t)MAX_SCRATCH_FRAMES);

    if(!m->mframe) {
        free(m);
        return NULL;
    }

    m->n_threads = vje_advise_num_threads(w * h);

    return (void*) m;
}

void magicscratcher_free(void *ptr)
{
    magicscratcher_t *m = (magicscratcher_t*) ptr;

    free(m->mframe);
    free(m);
}

static void magicscratcher_seed(magicscratcher_t *m, const uint8_t *restrict Y, int len)
{
    for(int i = 0; i < MAX_SCRATCH_FRAMES; i++)
        veejay_memcpy(m->mframe + (size_t)len * (size_t)i, Y, len);

    m->write_pos = 0;
    m->read_pos = 0;
    m->reverse = 0;
    m->seeded = 1;
}

static void magicscratcher_advance_depth(magicscratcher_t *m, int n, int pingpong)
{
    if(n <= 1) {
        m->read_pos = 0;
        m->reverse = 0;
        return;
    }

    if(pingpong) {
        if(m->reverse) {
            m->read_pos--;

            if(m->read_pos <= 0) {
                m->read_pos = 0;
                m->reverse = 0;
            }
        }
        else {
            m->read_pos++;

            if(m->read_pos >= n - 1) {
                m->read_pos = n - 1;
                m->reverse = 1;
            }
        }
    }
    else {
        m->reverse = 0;
        m->read_pos++;

        if(m->read_pos >= n)
            m->read_pos = 0;
    }
}

void magicscratcher_apply(void *ptr, VJFrame *frame, int *args)
{
    magicscratcher_t *m = (magicscratcher_t*) ptr;

    const int mode = args[P_MODE];
    const int n = clampi(args[P_SCRATCH_FRAMES], 1, MAX_SCRATCH_FRAMES - 1);
    const int pingpong = args[P_PINGPONG];
    const int grayscale = args[P_GRAYSCALE];
    const int len = frame->len;

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    if(!m->seeded)
        magicscratcher_seed(m, Y, len);

    if(m->read_pos >= n)
        m->read_pos = n - 1;

    int read_slot = m->write_pos - 1 - m->read_pos;

    while(read_slot < 0)
        read_slot += MAX_SCRATCH_FRAMES;

    uint8_t *restrict history = m->mframe + (size_t)len * (size_t)read_slot;
    uint8_t *restrict write = m->mframe + (size_t)len * (size_t)m->write_pos;
    pix_func_Y func_y = get_pix_func_Y(mode);

#pragma omp parallel for schedule(static) num_threads(m->n_threads)
    for(int i = 0; i < len; i++) {
        const uint8_t src = Y[i];

        Y[i] = func_y(history[i], src);
        write[i] = src;
    }

    m->write_pos++;

    if(m->write_pos >= MAX_SCRATCH_FRAMES)
        m->write_pos = 0;

    if(grayscale) {
        const int uv_len = frame->ssm ? len : frame->uv_len;

        veejay_memset(Cb, 128, uv_len);
        veejay_memset(Cr, 128, uv_len);
    }

    magicscratcher_advance_depth(m, n, pingpong);
}
