#include "vj-output-mesh.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define VJ_OUTPUT_MESH_INVALID UINT32_MAX
#define VJ_OUTPUT_MESH_INDEX_MASK 0x3fffffffu
#define VJ_OUTPUT_MESH_RIGHT_FLAG 0x40000000u
#define VJ_OUTPUT_MESH_DOWN_FLAG  0x80000000u
#define VJ_OUTPUT_MESH_MIN_GRID 2
#define VJ_OUTPUT_MESH_MAX_GRID 17

typedef struct {
    uint32_t index;
    uint16_t fx;
    uint16_t fy;
} vj_output_mesh_sample;

struct vj_output_mesh {
    int src_width;
    int src_height;
    int dst_width;
    int dst_height;
    int columns;
    int rows;
    int n_threads;
    float source_x;
    float source_y;
    float source_width;
    float source_height;
    vj_output_mesh_point *points;
    vj_output_mesh_sample *samples;
    int bound_x0;
    int bound_y0;
    int bound_x1;
    int bound_y1;
    int compiled;
    int identity;
};

static int vj_output_mesh_clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float vj_output_mesh_clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int vj_output_mesh_grid_valid(int columns, int rows)
{
    return columns >= VJ_OUTPUT_MESH_MIN_GRID &&
           rows >= VJ_OUTPUT_MESH_MIN_GRID &&
           columns <= VJ_OUTPUT_MESH_MAX_GRID &&
           rows <= VJ_OUTPUT_MESH_MAX_GRID;
}

static int vj_output_mesh_solve8(double a[8][9], double out[8])
{
    for(int col = 0; col < 8; col++) {
        int pivot = col;
        double pivot_abs = fabs(a[col][col]);
        for(int row = col + 1; row < 8; row++) {
            double candidate = fabs(a[row][col]);
            if(candidate > pivot_abs) {
                pivot_abs = candidate;
                pivot = row;
            }
        }

        if(pivot_abs < 1.0e-12)
            return 0;

        if(pivot != col) {
            for(int k = col; k < 9; k++) {
                double tmp = a[col][k];
                a[col][k] = a[pivot][k];
                a[pivot][k] = tmp;
            }
        }

        double inv = 1.0 / a[col][col];
        for(int k = col; k < 9; k++)
            a[col][k] *= inv;

        for(int row = 0; row < 8; row++) {
            if(row == col)
                continue;
            double factor = a[row][col];
            if(fabs(factor) < 1.0e-18)
                continue;
            for(int k = col; k < 9; k++)
                a[row][k] -= factor * a[col][k];
        }
    }

    for(int i = 0; i < 8; i++)
        out[i] = a[i][8];
    return 1;
}

static int vj_output_mesh_homography(const vj_output_mesh_point dst[4],
                                     const vj_output_mesh_point src[4],
                                     double h[9])
{
    double a[8][9];
    memset(a, 0, sizeof(a));

    for(int i = 0; i < 4; i++) {
        const double x = dst[i].x;
        const double y = dst[i].y;
        const double u = src[i].x;
        const double v = src[i].y;
        const int r = i * 2;

        a[r][0] = x;
        a[r][1] = y;
        a[r][2] = 1.0;
        a[r][6] = -u * x;
        a[r][7] = -u * y;
        a[r][8] = u;

        a[r + 1][3] = x;
        a[r + 1][4] = y;
        a[r + 1][5] = 1.0;
        a[r + 1][6] = -v * x;
        a[r + 1][7] = -v * y;
        a[r + 1][8] = v;
    }

    if(!vj_output_mesh_solve8(a, h))
        return 0;
    h[8] = 1.0;
    return 1;
}

static float vj_output_mesh_cross(vj_output_mesh_point a,
                                  vj_output_mesh_point b,
                                  vj_output_mesh_point c)
{
    return (b.x - a.x) * (c.y - b.y) -
           (b.y - a.y) * (c.x - b.x);
}

