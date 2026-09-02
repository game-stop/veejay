/*
 * Linux VeeJay
 *
 * Copyright(C)2026 Niels Elburg <nwelburg@gmail.com>
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
 */
#include <config.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-msg.h>
#include <libvje/vje.h>
#include <libvje/effects/common.h>
#include <veejaycore/yuvconv.h>
#include <libveejay/vj-output-graph.h>

#define VJ_OUTPUT_GRAPH_SOURCE_SCALE 10000

static const char *vj_output_pattern_name(int pattern)
{
    static const char *names[] = {
        "program", "black", "white", "gray", "color-bars",
        "grid", "checkerboard", "slice-ids", "blend-ramp", "structured-light"
    };
    return (pattern >= VJ_OUTPUT_PATTERN_PROGRAM &&
            pattern <= VJ_OUTPUT_PATTERN_STRUCTURED_LIGHT) ? names[pattern] : "unknown";
}

typedef struct {
    int *index;
    uint16_t *frac;
    uint16_t *gain;
    int length;
} vj_output_axis_map;

typedef struct {
    vj_output_slice_config config;
    vj_output_axis_map yx;
    vj_output_axis_map yy;
    vj_output_axis_map ux;
    vj_output_axis_map uy;
    int compiled;
    int source_width;
    int source_height;
    int source_uv_width;
    int source_uv_height;
} vj_output_slice;

struct vj_output_graph {
    int output_width;
    int output_height;
    int output_uv_width;
    int output_uv_height;
    int format;
    int pattern;
    int pattern_dirty;
    int structured_axis;
    int structured_bit;
    int structured_invert;
    int n_threads;
    pthread_mutex_t mutex;
    VJFrame *frame;
    uint8_t *buffer;
    vj_output_slice slices[VJ_OUTPUT_GRAPH_MAX_SLICES];
};

static int vj_output_graph_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static void vj_output_axis_free(vj_output_axis_map *map)
{
    if(!map)
        return;
    free(map->index);
    free(map->frac);
    free(map->gain);
    veejay_memset(map, 0, sizeof(*map));
}

static void vj_output_slice_free_maps(vj_output_slice *slice)
{
    vj_output_axis_free(&slice->yx);
    vj_output_axis_free(&slice->yy);
    vj_output_axis_free(&slice->ux);
    vj_output_axis_free(&slice->uy);
    slice->compiled = 0;
}

static int vj_output_axis_alloc(vj_output_axis_map *map, int length)
{
    vj_output_axis_free(map);
    if(length <= 0)
        return 0;
    map->index = (int*)vj_malloc(sizeof(int) * (size_t)length);
    map->frac = (uint16_t*)vj_malloc(sizeof(uint16_t) * (size_t)length);
    map->gain = (uint16_t*)vj_malloc(sizeof(uint16_t) * (size_t)length);
    if(!map->index || !map->frac || !map->gain) {
        vj_output_axis_free(map);
        return 0;
    }
    map->length = length;
    return 1;
}

static int vj_output_axis_build(vj_output_axis_map *map,
                                int dst_length,
                                int src_length,
                                int source_start,
                                int source_length,
                                int blend_start,
                                int blend_end,
                                int gamma100)
{
    if(!vj_output_axis_alloc(map, dst_length) || src_length <= 0)
        return 0;

    const double src0 = ((double)source_start * (double)src_length) /
                        (double)VJ_OUTPUT_GRAPH_SOURCE_SCALE;
    double span = ((double)source_length * (double)src_length) /
                  (double)VJ_OUTPUT_GRAPH_SOURCE_SCALE;
    if(span < 1.0)
        span = 1.0;
    const double gamma = gamma100 > 0 ? (double)gamma100 / 100.0 : 1.0;

    for(int i = 0; i < dst_length; i++) {
        const double t = dst_length > 1 ? (double)i / (double)(dst_length - 1) : 0.0;
        double source = src0 + t * (span - 1.0);
        if(source < 0.0)
            source = 0.0;
        if(source > (double)(src_length - 1))
            source = (double)(src_length - 1);
        int base = (int)source;
        double fraction = source - (double)base;
        map->index[i] = base;
        map->frac[i] = (uint16_t)(fraction * 65535.0 + 0.5);

        double gain = 1.0;
        if(blend_start > 0 && i < blend_start) {
            double edge = ((double)i + 0.5) / (double)blend_start;
            if(edge < gain)
                gain = edge;
        }
        if(blend_end > 0 && i >= dst_length - blend_end) {
            double edge = ((double)(dst_length - i) - 0.5) / (double)blend_end;
            if(edge < gain)
                gain = edge;
        }
        if(gain < 0.0)
            gain = 0.0;
        else if(gain > 1.0)
            gain = 1.0;
        if(gamma != 1.0 && gain > 0.0 && gain < 1.0)
            gain = pow(gain, gamma);
        map->gain[i] = (uint16_t)(gain * 256.0 + 0.5);
    }
    return 1;
}

