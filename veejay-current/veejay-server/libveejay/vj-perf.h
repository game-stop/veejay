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
#ifndef VJ_PERF_H
#define VJ_PERF_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    VJ_PERF_STAGE_SOURCE = 0,
    VJ_PERF_STAGE_FX,
    VJ_PERF_STAGE_TRANSITION,
    VJ_PERF_STAGE_COMPOSITE,
    VJ_PERF_STAGE_OSD,
    VJ_PERF_STAGE_OUTPUT_GRAPH,
    VJ_PERF_STAGE_QUEUE_COPY,
    VJ_PERF_STAGE_PRODUCER_TOTAL,
    VJ_PERF_STAGE_QUEUE_WAIT,
    VJ_PERF_STAGE_SYNC_WAIT,
    VJ_PERF_STAGE_SPLIT,
    VJ_PERF_STAGE_NETWORK,
    VJ_PERF_STAGE_SHM_READ,
    VJ_PERF_STAGE_SHM_WRITE,
    VJ_PERF_STAGE_CONVERT,
    VJ_PERF_STAGE_UPLOAD_PRESENT,
    VJ_PERF_STAGE_RENDERER_TOTAL,
    VJ_PERF_STAGE_DECODE,
    VJ_PERF_STAGE_CUDA_DECODE,
    VJ_PERF_STAGE_CUDA_CHROMA,
    VJ_PERF_STAGE_CUDA_D2H,
    VJ_PERF_STAGE_DECODE_COPY,
    VJ_PERF_STAGE_RAW_CACHE,
    VJ_PERF_STAGE_SAMPLE_SNAPSHOT,
    VJ_PERF_STAGE_PREVIEW_SNAPSHOT,
    VJ_PERF_STAGE_SDL_PACK,
    VJ_PERF_STAGE_SDL_UPLOAD,
    VJ_PERF_STAGE_SDL_TEXTURE_LOCK,
    VJ_PERF_STAGE_SDL_TEXTURE_UNLOCK,
    VJ_PERF_STAGE_SDL_TEXTURE_UPDATE,
    VJ_PERF_STAGE_SDL_RENDER_COPY,
    VJ_PERF_STAGE_PRESENT_BLOCK,
    VJ_PERF_STAGE_VIDEO_CONTROL,
    VJ_PERF_STAGE_VIDEO_PACE,
    VJ_PERF_STAGE_VIDEO_QUEUE_RESERVE,
    VJ_PERF_STAGE_AUDIO_DECODE,
    VJ_PERF_STAGE_AUDIO_WRITE,
    VJ_PERF_STAGE_AUDIO_PACE,
    VJ_PERF_STAGE_AUDIO_TOTAL,
    VJ_PERF_STAGE_COUNT
} vj_perf_stage_t;

typedef enum {
    VJ_PERF_ROLE_STANDALONE = 0,
    VJ_PERF_ROLE_PROGRAM = 1,
    VJ_PERF_ROLE_OUTPUT = 2
} vj_perf_role_t;

typedef struct vj_perf_context vj_perf_context;

typedef struct {
    uint64_t count;
    uint64_t total_ns;
    uint64_t max_ns;
    uint64_t p95_ns;
    uint64_t over_budget;
} vj_perf_stage_snapshot;

typedef struct {
    char instance_id[64];
    int role;
    int port;
    uint64_t budget_ns;
    uint64_t dropped_frames;
    uint64_t replaced_frames;
    uint64_t source_stalls;
    vj_perf_stage_snapshot stage[VJ_PERF_STAGE_COUNT];
} vj_perf_snapshot;

vj_perf_context *vj_perf_create(void);
void vj_perf_destroy(vj_perf_context *ctx);
void vj_perf_reset(vj_perf_context *ctx);
void vj_perf_set_identity(vj_perf_context *ctx, const char *instance_id,
                          int role, int port, double fps);
uint64_t vj_perf_now_ns(void);
void vj_perf_record(vj_perf_context *ctx, vj_perf_stage_t stage,
                    uint64_t start_ns, uint64_t end_ns);
void vj_perf_note_drop(vj_perf_context *ctx, uint64_t count);
void vj_perf_note_replace(vj_perf_context *ctx, uint64_t count);
void vj_perf_note_source_stall(vj_perf_context *ctx, uint64_t count);
int vj_perf_snapshot_read(const vj_perf_context *ctx, vj_perf_snapshot *snapshot);
size_t vj_perf_format_text(const vj_perf_context *ctx, char *dst, size_t dst_len);
const char *vj_perf_stage_name(vj_perf_stage_t stage);
const char *vj_perf_role_name(int role);

#endif
