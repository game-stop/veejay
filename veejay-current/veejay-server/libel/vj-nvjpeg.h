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
#ifndef VJ_NVJPEG_H
#define VJ_NVJPEG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vj_nvjpeg_decoder vj_nvjpeg_decoder;

typedef enum {
    VJ_NVJPEG_OUTPUT_422 = 0,
    VJ_NVJPEG_OUTPUT_444 = 1
} vj_nvjpeg_output;

vj_nvjpeg_decoder *vj_nvjpeg_decoder_create(int width,
                                             int height,
                                             char *reason,
                                             size_t reason_size);

void vj_nvjpeg_decoder_destroy(vj_nvjpeg_decoder *decoder);

/*
 * Returns 1 on success and -1 on failure.  actual_output reports whether the
 * requested 4:4:4 path was used or was safely downgraded to native 4:2:2.
 * A 4:2:0 source is never returned without conversion: if its required CUDA
 * expansion fails, this call fails so the caller can retry through software.
 * The decoder is not retired automatically: the caller can log last_error(),
 * retire it, and retry the same compressed frame through software.
 */
int vj_nvjpeg_decoder_decode(vj_nvjpeg_decoder *decoder,
                             const uint8_t *jpeg_data,
                             size_t jpeg_size,
                             vj_nvjpeg_output requested_output,
                             vj_nvjpeg_output *actual_output,
                             uint8_t *const dst[3],
                             const size_t dst_pitch[3]);

void vj_nvjpeg_decoder_retire(vj_nvjpeg_decoder *decoder);
int vj_nvjpeg_decoder_is_active(const vj_nvjpeg_decoder *decoder);
int vj_nvjpeg_decoder_supports_444(const vj_nvjpeg_decoder *decoder);
const char *vj_nvjpeg_decoder_engine(const vj_nvjpeg_decoder *decoder);
const char *vj_nvjpeg_decoder_upsampler(const vj_nvjpeg_decoder *decoder);
const char *vj_nvjpeg_decoder_source_format(const vj_nvjpeg_decoder *decoder);
const char *vj_nvjpeg_decoder_conversion(const vj_nvjpeg_decoder *decoder);
const char *vj_nvjpeg_decoder_last_error(const vj_nvjpeg_decoder *decoder);

#ifdef __cplusplus
}
#endif

#endif