static int vj_output_slice_compile(vj_output_graph *graph,
                                   vj_output_slice *slice,
                                   const VJFrame *input)
{
    const vj_output_slice_config *c = &slice->config;
    if(!c->enabled) {
        vj_output_slice_free_maps(slice);
        return 1;
    }

    int dx0 = vj_output_graph_clampi(c->dest_x, 0, graph->output_width);
    int dy0 = vj_output_graph_clampi(c->dest_y, 0, graph->output_height);
    int dx1 = vj_output_graph_clampi(c->dest_x + c->dest_width, 0, graph->output_width);
    int dy1 = vj_output_graph_clampi(c->dest_y + c->dest_height, 0, graph->output_height);
    const int dw = dx1 - dx0;
    const int dh = dy1 - dy0;
    if(dw <= 0 || dh <= 0)
        return 0;

    int ux0 = (dx0 * graph->output_uv_width) / graph->output_width;
    int uy0 = (dy0 * graph->output_uv_height) / graph->output_height;
    int ux1 = ((dx1 * graph->output_uv_width) + graph->output_width - 1) / graph->output_width;
    int uy1 = ((dy1 * graph->output_uv_height) + graph->output_height - 1) / graph->output_height;
    ux1 = vj_output_graph_clampi(ux1, ux0, graph->output_uv_width);
    uy1 = vj_output_graph_clampi(uy1, uy0, graph->output_uv_height);

    const int udw = ux1 - ux0;
    const int udh = uy1 - uy0;
    const int uv_bl = (c->blend_left * graph->output_uv_width + graph->output_width - 1) / graph->output_width;
    const int uv_br = (c->blend_right * graph->output_uv_width + graph->output_width - 1) / graph->output_width;
    const int uv_bt = (c->blend_top * graph->output_uv_height + graph->output_height - 1) / graph->output_height;
    const int uv_bb = (c->blend_bottom * graph->output_uv_height + graph->output_height - 1) / graph->output_height;

    if(!vj_output_axis_build(&slice->yx, dw, input->width,
                             c->source_x, c->source_width,
                             c->blend_left, c->blend_right, c->blend_gamma) ||
       !vj_output_axis_build(&slice->yy, dh, input->height,
                             c->source_y, c->source_height,
                             c->blend_top, c->blend_bottom, c->blend_gamma) ||
       !vj_output_axis_build(&slice->ux, udw, input->uv_width,
                             c->source_x, c->source_width,
                             uv_bl, uv_br, c->blend_gamma) ||
       !vj_output_axis_build(&slice->uy, udh, input->uv_height,
                             c->source_y, c->source_height,
                             uv_bt, uv_bb, c->blend_gamma))
    {
        vj_output_slice_free_maps(slice);
        return 0;
    }

    slice->source_width = input->width;
    slice->source_height = input->height;
    slice->source_uv_width = input->uv_width;
    slice->source_uv_height = input->uv_height;
    slice->compiled = 1;
    return 1;
}

static inline uint8_t vj_output_sample(const uint8_t *plane, int stride,
                                       int width, int height,
                                       int x0, int y0,
                                       uint32_t fx, uint32_t fy)
{
    int x1 = x0 + 1 < width ? x0 + 1 : x0;
    int y1 = y0 + 1 < height ? y0 + 1 : y0;
    const uint32_t wx = 65536u - fx;
    const uint32_t wy = 65536u - fy;
    const uint32_t a = (uint32_t)plane[y0 * stride + x0] * wx +
                       (uint32_t)plane[y0 * stride + x1] * fx;
    const uint32_t b = (uint32_t)plane[y1 * stride + x0] * wx +
                       (uint32_t)plane[y1 * stride + x1] * fx;
    return (uint8_t)(((uint64_t)a * wy + (uint64_t)b * fy + (1ULL << 31)) >> 32);
}

static inline uint8_t vj_output_blend_y(uint8_t dst, uint8_t src, unsigned int gain)
{
    return (uint8_t)(((unsigned int)src * gain + (unsigned int)dst * (256u - gain) + 128u) >> 8);
}

static inline uint8_t vj_output_blend_uv(uint8_t dst, uint8_t src, unsigned int gain)
{
    int value = 128 + ((((int)src - 128) * (int)gain +
                        ((int)dst - 128) * (int)(256u - gain) + 128) >> 8);
    return (uint8_t)vj_output_graph_clampi(value, 0, 255);
}

