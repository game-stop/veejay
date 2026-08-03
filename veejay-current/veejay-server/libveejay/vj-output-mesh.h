#ifndef VJ_OUTPUT_MESH_H
#define VJ_OUTPUT_MESH_H

#include <stdint.h>

typedef struct vj_output_mesh vj_output_mesh;

typedef struct {
    float x;
    float y;
} vj_output_mesh_point;

vj_output_mesh *vj_output_mesh_create(int src_width, int src_height,
                                      int dst_width, int dst_height,
                                      int columns, int rows);
void vj_output_mesh_destroy(vj_output_mesh *mesh);
void vj_output_mesh_set_thread_count(vj_output_mesh *mesh, int n_threads);

int vj_output_mesh_set_grid(vj_output_mesh *mesh, int columns, int rows);
void vj_output_mesh_get_grid(const vj_output_mesh *mesh, int *columns, int *rows);
int vj_output_mesh_point_count(const vj_output_mesh *mesh);

int vj_output_mesh_set_source_rect(vj_output_mesh *mesh,
                                   float x, float y, float width, float height);
int vj_output_mesh_set_quad(vj_output_mesh *mesh,
                            float x1, float y1,
                            float x2, float y2,
                            float x3, float y3,
                            float x4, float y4);
int vj_output_mesh_set_point(vj_output_mesh *mesh, int index, float x, float y);
int vj_output_mesh_get_point(const vj_output_mesh *mesh, int index,
                             vj_output_mesh_point *point);
int vj_output_mesh_move_point(vj_output_mesh *mesh, int index, float dx, float dy);

int vj_output_mesh_compile(vj_output_mesh *mesh);
int vj_output_mesh_get_bounds(const vj_output_mesh *mesh,
                              int *x0, int *y0, int *x1, int *y1);

void vj_output_mesh_render_yuv444(const vj_output_mesh *mesh,
                                  const uint8_t *input[3], uint8_t *output[3]);
void vj_output_mesh_render_yuv444_alpha(const vj_output_mesh *mesh,
                                        const uint8_t *input[4], uint8_t *output[4]);
void vj_output_mesh_render_luma(const vj_output_mesh *mesh,
                                const uint8_t *input, uint8_t *output);
void vj_output_mesh_render_yuyv(const vj_output_mesh *mesh,
                                const uint8_t *input[3], uint8_t *output);

#endif
