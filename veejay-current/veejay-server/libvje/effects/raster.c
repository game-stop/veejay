/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License , or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307 , USA.
 */

#include "common.h"
#include <veejaycore/vjmem.h>
#include "raster.h"

#define RASTER_PARAMS 5

#define P_GRID_SIZE      0
#define P_MODE           1
#define P_H_LINES        2
#define P_V_LINES        3
#define P_LINE_BLEND     4


static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline unsigned int raster_hash(unsigned int x, unsigned int y, unsigned int grid, unsigned int salt)
{
    unsigned int h = x * 374761393u + y * 668265263u + grid * 2246822519u + salt * 3266489917u;

    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;

    return h;
}

static inline int raster_pixel_energy_pow2(int coord, int preview_shift)
{
    const int scale_fp = 1 << (preview_shift + 8);
    const int half_fp = scale_fp >> 1;
    const int center_fp = (coord << 8) + 128;
    int dist = (center_fp & (scale_fp - 1)) - half_fp;

    if(dist < 0)
        dist = -dist;

    if(dist > half_fp)
        dist = scale_fp - dist;

    if(dist <= 128)
        return 255;

    int falloff = half_fp - 128;

    if(falloff < 128)
        falloff = 128;

    int energy = 255 - (((dist - 128) * 255) / falloff);

    return energy < 0 ? 0 : energy;
}

static inline int raster_pack_line_levels(int level0, int level1)
{
    return (clampi(level0, 0, 255) << 8) | clampi(level1, 0, 255);
}

static inline int raster_line_level_offset(int packed, int offset)
{
    return offset ? (packed & 255) : ((packed >> 8) & 255);
}

static inline int raster_line_is_off(int packed)
{
    return ((packed >> 8) & 255) == 0 && (packed & 255) == 0;
}

static inline int raster_line_pixel_energy(int coord)
{
    const int energy4 = raster_pixel_energy_pow2(coord, 2);
    const int energy8 = raster_pixel_energy_pow2(coord, 3);

    return energy4 < energy8 ? energy4 : energy8;
}

static inline int raster_resolve_line_level(int phase, int blend, unsigned int jitter)
{
    const int damage = 255 - blend;
    const int weakness = 255 - phase;
    const int tonal_loss = (damage * (32 + ((weakness * 150) >> 8))) >> 8;
    const int grain_loss = (int)(((jitter & 255u) * (unsigned int)damage) >> 9);
    int level = 255 - tonal_loss - grain_loss;

    const int cliff = (damage * damage) >> 8;
    const int drop_rank = ((weakness * 5) + (int)(jitter & 255u)) / 6;

    if(cliff > 96 && drop_rank > 384 - cliff)
        level -= (cliff - 80);

    if(level < 18)
        return 0;

    return level > 255 ? 255 : level;
}

static inline int raster_line_state(int line_start, int line_no, int grid, int keep, int blend, unsigned int axis)
{
    keep = clampi(keep, 0, 100);
    blend = clampi(blend, 0, 255);

    if(keep <= 0)
        return 0;

    if(keep >= 100 && blend >= 255)
        return raster_pack_line_levels(255, 255);

    const int phase0 = raster_line_pixel_energy(line_start);
    const int phase1 = raster_line_pixel_energy(line_start + 1);
    const int phase = (phase0 + phase1) >> 1;

    if(keep < 100) {
        const unsigned int h_keep = raster_hash((unsigned int)line_start, (unsigned int)line_no, (unsigned int)grid, axis);
        const int phase_rank = phase * 100 / 255;
        const int jitter = (int)((h_keep >> 24) & 99u);
        const int keep_rank = ((phase_rank * 68) + (jitter * 32)) / 100;

        if(keep_rank >= keep)
            return 0;
    }

    if(blend >= 255)
        return raster_pack_line_levels(255, 255);

    const unsigned int h = raster_hash((unsigned int)line_start, (unsigned int)line_no, (unsigned int)grid, axis + 211u);
    int level0 = raster_resolve_line_level(phase0, blend, h >> 8);
    int level1 = raster_resolve_line_level(phase1, blend, h >> 19);

    if(level0 && level1) {
        const int split = (int)((h >> 3) & 31u);

        if(level0 > level1 + 48 + split)
            level1 = (level1 * (160 + split)) >> 8;
        else if(level1 > level0 + 48 + split)
            level0 = (level0 * (160 + split)) >> 8;
    }

    return raster_pack_line_levels(level0, level1);
}

