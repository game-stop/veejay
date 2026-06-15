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
#include "magicphotos.h"

#define PHOTOPLAY_PARAMS 3

#define P_PHOTOS    0
#define P_WATERFALL 1
#define P_MODE      2

typedef struct {
    picture_t **photo_list;
    int num_photos;
    int grid_size;
    int frame_counter;
} photoplay_t;

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
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

    const int num_modes = get_matrix_func_n();

    ve->limits[0][P_PHOTOS] = 2;    ve->limits[1][P_PHOTOS] = 32;          ve->defaults[P_PHOTOS] = 2;
    ve->limits[0][P_WATERFALL] = 0; ve->limits[1][P_WATERFALL] = 1;        ve->defaults[P_WATERFALL] = 0;
    ve->limits[0][P_MODE] = 0;      ve->limits[1][P_MODE] = num_modes - 1; ve->defaults[P_MODE] = 1;

    ve->description = "Photoplay (timestretched mosaic)";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;
    ve->param_description = vje_build_param_list(ve->num_params, "Photos", "Waterfall", "Mode");

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_WATERFALL], P_WATERFALL, "Off", "On");
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE,
                              "Random",
                              "TopLeft to BottomRight : Horizontal",
                              "TopLeft to BottomRight : Vertical",
                              "BottomRight to TopLeft : Horizontal",
                              "BottomRight to TopLeft : Vertical",
                              "BottomLeft to TopRight : Horizontal",
                              "TopRight to BottomLeft : Vertical",
                              "TopRight to BottomLeft : Horizontal",
                              "BottomLeft to TopRight : Vertical");

    return ve;
}

static void destroy_filmstrip(photoplay_t *p)
{
    if(p->photo_list) {
        for(int i = 0; i < p->num_photos; i++) {
            picture_t *pic = p->photo_list[i];

            if(pic) {
                for(int j = 0; j < 3; j++)
                    free(pic->data[j]);

                free(pic);
            }
        }

        free(p->photo_list);
    }

    p->photo_list = NULL;
    p->num_photos = 0;
    p->grid_size = 0;
    p->frame_counter = 0;
}

static int prepare_filmstrip(photoplay_t *p, int grid_size, int w, int h)
{
    const int film_length = grid_size * grid_size;
    const int picture_width = w / grid_size;
    const int picture_height = h / grid_size;
    const int picture_len = picture_width * picture_height;

    p->photo_list = (picture_t**) vj_calloc(sizeof(picture_t*) * (film_length + 1));

    if(!p->photo_list)
        return 0;

    p->num_photos = film_length;
    p->grid_size = grid_size;

    for(int i = 0; i < p->num_photos; i++) {
        picture_t *pic = (picture_t*) vj_calloc(sizeof(picture_t));

        if(!pic)
            return 0;

        p->photo_list[i] = pic;
        pic->w = picture_width;
        pic->h = picture_height;

        for(int j = 0; j < 3; j++) {
            pic->data[j] = (uint8_t*) vj_malloc(sizeof(uint8_t) * (size_t)picture_len);

            if(!pic->data[j])
                return 0;

            veejay_memset(pic->data[j], j == 0 ? (i * 255) / p->num_photos : 128, picture_len);
        }
    }

    p->frame_counter = 0;

    return 1;
}

void *photoplay_malloc(int w, int h)
{
    (void) w;
    (void) h;

    return vj_calloc(sizeof(photoplay_t));
}

void photoplay_free(void *ptr)
{
    photoplay_t *p = (photoplay_t*) ptr;

    destroy_filmstrip(p);
    free(p);
}

void photoplay_apply(void *ptr, VJFrame *frame, int *args)
{
    photoplay_t *p = (photoplay_t*) ptr;

    const int grid_size = args[P_PHOTOS];
    const int waterfall = args[P_WATERFALL];
    const int mode = clampi(args[P_MODE], 0, get_matrix_func_n() - 1);
    const int width = frame->width;
    const int height = frame->height;
    const int len = frame->len;

    if((grid_size * grid_size) != p->num_photos || grid_size != p->grid_size) {
        destroy_filmstrip(p);

        if(!prepare_filmstrip(p, grid_size, width, height)) {
            destroy_filmstrip(p);
            return;
        }
    }

    uint8_t *restrict dstY = frame->data[0];
    uint8_t *restrict dstU = frame->data[1];
    uint8_t *restrict dstV = frame->data[2];

    const int current_slot = p->frame_counter % p->num_photos;

    take_photo(p, dstY, p->photo_list[current_slot]->data[0], width, height, current_slot);
    take_photo(p, dstU, p->photo_list[current_slot]->data[1], width, height, current_slot);
    take_photo(p, dstV, p->photo_list[current_slot]->data[2], width, height, current_slot);

    veejay_memset(dstY, pixel_Y_lo_, len);
    veejay_memset(dstU, 128, len);
    veejay_memset(dstV, 128, len);

    matrix_f matrix_placement = get_matrix_func(mode);

    for(int i = 0; i < p->num_photos; i++) {
        const int display_idx = waterfall ? (i + p->frame_counter) % p->num_photos : i;
        matrix_t m = matrix_placement(display_idx, grid_size, width, height);

        put_photo(p, dstY, p->photo_list[i]->data[0], width, height, i, m);
        put_photo(p, dstU, p->photo_list[i]->data[1], width, height, i, m);
        put_photo(p, dstV, p->photo_list[i]->data[2], width, height, i, m);
    }

    p->frame_counter++;
}
