# Legacy Removal Audit — `src/legacy/`

**Audit date:** 2026-06-18
**Scope:** Which parts of `src/legacy/` can safely be removed today — i.e. whose
features are already reimplemented or replaced by a promoted equivalent, and
whose deletion is now blocked only by mechanical consumer migration rather than
by an unbuilt feature.

This is a current-state snapshot. It does not delete any source and does not
change the per-subtree deletion gates; it reports what each gate currently says
and partitions the remaining blockers. The authoritative program lives in
[`legacy-retirement.md`](legacy-retirement.md), the value-gated feature map in
[`LEGACY-011`](../../tasks/backlog/architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md),
and the per-subtree executing tasks under `tasks/backlog/architecture/LEGACY-*.md`.

## Method

For each `LEGACY-*` deletion task, the audit ran that task's own consumer-grep
gate (from its Verification block) and then partitioned every matching consumer
into two classes:

- **Legacy-internal** — the consumer lives in another `src/legacy/<Subtree>/`
  that is itself doomed. These do not require feature work; they retire by
  deletion ordering (leaves first, foundation last).
- **External** — the consumer lives in a promoted final layer (`src/<layer>/`,
  non-legacy) or in `tests/`. These must migrate or retire before the gate can
  exit clean.

The gate definitions are reproduced verbatim from the task files and re-runnable
with `git grep`.

## Feature reimplementation status (LEGACY-011)

At the **feature** level there is no remaining blocker: every legacy feature
candidate in the [`LEGACY-011`](../../tasks/backlog/architecture/LEGACY-011-src-legacy-feature-reimplementation-map.md)
triage has a resolved outcome. The child tasks that build or explicitly retire a
promoted replacement are done — `CORE-002`, `ASSETIO-002`, `ASSETIO-003`,
`ASSETIO-004`, `HARDEN-081`, `RUNTIME-099`, `RUNTIME-100`, `RUNTIME-101`,
`RUNTIME-103`, `GRAPHICS-084`, `GRAPHICS-084C`, `GRAPHICS-085`, `GRAPHICS-086`.
The still-open child tasks (`PLATFORM-006`, `RUNTIME-102`, `RUNTIME-104`,
`UI-008`) are "retain current behavior / defer" decisions, not missing features
that gate any subtree deletion.

Consistent with `LEGACY-011`'s own acceptance criterion, the remaining
deletion blockers are therefore **purely mechanical consumer migration**, not
unnamed feature gaps.

## Per-subtree gate results

Consumer counts are distinct files matched outside the doomed subtree.

| Subtree (task) | Files | Legacy-internal consumers | External consumers | External breakdown |
|---|---|---|---|---|
| `Interface/` ([LEGACY-001](../../tasks/backlog/architecture/LEGACY-001-delete-src-legacy-interface.md)) | 4 | 6 | 1 | 1 test (`tests/contract/ui/Test_PanelRegistration.cpp`) |
| `Asset/` ([LEGACY-004](../../tasks/backlog/architecture/LEGACY-004-delete-src-legacy-asset.md)) | 6 | 50 | 10 | 10 tests, 0 promoted-src |
| `Core/` ([LEGACY-005](../../tasks/backlog/architecture/LEGACY-005-delete-src-legacy-core.md)) | 40 | 133 | 63 | **15 promoted-src** + 48 tests |
| `ECS/` ([LEGACY-006](../../tasks/backlog/architecture/LEGACY-006-delete-src-legacy-ecs.md)) | 29 | 37 | 25 | 25 tests, 0 promoted-src |
| `Graphics/` ([LEGACY-008](../../tasks/backlog/architecture/LEGACY-008-delete-src-legacy-graphics.md)) | 168 | 22 | 39 | 39 tests, 0 promoted-src |
| `RHI/` ([LEGACY-009](../../tasks/backlog/architecture/LEGACY-009-delete-src-legacy-rhi.md)) | 54 | 83 | 18 | 18 tests, 0 promoted-src |
| `Runtime/` ([LEGACY-010](../../tasks/backlog/architecture/LEGACY-010-delete-src-legacy-runtime.md)) | 29 | 0 | 19 | 19 tests, 0 promoted-src |