static int vj_output_mesh_quad_valid(const vj_output_mesh_point p[4])
{
    const vj_output_mesh_point q[4] = { p[0], p[1], p[3], p[2] };
    float sign = 0.0f;
    float area2 = 0.0f;

    for(int i = 0; i < 4; i++) {
        const vj_output_mesh_point a = q[i];
        const vj_output_mesh_point b = q[(i + 1) & 3];
        const vj_output_mesh_point c = q[(i + 2) & 3];
        const float cross = vj_output_mesh_cross(a, b, c);
        if(fabsf(cross) < 0.25f)
            return 0;
        if(sign == 0.0f)
            sign = cross;
        else if((sign < 0.0f) != (cross < 0.0f))
            return 0;
        area2 += a.x * b.y - b.x * a.y;
    }

    return fabsf(area2) >= 0.5f;
}

static void vj_output_mesh_interpolate_quad(vj_output_mesh *mesh,
                                             const vj_output_mesh_point corners[4])
{
    for(int row = 0; row < mesh->rows; row++) {
        const float v = mesh->rows > 1 ? (float)row / (float)(mesh->rows - 1) : 0.0f;
        for(int col = 0; col < mesh->columns; col++) {
            const float u = mesh->columns > 1 ? (float)col / (float)(mesh->columns - 1) : 0.0f;
            const float top_x = corners[0].x + (corners[1].x - corners[0].x) * u;
            const float top_y = corners[0].y + (corners[1].y - corners[0].y) * u;
            const float bottom_x = corners[2].x + (corners[3].x - corners[2].x) * u;
            const float bottom_y = corners[2].y + (corners[3].y - corners[2].y) * u;
            vj_output_mesh_point *point = &mesh->points[row * mesh->columns + col];
            point->x = top_x + (bottom_x - top_x) * v;
            point->y = top_y + (bottom_y - top_y) * v;
        }
    }
}

vj_output_mesh *vj_output_mesh_create(int src_width, int src_height,
                                      int dst_width, int dst_height,
                                      int columns, int rows)
{
    if(src_width <= 0 || src_height <= 0 || dst_width <= 0 || dst_height <= 0 ||
       !vj_output_mesh_grid_valid(columns, rows))
        return NULL;

    vj_output_mesh *mesh = (vj_output_mesh*)calloc(1, sizeof(*mesh));
    if(!mesh)
        return NULL;

    mesh->src_width = src_width;
    mesh->src_height = src_height;
    mesh->dst_width = dst_width;
    mesh->dst_height = dst_height;
    mesh->columns = columns;
    mesh->rows = rows;
    mesh->n_threads = 1;
    mesh->source_width = (float)src_width;
    mesh->source_height = (float)src_height;

    mesh->points = (vj_output_mesh_point*)calloc((size_t)columns * (size_t)rows,
                                                  sizeof(*mesh->points));
    mesh->samples = (vj_output_mesh_sample*)malloc((size_t)dst_width * (size_t)dst_height *
                                                    sizeof(*mesh->samples));
    if(!mesh->points || !mesh->samples) {
        vj_output_mesh_destroy(mesh);
        return NULL;
    }

    vj_output_mesh_point corners[4] = {
        { 0.0f, 0.0f },
        { (float)(dst_width - 1), 0.0f },
        { 0.0f, (float)(dst_height - 1) },
        { (float)(dst_width - 1), (float)(dst_height - 1) }
    };
    vj_output_mesh_interpolate_quad(mesh, corners);
    return vj_output_mesh_compile(mesh) ? mesh : (vj_output_mesh_destroy(mesh), (vj_output_mesh*)NULL);
}

void vj_output_mesh_destroy(vj_output_mesh *mesh)
{
    if(!mesh)
        return;
    free(mesh->points);
    free(mesh->samples);
    free(mesh);
}


void vj_output_mesh_set_thread_count(vj_output_mesh *mesh, int n_threads)
{
    if(mesh)
        mesh->n_threads = n_threads > 0 ? n_threads : 1;
}

