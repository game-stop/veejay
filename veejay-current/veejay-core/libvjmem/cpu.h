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
#ifndef VJ_CPU_H
#define VJ_CPU_H

#include <libavutil/cpu.h>
#include <stdint.h>

#if defined(__GNUC__)
#define VJ_CPU_INTERNAL __attribute__((visibility("hidden")))
#else
#define VJ_CPU_INTERNAL
#endif

VJ_CPU_INTERNAL int vj_cpu_get_flags(void);
VJ_CPU_INTERNAL int vj_cpu_supports(uint32_t available_flags,
									uint32_t required_flags);

#endif