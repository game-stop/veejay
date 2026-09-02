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
#include <string.h>

#include <libsubsample/subsample-arch.h>

#ifdef SUBSAMPLE_HAVE_ALTIVEC
#include <altivec.h>

static inline vector unsigned char altivec_load_u8(const uint8_t *source)
{
    vector unsigned char value;
    memcpy(&value, source, sizeof(value));
    return value;
}

static inline void altivec_store_u8(uint8_t *destination,
                                    vector unsigned char value)
{
    memcpy(destination, &value, sizeof(value));
}

static inline vector unsigned short altivec_subsample_420_vector(
    vector unsigned char top, vector unsigned char bottom,
    vector unsigned char ones)
{
    const vector unsigned short three = vec_splat_u16(3);
    const vector unsigned short nine = vec_splat_u16(9);
    const vector unsigned short eight = vec_splat_u16(8);
    const vector unsigned short shift = vec_splat_u16(4);
    const vector unsigned short top_even = vec_mule(top, ones);
    const vector unsigned short top_odd = vec_mulo(top, ones);
    const vector unsigned short bottom_even = vec_mule(bottom, ones);
    const vector unsigned short bottom_odd = vec_mulo(bottom, ones);
    vector unsigned short sum = vec_mladd(top_odd, three, top_even);

    sum = vec_mladd(bottom_even, three, sum);
    sum = vec_mladd(bottom_odd, nine, sum);
    return vec_sr(vec_add(sum, eight), shift);
}

void ss_444_to_422_drop_altivec(uint8_t *restrict U, uint8_t *restrict V,
                                int width, int height)
{
    const size_t total_dest_pixels = ((size_t)width * height) >> 1;
    const vector unsigned char ones = vec_splat_u8(1);
    size_t sample_index = 0;

    for (; sample_index + 16 <= total_dest_pixels; sample_index += 16) {
        const size_t source = sample_index << 1;
        const vector unsigned char u_low = altivec_load_u8(U + source);
        const vector unsigned char u_high = altivec_load_u8(U + source + 16);
        const vector unsigned char v_low = altivec_load_u8(V + source);
        const vector unsigned char v_high = altivec_load_u8(V + source + 16);
        const vector unsigned char u_output =
            vec_pack(vec_mule(u_low, ones), vec_mule(u_high, ones));
        const vector unsigned char v_output =
            vec_pack(vec_mule(v_low, ones), vec_mule(v_high, ones));

        altivec_store_u8(U + sample_index, u_output);
        altivec_store_u8(V + sample_index, v_output);
    }

    for (; sample_index < total_dest_pixels; sample_index++) {
        const size_t source = sample_index << 1;
        U[sample_index] = U[source];
        V[sample_index] = V[source];
    }
}

void tr_422_to_444_dup_altivec(uint8_t *restrict chroma, int width, int height)
{
    const int source_width = width >> 1;

    for (int row = height - 1; row >= 0; row--) {
        const uint8_t *source = chroma + (size_t)row * source_width;
        uint8_t *destination = chroma + (size_t)row * width;
        int column = source_width;

        while (column >= 16) {
            column -= 16;
            const vector unsigned char pixels = altivec_load_u8(source + column);

            altivec_store_u8(destination + (column << 1),
                             vec_mergeh(pixels, pixels));
            altivec_store_u8(destination + (column << 1) + 16,
                             vec_mergel(pixels, pixels));
        }

        while (column > 0) {
            const uint8_t value = source[--column];
            destination[column << 1] = value;
            destination[(column << 1) + 1] = value;
        }
    }
}

void ss_444_to_420jpeg_altivec(uint8_t *buffer, int width, int height)
{
    if (buffer == NULL || width < 2 || height < 2)
        return;

    const vector unsigned char ones = vec_splat_u8(1);
    uint8_t *destination = buffer;

    for (int row = 0; row + 1 < height; row += 2) {
        const uint8_t *top = buffer + (size_t)row * width;
        const uint8_t *bottom = top + width;
        int column = 0;

        for (; column + 31 < width; column += 32) {
            const vector unsigned char top_low = altivec_load_u8(top + column);
            const vector unsigned char top_high = altivec_load_u8(top + column + 16);
            const vector unsigned char bottom_low = altivec_load_u8(bottom + column);
            const vector unsigned char bottom_high = altivec_load_u8(bottom + column + 16);
            const vector unsigned char output = vec_pack(
                altivec_subsample_420_vector(top_low, bottom_low, ones),
                altivec_subsample_420_vector(top_high, bottom_high, ones));

            altivec_store_u8(destination, output);
            destination += 16;
        }

        for (; column + 1 < width; column += 2) {
            *destination++ = (uint8_t)((top[column] +
                                        3 * (top[column + 1] + bottom[column]) +
                                        9 * bottom[column + 1] + 8) >> 4);
        }
    }
}

void ss_420jpeg_to_444_altivec(uint8_t *buffer, int width, int height)
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
            const vector unsigned char pixels = altivec_load_u8(source + column);
            const vector unsigned char duplicated_low = vec_mergeh(pixels, pixels);
            const vector unsigned char duplicated_high = vec_mergel(pixels, pixels);

            altivec_store_u8(top + (column << 1), duplicated_low);
            altivec_store_u8(top + (column << 1) + 16, duplicated_high);
            altivec_store_u8(bottom + (column << 1), duplicated_low);
            altivec_store_u8(bottom + (column << 1) + 16, duplicated_high);
        }

        while (column > 0) {
            const uint8_t value = source[--column];
            top[column << 1] = value;
            top[(column << 1) + 1] = value;
            bottom[column << 1] = value;
            bottom[(column << 1) + 1] = value;
        }
    }
}
#endif