static void vj_output_render_plane(const uint8_t *src, int src_stride,
                                   int src_width, int src_height,
                                   uint8_t *dst, int dst_stride,
                                   int dst_x, int dst_y,
                                   const vj_output_axis_map *mx,
                                   const vj_output_axis_map *my,
                                   int chroma,
                                   int n_threads)
{
#pragma omp parallel for schedule(static) num_threads(n_threads) if(n_threads > 1 && mx->length * my->length >= 65536)
    for(int y = 0; y < my->length; y++) {
        const int sy = my->index[y];
        const uint32_t fy = my->frac[y];
        const unsigned int gy = my->gain[y];
        uint8_t *row = dst + (dst_y + y) * dst_stride + dst_x;
        for(int x = 0; x < mx->length; x++) {
            const unsigned int gain = (mx->gain[x] * gy + 128u) >> 8;
            if(gain == 0)
                continue;
            const uint8_t sample = vj_output_sample(src, src_stride,
                                                    src_width, src_height,
                                                    mx->index[x], sy,
                                                    mx->frac[x], fy);
            row[x] = chroma ? vj_output_blend_uv(row[x], sample, gain)
                            : vj_output_blend_y(row[x], sample, gain);
        }
    }
}

typedef struct {
    uint8_t y;
    uint8_t u;
    uint8_t v;
} vj_output_yuv;

static void vj_output_rgb_to_yuv(int r, int g, int b,
                                 uint8_t *y, uint8_t *u, uint8_t *v)
{
    int yy = 0;
    int uu = 128;
    int vv = 128;

    if(vje_get_rgb_parameter_conversion_type() == CCIR601_RGB)
        CCIR601_rgb2yuv(r, g, b, yy, uu, vv)
    else
        GIMP_rgb2yuv(r, g, b, yy, uu, vv)

    *y = (uint8_t)yy;
    *u = (uint8_t)uu;
    *v = (uint8_t)vv;
}

static void vj_output_graph_black(vj_output_graph *graph)
{
    uint8_t y, u, v;
    vj_output_rgb_to_yuv(0, 0, 0, &y, &u, &v);
    veejay_memset(graph->frame->data[0], y, (size_t)graph->frame->len);
    veejay_memset(graph->frame->data[1], u, (size_t)graph->frame->uv_len);
    veejay_memset(graph->frame->data[2], v, (size_t)graph->frame->uv_len);
}

#define VJ_OUTPUT_GRID_CELL 64
#define VJ_OUTPUT_CHECKER_CELL 32
#define VJ_OUTPUT_GRID_PERIOD2 (VJ_OUTPUT_GRID_CELL * 2)

static int vj_output_centered_grid_line(int coordinate, int extent)
{
    int remainder = ((coordinate * 2 + 1) - extent) % VJ_OUTPUT_GRID_PERIOD2;
    if(remainder < 0)
        remainder += VJ_OUTPUT_GRID_PERIOD2;
    const int distance = remainder < VJ_OUTPUT_GRID_PERIOD2 - remainder ?
                         remainder : VJ_OUTPUT_GRID_PERIOD2 - remainder;
    return distance <= 1;
}

static int vj_output_center_line(int coordinate, int extent)
{
    return abs((coordinate * 2 + 1) - extent) <= 1;
}

static vj_output_yuv vj_output_rgb_value(int r, int g, int b)
{
    vj_output_yuv value;
    vj_output_rgb_to_yuv(r, g, b, &value.y, &value.u, &value.v);
    return value;
}

static void vj_output_pattern_yuv(const vj_output_graph *graph, int x, int y,
                                  uint8_t *yy, uint8_t *uu, uint8_t *vv)
{
    static const uint8_t bars[8][3] = {
        {255,255,255}, {255,255,0}, {0,255,255}, {0,255,0},
        {255,0,255}, {255,0,0}, {0,0,255}, {0,0,0}
    };
    vj_output_yuv value = vj_output_rgb_value(0, 0, 0);

    switch(graph->pattern) {
        case VJ_OUTPUT_PATTERN_BLACK:
            break;
        case VJ_OUTPUT_PATTERN_WHITE:
            value = vj_output_rgb_value(255, 255, 255);
            break;
        case VJ_OUTPUT_PATTERN_GRAY:
            value = vj_output_rgb_value(128, 128, 128);
            break;
        case VJ_OUTPUT_PATTERN_COLOR_BARS: {
            int index = (int)(((int64_t)x * 8) / graph->output_width);
            index = vj_output_graph_clampi(index, 0, 7);
            value = vj_output_rgb_value(bars[index][0],
                                        bars[index][1],
                                        bars[index][2]);
            break;
        }
        case VJ_OUTPUT_PATTERN_CHECKER:
            value = (((x / VJ_OUTPUT_CHECKER_CELL) +
                      (y / VJ_OUTPUT_CHECKER_CELL)) & 1) ?
                    vj_output_rgb_value(204, 204, 204) :
                    vj_output_rgb_value(32, 32, 32);
            break;
        case VJ_OUTPUT_PATTERN_BLEND: {
            const int level = graph->output_width > 1 ?
                              (int)(((int64_t)x * 255) /
                                    (graph->output_width - 1)) : 0;
            value = vj_output_rgb_value(level, level, level);
            if((x % VJ_OUTPUT_GRID_CELL) == 0 ||
               (y % VJ_OUTPUT_GRID_CELL) == 0)
                value = vj_output_rgb_value(255, 0, 0);
            break;
        }
        case VJ_OUTPUT_PATTERN_STRUCTURED_LIGHT: {
            const int coordinate = graph->structured_axis == 0 ? x : y;
            const unsigned int gray = (unsigned int)coordinate ^
                                      ((unsigned int)coordinate >> 1);
            int on = (int)((gray >> graph->structured_bit) & 1u);
            if(graph->structured_invert)
                on = !on;
            value = on ? vj_output_rgb_value(255, 255, 255)
                       : vj_output_rgb_value(0, 0, 0);
            break;
        }
        case VJ_OUTPUT_PATTERN_GRID:
        default: {
            const int centre = vj_output_center_line(x, graph->output_width) ||
                               vj_output_center_line(y, graph->output_height);
            const int line = vj_output_centered_grid_line(x, graph->output_width) ||
                             vj_output_centered_grid_line(y, graph->output_height);
            if(centre)
                value = vj_output_rgb_value(255, 0, 0);
            else if(line)
                value = vj_output_rgb_value(255, 255, 255);
            break;
        }
    }

    *yy = value.y;
    *uu = value.u;
    *vv = value.v;
}

