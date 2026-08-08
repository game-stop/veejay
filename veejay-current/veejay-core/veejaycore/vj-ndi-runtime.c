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

#include <config.h>
#include <veejaycore/vj-ndi-runtime.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

const char *vj_ndi_runtime_arch_triplet(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64-linux-gnu";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64-linux-gnu";
#elif defined(__i386__) || defined(_M_IX86)
    return "i386-linux-gnu";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm-linux-gnueabihf";
#elif defined(__powerpc64__)
    return "powerpc64-linux-gnu";
#elif defined(__powerpc__)
    return "powerpc-linux-gnu";
#elif defined(__riscv) && __riscv_xlen == 64
    return "riscv64-linux-gnu";
#else
    return NULL;
#endif
}

int vj_ndi_runtime_default_user_dir(char *buffer, size_t buffer_size)
{
    const char *home;
    const char *triplet;
    int written;

    if(!buffer || buffer_size == 0)
        return 0;
    buffer[0] = '\0';

    home = getenv("HOME");
    triplet = vj_ndi_runtime_arch_triplet();
    if(!home || !*home || !triplet || !*triplet)
        return 0;

    written = snprintf(buffer, buffer_size,
                       "%s/opt/ndi-sdk6/lib/%s", home, triplet);
    return written > 0 && (size_t)written < buffer_size;
}

static int vj_ndi_runtime_emit(const char *candidate,
                               vj_ndi_runtime_candidate_func callback,
                               void *opaque)
{
    if(!candidate || !*candidate || !callback)
        return 0;
    return callback(candidate, opaque) ? 1 : 0;
}

static int vj_ndi_runtime_name_seen(const char *const *names,
                                    int count,
                                    const char *name)
{
    if(!name || !*name)
        return 1;
    for(int i = 0; i < count; i++) {
        if(names[i] && strcmp(names[i], name) == 0)
            return 1;
    }
    return 0;
}

static int vj_ndi_runtime_emit_dir(const char *folder,
                                   const char *const *names,
                                   int name_count,
                                   vj_ndi_runtime_candidate_func callback,
                                   void *opaque)
{
    char candidate[PATH_MAX];

    if(!folder || !*folder)
        return 0;

    for(int i = 0; i < name_count; i++) {
        const char *name = names[i];
        int written;
        if(!name || !*name)
            continue;
        if(name[0] == '/')
            continue;
        written = snprintf(candidate, sizeof(candidate), "%s/%s", folder, name);
        if(written <= 0 || (size_t)written >= sizeof(candidate))
            continue;
        if(vj_ndi_runtime_emit(candidate, callback, opaque))
            return 1;
    }
    return 0;
}

static int vj_ndi_runtime_emit_sdk_root(const char *folder,
                                        const char *const *names,
                                        int name_count,
                                        vj_ndi_runtime_candidate_func callback,
                                        void *opaque)
{
    char lib_dir[PATH_MAX];
    const char *triplet = vj_ndi_runtime_arch_triplet();
    int written;

    if(!folder || !*folder)
        return 0;

    if(triplet && *triplet) {
        written = snprintf(lib_dir, sizeof(lib_dir), "%s/lib/%s", folder, triplet);
        if(written > 0 && (size_t)written < sizeof(lib_dir) &&
           vj_ndi_runtime_emit_dir(lib_dir, names, name_count, callback, opaque))
            return 1;
    }

    written = snprintf(lib_dir, sizeof(lib_dir), "%s/lib", folder);
    if(written > 0 && (size_t)written < sizeof(lib_dir) &&
       vj_ndi_runtime_emit_dir(lib_dir, names, name_count, callback, opaque))
        return 1;

    return 0;
}

int vj_ndi_runtime_foreach_candidate(const char *preferred_library,
                                     const char *preferred_env_name,
                                     vj_ndi_runtime_candidate_func callback,
                                     void *opaque)
{
    const char *names[4];
    int name_count = 0;
    const char *env_names[3];
    int env_count = 0;
    char user_arch_dir[PATH_MAX];
    char user_lib_dir[PATH_MAX];
    const char *home = getenv("HOME");
    const char *system_dirs[] = {
        "/usr/local/lib",
        "/usr/local/lib64",
        "/usr/lib",
        "/usr/lib64",
        "/usr/lib/x86_64-linux-gnu",
        "/usr/lib/aarch64-linux-gnu",
        "/opt/ndi/lib"
    };

    if(!callback)
        return 0;

    if(preferred_library && *preferred_library)
        names[name_count++] = preferred_library;
    if(!vj_ndi_runtime_name_seen(names, name_count, "libndi.so.6"))
        names[name_count++] = "libndi.so.6";
    if(!vj_ndi_runtime_name_seen(names, name_count, "libndi.so"))
        names[name_count++] = "libndi.so";
    if(!vj_ndi_runtime_name_seen(names, name_count, "libndi.so.5"))
        names[name_count++] = "libndi.so.5";

    if(preferred_env_name && *preferred_env_name)
        env_names[env_count++] = preferred_env_name;
    if(!vj_ndi_runtime_name_seen(env_names, env_count, "NDI_RUNTIME_DIR_V6"))
        env_names[env_count++] = "NDI_RUNTIME_DIR_V6";
    if(!vj_ndi_runtime_name_seen(env_names, env_count, "NDI_RUNTIME_DIR_V5"))
        env_names[env_count++] = "NDI_RUNTIME_DIR_V5";

    for(int i = 0; i < env_count; i++) {
        const char *folder = getenv(env_names[i]);
        if(folder && *folder) {
            if(vj_ndi_runtime_emit_dir(folder, names, name_count, callback, opaque))
                return 1;
            if(vj_ndi_runtime_emit_sdk_root(folder, names, name_count,
                                            callback, opaque))
                return 1;
        }
    }

    for(int i = 0; i < name_count; i++) {
        if(vj_ndi_runtime_emit(names[i], callback, opaque))
            return 1;
    }

    if(vj_ndi_runtime_default_user_dir(user_arch_dir, sizeof(user_arch_dir)) &&
       vj_ndi_runtime_emit_dir(user_arch_dir, names, name_count, callback, opaque))
        return 1;

    if(home && *home) {
        int written = snprintf(user_lib_dir, sizeof(user_lib_dir),
                               "%s/opt/ndi-sdk6/lib", home);
        if(written > 0 && (size_t)written < sizeof(user_lib_dir) &&
           vj_ndi_runtime_emit_dir(user_lib_dir, names, name_count, callback, opaque))
            return 1;
    }

    for(size_t i = 0; i < sizeof(system_dirs) / sizeof(system_dirs[0]); i++) {
        if(vj_ndi_runtime_emit_dir(system_dirs[i], names, name_count,
                                   callback, opaque))
            return 1;
    }

    return 0;
}
