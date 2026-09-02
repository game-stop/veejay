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

#ifdef SUBSAMPLE_HAVE_ESP32
static inline uint32_t esp32_load_u32(const uint8_t *source)
{
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    return value;
}

static inline void esp32_store_u32(uint8_t *destination, uint32_t value)
{
    memcpy(destination, &value, sizeof(value));
}

static inline uint32_t esp32_pack_even_bytes(uint32_t low, uint32_t high)
{
    return (low & 0x000000ffu) |
           ((low >> 8) & 0x0000ff00u) |
           ((high << 16) & 0x00ff0000u) |
           ((high << 8) & 0xff000000u);
}

static inline void esp32_duplicate_four(uint8_t *destination, uint32_t pixels)
{
    const uint32_t low = ((pixels & 0xffu) * 0x00000101u) |
                         (((pixels >> 8) & 0xffu) * 0x01010000u);
    const uint32_t high = (((pixels >> 16) & 0xffu) * 0x00000101u) |
                          (((pixels >> 24) & 0xffu) * 0x01010000u);

    esp32_store_u32(destination, low);
    esp32_store_u32(destination + 4, high);
}

static inline uint8_t esp32_subsample_420_pixel(const uint8_t *top,
                                                const uint8_t *bottom,
                                                int column)
{
    return (uint8_t)((top[column] +
                      3 * (top[column + 1] + bottom[column]) +
                      9 * bottom[column + 1] + 8) >> 4);
}

void ss_444_to_422_drop_esp32(uint8_t *restrict U, uint8_t *restrict V,
                              int width, int height)
{
    const size_t total_dest_pixels = ((size_t)width * height) >> 1;
    size_t pixel = 0;

    for (; pixel + 4 <= total_dest_pixels; pixel += 4) {
        const size_t source = pixel << 1;
        const uint32_t u_low = esp32_load_u32(U + source);
        const uint32_t u_high = esp32_load_u32(U + source + 4);
        const uint32_t v_low = esp32_load_u32(V + source);
        const uint32_t v_high = esp32_load_u32(V + source + 4);

        esp32_store_u32(U + pixel, esp32_pack_even_bytes(u_low, u_high));
        esp32_store_u32(V + pixel, esp32_pack_even_bytes(v_low, v_high));
    }

    for (; pixel < total_dest_pixels; pixel++) {
        const size_t source = pixel << 1;
        U[pixel] = U[source];
        V[pixel] = V[source];
    }
}

void tr_422_to_444_dup_esp32(uint8_t *restrict chroma, int width, int height)
{
    const int source_width = width >> 1;

    for (int row = height - 1; row >= 0; row--) {
        const uint8_t *source = chroma + (size_t)row * source_width;
        uint8_t *destination = chroma + (size_t)row * width;
        int column = source_width;

        while (column >= 4) {
            column -= 4;
            esp32_duplicate_four(destination + (column << 1),
                                 esp32_load_u32(source + column));
        }

        while (column > 0) {
            const uint8_t pixel = source[--column];
            destination[column << 1] = pixel;
            destination[(column << 1) + 1] = pixel;
        }
    }
}

void ss_444_to_420jpeg_esp32(uint8_t *buffer, int width, int height)
{
    if (buffer == NULL || width < 2 || height < 2)
        return;

    uint8_t *destination = buffer;
    for (int row = 0; row + 1 < height; row += 2) {
        const uint8_t *top = buffer + (size_t)row * width;
        const uint8_t *bottom = top + width;
        int column = 0;

        for (; column + 7 < width; column += 8) {
            destination[0] = esp32_subsample_420_pixel(top, bottom, column);
            destination[1] = esp32_subsample_420_pixel(top, bottom, column + 2);
            destination[2] = esp32_subsample_420_pixel(top, bottom, column + 4);
            destination[3] = esp32_subsample_420_pixel(top, bottom, column + 6);
            destination += 4;
        }

        for (; column + 1 < width; column += 2)
            *destination++ = esp32_subsample_420_pixel(top, bottom, column);
    }
}

void ss_420jpeg_to_444_esp32(uint8_t *buffer, int width, int height)
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

        while (column >= 4) {
            column -= 4;
            const uint32_t pixels = esp32_load_u32(source + column);
            esp32_duplicate_four(top + (column << 1), pixels);
            esp32_duplicate_four(bottom + (column << 1), pixels);
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