static void vj_output_graph_fill_rect_yuv(vj_output_graph *graph,
                                          int x0, int y0, int x1, int y1,
                                          uint8_t yy, uint8_t uu, uint8_t vv)
{
    x0 = vj_output_graph_clampi(x0, 0, graph->output_width);
    y0 = vj_output_graph_clampi(y0, 0, graph->output_height);
    x1 = vj_output_graph_clampi(x1, x0, graph->output_width);
    y1 = vj_output_graph_clampi(y1, y0, graph->output_height);
    if(x1 <= x0 || y1 <= y0)
        return;

    for(int y = y0; y < y1; y++)
        veejay_memset(graph->frame->data[0] + y * graph->output_width + x0,
               yy, (size_t)(x1 - x0));

    const int ux0 = x0 * graph->output_uv_width / graph->output_width;
    const int uy0 = y0 * graph->output_uv_height / graph->output_height;
    const int ux1 = (x1 * graph->output_uv_width + graph->output_width - 1) /
                    graph->output_width;
    const int uy1 = (y1 * graph->output_uv_height + graph->output_height - 1) /
                    graph->output_height;
    for(int y = uy0; y < uy1; y++) {
        veejay_memset(graph->frame->data[1] + y * graph->output_uv_width + ux0,
               uu, (size_t)(ux1 - ux0));
        veejay_memset(graph->frame->data[2] + y * graph->output_uv_width + ux0,
               vv, (size_t)(ux1 - ux0));
    }
}

static void vj_output_graph_draw_digit(vj_output_graph *graph,
                                       int digit,
                                       int x0, int y0, int x1, int y1)
{
    static const uint8_t glyphs[8][7] = {
        {0x04,0x0c,0x04,0x04,0x04,0x04,0x0e},
        {0x0e,0x11,0x01,0x02,0x04,0x08,0x1f},
        {0x1e,0x01,0x01,0x0e,0x01,0x01,0x1e},
        {0x02,0x06,0x0a,0x12,0x1f,0x02,0x02},
        {0x1f,0x10,0x10,0x1e,0x01,0x01,0x1e},
        {0x0e,0x10,0x10,0x1e,0x11,0x11,0x0e},
        {0x1f,0x01,0x02,0x04,0x08,0x08,0x08},
        {0x0e,0x11,0x11,0x0e,0x11,0x11,0x0e}
    };
    if(digit < 1 || digit > 8)
        return;

    const int width = x1 - x0;
    const int height = y1 - y0;
    int scale = (width - 12) / 5;
    const int vertical_scale = (height - 12) / 7;
    if(vertical_scale < scale)
        scale = vertical_scale;
    scale = vj_output_graph_clampi(scale, 1, 36);
    if(width < 9 || height < 11)
        return;

    const int glyph_width = 5 * scale;
    const int glyph_height = 7 * scale;
    const int gx = x0 + (width - glyph_width) / 2;
    const int gy = y0 + (height - glyph_height) / 2;
    uint8_t white_y, white_u, white_v;
    vj_output_rgb_to_yuv(255, 255, 255, &white_y, &white_u, &white_v);

    for(int row = 0; row < 7; row++) {
        const uint8_t bits = glyphs[digit - 1][row];
        for(int column = 0; column < 5; column++) {
            if(!(bits & (1u << (4 - column))))
                continue;
            const int px = gx + column * scale;
            const int py = gy + row * scale;
            vj_output_graph_fill_rect_yuv(graph,
                                          px, py, px + scale, py + scale,
                                          white_y, white_u, white_v);
        }
    }
}

