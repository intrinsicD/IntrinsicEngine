# IntrinsicEngine — Architecture Analysis History

This document is reserved for historical architecture notes and completed-item narratives that should not live in the active backlog.

Current status:
- Historical DONE narratives have been consolidated into `ROADMAP.md` where they remain available for reference.
- Future completed initiatives should be summarized here (or release notes) once the corresponding backlog item is closed.

## 2026-02-26 Backlog Governance Completion

Completed governance cleanups moved out of the active backlog:

- Tightened backlog governance by moving feature roadmap and phase-planning content from `TODO.md` into `ROADMAP.md`.
- Resolved the policy violation where the active backlog mixed open actions with DONE narratives; `TODO.md` now remains open-actions-only while historical/completion notes live in `DONE.md`.

## 2026-02-26 Offline Dependency Configure Mode

Completed the CMake dependency-offline backlog item:

- Added `INTRINSIC_OFFLINE_DEPS` option in the top-level configure path.
- Added FetchContent offline enforcement in `cmake/Dependencies.cmake` via `FETCHCONTENT_FULLY_DISCONNECTED=ON`.
- Added per-dependency local source validation (`external/cache/<dep>-src`) with clear fatal diagnostics when sources are missing.
- Documented offline configure invocation in `README.md`.

## 2026-02-26 CMake Release Flag Consolidation

Completed the code-quality backlog item for release flag drift:

- Removed the duplicate top-level `INTRINSIC_RELEASE_FLAGS` assignment in `CMakeLists.txt` that previously shadowed an earlier value.
- Kept a single source of truth for release optimization flags (`-O3` for `Release`/`RelWithDebInfo`).
- Preserved `-march=native` as an explicit opt-in controlled only by `INTRINSIC_ENABLE_NATIVE_ARCH`.

## 2026-02-26 RHI TransientAllocator Ownership Cleanup

Completed the RHI ownership cleanup backlog item for manual allocator lifetime management:

- `RHI::VulkanDevice` no longer stores `TransientAllocator` as an opaque raw pointer.
- Ownership was migrated to `std::unique_ptr<TransientAllocator>` in the module interface.
- Construction now uses `std::make_unique`, and teardown uses `.reset()` instead of manual `delete`.
- This removes manual memory management from the device path while preserving existing destruction ordering.

## 2026-02-26 DAGScheduler Compile-Path Lookup + Dedupe Optimization

Completed the DAGScheduler compile-path optimization backlog items:

- Replaced linear resource-state lookup with an O(1)-average hash index (`resourceKey -> stateIndex`) while preserving frame-reset semantics and high-water reuse for pooled storage.
- Added adaptive edge dedupe for high-degree producer nodes: linear scan for small out-degree, then automatic promotion to a hash-backed membership set for faster duplicate detection.
- Preserved existing scheduling semantics and execution-layer output while reducing per-frame compile overhead growth for large resource/node counts.
