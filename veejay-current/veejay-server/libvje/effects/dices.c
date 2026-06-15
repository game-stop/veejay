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
 *
 * This Effect was ported from:
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 *
 * dice.c: a 'dicing' effect
 *  copyright (c) 2001 Sam Mertens.  This code is subject to the provisions of
 *  the GNU Public License.
 *
 */

#include "common.h"
#include "dices.h"

#define VJ_IMAGE_EFFECT_DICES_ORIENTATION_DEFAULT 4

typedef enum _dice_dir
{
    Up = 0,
    Right = 1,
    Down = 2,
    Left = 3,
    Random = 4,
} DiceDir;

typedef struct {
    int g_map_width;
    int g_map_height;
    int g_cube_size;
    int g_cube_bits;
    uint8_t *g_dicemap;
    uint8_t *src[3];
    uint8_t g_orientation;
    int n_threads;
} dices_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int dices_max_bits(int width, int height)
{
    int near = width < height ? width : height;
    int bits = 0;

    while(bits < 30 && (1 << (bits + 1)) <= near)
        bits++;

    return bits;
}

static void dice_create_map_orientation(dices_t *d, int w, int h, int orientation)
{
    int i = 0;

    d->g_map_height = h >> d->g_cube_bits;
    d->g_map_width = w >> d->g_cube_bits;
    d->g_cube_size = 1 << d->g_cube_bits;

    if(d->g_map_height <= 0)
        d->g_map_height = 1;

    if(d->g_map_width <= 0)
        d->g_map_width = 1;

    const int maplen = d->g_map_width * d->g_map_height;

    for(i = 0; i < maplen; i++)
        d->g_dicemap[i] = (uint8_t)orientation;
}

static void dice_create_map(dices_t *d, int w, int h)
{
    d->g_map_height = h >> d->g_cube_bits;
    d->g_map_width = w >> d->g_cube_bits;
    d->g_cube_size = 1 << d->g_cube_bits;

    if(d->g_map_height <= 0)
        d->g_map_height = 1;

    if(d->g_map_width <= 0)
        d->g_map_width = 1;

    const int maplen = d->g_map_width * d->g_map_height;

    for(int i = 0; i < maplen; i++)
        d->g_dicemap[i] = (uint8_t)((rand() ^ (i * 1103515245)) & 0x03);
}

vj_effect *dices_init(int width, int height)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    ve->num_params = 2;
    ve->defaults = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *) vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *) vj_calloc(sizeof(int) * ve->num_params);

    const int max_bits = dices_max_bits(width, height);

    ve->defaults[0] = 4 <= max_bits ? 4 : max_bits;
    ve->defaults[1] = VJ_IMAGE_EFFECT_DICES_ORIENTATION_DEFAULT;

    ve->limits[0][0] = 0;
    ve->limits[1][0] = max_bits;
    ve->limits[0][1] = 0;
    ve->limits[1][1] = 4;

    ve->description = "Dices (EffectTV)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Dice size", "Orientation");
    ve->hints = vje_init_value_hint_list(ve->num_params);

    vje_build_value_hint_list(ve->hints, ve->limits[1][1], 1, "Up", "Right", "Down", "Left", "Random");

    ve->beat_hints = vje_build_beat_hint_list(
        ve->num_params,
        VJ_BEAT_GRID_SIZE, VJ_BEAT_F_PHRASE_ONLY | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, 1,                  max_bits,            4,  16, 3200, 8600, 2400, 24,
        VJ_BEAT_SELECTOR,  VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL | VJ_BEAT_F_REBUILDS_STATE,                              VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0,  0,    0,    0,    0,    -1000
    );

    return ve;
}

void *dices_malloc(int width, int height)
{
    dices_t *d = (dices_t*) vj_calloc(sizeof(dices_t));

    if(!d)
        return NULL;

    const int len = width * height;

    if(len <= 0) {
        free(d);
        return NULL;
    }

    d->g_dicemap = (uint8_t*) vj_malloc(sizeof(uint8_t) * len);
    d->src[0] = (uint8_t*) vj_malloc(sizeof(uint8_t) * len * 3);

    if(!d->g_dicemap || !d->src[0]) {
        dices_free(d);
        return NULL;
    }

    d->src[1] = d->src[0] + len;
    d->src[2] = d->src[1] + len;
    d->g_orientation = 255;
    d->g_cube_bits = -1;
    d->n_threads = vje_advise_num_threads(len);

    if(d->n_threads < 1)
        d->n_threads = 1;

    return d;
}