int vj_output_mesh_set_grid(vj_output_mesh *mesh, int columns, int rows)
{
    if(!mesh || !vj_output_mesh_grid_valid(columns, rows))
        return 0;
    if(columns == mesh->columns && rows == mesh->rows)
        return 1;

    vj_output_mesh_point corners[4] = {
        mesh->points[0],
        mesh->points[mesh->columns - 1],
        mesh->points[(mesh->rows - 1) * mesh->columns],
        mesh->points[(mesh->rows * mesh->columns) - 1]
    };

    vj_output_mesh_point *points = (vj_output_mesh_point*)calloc(
        (size_t)columns * (size_t)rows, sizeof(*points));
    if(!points)
        return 0;

    vj_output_mesh_point *old_points = mesh->points;
    const int old_columns = mesh->columns;
    const int old_rows = mesh->rows;

    mesh->points = points;
    mesh->columns = columns;
    mesh->rows = rows;
    vj_output_mesh_interpolate_quad(mesh, corners);
    mesh->compiled = 0;

    if(!vj_output_mesh_compile(mesh)) {
        free(mesh->points);
        mesh->points = old_points;
        mesh->columns = old_columns;
        mesh->rows = old_rows;
        mesh->compiled = 0;
        vj_output_mesh_compile(mesh);
        return 0;
    }

    free(old_points);
    return 1;
}

void vj_output_mesh_get_grid(const vj_output_mesh *mesh, int *columns, int *rows)
{
    if(columns)
        *columns = mesh ? mesh->columns : 0;
    if(rows)
        *rows = mesh ? mesh->rows : 0;
}

int vj_output_mesh_point_count(const vj_output_mesh *mesh)
{
    return mesh ? mesh->columns * mesh->rows : 0;
}

int vj_output_mesh_set_source_rect(vj_output_mesh *mesh,
                                   float x, float y, float width, float height)
{
    if(!mesh || width <= 0.0f || height <= 0.0f)
        return 0;
    if(x < 0.0f || y < 0.0f || x + width > mesh->src_width || y + height > mesh->src_height)
        return 0;
    mesh->source_x = x;
    mesh->source_y = y;
    mesh->source_width = width;
    mesh->source_height = height;
    mesh->compiled = 0;
    return 1;
}

int vj_output_mesh_set_quad(vj_output_mesh *mesh,
                            float x1, float y1,
                            float x2, float y2,
                            float x3, float y3,
                            float x4, float y4)
{
    if(!mesh)
        return 0;
    vj_output_mesh_point corners[4] = {
        { x1, y1 }, { x2, y2 }, { x4, y4 }, { x3, y3 }
    };
    vj_output_mesh_interpolate_quad(mesh, corners);
    mesh->compiled = 0;
    return 1;
}

int vj_output_mesh_set_point(vj_output_mesh *mesh, int index, float x, float y)
{
    if(!mesh || index < 0 || index >= mesh->columns * mesh->rows)
        return 0;
    mesh->points[index].x = x;
    mesh->points[index].y = y;
    mesh->compiled = 0;
    return 1;
}

int vj_output_mesh_get_point(const vj_output_mesh *mesh, int index,
                             vj_output_mesh_point *point)
{
    if(!mesh || !point || index < 0 || index >= mesh->columns * mesh->rows)
        return 0;
    *point = mesh->points[index];
    return 1;
}

int vj_output_mesh_move_point(vj_output_mesh *mesh, int index, float dx, float dy)
{
    if(!mesh || index < 0 || index >= mesh->columns * mesh->rows)
        return 0;
    mesh->points[index].x += dx;
    mesh->points[index].y += dy;
    mesh->compiled = 0;
    return 1;
}

static int vj_output_mesh_is_identity(const vj_output_mesh *mesh)
{
    if(mesh->src_width != mesh->dst_width || mesh->src_height != mesh->dst_height ||
       mesh->source_x != 0.0f || mesh->source_y != 0.0f ||
       mesh->source_width != (float)mesh->src_width ||
       mesh->source_height != (float)mesh->src_height)
        return 0;

    const float max_x = (float)(mesh->dst_width - 1);
    const float max_y = (float)(mesh->dst_height - 1);
    const float epsilon = 1.0e-4f;

    for(int row = 0; row < mesh->rows; row++) {
        const float expected_y = mesh->rows > 1 ?
            max_y * (float)row / (float)(mesh->rows - 1) : 0.0f;
        for(int col = 0; col < mesh->columns; col++) {
            const float expected_x = mesh->columns > 1 ?
                max_x * (float)col / (float)(mesh->columns - 1) : 0.0f;
            const vj_output_mesh_point *point = &mesh->points[row * mesh->columns + col];
            if(fabsf(point->x - expected_x) > epsilon ||
               fabsf(point->y - expected_y) > epsilon)
                return 0;
        }
    }

    return 1;
}

