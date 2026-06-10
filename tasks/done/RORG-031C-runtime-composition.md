---
id: RORG-031C
theme: A
depends_on: []
---
# RORG-031C — Runtime composition backlog seed

## Goal
- Capture runtime composition-root and lifecycle backlog work as a structured task.

## Non-goals
- Runtime API redesign in this task.
- Subsystem behavior changes.

## Context
- Runtime ownership and lifecycle gates are critical to the reorganization but were previously embedded in a broad narrative backlog.
- [`RUNTIME-099`](RUNTIME-099-runtime-lifecycle-composition.md) is retired as the implementation child for the explicit lifecycle pipeline (`begin_frame`, input/UI, fixed-step simulation, extraction, prepare, execute, present/end, maintenance, shutdown).
- Related runtime legacy-feature children are [`RUNTIME-100`](RUNTIME-100-scene-manager-lifecycle.md), [`RUNTIME-101`](../backlog/runtime/RUNTIME-101-asset-ingest-state-machine.md), [`RUNTIME-102`](RUNTIME-102-editor-command-history.md), [`RUNTIME-103`](../backlog/runtime/RUNTIME-103-geometry-algorithm-execution-queue.md), and [`RUNTIME-104`](../backlog/runtime/RUNTIME-104-derived-overlay-producer-lifecycle.md).
- Cross-layer feature-retirement map: [`LEGACY-011`](../backlog/architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md).
- Seed complete (2026-06-10): composition-root ownership and shutdown determinism landed and retired via `RUNTIME-099` (lifecycle pipeline, `CPUContracted`) with `RUNTIME-100`/`RUNTIME-102` retired alongside; the remaining runtime children (`RUNTIME-101`, `RUNTIME-103`, `RUNTIME-104`) are independently tracked backlog tasks synchronized with the `LEGACY-011` feature map rows and child list. The seed's job — replacing the unnamed narrative gap with concrete child tasks — is done.
- Completed: 2026-06-10.
- PR/commit: branch `claude/pensive-albattani-pu2t14` (pending local commit).

## Required changes
- [x] Track composition-root ownership (`begin_frame`, extraction, prepare, execute, end) as explicit runtime backlog work.
- [x] Track shutdown determinism and subsystem wiring responsibilities under runtime ownership.
- [x] Execute or retire [`RUNTIME-099`](RUNTIME-099-runtime-lifecycle-composition.md) before treating this seed as complete.
- [x] Keep runtime child tasks synchronized with the legacy feature map in [`LEGACY-011`](../backlog/architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md).

## Tests
- [x] Ensure the task file uses repository task template sections.

## Docs
- [x] Keep runtime architecture docs synchronized when this backlog item is executed.

## Acceptance criteria
- [x] Runtime backlog exists as a structured task file under `tasks/backlog/runtime/`.
- [x] Runtime composition work has concrete child tasks rather than an unnamed narrative gap.

## Verification
```bash
test -f tasks/backlog/runtime/RORG-031-runtime-composition.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
