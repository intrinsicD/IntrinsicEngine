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
[`LEGACY-011`](../../tasks/done/LEGACY-011-src-legacy-feature-reimplementation-map.md),
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
candidate in the [`LEGACY-011`](../../tasks/done/LEGACY-011-src-legacy-feature-reimplementation-map.md)
triage has a resolved outcome. The child tasks that build or explicitly retire a
promoted replacement are done — `CORE-002`, `ASSETIO-002`, `ASSETIO-003`,
`ASSETIO-004`, `HARDEN-081`, `RUNTIME-099`, `RUNTIME-100`, `RUNTIME-101`,
`RUNTIME-102`, `RUNTIME-103`, `RUNTIME-104`, `PLATFORM-006`, `GRAPHICS-084`,
`GRAPHICS-084C`, `GRAPHICS-085`, `GRAPHICS-086`, and `UI-008`.
The retained/deferred outcomes in those tasks are decisions, not missing
features that gate any subtree deletion.

Consistent with `LEGACY-011`'s own acceptance criterion, the remaining
deletion blockers are therefore **purely mechanical consumer migration**, not
unnamed feature gaps.

## Per-subtree gate results

Consumer counts are distinct files matched outside the doomed subtree.

| Subtree (task) | Files | Legacy-internal consumers | External consumers | External breakdown |
|---|---|---|---|---|
| `Interface/` ([LEGACY-001](../../tasks/backlog/architecture/LEGACY-001-delete-src-legacy-interface.md)) | 4 | 6 | 0 | none |
| `Asset/` ([LEGACY-004](../../tasks/backlog/architecture/LEGACY-004-delete-src-legacy-asset.md)) | 6 | 50 | 10 | 10 tests, 0 promoted-src |
| `Core/` ([LEGACY-005](../../tasks/backlog/architecture/LEGACY-005-delete-src-legacy-core.md)) | 40 | 133 | 40 | 40 tests, 0 promoted-src |
| `ECS/` ([LEGACY-006](../../tasks/backlog/architecture/LEGACY-006-delete-src-legacy-ecs.md)) | 29 | 37 | 25 | 25 tests, 0 promoted-src |
| `Graphics/` ([LEGACY-008](../../tasks/backlog/architecture/LEGACY-008-delete-src-legacy-graphics.md)) | 168 | 22 | 39 | 39 tests, 0 promoted-src |
| `RHI/` ([LEGACY-009](../../tasks/backlog/architecture/LEGACY-009-delete-src-legacy-rhi.md)) | 54 | 83 | 18 | 18 tests, 0 promoted-src |
| `Runtime/` ([LEGACY-010](../../tasks/backlog/architecture/LEGACY-010-delete-src-legacy-runtime.md)) | 29 | 0 | 19 | 19 tests, 0 promoted-src |

"Files" includes each subtree's `CMakeLists.txt`.

### Reading the table

- **No subtree is removable in isolation today.** Every subtree still has either
  legacy-internal consumers (which gate deletion ordering) or external test
  consumers (which gate the grep).
- **Tests are the dominant blocker.** For the six subtrees that still have
  external consumers, *every* external consumer is a test file. These are
  exactly the consumers owned by
  [`LEGACY-012`](../../tasks/backlog/architecture/LEGACY-012-migrate-legacy-consumer-tests.md).
- **`Interface/` now has zero external test consumers.** `LEGACY-018` retired
  the legacy-only panel-registration test instead of migrating the unpromoted
  `Interface::GUI` API; `LEGACY-001` remains blocked only by six
  legacy-internal Graphics/Runtime consumers.
- **`Runtime/` has zero legacy-internal consumers** — nothing else in
  `src/legacy/` imports the doomed `Runtime.*` modules. Once its 19 test
  consumers migrate, `LEGACY-010` becomes a pure mechanical deletion.

## Promoted-engine-code blocker status

`LEGACY-013` cleared the only legacy modules that were still imported by
**promoted, non-test engine code**: legacy `Core.*` modules in 15 promoted
geometry/runtime files:

- `src/geometry/` — 14 files import `Core.Memory`, `Core.Logging`, `Core.Error`,
  `Core.Handle`, and `Core.ResourcePool`.
- `src/runtime/Runtime.AssetGeometryIO.cpp` — imports `Core.Error`.

Re-runnable gate (must print nothing when clear):

```bash
git grep -nE '^\s*(export\s+)?import\s+(Core|ECS|Graphics|RHI|Runtime|Asset|Interface)\b' \
    -- 'src/**' ':!src/legacy/**' | grep -vE 'import\s+Extrinsic'
```

Promoted replacements are now in use for all of these:
`Extrinsic.Core.Error`, `Extrinsic.Core.Logging`, `Extrinsic.Core.Memory`,
`Extrinsic.Core.ResourcePool`, and `Extrinsic.Core.StrongHandle`. Note that
legacy `Core.Handle` (source of `Core::StrongHandle`) maps to the promoted
`Extrinsic.Core.StrongHandle` rather than a same-named module, so this migration
was a small semantic change, not a pure module rename. `src/geometry/CMakeLists.txt`
also no longer links `IntrinsicCore`.

