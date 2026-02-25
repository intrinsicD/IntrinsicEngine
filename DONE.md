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
