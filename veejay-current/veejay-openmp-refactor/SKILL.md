---
name: veejay-openmp-refactor
description: Refactor or review Linux VeeJay internal effects for outer-team OpenMP safety, copyprivate removal, uniform bypass logic, and parallel frame-buffer copies. Use for VJE effect threading work.
---

# VeeJay OpenMP Refactor

Edit effects in place. Do not print full replacement files, change unrelated code, or modify files that need no correction.

## Architecture

- Normal internal `*_apply()` functions are entered by every worker of an existing outer `#pragma omp parallel` region through `vj-perform.c`, `vjert.c`, and `libvje.c`.
- Plugins, init/prepare/free functions, and explicit `_serial` paths are single-threaded unless their callers prove otherwise.
- `vj_effect.parallel` is obsolete metadata. Do not use or modify it.

## Workflow

1. Search the complete effects tree for `copyprivate`, skip/ready flags, `omp single`, nested `omp parallel`, `parallel for`, `num_threads`, whole-frame `veejay_memcpy`, and helpers containing OpenMP pragmas.
2. Process at most 10 target files per request and build-check the batch.
3. On `continue`, resume from the remaining target list. If interrupted, report the exact remaining files.

## Safety Rules

- Never add nested `omp parallel`, `parallel for`, or `num_threads` to normal apply paths. Use orphaned `#pragma omp for schedule(static)`.
- Every worker must encounter worksharing constructs in the same order. Never guard `omp for`, `single`, `barrier`, or a helper containing them with a condition on which workers can disagree.
- Function locals are private per worker. Never set a local skip/result flag in `single` and branch on it outside. Compute argument-only conditions on every worker, or publish state-dependent decisions through shared effect state in `single` and its implicit barrier.
- Never return from `single`. Return only from team-uniform branches after required barriers.
- Keep shared counters, history heads, swaps, resets, allocation decisions, and map/LUT rebuilds in `single`. Use `nowait` only when later work has no dependency.
- Helpers containing worksharing must be called uniformly by the whole team, never from `single`. Apply this rule to motionmap and all callers.

## Copyprivate

Remove `copyprivate` by recomputing cheap values per worker, using existing shared effect state, or using a correct reduction plus barrier. Never merely delete the clause.

Retaining `single copyprivate(threshold)` in stateless `bwotsu.c` is acceptable: adding instance state solely to broadcast one Otsu threshold is not worthwhile.

## Frame Copies

In normal apply paths, parallelize complete frame-plane copies between `frame->data[]` and persistent FX buffers in either direction. Partition bytes or rows across the whole team; avoid a three-iteration plane loop that uses only three workers.

Keep `veejay_memcpy` for serial paths and small, partial, metadata, LUT, or map copies. Team-uniform full-copy endpoints such as `opacity == 255` must finish their parallel copies before returning.

## Validation

Compile touched files and inspect normal, bypass, full-copy, first-frame, reset, missing-history, and motionmap paths with 1, 2, and 8 workers. Confirm that no subset of the team can enter a worksharing construct alone.

Report changed files, deadlocks fixed, frame copies parallelized, `copyprivate` decisions, checks run, and remaining targets.