static inline uint8_t raster_blend_luma(uint8_t pixel_color, int level)
{
    const int delta = (int)pixel_color - 128;

    return (uint8_t)(128 + ((delta * clampi(level, 0, 255)) / 255));
}

static inline void raster_paint_pixel(uint8_t *restrict Y, uint8_t *restrict Cb, uint8_t *restrict Cr, int i, uint8_t pixel_color)
{
    Y[i] = pixel_color;
    Cb[i] = 128;
    Cr[i] = 128;
}

vj_effect *raster_init(int w, int h)
{
    (void)w;

    vj_effect *ve = (vj_effect *)vj_calloc(sizeof(vj_effect));

    if(!ve)
        return NULL;

    ve->num_params = RASTER_PARAMS;
    ve->defaults = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[0] = (int *)vj_calloc(sizeof(int) * ve->num_params);
    ve->limits[1] = (int *)vj_calloc(sizeof(int) * ve->num_params);

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

    int grid_max = h >> 2;

    if(grid_max < 4)
        grid_max = 4;

    ve->limits[0][P_GRID_SIZE] = 4;      ve->limits[1][P_GRID_SIZE] = grid_max; ve->defaults[P_GRID_SIZE] = 4;
    ve->limits[0][P_MODE] = 0;           ve->limits[1][P_MODE] = 1;             ve->defaults[P_MODE] = 1;
    ve->limits[0][P_H_LINES] = 0;        ve->limits[1][P_H_LINES] = 100;        ve->defaults[P_H_LINES] = 100;
    ve->limits[0][P_V_LINES] = 0;        ve->limits[1][P_V_LINES] = 100;        ve->defaults[P_V_LINES] = 100;
    ve->limits[0][P_LINE_BLEND] = 0; ve->limits[1][P_LINE_BLEND] = 255; ve->defaults[P_LINE_BLEND] = 255;

    ve->description = "Grid";
    ve->sub_format = 1;
    ve->extra_frame = 0;
    ve->has_user = 0;

    ve->param_description = vje_build_param_list(
        ve->num_params,
        "Grid size",
        "Mode",
        "Horizontal lines",
        "Vertical lines",
        "Line blend"
    );

    ve->hints = vje_init_value_hint_list(ve->num_params);
    vje_build_value_hint_list(ve->hints, ve->limits[1][P_MODE], P_MODE, "Black", "White");

    int grid_hi = grid_max;

    if(grid_hi > 32)
        grid_hi = 32;

    {
        const vj_beat_param_hint_t beat_hints[] = {
            VJ_BEAT_HINT_V2(VJ_BEAT_GRID_SIZE, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_DISCRETE | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_VELOCITY, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_EASE_OUT, 4, grid_hi, 86, 100, 12, 520, 0, 1, 100, VJ_BEAT_COST_CHEAP, 92, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_SELECTOR, VJ_BEAT_F_REJECT | VJ_BEAT_F_STRUCTURAL, VJ_BEAT_SRC_NONE, VJ_BEAT_OP_NONE, VJ_BEAT_POLARITY_POSITIVE, VJ_BEAT_CURVE_LINEAR, VJ_BEAT_SOFT_UNSET, VJ_BEAT_SOFT_UNSET, 0, 0, 0, 0, 0, 0, 0, VJ_BEAT_COST_STRUCTURAL, -1000, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_LOW_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_PUNCH, 10, 100, 82, 100, 4, 420, 20, 1, 0, VJ_BEAT_COST_CHEAP, 78, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_DENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_MID_ONSET, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_PUNCH, 8, 100, 82, 100, 4, 420, 20, 1, 0, VJ_BEAT_COST_CHEAP, 80, 0, 0, VJ_BEAT_GROUP_NONE, 0),
            VJ_BEAT_HINT_V2(VJ_BEAT_INTENSITY, VJ_BEAT_F_CONTINUOUS | VJ_BEAT_F_NO_ZERO_CROSS, VJ_BEAT_SRC_SCRATCH_BURST, VJ_BEAT_OP_MAP_RANGE, VJ_BEAT_POLARITY_NEGATIVE, VJ_BEAT_CURVE_PUNCH, 28, 255, 84, 100, 6, 480, 24, 1, 0, VJ_BEAT_COST_CHEAP, 84, 0, 0, VJ_BEAT_GROUP_NONE, 0)
        };
        ve->beat_hints = vje_build_beat_hint_list_v2(ve->num_params, beat_hints);
    }
    return ve;
}