int vj_output_mesh_compile(vj_output_mesh *mesh)
{
    if(!mesh || !mesh->samples || !mesh->points)
        return 0;

    const size_t source_count = (size_t)mesh->src_width * (size_t)mesh->src_height;
    if(source_count > (size_t)VJ_OUTPUT_MESH_INDEX_MASK)
        return 0;

    mesh->identity = vj_output_mesh_is_identity(mesh);
    if(mesh->identity) {
        mesh->bound_x0 = 0;
        mesh->bound_y0 = 0;
        mesh->bound_x1 = mesh->dst_width;
        mesh->bound_y1 = mesh->dst_height;
        mesh->compiled = 1;
        return 1;
    }

    const size_t sample_count = (size_t)mesh->dst_width * (size_t)mesh->dst_height;
    for(size_t i = 0; i < sample_count; i++) {
        mesh->samples[i].index = VJ_OUTPUT_MESH_INVALID;
        mesh->samples[i].fx = 0;
        mesh->samples[i].fy = 0;
    }

    mesh->bound_x0 = mesh->dst_width;
    mesh->bound_y0 = mesh->dst_height;
    mesh->bound_x1 = 0;
    mesh->bound_y1 = 0;
    int valid_cells = 0;

    for(int row = 0; row < mesh->rows - 1; row++) {
        const float sv0 = (float)row / (float)(mesh->rows - 1);
        const float sv1 = (float)(row + 1) / (float)(mesh->rows - 1);
        const float sy0 = mesh->source_y + mesh->source_height * sv0;
        const float sy1 = mesh->source_y + mesh->source_height * sv1;

        for(int col = 0; col < mesh->columns - 1; col++) {
            const float su0 = (float)col / (float)(mesh->columns - 1);
            const float su1 = (float)(col + 1) / (float)(mesh->columns - 1);
            const float sx0 = mesh->source_x + mesh->source_width * su0;
            const float sx1 = mesh->source_x + mesh->source_width * su1;

            const int p00 = row * mesh->columns + col;
            const int p10 = p00 + 1;
            const int p01 = p00 + mesh->columns;
            const int p11 = p01 + 1;
            vj_output_mesh_point dst[4] = {
                mesh->points[p00], mesh->points[p10],
                mesh->points[p01], mesh->points[p11]
            };
            vj_output_mesh_point src[4] = {
                { sx0, sy0 }, { sx1, sy0 }, { sx0, sy1 }, { sx1, sy1 }
            };

            if(!vj_output_mesh_quad_valid(dst))
                continue;

            double h[9];
            if(!vj_output_mesh_homography(dst, src, h))
                continue;

            float min_x = FLT_MAX, min_y = FLT_MAX;
            float max_x = -FLT_MAX, max_y = -FLT_MAX;
            for(int i = 0; i < 4; i++) {
                if(dst[i].x < min_x) min_x = dst[i].x;
                if(dst[i].y < min_y) min_y = dst[i].y;
                if(dst[i].x > max_x) max_x = dst[i].x;
                if(dst[i].y > max_y) max_y = dst[i].y;
            }

            const int x0 = vj_output_mesh_clampi((int)floorf(min_x), 0, mesh->dst_width - 1);
            const int y0 = vj_output_mesh_clampi((int)floorf(min_y), 0, mesh->dst_height - 1);
            const int x1 = vj_output_mesh_clampi((int)ceilf(max_x), 0, mesh->dst_width - 1);
            const int y1 = vj_output_mesh_clampi((int)ceilf(max_y), 0, mesh->dst_height - 1);
            const double eps = 1.0e-4;

            for(int y = y0; y <= y1; y++) {
                for(int x = x0; x <= x1; x++) {
                    const double px = (double)x + 0.5;
                    const double py = (double)y + 0.5;
                    const double den = h[6] * px + h[7] * py + h[8];
                    if(fabs(den) < 1.0e-12)
                        continue;
                    double source_x = (h[0] * px + h[1] * py + h[2]) / den;
                    double source_y = (h[3] * px + h[4] * py + h[5]) / den;
                    if(source_x < sx0 - eps || source_x > sx1 + eps ||
                       source_y < sy0 - eps || source_y > sy1 + eps)
                        continue;

                    source_x -= 0.5;
                    source_y -= 0.5;
                    source_x = vj_output_mesh_clampf((float)source_x, 0.0f,
                                                     (float)(mesh->src_width - 1));
                    source_y = vj_output_mesh_clampf((float)source_y, 0.0f,
                                                     (float)(mesh->src_height - 1));
                    int ix = (int)floor(source_x);
                    int iy = (int)floor(source_y);
                    float fx = (float)source_x - (float)ix;
                    float fy = (float)source_y - (float)iy;
                    if(ix >= mesh->src_width - 1) {
                        ix = mesh->src_width - 1;
                        fx = 0.0f;
                    }
                    if(iy >= mesh->src_height - 1) {
                        iy = mesh->src_height - 1;
                        fy = 0.0f;
                    }

                    vj_output_mesh_sample *sample = &mesh->samples[y * mesh->dst_width + x];
                    uint32_t packed_index = (uint32_t)(iy * mesh->src_width + ix);
                    if(ix + 1 < mesh->src_width)
                        packed_index |= VJ_OUTPUT_MESH_RIGHT_FLAG;
                    if(iy + 1 < mesh->src_height)
                        packed_index |= VJ_OUTPUT_MESH_DOWN_FLAG;
                    sample->index = packed_index;
                    sample->fx = (uint16_t)(fx * 65535.0f + 0.5f);
                    sample->fy = (uint16_t)(fy * 65535.0f + 0.5f);

                    if(x < mesh->bound_x0) mesh->bound_x0 = x;
                    if(y < mesh->bound_y0) mesh->bound_y0 = y;
                    if(x + 1 > mesh->bound_x1) mesh->bound_x1 = x + 1;
                    if(y + 1 > mesh->bound_y1) mesh->bound_y1 = y + 1;
                }
            }
            valid_cells++;
        }
    }

    mesh->compiled = valid_cells > 0 && mesh->bound_x1 > mesh->bound_x0 &&
                     mesh->bound_y1 > mesh->bound_y0;
    return mesh->compiled;
}