static void vj_output_graph_draw_slice_ids(vj_output_graph *graph)
{
    static const uint8_t colors[8][3] = {
        {255,64,64},{64,255,64},{64,64,255},{255,255,64},
        {255,64,255},{64,255,255},{255,160,64},{192,192,192}
    };
    uint8_t bg_y, bg_u, bg_v;
    uint8_t white_y, white_u, white_v;
    vj_output_rgb_to_yuv(12, 16, 22, &bg_y, &bg_u, &bg_v);
    vj_output_rgb_to_yuv(255, 255, 255, &white_y, &white_u, &white_v);
    vj_output_graph_fill_rect_yuv(graph, 0, 0,
                                  graph->output_width, graph->output_height,
                                  bg_y, bg_u, bg_v);

    for(int s = 0; s < VJ_OUTPUT_GRAPH_MAX_SLICES; s++) {
        const vj_output_slice_config *c = &graph->slices[s].config;
        if(!c->enabled)
            continue;

        const int x0 = vj_output_graph_clampi(c->dest_x, 0, graph->output_width);
        const int y0 = vj_output_graph_clampi(c->dest_y, 0, graph->output_height);
        const int x1 = vj_output_graph_clampi(c->dest_x + c->dest_width,
                                              x0, graph->output_width);
        const int y1 = vj_output_graph_clampi(c->dest_y + c->dest_height,
                                              y0, graph->output_height);
        if(x1 <= x0 || y1 <= y0)
            continue;

        uint8_t yy, uu, vv;
        vj_output_rgb_to_yuv(colors[s][0], colors[s][1], colors[s][2],
                             &yy, &uu, &vv);
        vj_output_graph_fill_rect_yuv(graph, x0, y0, x1, y1, yy, uu, vv);

        const int short_side = (x1 - x0) < (y1 - y0) ?
                               (x1 - x0) : (y1 - y0);
        const int border = vj_output_graph_clampi(short_side / 80, 2, 8);
        vj_output_graph_fill_rect_yuv(graph, x0, y0, x1, y0 + border,
                                      white_y, white_u, white_v);
        vj_output_graph_fill_rect_yuv(graph, x0, y1 - border, x1, y1,
                                      white_y, white_u, white_v);
        vj_output_graph_fill_rect_yuv(graph, x0, y0 + border,
                                      x0 + border, y1 - border,
                                      white_y, white_u, white_v);
        vj_output_graph_fill_rect_yuv(graph, x1 - border, y0 + border,
                                      x1, y1 - border,
                                      white_y, white_u, white_v);
        vj_output_graph_draw_digit(graph, s + 1,
                                   x0 + border, y0 + border,
                                   x1 - border, y1 - border);
    }
}

static void vj_output_graph_generate_pattern(vj_output_graph *graph)
{
    if(graph->pattern == VJ_OUTPUT_PATTERN_SLICE_IDS) {
        vj_output_graph_draw_slice_ids(graph);
        return;
    }

#pragma omp parallel for schedule(static) num_threads(graph->n_threads) if(graph->n_threads > 1 && graph->output_width * graph->output_height >= 65536)
    for(int y = 0; y < graph->output_height; y++) {
        uint8_t *row = graph->frame->data[0] + y * graph->output_width;
        for(int x = 0; x < graph->output_width; x++) {
            uint8_t yy, uu, vv;
            vj_output_pattern_yuv(graph, x, y, &yy, &uu, &vv);
            row[x] = yy;
        }
    }

#pragma omp parallel for schedule(static) num_threads(graph->n_threads) if(graph->n_threads > 1 && graph->output_uv_width * graph->output_uv_height >= 32768)
    for(int y = 0; y < graph->output_uv_height; y++) {
        uint8_t *urow = graph->frame->data[1] + y * graph->output_uv_width;
        uint8_t *vrow = graph->frame->data[2] + y * graph->output_uv_width;
        const int ly = vj_output_graph_clampi(
            (int)(((int64_t)(2 * y + 1) * graph->output_height) /
                  (2 * graph->output_uv_height)),
            0, graph->output_height - 1);
        for(int x = 0; x < graph->output_uv_width; x++) {
            const int lx = vj_output_graph_clampi(
                (int)(((int64_t)(2 * x + 1) * graph->output_width) /
                      (2 * graph->output_uv_width)),
                0, graph->output_width - 1);
            uint8_t yy, uu, vv;
            vj_output_pattern_yuv(graph, lx, ly, &yy, &uu, &vv);
            urow[x] = uu;
            vrow[x] = vv;
        }
    }
}