static inline void raster_draw_horizontal(uint8_t *restrict Y, uint8_t *restrict Cb, uint8_t *restrict Cr, int row, int width, uint8_t pixel_color)
{
    for(int x = 0; x < width; x++)
        raster_paint_pixel(Y, Cb, Cr, row + x, pixel_color);
}

static inline void raster_draw_vertical_column(uint8_t *restrict Y, uint8_t *restrict Cb, uint8_t *restrict Cr, int x, int width, int height, uint8_t pixel_color)
{
    for(int y = 0, i = x; y < height; y++, i += width)
        raster_paint_pixel(Y, Cb, Cr, i, pixel_color);
}

void raster_apply(void *ptr, VJFrame *frame, int *args)
{
    (void)ptr;

    const int width = frame->width;
    const int height = frame->height;
    int grid_max = height >> 2;

    if(grid_max < 4)
        grid_max = 4;

    const int grid = clampi(args[P_GRID_SIZE], 4, grid_max);
    const int h_lines = clampi(args[P_H_LINES], 0, 100);
    const int v_lines = clampi(args[P_V_LINES], 0, 100);
    const int blend = clampi(args[P_LINE_BLEND], 0, 255);
    const uint8_t pixel_color = args[P_MODE] ? pixel_Y_hi_ : pixel_Y_lo_;
    const int n_threads = vje_advise_num_threads(frame->len);

    uint8_t *restrict Y = frame->data[0];
    uint8_t *restrict Cb = frame->data[1];
    uint8_t *restrict Cr = frame->data[2];

    const int h_count = (height + grid - 1) / grid;
    const int v_count = (width + grid - 1) / grid;

    if(h_lines >= 100 && v_lines >= 100 && blend >= 255) {
#pragma omp parallel num_threads(n_threads)
        {
#pragma omp for schedule(static)
            for(int line_no = 0; line_no < v_count; line_no++) {
                const int x0 = line_no * grid;

                raster_draw_vertical_column(Y, Cb, Cr, x0, width, height, pixel_color);

                if(x0 + 1 < width)
                    raster_draw_vertical_column(Y, Cb, Cr, x0 + 1, width, height, pixel_color);
            }

#pragma omp for schedule(static)
            for(int line_no = 0; line_no < h_count; line_no++) {
                const int y0 = line_no * grid;

                raster_draw_horizontal(Y, Cb, Cr, y0 * width, width, pixel_color);

                if(y0 + 1 < height)
                    raster_draw_horizontal(Y, Cb, Cr, (y0 + 1) * width, width, pixel_color);
            }
        }
        return;
    }

#pragma omp parallel num_threads(n_threads)
    {
#pragma omp for schedule(static)
        for(int line_no = 0; line_no < h_count; line_no++) {
            const int y0 = line_no * grid;
            const int h_state = raster_line_state(y0, line_no, grid, h_lines, blend, 17u);

            if(raster_line_is_off(h_state))
                continue;

            const int level0 = raster_line_level_offset(h_state, 0);
            const int level1 = raster_line_level_offset(h_state, 1);

            if(level0)
                raster_draw_horizontal(Y, Cb, Cr, y0 * width, width, raster_blend_luma(pixel_color, level0));

            if(y0 + 1 < height && level1)
                raster_draw_horizontal(Y, Cb, Cr, (y0 + 1) * width, width, raster_blend_luma(pixel_color, level1));
        }

#pragma omp for schedule(static)
        for(int line_no = 0; line_no < v_count; line_no++) {
            const int x0 = line_no * grid;
            const int state = raster_line_state(x0, line_no, grid, v_lines, blend, 29u);

            if(raster_line_is_off(state))
                continue;

            const int level0 = raster_line_level_offset(state, 0);
            const int level1 = raster_line_level_offset(state, 1);

            if(level0)
                raster_draw_vertical_column(Y, Cb, Cr, x0, width, height, raster_blend_luma(pixel_color, level0));

            if(x0 + 1 < width && level1)
                raster_draw_vertical_column(Y, Cb, Cr, x0 + 1, width, height, raster_blend_luma(pixel_color, level1));
        }
    }
}
