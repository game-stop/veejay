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

#ifndef VJ_OMP_COMPAT_H
#define VJ_OMP_COMPAT_H

#ifdef _OPENMP
#include <omp.h>
#else
typedef int omp_lock_t;

static inline int omp_get_max_threads(void)
{
    return 1;
}

static inline int omp_get_num_threads(void)
{
    return 1;
}

static inline int omp_get_thread_num(void)
{
    return 0;
}

static inline int omp_get_num_procs(void)
{
    return 1;
}

static inline void omp_set_dynamic(int dynamic_threads)
{
    (void) dynamic_threads;
}

static inline void omp_set_num_threads(int num_threads)
{
    (void) num_threads;
}

static inline void omp_set_nested(int nested)
{
    (void) nested;
}

static inline void omp_init_lock(omp_lock_t *lock)
{
    *lock = 0;
}

static inline void omp_set_lock(omp_lock_t *lock)
{
    (void) lock;
}

static inline void omp_unset_lock(omp_lock_t *lock)
{
    (void) lock;
}
#endif

#endif