int vj_output_mesh_get_bounds(const vj_output_mesh *mesh,
                              int *x0, int *y0, int *x1, int *y1)
{
    if(!mesh || !mesh->compiled)
        return 0;
    if(x0) *x0 = mesh->bound_x0;
    if(y0) *y0 = mesh->bound_y0;
    if(x1) *x1 = mesh->bound_x1;
    if(y1) *y1 = mesh->bound_y1;
    return 1;
}

static inline uint8_t vj_output_mesh_bilinear(const uint8_t *restrict plane,
                                                uint32_t i00,
                                                uint32_t i10,
                                                uint32_t i01,
                                                uint32_t i11,
                                                uint32_t fx,
                                                uint32_t fy,
                                                uint32_t wx0,
                                                uint32_t wy0)
{
    const uint32_t top = (uint32_t)plane[i00] * wx0 + (uint32_t)plane[i10] * fx;
    const uint32_t bottom = (uint32_t)plane[i01] * wx0 + (uint32_t)plane[i11] * fx;
    return (uint8_t)(((uint64_t)top * wy0 + (uint64_t)bottom * fy + (1ULL << 31)) >> 32);
}

static inline void vj_output_mesh_indices(const vj_output_mesh *mesh,
                                          const vj_output_mesh_sample *sample,
                                          uint32_t *i00,
                                          uint32_t *i10,
                                          uint32_t *i01,
                                          uint32_t *i11)
{
    const uint32_t packed = sample->index;
    const uint32_t base = packed & VJ_OUTPUT_MESH_INDEX_MASK;
    const uint32_t right = (packed & VJ_OUTPUT_MESH_RIGHT_FLAG) != 0;
    const uint32_t down = (packed & VJ_OUTPUT_MESH_DOWN_FLAG) ? (uint32_t)mesh->src_width : 0u;
    *i00 = base;
    *i10 = base + right;
    *i01 = base + down;
    *i11 = base + down + right;
}

