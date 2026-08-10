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
#include <libel/vj-nvjpeg-kernel.h>

#if defined(VJ_NVJPEG_KERNEL_HOST_TEST)
#define VJ_NVJPEG_DEVICE_INLINE static inline
#else
#define VJ_NVJPEG_DEVICE_INLINE static __device__ __forceinline__
#endif

VJ_NVJPEG_DEVICE_INLINE uint8_t vj_nvjpeg_clamp_u8(int value)
{
    return (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
}

VJ_NVJPEG_DEVICE_INLINE void vj_nvjpeg_mitchell_indices(
    int sample,
    int length,
    int *i0,
    int *i1,
    int *i2,
    int *i3)
{
    if(sample == 0) {
        *i0 = 0;
        *i1 = 0;
        *i2 = length > 1 ? 1 : 0;
        *i3 = length > 2 ? 2 : length - 1;
    }
    else if(sample == length - 1) {
        *i0 = sample > 1 ? sample - 2 : 0;
        *i1 = sample - 1;
        *i2 = sample;
        *i3 = sample;
    }
    else {
        *i0 = sample - 1;
        *i1 = sample;
        *i2 = sample + 1;
        *i3 = sample + 2 < length ? sample + 2 : length - 1;
    }
}

VJ_NVJPEG_DEVICE_INLINE uint8_t vj_nvjpeg_mitchell_values(
    uint8_t s0,
    uint8_t s1,
    uint8_t s2,
    uint8_t s3)
{
    return vj_nvjpeg_clamp_u8(
        (-9 * (int)s0 +
         111 * (int)s1 +
          29 * (int)s2 -
           3 * (int)s3 + 64) >> 7);
}

VJ_NVJPEG_DEVICE_INLINE uint8_t vj_nvjpeg_sample_row(
    const uint8_t *row,
    int source_width,
    int x,
    int expand_x,
    vj_nvjpeg_upsample_mode mode)
{
    int i0;
    int i1;
    int i2;
    int i3;
    const int sample = expand_x ? x >> 1 : x;

    if(!expand_x || mode == VJ_NVJPEG_UPSAMPLE_DUP || (x & 1) == 0)
        return row[sample];

    vj_nvjpeg_mitchell_indices(sample, source_width,
                               &i0, &i1, &i2, &i3);
    return vj_nvjpeg_mitchell_values(row[i0], row[i1], row[i2], row[i3]);
}

VJ_NVJPEG_DEVICE_INLINE uint8_t vj_nvjpeg_sample_plane(
    const uint8_t *source,
    size_t source_pitch,
    int source_width,
    int source_height,
    int x,
    int y,
    int expand_x,
    int expand_y,
    vj_nvjpeg_upsample_mode mode)
{
    int i0;
    int i1;
    int i2;
    int i3;
    const int sample_y = expand_y ? y >> 1 : y;

    if(!expand_y || mode == VJ_NVJPEG_UPSAMPLE_DUP || (y & 1) == 0) {
        const uint8_t *row = source + (size_t)sample_y * source_pitch;
        return vj_nvjpeg_sample_row(row, source_width, x, expand_x, mode);
    }

    vj_nvjpeg_mitchell_indices(sample_y, source_height,
                               &i0, &i1, &i2, &i3);
    return vj_nvjpeg_mitchell_values(
        vj_nvjpeg_sample_row(source + (size_t)i0 * source_pitch,
                             source_width, x, expand_x, mode),
        vj_nvjpeg_sample_row(source + (size_t)i1 * source_pitch,
                             source_width, x, expand_x, mode),
        vj_nvjpeg_sample_row(source + (size_t)i2 * source_pitch,
                             source_width, x, expand_x, mode),
        vj_nvjpeg_sample_row(source + (size_t)i3 * source_pitch,
                             source_width, x, expand_x, mode));
}

static int vj_nvjpeg_valid_axis(int source, int destination)
{
    const long long doubled = 2LL * (long long)source;

    return source > 0 && destination > 0 &&
        (destination == source ||
         (long long)destination == doubled ||
         (long long)destination == doubled - 1LL);
}

#if defined(VJ_NVJPEG_KERNEL_HOST_TEST)

extern "C" cudaError_t vj_nvjpeg_upsample_chroma(
    const uint8_t *src_u,
    size_t src_u_pitch,
    const uint8_t *src_v,
    size_t src_v_pitch,
    uint8_t *dst_u,
    size_t dst_u_pitch,
    uint8_t *dst_v,
    size_t dst_v_pitch,
    int src_width,
    int src_height,
    int dst_width,
    int dst_height,
    vj_nvjpeg_upsample_mode mode,
    cudaStream_t stream)
{
    const int expand_x = dst_width != src_width;
    const int expand_y = dst_height != src_height;
    (void)stream;

    if(!src_u || !src_v || !dst_u || !dst_v ||
       !vj_nvjpeg_valid_axis(src_width, dst_width) ||
       !vj_nvjpeg_valid_axis(src_height, dst_height) ||
       src_u_pitch < (size_t)src_width ||
       src_v_pitch < (size_t)src_width ||
       dst_u_pitch < (size_t)dst_width ||
       dst_v_pitch < (size_t)dst_width ||
       (mode != VJ_NVJPEG_UPSAMPLE_DUP &&
        mode != VJ_NVJPEG_UPSAMPLE_MITCHELL))
        return cudaErrorInvalidValue;

    for(int y = 0; y < dst_height; y++) {
        uint8_t *out_u = dst_u + (size_t)y * dst_u_pitch;
        uint8_t *out_v = dst_v + (size_t)y * dst_v_pitch;

        for(int x = 0; x < dst_width; x++) {
            out_u[x] = vj_nvjpeg_sample_plane(
                src_u, src_u_pitch, src_width, src_height,
                x, y, expand_x, expand_y, mode);
            out_v[x] = vj_nvjpeg_sample_plane(
                src_v, src_v_pitch, src_width, src_height,
                x, y, expand_x, expand_y, mode);
        }
    }

    return cudaSuccess;
}

#else

static __global__ void vj_nvjpeg_upsample_kernel(
    const uint8_t *src_u,
    size_t src_u_pitch,
    const uint8_t *src_v,
    size_t src_v_pitch,
    uint8_t *dst_u,
    size_t dst_u_pitch,
    uint8_t *dst_v,
    size_t dst_v_pitch,
    int src_width,
    int src_height,
    int dst_width,
    int dst_height,
    vj_nvjpeg_upsample_mode mode)
{
    const int x = (int)(blockIdx.x * blockDim.x + threadIdx.x);
    const int y = (int)(blockIdx.y * blockDim.y + threadIdx.y);
    const int expand_x = dst_width != src_width;
    const int expand_y = dst_height != src_height;

    if(x >= dst_width || y >= dst_height)
        return;

    uint8_t *out_u = dst_u + (size_t)y * dst_u_pitch;
    uint8_t *out_v = dst_v + (size_t)y * dst_v_pitch;

    out_u[x] = vj_nvjpeg_sample_plane(
        src_u, src_u_pitch, src_width, src_height,
        x, y, expand_x, expand_y, mode);
    out_v[x] = vj_nvjpeg_sample_plane(
        src_v, src_v_pitch, src_width, src_height,
        x, y, expand_x, expand_y, mode);
}

extern "C" cudaError_t vj_nvjpeg_upsample_chroma(
    const uint8_t *src_u,
    size_t src_u_pitch,
    const uint8_t *src_v,
    size_t src_v_pitch,
    uint8_t *dst_u,
    size_t dst_u_pitch,
    uint8_t *dst_v,
    size_t dst_v_pitch,
    int src_width,
    int src_height,
    int dst_width,
    int dst_height,
    vj_nvjpeg_upsample_mode mode,
    cudaStream_t stream)
{
    const dim3 block(32, 8);
    dim3 grid;

    if(!src_u || !src_v || !dst_u || !dst_v ||
       !vj_nvjpeg_valid_axis(src_width, dst_width) ||
       !vj_nvjpeg_valid_axis(src_height, dst_height) ||
       src_u_pitch < (size_t)src_width ||
       src_v_pitch < (size_t)src_width ||
       dst_u_pitch < (size_t)dst_width ||
       dst_v_pitch < (size_t)dst_width ||
       (mode != VJ_NVJPEG_UPSAMPLE_DUP &&
        mode != VJ_NVJPEG_UPSAMPLE_MITCHELL))
        return cudaErrorInvalidValue;

    grid = dim3((unsigned int)(dst_width + (int)block.x - 1) / block.x,
                (unsigned int)(dst_height + (int)block.y - 1) / block.y);

    vj_nvjpeg_upsample_kernel<<<grid, block, 0, stream>>>(
        src_u, src_u_pitch,
        src_v, src_v_pitch,
        dst_u, dst_u_pitch,
        dst_v, dst_v_pitch,
        src_width, src_height,
        dst_width, dst_height,
        mode);

    return cudaGetLastError();
}

#endif
