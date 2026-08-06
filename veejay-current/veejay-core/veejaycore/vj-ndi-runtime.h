/*
 * Copyright (C) 2026 Niels Elburg <nwelburg@gmail.com>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef VJ_NDI_RUNTIME_H
#define VJ_NDI_RUNTIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*vj_ndi_runtime_candidate_func)(const char *candidate, void *opaque);

const char *vj_ndi_runtime_arch_triplet(void);
int vj_ndi_runtime_default_user_dir(char *buffer, size_t buffer_size);
int vj_ndi_runtime_foreach_candidate(const char *preferred_library,
                                     const char *preferred_env_name,
                                     vj_ndi_runtime_candidate_func callback,
                                     void *opaque);

#ifdef __cplusplus
}
#endif

#endif
