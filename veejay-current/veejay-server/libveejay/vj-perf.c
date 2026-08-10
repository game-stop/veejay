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
#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <veejaycore/vjmem.h>
#include <libveejay/vj-perf.h>

#define VJ_PERF_HIST_BUCKETS 16

static const uint64_t vj_perf_hist_upper_ns[VJ_PERF_HIST_BUCKETS] = {
    100000ULL, 250000ULL, 500000ULL, 1000000ULL,
    2000000ULL, 4000000ULL, 8000000ULL, 16000000ULL,
    25000000ULL, 33000000ULL, 50000000ULL, 66000000ULL,
    100000000ULL, 250000000ULL, 500000000ULL, UINT64_MAX
};

typedef struct {
    volatile unsigned long long count;
    volatile unsigned long long total_ns;
    volatile unsigned long long max_ns;
    volatile unsigned long long recent_ns;
    volatile unsigned long long over_budget;
    volatile unsigned long long histogram[VJ_PERF_HIST_BUCKETS];
} vj_perf_counter;

struct vj_perf_context {
    char instance_id[64];
    volatile int role;
    volatile int port;
    volatile unsigned long long budget_ns;
    volatile unsigned long long dropped_frames;
    volatile unsigned long long replaced_frames;
    volatile unsigned long long source_stalls;
    vj_perf_counter stage[VJ_PERF_STAGE_COUNT];
};