static int vj_output_graph_needs_clear(const vj_output_graph *graph)
{
    const vj_output_slice_config *only = NULL;
    for(int i = 0; i < VJ_OUTPUT_GRAPH_MAX_SLICES; i++) {
        const vj_output_slice_config *c = &graph->slices[i].config;
        if(!c->enabled)
            continue;
        if(only)
            return 1;
        only = c;
    }
    if(!only)
        return 1;
    return only->dest_x != 0 || only->dest_y != 0 ||
           only->dest_width != graph->output_width ||
           only->dest_height != graph->output_height ||
           only->blend_left != 0 || only->blend_right != 0 ||
           only->blend_top != 0 || only->blend_bottom != 0;
}

static int vj_output_graph_identity(const vj_output_graph *graph, const VJFrame *input)
{
    const vj_output_slice_config *c = &graph->slices[0].config;
    if(graph->pattern != VJ_OUTPUT_PATTERN_PROGRAM || !c->enabled)
        return 0;
    for(int i = 1; i < VJ_OUTPUT_GRAPH_MAX_SLICES; i++)
        if(graph->slices[i].config.enabled)
            return 0;
    return input->width == graph->output_width && input->height == graph->output_height &&
           c->source_x == 0 && c->source_y == 0 &&
           c->source_width == VJ_OUTPUT_GRAPH_SOURCE_SCALE &&
           c->source_height == VJ_OUTPUT_GRAPH_SOURCE_SCALE &&
           c->dest_x == 0 && c->dest_y == 0 &&
           c->dest_width == graph->output_width && c->dest_height == graph->output_height &&
           c->blend_left == 0 && c->blend_right == 0 &&
           c->blend_top == 0 && c->blend_bottom == 0;
}

vj_output_graph *vj_output_graph_create(int output_width, int output_height,
                                        const VJFrame *prototype)
{
    if(!prototype || output_width <= 0 || output_height <= 0)
        return NULL;

    vj_output_graph *graph = (vj_output_graph*)vj_calloc(sizeof(vj_output_graph));
    if(!graph)
        return NULL;
    if(pthread_mutex_init(&graph->mutex, NULL) != 0) {
        free(graph);
        return NULL;
    }
    graph->frame = yuv_yuv_template(NULL, NULL, NULL,
                                    output_width, output_height,
                                    alpha_fmt_to_yuv(prototype->format));
    if(!graph->frame) {
        pthread_mutex_destroy(&graph->mutex);
        free(graph);
        return NULL;
    }
    const size_t size = (size_t)graph->frame->len +
                        (size_t)graph->frame->uv_len * 2u;
    graph->buffer = (uint8_t*)vj_malloc(size);
    if(!graph->buffer) {
        free(graph->frame);
        pthread_mutex_destroy(&graph->mutex);
        free(graph);
        return NULL;
    }
    graph->frame->data[0] = graph->buffer;
    graph->frame->data[1] = graph->buffer + graph->frame->len;
    graph->frame->data[2] = graph->frame->data[1] + graph->frame->uv_len;
    graph->frame->data[3] = NULL;
    graph->output_width = output_width;
    graph->output_height = output_height;
    graph->output_uv_width = graph->frame->uv_width;
    graph->output_uv_height = graph->frame->uv_height;
    graph->format = prototype->format;
    graph->frame->fps = prototype->fps;
    graph->n_threads = vje_advise_num_threads(output_width * output_height);
    vj_output_graph_reset(graph);
    return graph;
}

void vj_output_graph_destroy(vj_output_graph *graph)
{
    if(!graph)
        return;
    pthread_mutex_lock(&graph->mutex);
    for(int i = 0; i < VJ_OUTPUT_GRAPH_MAX_SLICES; i++)
        vj_output_slice_free_maps(&graph->slices[i]);
    free(graph->buffer);
    free(graph->frame);
    pthread_mutex_unlock(&graph->mutex);
    pthread_mutex_destroy(&graph->mutex);
    free(graph);
}

static void vj_output_graph_reset_locked(vj_output_graph *graph)
{
    for(int i = 0; i < VJ_OUTPUT_GRAPH_MAX_SLICES; i++) {
        vj_output_slice_free_maps(&graph->slices[i]);
        veejay_memset(&graph->slices[i].config, 0, sizeof(vj_output_slice_config));
        graph->slices[i].config.blend_gamma = 100;
    }
    vj_output_slice_config *c = &graph->slices[0].config;
    c->enabled = 1;
    c->source_width = VJ_OUTPUT_GRAPH_SOURCE_SCALE;
    c->source_height = VJ_OUTPUT_GRAPH_SOURCE_SCALE;
    c->dest_width = graph->output_width;
    c->dest_height = graph->output_height;
    c->blend_gamma = 100;
    graph->pattern = VJ_OUTPUT_PATTERN_PROGRAM;
    graph->structured_axis = 0;
    graph->structured_bit = 0;
    graph->structured_invert = 0;
    graph->pattern_dirty = 1;
}

void vj_output_graph_reset(vj_output_graph *graph)
{
    if(!graph)
        return;
    pthread_mutex_lock(&graph->mutex);
    vj_output_graph_reset_locked(graph);
    pthread_mutex_unlock(&graph->mutex);
}