"Files" includes each subtree's `CMakeLists.txt`.

### Reading the table

- **No subtree is removable in isolation today.** Every subtree still has either
  legacy-internal consumers (which gate deletion ordering) or external test
  consumers (which gate the grep).
- **Tests are the dominant blocker.** For six of the seven subtrees, *every*
  external consumer is a test file. These are exactly the consumers owned by
  [`LEGACY-012`](../../tasks/backlog/architecture/LEGACY-012-migrate-legacy-consumer-tests.md).
- **`Runtime/` has zero legacy-internal consumers** — nothing else in
  `src/legacy/` imports the doomed `Runtime.*` modules. Once its 19 test
  consumers migrate, `LEGACY-010` becomes a pure mechanical deletion.

## The single promoted-engine-code blocker

The only legacy modules still imported by **promoted, non-test engine code** are
legacy `Core.*` modules, imported by 15 files:

- `src/geometry/` — 14 files import `Core.Memory`, `Core.Logging`, `Core.Error`,
  `Core.Handle`, and `Core.ResourcePool`.
- `src/runtime/Runtime.AssetGeometryIO.cpp` — imports `Core.Error`.

Re-runnable gate (must print nothing when clear):

```bash
git grep -nE '^\s*(export\s+)?import\s+(Core|ECS|Graphics|RHI|Runtime|Asset|Interface)\b' \
    -- 'src/**' ':!src/legacy/**' | grep -vE 'import\s+Extrinsic'
```

Promoted replacements already exist for all of these:
`Extrinsic.Core.Error`, `Extrinsic.Core.Logging`, `Extrinsic.Core.Memory`,
`Extrinsic.Core.ResourcePool`, and `Extrinsic.Core.StrongHandle`. Note that
legacy `Core.Handle` (source of `Core::StrongHandle`) maps to the promoted
`Extrinsic.Core.StrongHandle` rather than a same-named module, so this migration
is a small semantic change, not a pure module rename. It is the prerequisite the
`LEGACY-005` row in `legacy-retirement.md` already calls out ("`src/legacy/Core/`
… still imported by promoted `geometry`/`runtime`, so it retires last").

## What can safely be removed, and in what order

Removal is gated by consumer migration only; the deletion order is unchanged
(consumer-leaves first, foundation last). The shortest safe path:

1. **Migrate the 15 promoted-src files off legacy `Core.*`** to the
   `Extrinsic.Core.*` modules above. This is the only blocker that touches
   promoted engine code and it unblocks `LEGACY-005`. (Owned by the
   `LEGACY-005` prerequisite / a focused geometry-core migration slice.)
2. **Migrate or retire the test consumers** for every subtree
   ([`LEGACY-012`](../../tasks/backlog/architecture/LEGACY-012-migrate-legacy-consumer-tests.md)).
   After this, the external consumer count for `Interface/`, `Asset/`, `ECS/`,
   `Graphics/`, `RHI/`, and `Runtime/` drops to zero.
3. **Delete the subtrees leaves-first** as the individual mechanical commits
   already specified by `LEGACY-001`, `LEGACY-008`, `LEGACY-010` (leaves and
   high-level consumers), then `LEGACY-004`, `LEGACY-006`, `LEGACY-009`, and
   finally `LEGACY-005` (foundation). Each subtree's gate exits clean once its
   legacy-internal consumers above it have been deleted and its external
   consumers have migrated.

In short: **all of `src/legacy/` is reimplemented or value-gated and is
slated for removal** — the work that remains is consumer migration (one small
geometry/runtime Core migration plus the `LEGACY-012` test sweep), after which
the entire tree deletes mechanically in the documented order.
