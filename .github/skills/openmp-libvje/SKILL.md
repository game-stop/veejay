---
name: openmp-libvje
description: Describe what this skill does and when to use it. Include keywords that help agents identify relevant tasks.
---

Refactor all VJE effects in place for OpenMP safety. Do not print replacement files or make unrelated formatting changes.

First search the entire effects tree for `copyprivate`, skip/ready flags, `omp single`, nested `omp parallel`, `parallel for`, `num_threads`, whole-frame `veejay_memcpy`, and helpers containing OpenMP pragmas. Process the results in batches of at most 10 files, build-check each batch, and continue until all matches are reviewed. Leave files unchanged when no fix is needed. If a limit stops you, finish the current file and report the exact remaining files so I can say `continue`.

Architecture:
- Normal internal `*_apply()` functions are already called by every worker inside an outer `#pragma omp parallel` region through `vj-perform.c`, `vjert.c`, and `libvje.c`.
- Plugins, init/prepare/free functions, and explicit `_serial` paths are single-threaded unless their callers prove otherwise.
- `vj_effect.parallel` is obsolete; do not use or modify it.

Rules:
1. In normal apply paths, never add nested `omp parallel`, `parallel for`, or `num_threads`. Use orphaned `#pragma omp for schedule(static)`.
2. Every worker must encounter worksharing constructs in the same order. Never guard `omp for`, `single`, `barrier`, or a helper containing them with a condition on which workers can disagree.
3. Function locals are private per worker. Never set a local skip/result flag inside `omp single` and branch on it outside; this can deadlock. Compute argument-only conditions on every worker, or publish state-dependent decisions through shared effect state inside `single` and its implicit barrier.
4. Never return from `omp single`. Early returns are allowed only on team-uniform branches after required barriers.
5. Keep shared state updates such as counters, history heads, swaps, resets, and map/LUT rebuilds in `single`. Use `nowait` only when nothing later depends on the result.
6. Helpers containing worksharing must be called uniformly by the whole team, never from `single`. Apply this to motionmap and every caller.
7. Remove `copyprivate` by recomputing cheap values per worker, using existing shared effect state, or using a correct reduction plus barrier. Do not only delete the clause. Retaining `single copyprivate(threshold)` in stateless `bwotsu.c` is an acceptable scalar-broadcast tradeoff.
8. In normal apply paths, parallelize complete frame-plane copies between `frame->data[]` and persistent FX buffers, in either direction. Partition bytes or rows across all workers; avoid a three-iteration plane loop that uses only three threads. Keep `veejay_memcpy` for serial paths and small, partial, metadata, LUT, or map copies.
9. Bypass conditions such as `opacity == 0`, `opacity == 255`, or `strength <= 0` may be evaluated by every worker. Full-copy endpoints must be entered by the whole team and complete their parallel copies before returning.

Validate touched files by compiling and reviewing normal, bypass, full-copy, first-frame, reset, missing-history, and motionmap paths with 1, 2, and 8 OpenMP threads. Check that no subset of workers can enter a worksharing construct alone.

Report only: changed files, deadlocks fixed, frame copies parallelized, `copyprivate` removed or retained with reasons, checks run, and remaining files.