static inline uint64_t vj_perf_load_u64(const volatile unsigned long long *value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

static inline void vj_perf_store_u64(volatile unsigned long long *value, uint64_t v)
{
    __atomic_store_n(value, v, __ATOMIC_RELAXED);
}

static void vj_perf_update_max(volatile unsigned long long *value, uint64_t sample)
{
    unsigned long long old = vj_perf_load_u64(value);
    const unsigned long long candidate = (unsigned long long)sample;
    while(candidate > old &&
          !__atomic_compare_exchange_n(value, &old, candidate, 1,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}

static void vj_perf_update_recent(volatile unsigned long long *value, uint64_t sample)
{
    unsigned long long old = vj_perf_load_u64(value);
    const unsigned long long candidate = (unsigned long long)sample;
    for(;;) {
        const unsigned long long next = old == 0 ? candidate :
            (((old * 7ULL) + candidate + 4ULL) >> 3);
        if(__atomic_compare_exchange_n(value, &old, next, 1,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            break;
    }
}

vj_perf_context *vj_perf_create(void)
{
    vj_perf_context *ctx = (vj_perf_context*)vj_calloc(sizeof(vj_perf_context));
    if(ctx)
        snprintf(ctx->instance_id, sizeof(ctx->instance_id), "%s", "veejay");
    return ctx;
}

void vj_perf_destroy(vj_perf_context *ctx)
{
    free(ctx);
}

void vj_perf_reset(vj_perf_context *ctx)
{
    if(!ctx)
        return;

    for(int i = 0; i < VJ_PERF_STAGE_COUNT; i++) {
        vj_perf_store_u64(&ctx->stage[i].count, 0);
        vj_perf_store_u64(&ctx->stage[i].total_ns, 0);
        vj_perf_store_u64(&ctx->stage[i].max_ns, 0);
        vj_perf_store_u64(&ctx->stage[i].recent_ns, 0);
        vj_perf_store_u64(&ctx->stage[i].over_budget, 0);
        for(int b = 0; b < VJ_PERF_HIST_BUCKETS; b++)
            vj_perf_store_u64(&ctx->stage[i].histogram[b], 0);
    }

    vj_perf_store_u64(&ctx->dropped_frames, 0);
    vj_perf_store_u64(&ctx->replaced_frames, 0);
    vj_perf_store_u64(&ctx->source_stalls, 0);
}

void vj_perf_set_identity(vj_perf_context *ctx, const char *instance_id,
                          int role, int port, double fps)
{
    if(!ctx)
        return;

    snprintf(ctx->instance_id, sizeof(ctx->instance_id), "%s",
             (instance_id && *instance_id) ? instance_id : "veejay");
    __atomic_store_n(&ctx->role, role, __ATOMIC_RELAXED);
    __atomic_store_n(&ctx->port, port, __ATOMIC_RELAXED);

    uint64_t budget = 0;
    if(fps > 0.0)
        budget = (uint64_t)(1000000000.0 / fps + 0.5);
    vj_perf_store_u64(&ctx->budget_ns, budget);
}

uint64_t vj_perf_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

void vj_perf_record(vj_perf_context *ctx, vj_perf_stage_t stage,
                    uint64_t start_ns, uint64_t end_ns)
{
    if(!ctx || stage < 0 || stage >= VJ_PERF_STAGE_COUNT || end_ns < start_ns)
        return;

    const uint64_t elapsed = end_ns - start_ns;
    vj_perf_counter *counter = &ctx->stage[stage];
    __atomic_fetch_add(&counter->count, 1ULL, __ATOMIC_RELAXED);
    __atomic_fetch_add(&counter->total_ns, elapsed, __ATOMIC_RELAXED);
    vj_perf_update_max(&counter->max_ns, elapsed);
    vj_perf_update_recent(&counter->recent_ns, elapsed);

    const uint64_t budget = vj_perf_load_u64(&ctx->budget_ns);
    if(budget > 0 && elapsed > budget)
        __atomic_fetch_add(&counter->over_budget, 1ULL, __ATOMIC_RELAXED);

    int bucket = 0;
    while(bucket < VJ_PERF_HIST_BUCKETS - 1 && elapsed > vj_perf_hist_upper_ns[bucket])
        bucket++;
    __atomic_fetch_add(&counter->histogram[bucket], 1ULL, __ATOMIC_RELAXED);
}

void vj_perf_note_drop(vj_perf_context *ctx, uint64_t count)
{
    if(ctx && count)
        __atomic_fetch_add(&ctx->dropped_frames, count, __ATOMIC_RELAXED);
}

void vj_perf_note_replace(vj_perf_context *ctx, uint64_t count)
{
    if(ctx && count)
        __atomic_fetch_add(&ctx->replaced_frames, count, __ATOMIC_RELAXED);
}

void vj_perf_note_source_stall(vj_perf_context *ctx, uint64_t count)
{
    if(ctx && count)
        __atomic_fetch_add(&ctx->source_stalls, count, __ATOMIC_RELAXED);
}

static uint64_t vj_perf_hist_p95(const vj_perf_counter *counter, uint64_t count)
{
    if(count == 0)
        return 0;

    const uint64_t target = (count * 95ULL + 99ULL) / 100ULL;
    uint64_t cumulative = 0;
    for(int b = 0; b < VJ_PERF_HIST_BUCKETS; b++) {
        cumulative += vj_perf_load_u64(&counter->histogram[b]);
        if(cumulative >= target)
            return vj_perf_hist_upper_ns[b];
    }
    return vj_perf_hist_upper_ns[VJ_PERF_HIST_BUCKETS - 1];
}

int vj_perf_snapshot_read(const vj_perf_context *ctx, vj_perf_snapshot *snapshot)
{
    if(!ctx || !snapshot)
        return 0;

    memset(snapshot, 0, sizeof(*snapshot));
    snprintf(snapshot->instance_id, sizeof(snapshot->instance_id), "%s", ctx->instance_id);
    snapshot->role = __atomic_load_n(&ctx->role, __ATOMIC_RELAXED);
    snapshot->port = __atomic_load_n(&ctx->port, __ATOMIC_RELAXED);
    snapshot->budget_ns = vj_perf_load_u64(&ctx->budget_ns);
    snapshot->dropped_frames = vj_perf_load_u64(&ctx->dropped_frames);
    snapshot->replaced_frames = vj_perf_load_u64(&ctx->replaced_frames);
    snapshot->source_stalls = vj_perf_load_u64(&ctx->source_stalls);

    for(int i = 0; i < VJ_PERF_STAGE_COUNT; i++) {
        const vj_perf_counter *counter = &ctx->stage[i];
        vj_perf_stage_snapshot *out = &snapshot->stage[i];
        out->count = vj_perf_load_u64(&counter->count);
        out->total_ns = vj_perf_load_u64(&counter->total_ns);
        out->max_ns = vj_perf_load_u64(&counter->max_ns);
        out->over_budget = vj_perf_load_u64(&counter->over_budget);
        out->p95_ns = vj_perf_hist_p95(counter, out->count);
    }
    return 1;
}

const char *vj_perf_stage_name(vj_perf_stage_t stage)
{
    static const char *names[VJ_PERF_STAGE_COUNT] = {
        "source", "fx", "transition", "composite", "osd", "output_graph",
        "queue_copy", "producer_total", "queue_wait", "sync_wait", "split",
        "network", "shm_read", "shm_write", "convert", "upload_present",
        "renderer_total", "decode", "cuda_decode", "cuda_chroma", "cuda_d2h",
        "decode_copy", "raw_cache", "sample_snapshot", "preview_snapshot",
        "sdl_pack", "sdl_upload", "sdl_texture_lock", "sdl_texture_unlock",
        "sdl_texture_update", "sdl_render_copy", "present_block",
        "video_control", "video_pace", "video_queue_reserve", "audio_decode",
        "audio_write", "audio_pace", "audio_total"
    };
    return (stage >= 0 && stage < VJ_PERF_STAGE_COUNT) ? names[stage] : "unknown";
}

const char *vj_perf_role_name(int role)
{
    switch(role) {
        case VJ_PERF_ROLE_PROGRAM: return "program";
        case VJ_PERF_ROLE_OUTPUT: return "output";
        default: return "standalone";
    }
}

size_t vj_perf_format_text(const vj_perf_context *ctx, char *dst, size_t dst_len)
{
    vj_perf_snapshot snapshot;
    size_t used = 0;

    if(!dst || dst_len == 0)
        return 0;
    dst[0] = '\0';
    if(!vj_perf_snapshot_read(ctx, &snapshot))
        return 0;

    int n = snprintf(dst, dst_len,
                     "VJPERF 1 id=%s role=%s port=%d budget_us=%llu dropped=%llu replaced=%llu stalls=%llu",
                     snapshot.instance_id, vj_perf_role_name(snapshot.role), snapshot.port,
                     (unsigned long long)(snapshot.budget_ns / 1000ULL),
                     (unsigned long long)snapshot.dropped_frames,
                     (unsigned long long)snapshot.replaced_frames,
                     (unsigned long long)snapshot.source_stalls);
    if(n < 0)
        return 0;
    used = (size_t)n < dst_len ? (size_t)n : dst_len - 1;

    for(int i = 0; i < VJ_PERF_STAGE_COUNT && used + 1 < dst_len; i++) {
        const vj_perf_stage_snapshot *stage = &snapshot.stage[i];
        if(stage->count == 0)
            continue;
        const uint64_t avg_ns = stage->total_ns / stage->count;
        n = snprintf(dst + used, dst_len - used,
                     " %s=%llu,%llu,%llu,%llu,%llu,%llu",
                     vj_perf_stage_name((vj_perf_stage_t)i),
                     (unsigned long long)stage->count,
                     (unsigned long long)(avg_ns / 1000ULL),
                     (unsigned long long)(stage->p95_ns / 1000ULL),
                     (unsigned long long)(stage->max_ns / 1000ULL),
                     (unsigned long long)stage->over_budget,
                     (unsigned long long)(vj_perf_load_u64(&ctx->stage[i].recent_ns) / 1000ULL));
        if(n < 0)
            break;
        if((size_t)n >= dst_len - used) {
            used = dst_len - 1;
            break;
        }
        used += (size_t)n;
    }
    return used;
}
