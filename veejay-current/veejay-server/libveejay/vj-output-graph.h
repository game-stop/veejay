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
#ifndef VJ_OUTPUT_GRAPH_H
#define VJ_OUTPUT_GRAPH_H

#include <stddef.h>
#include <libvje/vje.h>

#define VJ_OUTPUT_GRAPH_MAX_SLICES 8

typedef enum {
    VJ_OUTPUT_PATTERN_PROGRAM = 0,
    VJ_OUTPUT_PATTERN_BLACK = 1,
    VJ_OUTPUT_PATTERN_WHITE = 2,
    VJ_OUTPUT_PATTERN_GRAY = 3,
    VJ_OUTPUT_PATTERN_COLOR_BARS = 4,
    VJ_OUTPUT_PATTERN_GRID = 5,
    VJ_OUTPUT_PATTERN_CHECKER = 6,
    VJ_OUTPUT_PATTERN_SLICE_IDS = 7,
    VJ_OUTPUT_PATTERN_BLEND = 8,
    VJ_OUTPUT_PATTERN_STRUCTURED_LIGHT = 9
} vj_output_pattern_t;

typedef struct {
    int enabled;
    int source_x;
    int source_y;
    int source_width;
    int source_height;
    int dest_x;
    int dest_y;
    int dest_width;
    int dest_height;
    int blend_left;
    int blend_right;
    int blend_top;
    int blend_bottom;
    int blend_gamma;
} vj_output_slice_config;

typedef struct vj_output_graph vj_output_graph;

vj_output_graph *vj_output_graph_create(int output_width, int output_height,
                                        const VJFrame *prototype);
void vj_output_graph_destroy(vj_output_graph *graph);
void vj_output_graph_reset(vj_output_graph *graph);
int vj_output_graph_set_slice(vj_output_graph *graph, int index,
                              const vj_output_slice_config *config);
int vj_output_graph_get_slice(const vj_output_graph *graph, int index,
                              vj_output_slice_config *config);
int vj_output_graph_set_pattern(vj_output_graph *graph, int pattern);
int vj_output_graph_get_pattern(const vj_output_graph *graph);
int vj_output_graph_set_structured_light(vj_output_graph *graph, int axis, int bit, int invert);
VJFrame *vj_output_graph_process(vj_output_graph *graph, const VJFrame *input);
VJFrame *vj_output_graph_process_ex(vj_output_graph *graph, const VJFrame *input,
                                      int *pattern_used);
size_t vj_output_graph_format_status(const vj_output_graph *graph,
                                     char *dst, size_t dst_len);

#endif