The same checkpoint migrated four geometry tests directly coupled to those
promoted geometry API surfaces (`Test_GJK.cpp`, `Test_ContactManifold.cpp`,
`Test_RuntimeGeometry.cpp`, and `Test.GeometryIO.cpp`) from legacy `Core.*`
types to promoted `Extrinsic.Core.*` types, reducing the remaining Core test
consumer set for `LEGACY-012` from 48 files to 44 files.
`LEGACY-014` then removed an unused bare `Core` import from
`Test_RuntimeGraph.cpp`, reducing the remaining Core test-consumer set to 43
files.
`LEGACY-015` migrated the CoreError unit test to promoted
`Extrinsic.Core.Error` and renamed it to `Test.CoreError.cpp`, reducing the set
to 42 files.
`LEGACY-016` migrated the LogRingBuffer unit test to promoted
`Extrinsic.Core.Logging` and renamed it to `Test.LogRingBuffer.cpp`, reducing
the set to 41 files.
`LEGACY-017` retired the duplicate legacy CoreHash test in favor of the
existing promoted `Extrinsic.Core.Hash` coverage and renamed that promoted test
to `Test.CoreHash.cpp`, reducing the set to 40 files.
`LEGACY-018` retired the only external `Interface` test consumer,
`tests/contract/ui/Test_PanelRegistration.cpp`; current promoted UI behavior is
covered by `Extrinsic.Runtime.SandboxEditorUi` contract tests and the
app-to-runtime dependency proof rather than by a promoted `Interface::GUI`
registration endpoint.

**The Core migration sequence above clears only the promoted-src subset of the
`LEGACY-005` gate.** The
`LEGACY-005` consumer-grep searches every consumer of legacy `Core.*` outside
`src/legacy/Core/**`, which the table above now counts as 133 legacy-internal +
40 test files. `LEGACY-005` stays blocked by its 40 test consumers
(`LEGACY-012`) and by all 133 legacy-internal consumers until the subtrees above
Core have been deleted. This is why the `LEGACY-005` row in
`legacy-retirement.md` says Core "retires last".

## Legacy-internal deletion order

Each subtree's gate fails while *any* consumer remains outside the doomed
subtree, including consumers in other legacy subtrees. So pure deletion ordering
can only run consumers-before-producers. The legacy-internal import graph (count
of consuming files per importing subtree) is:

| Legacy module | Imported by (legacy-internal) |
|---|---|
| `Runtime.*` | — (nothing) |
| `Graphics` | Runtime |
| `Interface` | Graphics, Runtime |
| `ECS` | Graphics, Runtime |
| `Asset.*` | Graphics, Runtime |
| `RHI` | Asset, Graphics, Interface, Runtime |
| `Core` | Asset, ECS, Graphics, Interface, RHI, Runtime |

The only valid legacy-internal order is therefore **Runtime first, then
Graphics, then Interface/ECS/Asset, then RHI, then Core last** — not the
leaves-named-first reading. In particular `Interface` (`LEGACY-001`) and
`Graphics` (`LEGACY-008`) cannot be deleted before `Runtime` (`LEGACY-010`):
legacy Runtime still imports both, and the `LEGACY-001`/`LEGACY-008` gates search
outside their own subtree, so those deletions would fail the gate (or break the
remaining legacy build). Either delete Runtime/Graphics first, or migrate/fence
their legacy-internal imports before the upstream subtrees are removed.

## What can safely be removed, and in what order

Removal is gated by consumer migration only. The safe path:

1. **Done by `LEGACY-013`: migrate the 15 promoted-src files off legacy
   `Core.*`** to the `Extrinsic.Core.*` modules above. This cleared only the
   promoted-src subset of the `LEGACY-005` gate (see above) and did not, on its
   own, unblock `LEGACY-005`.
2. **Migrate or retire the remaining test consumers**
   ([`LEGACY-012`](../../tasks/backlog/architecture/LEGACY-012-migrate-legacy-consumer-tests.md)).
   This is required for every subtree that still has external consumers,
   including `Runtime` — even the subtree with zero legacy-internal consumers
   still has 19 test consumers, so no gate exits clean until its
   `LEGACY-012`-owned tests migrate or retire. `Interface/` already has zero
   external test consumers after `LEGACY-018`; it remains blocked by six
   legacy-internal Graphics/Runtime consumers until subtree ordering removes or
   fences them.
3. **Delete the subtrees consumers-first**, following the legacy-internal order
   above: `Runtime` (`LEGACY-010`) → `Graphics` (`LEGACY-008`) →
   `Interface`/`ECS`/`Asset` (`LEGACY-001`/`LEGACY-006`/`LEGACY-004`) → `RHI`
   (`LEGACY-009`) → `Core` (`LEGACY-005`, foundation, last). Each subtree's gate
   exits clean only once the subtrees that import it have been deleted and its
   test consumers have migrated.

In short: **all of `src/legacy/` is reimplemented or value-gated and is
slated for removal** — the work that remains is consumer migration
(`LEGACY-012` for tests) plus mechanical deletion in the Runtime-first →
Core-last order above.