static void vj_output_mesh_clear_plane_margins(const vj_output_mesh *mesh,
                                               uint8_t *output,
                                               uint8_t value)
{
    const int width = mesh->dst_width;
    const int height = mesh->dst_height;
    const int x0 = mesh->bound_x0;
    const int x1 = mesh->bound_x1;
    const int y0 = mesh->bound_y0;
    const int y1 = mesh->bound_y1;

    if(y0 > 0)
        memset(output, value, (size_t)y0 * (size_t)width);
    if(x0 > 0 || x1 < width) {
        for(int y = y0; y < y1; y++) {
            uint8_t *row = output + (size_t)y * (size_t)width;
            if(x0 > 0)
                memset(row, value, (size_t)x0);
            if(x1 < width)
                memset(row + x1, value, (size_t)(width - x1));
        }
    }
    if(y1 < height)
        memset(output + (size_t)y1 * (size_t)width, value,
               (size_t)(height - y1) * (size_t)width);
}

static void vj_output_mesh_clear_yuv444_margins(const vj_output_mesh *mesh,
                                                uint8_t *restrict output[3])
{
    vj_output_mesh_clear_plane_margins(mesh, output[0], 0);
    vj_output_mesh_clear_plane_margins(mesh, output[1], 128);
    vj_output_mesh_clear_plane_margins(mesh, output[2], 128);
}

void vj_output_mesh_render_yuv444(const vj_output_mesh *mesh,
                                  const uint8_t *input[3],
                                  uint8_t *output[3])
{
    if(!mesh || !mesh->compiled || !input || !output)
        return;

    const size_t len = (size_t)mesh->dst_width * (size_t)mesh->dst_height;
    if(mesh->identity) {
        if(output[0] != input[0]) memcpy(output[0], input[0], len);
        if(output[1] != input[1]) memcpy(output[1], input[1], len);
        if(output[2] != input[2]) memcpy(output[2], input[2], len);
        return;
    }

    const int width = mesh->dst_width;
    const int x0 = mesh->bound_x0;
    const int x1 = mesh->bound_x1;
    const int y0 = mesh->bound_y0;
    const int y1 = mesh->bound_y1;

    if(x0 != 0 || y0 != 0 || x1 != mesh->dst_width || y1 != mesh->dst_height)
        vj_output_mesh_clear_yuv444_margins(mesh, output);

    const uint8_t *restrict src_y = input[0];
    const uint8_t *restrict src_u = input[1];
    const uint8_t *restrict src_v = input[2];
    uint8_t *restrict dst_y = output[0];
    uint8_t *restrict dst_u = output[1];
    uint8_t *restrict dst_v = output[2];

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(mesh->n_threads)
#endif
    for(int y = y0; y < y1; y++) {
        int i = y * width + x0;
        const int end = y * width + x1;
        for(; i < end; i++) {
            const vj_output_mesh_sample *sample = &mesh->samples[i];
            if(sample->index == VJ_OUTPUT_MESH_INVALID) {
                dst_y[i] = 0;
                dst_u[i] = 128;
                dst_v[i] = 128;
                continue;
            }

            uint32_t i00, i10, i01, i11;
            vj_output_mesh_indices(mesh, sample, &i00, &i10, &i01, &i11);
            const uint32_t fx = sample->fx;
            const uint32_t fy = sample->fy;
            const uint32_t wx0 = 65536u - fx;
            const uint32_t wy0 = 65536u - fy;
            dst_y[i] = vj_output_mesh_bilinear(src_y, i00, i10, i01, i11, fx, fy, wx0, wy0);
            dst_u[i] = vj_output_mesh_bilinear(src_u, i00, i10, i01, i11, fx, fy, wx0, wy0);
            dst_v[i] = vj_output_mesh_bilinear(src_v, i00, i10, i01, i11, fx, fy, wx0, wy0);
        }
    }
}