int vj_output_graph_set_slice(vj_output_graph *graph, int index,
                              const vj_output_slice_config *config)
{
    if(!graph || !config || index < 0 || index >= VJ_OUTPUT_GRAPH_MAX_SLICES ||
       config->source_x < 0 || config->source_y < 0 ||
       config->source_width <= 0 || config->source_height <= 0 ||
       (int64_t)config->source_x + config->source_width > VJ_OUTPUT_GRAPH_SOURCE_SCALE ||
       (int64_t)config->source_y + config->source_height > VJ_OUTPUT_GRAPH_SOURCE_SCALE ||
       config->dest_x < 0 || config->dest_y < 0 ||
       config->dest_width < 0 || config->dest_height < 0 ||
       (config->enabled && (config->dest_width <= 0 || config->dest_height <= 0)) ||
       (int64_t)config->dest_x + config->dest_width > graph->output_width ||
       (int64_t)config->dest_y + config->dest_height > graph->output_height ||
       config->blend_left < 0 || config->blend_right < 0 ||
       config->blend_top < 0 || config->blend_bottom < 0 ||
       (int64_t)config->blend_left + config->blend_right > config->dest_width ||
       (int64_t)config->blend_top + config->blend_bottom > config->dest_height ||
       config->blend_gamma < 10 || config->blend_gamma > 1000)
        return 0;

    pthread_mutex_lock(&graph->mutex);
    vj_output_slice_free_maps(&graph->slices[index]);
    graph->slices[index].config = *config;
    graph->slices[index].config.enabled = config->enabled ? 1 : 0;
    graph->pattern_dirty = 1;
    pthread_mutex_unlock(&graph->mutex);
    return 1;
}

int vj_output_graph_get_slice(const vj_output_graph *graph, int index,
                              vj_output_slice_config *config)
{
    if(!graph || !config || index < 0 || index >= VJ_OUTPUT_GRAPH_MAX_SLICES)
        return 0;
    pthread_mutex_lock((pthread_mutex_t*)&graph->mutex);
    *config = graph->slices[index].config;
    pthread_mutex_unlock((pthread_mutex_t*)&graph->mutex);
    return 1;
}

int vj_output_graph_set_pattern(vj_output_graph *graph, int pattern)
{
    if(!graph || pattern < VJ_OUTPUT_PATTERN_PROGRAM || pattern > VJ_OUTPUT_PATTERN_STRUCTURED_LIGHT)
        return 0;
    pthread_mutex_lock(&graph->mutex);
    if(graph->pattern != pattern) {
        graph->pattern = pattern;
        graph->pattern_dirty = 1;
        veejay_msg(VEEJAY_MSG_INFO,
                   "[OUTPUT] Test pattern changed to %s (%d)",
                   vj_output_pattern_name(pattern), pattern);
    }
    pthread_mutex_unlock(&graph->mutex);
    return 1;
}

int vj_output_graph_get_pattern(const vj_output_graph *graph)
{
    if(!graph)
        return VJ_OUTPUT_PATTERN_PROGRAM;
    pthread_mutex_lock((pthread_mutex_t*)&graph->mutex);
    const int pattern = graph->pattern;
    pthread_mutex_unlock((pthread_mutex_t*)&graph->mutex);
    return pattern;
}

int vj_output_graph_set_structured_light(vj_output_graph *graph, int axis, int bit, int invert)
{
    if(!graph || (axis != 0 && axis != 1) || bit < 0 || bit > 14)
        return 0;

    const int extent = axis == 0 ? graph->output_width : graph->output_height;
    if(extent <= 1 || (1 << bit) >= extent)
        return 0;

    pthread_mutex_lock(&graph->mutex);
    if(graph->pattern != VJ_OUTPUT_PATTERN_STRUCTURED_LIGHT ||
       graph->structured_axis != axis ||
       graph->structured_bit != bit ||
       graph->structured_invert != (invert ? 1 : 0)) {
        graph->pattern = VJ_OUTPUT_PATTERN_STRUCTURED_LIGHT;
        graph->structured_axis = axis;
        graph->structured_bit = bit;
        graph->structured_invert = invert ? 1 : 0;
        graph->pattern_dirty = 1;
        veejay_msg(VEEJAY_MSG_INFO,
                   "[OUTPUT] Structured-light pattern axis=%c bit=%d invert=%d",
                   axis == 0 ? 'X' : 'Y', bit, graph->structured_invert);
    }
    pthread_mutex_unlock(&graph->mutex);
    return 1;
}

