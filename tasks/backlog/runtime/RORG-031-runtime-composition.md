# RORG-031C — Runtime composition backlog seed

## Goal
- Capture runtime composition-root and lifecycle backlog work as a structured task.

## Non-goals
- Runtime API redesign in this task.
- Subsystem behavior changes.

## Context
- Runtime ownership and lifecycle gates are critical to the reorganization but were previously embedded in a broad narrative backlog.
- [`RUNTIME-099`](../../done/RUNTIME-099-runtime-lifecycle-composition.md) is retired as the implementation child for the explicit lifecycle pipeline (`begin_frame`, input/UI, fixed-step simulation, extraction, prepare, execute, present/end, maintenance, shutdown).
- Related runtime legacy-feature children are [`RUNTIME-100`](../../done/RUNTIME-100-scene-manager-lifecycle.md), [`RUNTIME-101`](RUNTIME-101-asset-ingest-state-machine.md), [`RUNTIME-102`](../../done/RUNTIME-102-editor-command-history.md), [`RUNTIME-103`](RUNTIME-103-geometry-algorithm-execution-queue.md), and [`RUNTIME-104`](RUNTIME-104-derived-overlay-producer-lifecycle.md).
- Cross-layer feature-retirement map: [`LEGACY-011`](../architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md).

## Required changes
- [ ] Track composition-root ownership (`begin_frame`, extraction, prepare, execute, end) as explicit runtime backlog work.
- [ ] Track shutdown determinism and subsystem wiring responsibilities under runtime ownership.
- [x] Execute or retire [`RUNTIME-099`](../../done/RUNTIME-099-runtime-lifecycle-composition.md) before treating this seed as complete.
- [ ] Keep runtime child tasks synchronized with the legacy feature map in [`LEGACY-011`](../architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md).

## Tests
- [ ] Ensure the task file uses repository task template sections.

## Docs
- [ ] Keep runtime architecture docs synchronized when this backlog item is executed.

## Acceptance criteria
- [ ] Runtime backlog exists as a structured task file under `tasks/backlog/runtime/`.
- [ ] Runtime composition work has concrete child tasks rather than an unnamed narrative gap.

## Verification
```bash
test -f tasks/backlog/runtime/RORG-031-runtime-composition.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