void vj_output_mesh_render_yuv444_alpha(const vj_output_mesh *mesh,
                                        const uint8_t *input[4],
                                        uint8_t *output[4])
{
    if(!mesh || !mesh->compiled || !input || !output)
        return;

    const size_t len = (size_t)mesh->dst_width * (size_t)mesh->dst_height;
    if(mesh->identity) {
        if(output[0] != input[0]) memcpy(output[0], input[0], len);
        if(output[1] != input[1]) memcpy(output[1], input[1], len);
        if(output[2] != input[2]) memcpy(output[2], input[2], len);
        if(output[3] != input[3]) memcpy(output[3], input[3], len);
        return;
    }

    const int width = mesh->dst_width;
    const int x0 = mesh->bound_x0;
    const int x1 = mesh->bound_x1;
    const int y0 = mesh->bound_y0;
    const int y1 = mesh->bound_y1;

    if(x0 != 0 || y0 != 0 || x1 != mesh->dst_width || y1 != mesh->dst_height) {
        vj_output_mesh_clear_yuv444_margins(mesh, output);
        vj_output_mesh_clear_plane_margins(mesh, output[3], 0);
    }

    const uint8_t *restrict src_y = input[0];
    const uint8_t *restrict src_u = input[1];
    const uint8_t *restrict src_v = input[2];
    const uint8_t *restrict src_a = input[3];
    uint8_t *restrict dst_y = output[0];
    uint8_t *restrict dst_u = output[1];
    uint8_t *restrict dst_v = output[2];
    uint8_t *restrict dst_a = output[3];

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(mesh->n_threads)
#endif
    for(int y = y0; y < y1; y++) {
        int i = y * width + x0;
        const int end = y * width + x1;
        for(; i < end; i++) {
            const vj_output_mesh_sample *sample = &mesh->samples[i];
            if(sample->index == VJ_OUTPUT_MESH_INVALID) {
                dst_y[i] = 0;
                dst_u[i] = 128;
                dst_v[i] = 128;
                dst_a[i] = 0;
                continue;
            }

            uint32_t i00, i10, i01, i11;
            vj_output_mesh_indices(mesh, sample, &i00, &i10, &i01, &i11);
            const uint32_t fx = sample->fx;
            const uint32_t fy = sample->fy;
            const uint32_t wx0 = 65536u - fx;
            const uint32_t wy0 = 65536u - fy;
            dst_y[i] = vj_output_mesh_bilinear(src_y, i00, i10, i01, i11, fx, fy, wx0, wy0);
            dst_u[i] = vj_output_mesh_bilinear(src_u, i00, i10, i01, i11, fx, fy, wx0, wy0);
            dst_v[i] = vj_output_mesh_bilinear(src_v, i00, i10, i01, i11, fx, fy, wx0, wy0);
            dst_a[i] = vj_output_mesh_bilinear(src_a, i00, i10, i01, i11, fx, fy, wx0, wy0);
        }
    }
}

void vj_output_mesh_render_luma(const vj_output_mesh *mesh,
                                const uint8_t *restrict input,
                                uint8_t *restrict output)
{
    if(!mesh || !mesh->compiled || !input || !output)
        return;

    const size_t len = (size_t)mesh->dst_width * (size_t)mesh->dst_height;
    if(mesh->identity) {
        if(output != input) memcpy(output, input, len);
        return;
    }

    const int width = mesh->dst_width;
    const int x0 = mesh->bound_x0;
    const int x1 = mesh->bound_x1;
    const int y0 = mesh->bound_y0;
    const int y1 = mesh->bound_y1;
    if(x0 != 0 || y0 != 0 || x1 != mesh->dst_width || y1 != mesh->dst_height)
        vj_output_mesh_clear_plane_margins(mesh, output, 0);

#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(mesh->n_threads)
#endif
    for(int y = y0; y < y1; y++) {
        int i = y * width + x0;
        const int end = y * width + x1;
        for(; i < end; i++) {
            const vj_output_mesh_sample *sample = &mesh->samples[i];
            if(sample->index == VJ_OUTPUT_MESH_INVALID) {
                output[i] = 0;
                continue;
            }
            uint32_t i00, i10, i01, i11;
            vj_output_mesh_indices(mesh, sample, &i00, &i10, &i01, &i11);
            output[i] = vj_output_mesh_bilinear(input, i00, i10, i01, i11,
                                                 sample->fx, sample->fy,
                                                 65536u - sample->fx, 65536u - sample->fy);
        }
    }
}

static void vj_output_mesh_clear_yuyv(uint8_t *output, int width, int height)
{
    const size_t pairs = ((size_t)width * (size_t)height) >> 1;
    uint8_t *dst = output;
    for(size_t i = 0; i < pairs; i++) {
        dst[0] = 0;
        dst[1] = 128;
        dst[2] = 0;
        dst[3] = 128;
        dst += 4;
    }
    if(((size_t)width * (size_t)height) & 1u) {
        dst[0] = 0;
        dst[1] = 128;
    }
}

