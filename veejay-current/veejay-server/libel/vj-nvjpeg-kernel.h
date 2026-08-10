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
#ifndef VJ_NVJPEG_KERNEL_H
#define VJ_NVJPEG_KERNEL_H

#include <cuda_runtime_api.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VJ_NVJPEG_UPSAMPLE_DUP = 0,
    VJ_NVJPEG_UPSAMPLE_MITCHELL = 1
} vj_nvjpeg_upsample_mode;

/*
 * Expand planar chroma by one or two axes.  Each destination dimension must
 * either match its source dimension or be its 2x expansion (2*n or 2*n-1,
 * the latter covering odd luma dimensions).  This supports 4:2:0 -> 4:2:2,
 * 4:2:0 -> 4:4:4 and 4:2:2 -> 4:4:4 without an intermediate allocation.
 */
cudaError_t vj_nvjpeg_upsample_chroma(
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
    cudaStream_t stream);

#ifdef __cplusplus
}
#endif

#endif
