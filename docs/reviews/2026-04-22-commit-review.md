# Commit Review — 2026-04-22

Scope: commits `3bcd430c962f28fd64b0a2943a30ada7b19a9b6e` and `019d0c806fe1ec8269eaf6663ad6389d9fc74c05`.

## Executive Summary

- **Overall trajectory is positive** for modularity (component decomposition, subsystem wiring, improved test organization), but there are **two high-risk issues** that should be fixed before merge:
  1. **Potential build break** from inconsistent CMake target names in `src_new/ECS/Components/CMakeLists.txt`.
  2. **Silent failure paths** in runtime graph execution (compile/plan/execute failures are dropped without telemetry or error propagation).
- Test changes in `3bcd430` meaningfully improve structure and local coverage of core systems, but there are still gaps around scheduler-driven parallelism and streaming error behavior.

## Commit-by-Commit Assessment

### 1) `3bcd430` — Core/ECS/Tests

#### Modularity
- ✅ Good: `TaskGraph` hash token computation was centralized into `Detail::TypeTokenValue<T>()`, reducing duplicate logic and improving maintainability.
- ✅ Good: tests were moved under `tests/Core/` and hooked into `tests/Core/CMakeLists.txt`, improving test locality and discoverability.

#### Clarity / Explicitness
- ⚠️ Minor concern: interface comments claim `ExecutePass()` "asserts" on invalid index, but implementation currently does a silent bounds check and no assert. This creates contract drift and can hide misuse.

#### Performance
- ✅ Good: hazard tracking logic moved to per-resource ordered access sequences; this is clearer and less error-prone than maintaining separate reader/writer maps.
- ⚠️ Risk to monitor: hazard edge construction remains O(k²) per resource for k accesses. This is acceptable for modest pass counts, but could become heavy with large transient graphs.

#### Usability
- ✅ Good: expanded `TaskGraph` tests improve confidence in compile/build-plan ordering behavior and reset semantics.
- ⚠️ Coverage gap: tests still don't stress error/diagnostic paths for out-of-range single-pass execution APIs.

### 2) `019d0c8` — ECS/Graphics schema expansion + runtime composition root

#### Modularity
- ✅ Strong improvement: splitting Transform into Local/World and moving GPU-facing concepts into `Graphics.Component.*` aligns boundaries with ownership domains.
- ✅ Strong improvement: runtime composition root now explicitly wires scheduler, frame graph, streaming graph, asset service, and scene registry in clear dependency order.

#### Usability
- ⚠️ **High severity**: `TickStreamingGraph()` and fixed-step frame-graph execution both suppress failures (`Compile`, `BuildPlan`, `Execute`) without emitting logs or surfacing errors. This makes runtime faults hard to diagnose.

#### Performance
- ✅ Directionally good: introduction of streaming graph dispatch and explicit maintenance lane creates a path for async throughput.
- ⚠️ Potential throughput hazard: all streaming passes are dispatched every frame with no visible backpressure/queue depth policy; if producers outrun workers, latency and memory pressure may grow.

#### Explicitness / Clarity
- ⚠️ Documentation reduced sharply in `src_new/Core/README.md`; while a "scorched-earth" policy can reduce stale docs, this removes architecture context that is currently still in flux.

## High-Priority Findings

### [H1] Inconsistent CMake target names likely break linking
- File: `src_new/ECS/Components/CMakeLists.txt`
- Observation: links against `IntrinsicCore`, `IntrinsicAsset`, `IntrinsicGeometry`, while neighboring modules use `Extrinsic*` naming.
- Impact: configuration/build failure or unresolved target errors depending on CMake graph.
- Recommendation: normalize to existing `Extrinsic*` targets (and verify exact asset/geometry target spellings).

### [H2] Silent runtime graph failures reduce debuggability and operational safety
- File: `src_new/Runtime/Runtime.Engine.cpp`
- Observation: graph `Compile()/BuildPlan()/Execute()` failures are ignored or dropped silently.
- Impact: frame work may be skipped with no signal, causing non-deterministic behavior and hard-to-diagnose stalls.
- Recommendation: emit structured logs and/or return error state to app-level hooks; at minimum, add counters/telemetry for dropped frames/tasks.

## Medium / Low Findings

### [M1] Contract mismatch in `ExecutePass()` documentation
- Files: `src_new/Core/Core.Dag.TaskGraph.cppm`, `src_new/Core/Core.Dag.TaskGraph.cpp`
- Observation: doc says "asserts in range"; code silently returns for out-of-range.
- Recommendation: either add assert or change docs to "no-op on invalid index".

### [M2] Parallelism claim does not match current implementation
- File: `src_new/Core/Core.Dag.TaskGraph.cpp`
- Observation: comments mention parallel layer dispatch, but code still executes sequentially with TODO.
- Recommendation: adjust comments to current behavior or prioritize scheduler integration to match intended performance profile.

## Software Metrics Snapshot (qualitative)

- **Churn:** High for `019d0c8` (large schema and runtime changes); moderate for `3bcd430` with substantial test migration.
- **Cohesion:** Improved in ECS/Graphics ownership boundaries.
- **Coupling:** Reduced conceptually (better domain split), but runtime composition root now has broader direct dependencies that should be mediated via narrower interfaces over time.
- **Testability:** Improved in Core domain via dedicated test modules; runtime streaming failure paths still weakly testable due to silent handling.
- **Operational observability:** Regressed/insufficient in new async graph paths due to missing logs/counters.

## Recommended Follow-up Plan

1. Fix CMake target names in ECS components and run clean configure.
2. Add explicit error reporting path for `FrameGraph` and streaming graph failures.
3. Align `ExecutePass` docs/behavior and add unit tests for invalid index behavior.
4. Add telemetry counters:
   - graph compile failures,
   - dropped passes,
   - streaming queue depth / dispatch count per frame.
5. Add stress tests for large pass counts and high-contention resource hazards.