void vj_output_mesh_render_yuyv(const vj_output_mesh *mesh,
                                const uint8_t *input[3],
                                uint8_t *output)
{
    if(!mesh || !mesh->compiled || !input || !output)
        return;

    const int width = mesh->dst_width;
    const int height = mesh->dst_height;
    if(mesh->identity) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(mesh->n_threads)
#endif
        for(int y = 0; y < height; y++) {
            uint8_t *restrict row = output + (size_t)y * (size_t)width * 2u;
            const int base = y * width;
            int x = 0;
            for(; x + 1 < width; x += 2) {
                const int i0 = base + x;
                const int i1 = i0 + 1;
                row[x * 2] = input[0][i0];
                row[x * 2 + 1] = (uint8_t)(((unsigned int)input[1][i0] + input[1][i1]) >> 1);
                row[x * 2 + 2] = input[0][i1];
                row[x * 2 + 3] = (uint8_t)(((unsigned int)input[2][i0] + input[2][i1]) >> 1);
            }
            if(x < width) {
                const int i = base + x;
                row[x * 2] = input[0][i];
                row[x * 2 + 1] = input[1][i];
            }
        }
        return;
    }

    vj_output_mesh_clear_yuyv(output, width, height);
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(mesh->n_threads)
#endif
    for(int y = mesh->bound_y0; y < mesh->bound_y1; y++) {
        uint8_t *restrict row = output + (size_t)y * (size_t)width * 2u;
        int x = mesh->bound_x0 & ~1;
        int end = (mesh->bound_x1 + 1) & ~1;
        if(end > width)
            end = width;
        for(; x + 1 < end; x += 2) {
            const int i0 = y * width + x;
            const int i1 = i0 + 1;
            const vj_output_mesh_sample *s0 = &mesh->samples[i0];
            const vj_output_mesh_sample *s1 = &mesh->samples[i1];
            uint8_t yv0 = 0, yv1 = 0, u0 = 128, u1 = 128, v0 = 128, v1 = 128;

            if(s0->index != VJ_OUTPUT_MESH_INVALID) {
                uint32_t a, b, c, d;
                vj_output_mesh_indices(mesh, s0, &a, &b, &c, &d);
                yv0 = vj_output_mesh_bilinear(input[0], a, b, c, d, s0->fx, s0->fy, 65536u - s0->fx, 65536u - s0->fy);
                u0 = vj_output_mesh_bilinear(input[1], a, b, c, d, s0->fx, s0->fy, 65536u - s0->fx, 65536u - s0->fy);
                v0 = vj_output_mesh_bilinear(input[2], a, b, c, d, s0->fx, s0->fy, 65536u - s0->fx, 65536u - s0->fy);
            }
            if(s1->index != VJ_OUTPUT_MESH_INVALID) {
                uint32_t a, b, c, d;
                vj_output_mesh_indices(mesh, s1, &a, &b, &c, &d);
                yv1 = vj_output_mesh_bilinear(input[0], a, b, c, d, s1->fx, s1->fy, 65536u - s1->fx, 65536u - s1->fy);
                u1 = vj_output_mesh_bilinear(input[1], a, b, c, d, s1->fx, s1->fy, 65536u - s1->fx, 65536u - s1->fy);
                v1 = vj_output_mesh_bilinear(input[2], a, b, c, d, s1->fx, s1->fy, 65536u - s1->fx, 65536u - s1->fy);
            }
            row[x * 2] = yv0;
            row[x * 2 + 1] = (uint8_t)(((unsigned int)u0 + u1) >> 1);
            row[x * 2 + 2] = yv1;
            row[x * 2 + 3] = (uint8_t)(((unsigned int)v0 + v1) >> 1);
        }
        if(x < end && x < width) {
            const vj_output_mesh_sample *sample = &mesh->samples[y * width + x];
            if(sample->index != VJ_OUTPUT_MESH_INVALID) {
                uint32_t a, b, c, d;
                vj_output_mesh_indices(mesh, sample, &a, &b, &c, &d);
                row[x * 2] = vj_output_mesh_bilinear(input[0], a, b, c, d,
                                                 sample->fx, sample->fy,
                                                 65536u - sample->fx, 65536u - sample->fy);
                row[x * 2 + 1] = vj_output_mesh_bilinear(input[1], a, b, c, d,
                                                 sample->fx, sample->fy,
                                                 65536u - sample->fx, 65536u - sample->fy);
            }
        }
    }
}
