/* 
 * Linux VeeJay
 *
 * Copyright(C)2005 Niels Elburg <nwelburg@gmail.com>
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
#include "photoplay.h"

#define PHOTOPLAY_PARAMS 3

#define P_GRID_SIZE   0
#define P_FRAME_DELAY 1
#define P_MODE        2

typedef struct {
    picture_t **photo_list;
    int num_photos;
    int grid_size;
    int frame_counter;
    int frame_delay;
    int *rt;
    int last_mode;
} photoplay_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline int pp_mode_max(void)
{
    const int n = get_matrix_func_n();

    return n > 0 ? n : 1;
}

vj_effect *photoplay_init(int w, int h)
{
    vj_effect *ve = (vj_effect *) vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = PHOTOPLAY_PARAMS;
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

    const int max_grid_w = max_power(w);
    const int max_grid_h = max_power(h);
    int max_grid = max_grid_w < max_grid_h ? max_grid_w : max_grid_h;

    if(max_grid < 2)
        max_grid = 2;

    ve->limits[0][P_GRID_SIZE] = 2;   ve->limits[1][P_GRID_SIZE] = max_grid;       ve->defaults[P_GRID_SIZE] = 2;
    ve->limits[0][P_FRAME_DELAY] = 1; ve->limits[1][P_FRAME_DELAY] = 250;          ve->defaults[P_FRAME_DELAY] = 2;
    ve->limits[0][P_MODE] = 0;        ve->limits[1][P_MODE] = pp_mode_max();       ve->defaults[P_MODE] = 1;

    ve->description = "Photoplay (timestretched mosaic)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Photos",
        "Frame Delay",
        "Mode"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(
        ve->hints,
        ve->limits[1][P_MODE],
        P_MODE,
        "Random",
        "TopLeft to BottomRight : Horizontal",
        "TopLeft to BottomRight : Vertical",
        "BottomRight to TopLeft : Horizontal",
        "BottomRight to TopLeft : Vertical",
        "BottomLeft to TopRight : Horizontal",
        "TopRight to BottomLeft : Vertical",
        "TopRight to BottomLeft : Horizontal",
        "BottomLeft to TopRight : Vertical"
    );

    int photos_hi = ve->limits[1][P_GRID_SIZE];

    if(photos_hi > 8)
        photos_hi = 8;
    if(photos_hi < 2)
        photos_hi = 2;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_DISCRETE | VJ_BEAT_F_REBUILDS_STATE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_ACTIVITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_EASE_OUT, 2, photos_hi, 38, 72, 20, 1000, 0, 1, 2200, VJ_BEAT_COST_EXPENSIVE, 48, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SPEED, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 1, 96, 90, 100, 8, 420, 0, 1, 80, VJ_BEAT_COST_CHEAP, 100, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

static void destroy_filmstrip(photoplay_t *p)
{
    if(p->photo_list) {
        for(int i = 0; i < p->num_photos; i++) {
            if(p->photo_list[i]) {
                for(int j = 0; j < 3; j++)
                    free(p->photo_list[i]->data[j]);

                free(p->photo_list[i]);
            }
        }

        free(p->photo_list);
        p->photo_list = NULL;
    }

    free(p->rt);
    p->rt = NULL;

    p->num_photos = 0;
    p->grid_size = 0;
    p->frame_counter = 0;
    p->frame_delay = 0;
    p->last_mode = -1;
}

static int prepare_filmstrip(photoplay_t *p, int grid_size, int w, int h)
{
    const int film_length = grid_size * grid_size;
    const int picture_width = w / grid_size;
    const int picture_height = h / grid_size;
    const int picture_len = picture_width * picture_height;

    destroy_filmstrip(p);

    p->photo_list = (picture_t**) vj_calloc(sizeof(picture_t*) * (film_length + 1));

    if(!p->photo_list)
        goto fail;

    p->rt = (int*) vj_calloc(sizeof(int) * film_length);

    if(!p->rt)
        goto fail;

    p->num_photos = film_length;
    p->grid_size = grid_size;

    for(int i = 0; i < p->num_photos; i++) {
        p->photo_list[i] = (picture_t*) vj_calloc(sizeof(picture_t));

        if(!p->photo_list[i])
            goto fail;

        p->photo_list[i]->w = picture_width;
        p->photo_list[i]->h = picture_height;

        for(int j = 0; j < 3; j++) {
            p->photo_list[i]->data[j] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)picture_len);

            if(!p->photo_list[i]->data[j])
                goto fail;

            veejay_memset(p->photo_list[i]->data[j], j == 0 ? pixel_Y_lo_ : 128, picture_len);
        }
    }

    p->frame_counter = 0;
    p->frame_delay = 0;
    p->last_mode = -1;

    return 1;

fail:
    destroy_filmstrip(p);
    return 0;
}

void *photoplay_malloc(int w, int h)
{
    photoplay_t *p = (photoplay_t*) vj_calloc(sizeof(photoplay_t));

    if(!p)
        return NULL;

    p->last_mode = -1;

    (void)w;
    (void)h;

    return (void*) p;
}

void photoplay_free(void *ptr)
{
    photoplay_t *p = (photoplay_t*) ptr;

    destroy_filmstrip(p);
    free(p);
}

static void photoplay_reset_order(photoplay_t *p, int mode)
{
    for(int i = 0; i < p->num_photos; i++)
        p->rt[i] = i;

    if(mode == 0)
        fx_shuffle_int_array(p->rt, p->num_photos);

    p->last_mode = mode;
}

static void take_photo(photoplay_t *p,
                       const uint8_t *restrict src_plane,
                       uint8_t *restrict dst_plane,
                       int src_w,
                       int src_h,
                       int index)
{
    const int box_w = p->photo_list[index]->w;
    const int box_h = p->photo_list[index]->h;
    const int step_x = (src_w << 16) / box_w;
    const int step_y = (src_h << 16) / box_h;

    for(int dst_y = 0; dst_y < box_h; dst_y++) {
        const int src_y = (dst_y * step_y) >> 16;
        const uint8_t *restrict src_row = src_plane + src_y * src_w;
        uint8_t *restrict dst_row = dst_plane + dst_y * box_w;

        for(int dst_x = 0; dst_x < box_w; dst_x++)
            dst_row[dst_x] = src_row[(dst_x * step_x) >> 16];
    }
}

static void put_photo(photoplay_t *p,
                      uint8_t *restrict dst_plane,
                      const uint8_t *restrict photo,
                      int dst_w,
                      int dst_h,
                      int index,
                      matrix_t matrix)
{
    const int box_w = p->photo_list[index]->w;
    const int box_h = p->photo_list[index]->h;

    if(matrix.w < 0 || matrix.h < 0 || matrix.w >= dst_w || matrix.h >= dst_h)
        return;

    const int copy_w = matrix.w + box_w <= dst_w ? box_w : dst_w - matrix.w;
    const int copy_h = matrix.h + box_h <= dst_h ? box_h : dst_h - matrix.h;

    if(copy_w <= 0 || copy_h <= 0)
        return;

    uint8_t *restrict dst_ptr = dst_plane + matrix.h * dst_w + matrix.w;
    const uint8_t *restrict src_ptr = photo;

    for(int y = 0; y < copy_h; y++) {
        veejay_memcpy(dst_ptr, src_ptr, copy_w);
        dst_ptr += dst_w;
        src_ptr += box_w;
    }
}

void photoplay_apply(void *ptr, VJFrame *frame, int *args)
{
    photoplay_t *p = (photoplay_t*) ptr;

    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;
    const int uv_len = frame->ssm ? len : frame->uv_len;
    const int max_grid_w = max_power(width);
    const int max_grid_h = max_power(height);
    const int max_grid = max_grid_w < max_grid_h ? max_grid_w : max_grid_h;
    const int size = clampi(args[P_GRID_SIZE], 2, max_grid);
    const int delay = args[P_FRAME_DELAY];
    const int mode = clampi(args[P_MODE], 0, pp_mode_max());

    if((size * size) != p->num_photos || p->grid_size != size) {
        if(!prepare_filmstrip(p, size, width, height))
            return;

        photoplay_reset_order(p, mode);
    }
    else if(p->last_mode != mode) {
        photoplay_reset_order(p, mode);
    }

    if(p->frame_delay > 0)
        p->frame_delay--;

    if(p->frame_delay == 0) {
        const int photo_index = p->frame_counter % p->num_photos;

        for(int i = 0; i < 3; i++) {
            take_photo(
                p,
                frame->data[i],
                p->photo_list[photo_index]->data[i],
                width,
                height,
                photo_index
            );
        }

        p->frame_delay = delay;
        p->frame_counter++;
    }

    matrix_f matrix_placement = mode == 0 ? get_matrix_func(0) : get_matrix_func(mode - 1);

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    veejay_memset(dstY, pixel_Y_lo_, len);
    veejay_memset(dstU, 128, uv_len);
    veejay_memset(dstV, 128, uv_len);

    for(int i = 0; i < p->num_photos; i++) {
        const int photo_index = p->rt[i];
        matrix_t m = matrix_placement(i, size, width, height);

        put_photo(p, dstY, p->photo_list[photo_index]->data[0], width, height, photo_index, m);
        put_photo(p, dstU, p->photo_list[photo_index]->data[1], width, height, photo_index, m);
        put_photo(p, dstV, p->photo_list[photo_index]->data[2], width, height, photo_index, m);
    }
}
