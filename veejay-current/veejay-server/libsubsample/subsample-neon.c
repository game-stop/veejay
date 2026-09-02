/* 
 * Linux VeeJay
 *
 * Copyright(C)2002 - 2026 Niels Elburg <nwelburg@gmail.com>
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
#include <stddef.h>
#include <stdint.h>

#include <libsubsample/subsample-arch.h>

#ifdef SUBSAMPLE_HAVE_NEON
#include <arm_neon.h>

void ss_444_to_422_drop_neon(uint8_t *restrict U, uint8_t *restrict V,
                             int width, int height)
{
    const size_t total_dest_pixels = ((size_t)width * height) >> 1;
    size_t pixel = 0;

    for (; pixel + 16 <= total_dest_pixels; pixel += 16) {
        const size_t source = pixel << 1;
        const uint8x16x2_t u_pixels = vld2q_u8(U + source);
        const uint8x16x2_t v_pixels = vld2q_u8(V + source);

        vst1q_u8(U + pixel, u_pixels.val[0]);
        vst1q_u8(V + pixel, v_pixels.val[0]);
    }

    for (; pixel < total_dest_pixels; pixel++) {
        const size_t source = pixel << 1;
        U[pixel] = U[source];
        V[pixel] = V[source];
    }
}

void tr_422_to_444_dup_neon(uint8_t *restrict chroma, int width, int height)
{
    const int source_width = width >> 1;

    for (int row = height - 1; row >= 0; row--) {
        const uint8_t *source = chroma + (size_t)row * source_width;
        uint8_t *destination = chroma + (size_t)row * width;
        int column = source_width;

        while (column >= 16) {
            column -= 16;
            const uint8x16_t pixels = vld1q_u8(source + column);
            const uint8x16x2_t duplicated = vzipq_u8(pixels, pixels);

            vst1q_u8(destination + (column << 1), duplicated.val[0]);
            vst1q_u8(destination + (column << 1) + 16, duplicated.val[1]);
        }

        while (column > 0) {
            const uint8_t pixel = source[--column];
            destination[column << 1] = pixel;
            destination[(column << 1) + 1] = pixel;
        }
    }
}

void ss_444_to_420jpeg_neon(uint8_t *buffer, int width, int height)
{
    if (buffer == NULL || width < 2 || height < 2)
        return;

    uint8_t *destination = buffer;
    for (int row = 0; row + 1 < height; row += 2) {
        const uint8_t *top = buffer + (size_t)row * width;
        const uint8_t *bottom = top + width;
        int column = 0;

        for (; column + 31 < width; column += 32) {
            const uint8x16x2_t top_pixels = vld2q_u8(top + column);
            const uint8x16x2_t bottom_pixels = vld2q_u8(bottom + column);
            uint16x8_t sum_low = vmovl_u8(vget_low_u8(top_pixels.val[0]));
            uint16x8_t sum_high = vmovl_u8(vget_high_u8(top_pixels.val[0]));

            sum_low = vmlaq_n_u16(sum_low, vmovl_u8(vget_low_u8(top_pixels.val[1])), 3);
            sum_low = vmlaq_n_u16(sum_low, vmovl_u8(vget_low_u8(bottom_pixels.val[0])), 3);
            sum_low = vmlaq_n_u16(sum_low, vmovl_u8(vget_low_u8(bottom_pixels.val[1])), 9);
            sum_high = vmlaq_n_u16(sum_high, vmovl_u8(vget_high_u8(top_pixels.val[1])), 3);
            sum_high = vmlaq_n_u16(sum_high, vmovl_u8(vget_high_u8(bottom_pixels.val[0])), 3);
            sum_high = vmlaq_n_u16(sum_high, vmovl_u8(vget_high_u8(bottom_pixels.val[1])), 9);

            vst1q_u8(destination, vcombine_u8(vrshrn_n_u16(sum_low, 4),
                                              vrshrn_n_u16(sum_high, 4)));
            destination += 16;
        }

        for (; column + 1 < width; column += 2) {
            *destination++ = (uint8_t)((top[column] +
                                        3 * (top[column + 1] + bottom[column]) +
                                        9 * bottom[column + 1] + 8) >> 4);
        }
    }
}

void ss_420jpeg_to_444_neon(uint8_t *buffer, int width, int height)
{
    if (buffer == NULL || width <= 0 || height <= 0)
        return;

    const int source_width = width >> 1;
    const int source_height = height >> 1;

    for (int row = source_height - 1; row >= 0; row--) {
        const uint8_t *source = buffer + (size_t)row * source_width;
        uint8_t *top = buffer + (size_t)(row << 1) * width;
        uint8_t *bottom = top + width;
        int column = source_width;

        while (column >= 16) {
            column -= 16;
            const uint8x16_t pixels = vld1q_u8(source + column);
            const uint8x16x2_t duplicated = vzipq_u8(pixels, pixels);

            vst1q_u8(top + (column << 1), duplicated.val[0]);
            vst1q_u8(top + (column << 1) + 16, duplicated.val[1]);
            vst1q_u8(bottom + (column << 1), duplicated.val[0]);
            vst1q_u8(bottom + (column << 1) + 16, duplicated.val[1]);
        }

        while (column > 0) {
            const uint8_t pixel = source[--column];
            top[column << 1] = pixel;
            top[(column << 1) + 1] = pixel;
            bottom[column << 1] = pixel;
            bottom[(column << 1) + 1] = pixel;
        }
    }
}
#endif