VJFrame *vj_output_graph_process_ex(vj_output_graph *graph, const VJFrame *input,
                                      int *pattern_used)
{
    if(pattern_used)
        *pattern_used = VJ_OUTPUT_PATTERN_PROGRAM;
    if(!graph || !input || !input->data[0] || !input->data[1] || !input->data[2])
        return (VJFrame*)input;

    pthread_mutex_lock(&graph->mutex);
    const int pattern = graph->pattern;
    if(pattern_used)
        *pattern_used = pattern;

    if(pattern != VJ_OUTPUT_PATTERN_PROGRAM) {
        if(graph->pattern_dirty) {
            vj_output_graph_generate_pattern(graph);
            graph->pattern_dirty = 0;
        }
        graph->frame->frame_num = input->frame_num;
        graph->frame->fps = input->fps;
        pthread_mutex_unlock(&graph->mutex);
        return graph->frame;
    }

    if(input->format != graph->format) {
        pthread_mutex_unlock(&graph->mutex);
        return (VJFrame*)input;
    }
    if(vj_output_graph_identity(graph, input)) {
        pthread_mutex_unlock(&graph->mutex);
        return (VJFrame*)input;
    }

    if(vj_output_graph_needs_clear(graph))
        vj_output_graph_black(graph);
    for(int i = 0; i < VJ_OUTPUT_GRAPH_MAX_SLICES; i++) {
        vj_output_slice *slice = &graph->slices[i];
        const vj_output_slice_config *c = &slice->config;
        if(!c->enabled)
            continue;
        if(!slice->compiled || slice->source_width != input->width ||
           slice->source_height != input->height ||
           slice->source_uv_width != input->uv_width ||
           slice->source_uv_height != input->uv_height)
        {
            if(!vj_output_slice_compile(graph, slice, input))
                continue;
        }

        const int dx = c->dest_x;
        const int dy = c->dest_y;
        const int ux = dx * graph->output_uv_width / graph->output_width;
        const int uy = dy * graph->output_uv_height / graph->output_height;

        vj_output_render_plane(input->data[0], input->stride[0],
                               input->width, input->height,
                               graph->frame->data[0], graph->output_width,
                               dx, dy, &slice->yx, &slice->yy, 0, graph->n_threads);
        vj_output_render_plane(input->data[1], input->stride[1],
                               input->uv_width, input->uv_height,
                               graph->frame->data[1], graph->output_uv_width,
                               ux, uy, &slice->ux, &slice->uy, 1, graph->n_threads);
        vj_output_render_plane(input->data[2], input->stride[2],
                               input->uv_width, input->uv_height,
                               graph->frame->data[2], graph->output_uv_width,
                               ux, uy, &slice->ux, &slice->uy, 1, graph->n_threads);
    }
    graph->frame->frame_num = input->frame_num;
    graph->frame->fps = input->fps;
    VJFrame *result = graph->frame;
    pthread_mutex_unlock(&graph->mutex);
    return result;
}

VJFrame *vj_output_graph_process(vj_output_graph *graph, const VJFrame *input)
{
    return vj_output_graph_process_ex(graph, input, NULL);
}

size_t vj_output_graph_format_status(const vj_output_graph *graph,
                                     char *dst, size_t dst_len)
{
    if(!graph || !dst || dst_len == 0)
        return 0;

    pthread_mutex_lock((pthread_mutex_t*)&graph->mutex);
    int enabled = 0;
    for(int i = 0; i < VJ_OUTPUT_GRAPH_MAX_SLICES; i++)
        enabled += graph->slices[i].config.enabled ? 1 : 0;
    int n;
    if(graph->pattern == VJ_OUTPUT_PATTERN_STRUCTURED_LIGHT)
        n = snprintf(dst, dst_len,
                     "VJOUTPUT 1 width=%d height=%d pattern=%d structured=%d,%d,%d slices=%d",
                     graph->output_width, graph->output_height, graph->pattern,
                     graph->structured_axis, graph->structured_bit, graph->structured_invert,
                     enabled);
    else
        n = snprintf(dst, dst_len,
                     "VJOUTPUT 1 width=%d height=%d pattern=%d slices=%d",
                     graph->output_width, graph->output_height,
                     graph->pattern, enabled);
    size_t used = n > 0 && (size_t)n < dst_len ? (size_t)n : dst_len - 1;
    for(int i = 0; i < VJ_OUTPUT_GRAPH_MAX_SLICES && used + 1 < dst_len; i++) {
        const vj_output_slice_config *c = &graph->slices[i].config;
        if(!c->enabled)
            continue;
        n = snprintf(dst + used, dst_len - used,
                     " slice%d=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                     i, c->source_x, c->source_y, c->source_width, c->source_height,
                     c->dest_x, c->dest_y, c->dest_width, c->dest_height,
                     c->blend_left, c->blend_right, c->blend_top, c->blend_bottom,
                     c->blend_gamma);
        if(n < 0 || (size_t)n >= dst_len - used)
            break;
        used += (size_t)n;
    }
    pthread_mutex_unlock((pthread_mutex_t*)&graph->mutex);
    return used;
}