void dices_free(void *ptr)
{
    dices_t *d = (dices_t*) ptr;

    if(!d)
        return;

    if(d->g_dicemap)
        free(d->g_dicemap);

    if(d->src[0])
        free(d->src[0]);

    free(d);
}

static void dice_copy_block(dices_t *d,
                            uint8_t *restrict Y,
                            uint8_t *restrict Cb,
                            uint8_t *restrict Cr,
                            const uint8_t *restrict sY,
                            const uint8_t *restrict sCb,
                            const uint8_t *restrict sCr,
                            int width,
                            int base,
                            int dir)
{
    const int size = d->g_cube_size;

    switch(dir)
    {
        case Left:
            for(int dy = 0; dy < size; dy++)
            {
                for(int dx = 0; dx < size; dx++)
                {
                    const int si = base + dy * width + dx;
                    const int di = base + dx * width + (size - dy - 1);

                    Y[di] = sY[si];
                    Cb[di] = sCb[si];
                    Cr[di] = sCr[si];
                }
            }
            break;

        case Down:
            for(int dy = 0; dy < size; dy++)
            {
                for(int dx = 0; dx < size; dx++)
                {
                    const int si = base + (size - dy - 1) * width + (size - dx - 1);
                    const int di = base + dy * width + dx;

                    Y[di] = sY[si];
                    Cb[di] = sCb[si];
                    Cr[di] = sCr[si];
                }
            }
            break;

        case Right:
            for(int dy = 0; dy < size; dy++)
            {
                for(int dx = 0; dx < size; dx++)
                {
                    const int si = base + dy * width + dx;
                    const int di = base + dy + (size - dx - 1) * width;

                    Y[di] = sY[si];
                    Cb[di] = sCb[si];
                    Cr[di] = sCr[si];
                }
            }
            break;

        default:
            for(int dy = 0; dy < size; dy++)
            {
                for(int dx = 0; dx < size; dx++)
                {
                    const int si = base + dy * width + dx;
                    const int di = base + (size - dy - 1) * width + (size - dx - 1);

                    Y[di] = sY[si];
                    Cb[di] = sCb[si];
                    Cr[di] = sCr[si];
                }
            }
            break;
    }
}

void dices_apply(void *ptr, VJFrame *frame, int *args)
{
    dices_t *d = (dices_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    const int max_bits = dices_max_bits(width, height);
    const int cube_bits = clampi(args[0], 0, max_bits);
    const int orientation = clampi(args[1], 0, VJ_IMAGE_EFFECT_DICES_ORIENTATION_DEFAULT);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    uint8_t *restrict sY = d->src[0];
    uint8_t *restrict sCb = d->src[1];
    uint8_t *restrict sCr = d->src[2];

    if((cube_bits != d->g_cube_bits) || (orientation != d->g_orientation))
    {
        d->g_cube_bits = cube_bits;
        d->g_orientation = (uint8_t)orientation;

        if(orientation == VJ_IMAGE_EFFECT_DICES_ORIENTATION_DEFAULT)
            dice_create_map(d, width, height);
        else
            dice_create_map_orientation(d, width, height, orientation);
    }

    const int g_map_width = d->g_map_width;
    const int g_map_height = d->g_map_height;
    const int g_cube_bits = d->g_cube_bits;
    const int g_cube_size = d->g_cube_size;

    if(g_map_width <= 0 || g_map_height <= 0 || g_cube_size <= 0)
        return;

    veejay_memcpy(sY, Y, len);
    veejay_memcpy(sCb, Cb, len);
    veejay_memcpy(sCr, Cr, len);

    const unsigned int shift_w = (width - (g_cube_size * g_map_width)) >> 1;
    const unsigned int shift_h = (height - (g_cube_size * g_map_height)) >> 1;

    #pragma omp parallel for collapse(2) schedule(static) num_threads(d->n_threads)
    for(int map_y = 0; map_y < g_map_height; map_y++)
    {
        for(int map_x = 0; map_x < g_map_width; map_x++)
        {
            const int map_i = map_y * g_map_width + map_x;
            const int base = (shift_h + (map_y << g_cube_bits)) * width + (map_x << g_cube_bits) + shift_w;

            dice_copy_block(d, Y, Cb, Cr, sY, sCb, sCr, width, base, d->g_dicemap[map_i]);
        }
    }
}
