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
#ifndef SUBSAMPLE_ARCH_H
#define SUBSAMPLE_ARCH_H

#include <stdint.h>

#if defined(HAVE_ARM_NEON) || defined(HAVE_ARM_ASIMD)
#define SUBSAMPLE_HAVE_NEON 1

void ss_444_to_422_drop_neon(uint8_t *restrict U, uint8_t *restrict V,
                             int width, int height);
void tr_422_to_444_dup_neon(uint8_t *restrict chroma, int width, int height);
void ss_444_to_420jpeg_neon(uint8_t *buffer, int width, int height);
void ss_420jpeg_to_444_neon(uint8_t *buffer, int width, int height);
#endif

#if defined(HAVE_ASM_ALTIVEC)
#define SUBSAMPLE_HAVE_ALTIVEC 1

void ss_444_to_422_drop_altivec(uint8_t *restrict U, uint8_t *restrict V,
                                int width, int height);
void tr_422_to_444_dup_altivec(uint8_t *restrict chroma, int width, int height);
void ss_444_to_420jpeg_altivec(uint8_t *buffer, int width, int height);
void ss_420jpeg_to_444_altivec(uint8_t *buffer, int width, int height);
#endif

#if defined(HAVE_ESP32) || defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#define SUBSAMPLE_HAVE_ESP32 1

void ss_444_to_422_drop_esp32(uint8_t *restrict U, uint8_t *restrict V,
                              int width, int height);
void tr_422_to_444_dup_esp32(uint8_t *restrict chroma, int width, int height);
void ss_444_to_420jpeg_esp32(uint8_t *buffer, int width, int height);
void ss_420jpeg_to_444_esp32(uint8_t *buffer, int width, int height);
#endif

#endif