# HARDEN-070 — Drop dead null guards on reference-initialised helpers

## Goal
- Remove the ~8 defensive `m_X == nullptr` early-return guards in three helper
  modules whose non-owning pointers are initialised from constructor reference
  parameters and can therefore never be null in well-formed code, replacing
  the implicit "may be null" contract with an explicit lifetime-contract note
  in each helper's header. (Originally ~7; the
  [2026-05-28 audit](../../../docs/reports/2026-05-28-agent-output-audit.md)
  Row 5 added the fourth spatial-debug adapter, `ConvexHullAdapter`, which
  landed after this task was filed carrying the identical pattern.)

## Non-goals
- Do not change the public API of any of the three helpers (no signature
  changes, no member renames, no header reorganization).
- Do not remove the other early-return guards in the same methods (empty-span,
  `IsOperational()`, zero-endpoint, cap-overflow). Those check real call-site
  inputs, not internal invariants.
- Do not delete or re-export the `I*` virtual interfaces — Slice D in
  GRAPHICS-077 / GRAPHICS-078 still plans to add the Vulkan-tuned second
  implementation against each interface, and `ISpatialDebugAdapter` now has
  four concretes (`BvhAdapter`, `KdTreeAdapter`, `OctreeAdapter`,
  `ConvexHullAdapter`).
- Do not migrate other helpers/adapters with similar patterns in `src/`. The
  scope is strictly the three modules introduced in the 2026-05-18 → 2026-05-26
  audit window, plus the `ConvexHullAdapter::Append` guard in the *same*
  `Runtime.SpatialDebugAdapters` module that landed in the
  2026-05-26 → 2026-05-28 window (RUNTIME-082 Slice C, `2697f86`). A
  same-file sibling adapter is in scope; unrelated helpers elsewhere in
  `src/` are not.

## Context
- Owning subsystem/layer: `runtime` (`src/runtime/SpatialDebug/`) and
  `graphics/renderer` (`src/graphics/renderer/`). Cross-layer but each
  layer's edit is independent.
- Source of the finding:
  [`docs/reports/2026-05-26-agent-output-audit.md`](../../../docs/reports/2026-05-26-agent-output-audit.md)
  Row 5 (defensive validation at internal boundaries). The pattern repeats
  across the three new modules introduced this window, so the audit recorded
  it as a single hygiene-cleanup task rather than three per-module cleanups.
- The constructors take `const Geometry::BVH&` / `const Geometry::KDTree&` /
  `const Geometry::Octree&` (adapters) and `RHI::IDevice& + RHI::BufferManager&`
  (upload helpers). The members are non-owning pointers initialised from
  `&parameter`. The adapter modules additionally `delete` the rvalue overload
  so a temporary cannot be bound. There is no default constructor, no move,
  no resetter — the pointers are non-null for the lifetime of the helper.
- The Row-5 finding cites these exact lines:
  - `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cpp:61` (`BvhAdapter::Append`)
  - `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cpp:163` (`KdTreeAdapter::Append`)
  - `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cpp:259` (`OctreeAdapter::Append`)
  - `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cpp:391` (`ConvexHullAdapter::Append` — `m_Hull == nullptr`; added by the 2026-05-28 audit)
  - `src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp:173` (`UploadTriangles`)
  - `src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp:229` (`UploadLines`)
  - `src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp:289` (`UploadPoints`)
  - `src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cpp:176` (`UploadVectorFields`)
  - `src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cpp:274` (`UploadIsolines`)
- Existing test coverage: `tests/contract/runtime/Test.SpatialDebugAdapters.cpp`
  (10 cases), `tests/contract/graphics/Test.TransientDebugSurfacePass.cpp`
  (16 cases), `tests/contract/graphics/Test.VisualizationOverlayPass.cpp`
  (14 cases). None exercise the null branch (because the precondition
  prevents reaching it).

## Required changes
- [ ] In `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cpp`, remove
      the `if (m_X == nullptr) return;` early-return in each of `BvhAdapter::Append`,
      `KdTreeAdapter::Append`, `OctreeAdapter::Append`, and
      `ConvexHullAdapter::Append` (the `m_Hull == nullptr` guard). Leave the
      `if (nodes.empty()) return;` / `if (vertices.empty()) return;` guards
      untouched — those check the source's observable state, not an internal
      invariant.
- [ ] In `src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cppm`, add a
      one-line lifetime-contract note to each adapter class (or a single
      shared note above the first adapter) saying: "Constructed from a const
      reference to the source tree; the rvalue overload is deleted so the
      non-owning pointer is non-null for the adapter's lifetime."
- [ ] In `src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp`,
      remove the `m_Device == nullptr || m_BufferManager == nullptr` clauses
      from the three `Upload*` early-return conditions in `UploadTriangles`,
      `UploadLines`, `UploadPoints`. Keep the `triangles.empty() / lines.empty()
      / points.empty() || !m_Device->IsOperational()` clauses untouched.
- [ ] In `src/graphics/renderer/Graphics.TransientDebugUploadHelper.cppm`,
      add a one-line lifetime-contract note to the
      `TransientDebugUploadHelper` class saying: "Constructed from
      `RHI::IDevice& + RHI::BufferManager&`; the device and manager
      pointers are non-null for the helper's lifetime (the renderer owns
      both and resets the helper before the manager in `Shutdown()`)."
- [ ] In `src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cpp`,
      remove the `m_Device == nullptr || m_BufferManager == nullptr` clauses
      from the two `Upload*` early-return conditions in `UploadVectorFields`
      and `UploadIsolines`.
- [ ] In `src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cppm`,
      add the same one-line lifetime-contract note to the
      `VisualizationOverlayUploadHelper` class.
- [ ] No changes to the matching test files. The existing contract tests
      cover the surviving guards (empty input, non-operational device,
      cap-overflow) and continue to pass without the dead branches.

## Tests
- [ ] No new tests. Removing the dead branch does not create a new failure
      mode; the precondition (`m_X != nullptr` from a reference-binding
      constructor) is enforced by the type system.
- [ ] `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine'`
      passes; specifically the three suites above must remain green.

## Docs
- [ ] No docs changes needed. The lifetime-contract notes added to the
      `.cppm` headers are the documentation. `src/runtime/SpatialDebug/README.md`
      and `src/graphics/renderer/README.md` already describe the renderer-
      owned lifetime; no edits required there.
- [ ] Cross-link this task from the
      [2026-05-26 audit](../../../docs/reports/2026-05-26-agent-output-audit.md)
      Row 5 follow-up note when this task lands (replace the `(planned)`
      task-seed mention with the actual task ID).

## Acceptance criteria
- [ ] No `m_(Bvh|KdTree|Octree|Hull|Device|BufferManager) == nullptr` check
      remains in the three touched `.cpp` files.
- [ ] Each touched `.cppm` carries a one-line lifetime-contract note on
      the affected class (or a single shared note covering all classes
      in `SpatialDebugAdapters.cppm`).
- [ ] All three suites (`Test.SpatialDebugAdapters.cpp`,
      `Test.TransientDebugSurfacePass.cpp`,
      `Test.VisualizationOverlayPass.cpp`) stay green at their current
      test count (10 / 16 / 14 cases).
- [ ] `python3 tools/repo/check_layering.py --root src --strict` passes.
- [ ] Diff for the task is restricted to the six files named above plus
      this task file's retire move to `tasks/done/`.

## Verification
```bash
# Focused build/test before broadening.
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
    -R 'SpatialDebugAdapters|TransientDebugSurfacePass|VisualizationOverlayPass' \
    --timeout 60

# Full CPU/null gate.
ctest --test-dir build/ci --output-on-failure \
    -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Structural checks.
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict

# Sanity: confirm the dead-guard pattern is gone from the three modules.
! grep -nE 'm_(Bvh|KdTree|Octree|Hull|Device|BufferManager)\s*==\s*nullptr' \
    src/runtime/SpatialDebug/Runtime.SpatialDebugAdapters.cpp \
    src/graphics/renderer/Graphics.TransientDebugUploadHelper.cpp \
    src/graphics/renderer/Graphics.VisualizationOverlayUploadHelper.cpp
```

## Forbidden changes
- Removing any of the *other* early-return guards in the touched methods
  (empty-span, `IsOperational()`, cap-overflow). Those check real call-site
  inputs, not internal invariants.
- Changing the constructor signatures, adding default constructors, or
  adding null-pointer reset paths. The whole point of the cleanup is that
  the reference-binding constructor already guarantees the precondition.
- Migrating the same pattern in helpers/adapters in *other* `src/` modules.
  Out of scope for this task; if reviewers find the pattern repeats in an
  unrelated module, open a successor task rather than expanding this one. (The
  fourth adapter `ConvexHullAdapter::Append` is explicitly in scope because it
  lives in the same `Runtime.SpatialDebugAdapters` module this task already
  edits — see Context.)
- Mixing this hygiene cleanup with any feature work or any other audit-row
  finding.

## Maturity
- Target: `Retired` (pure hygiene cleanup; no maturity progression on the
  underlying helpers — they remain at `CPUContracted`).
- Single-slice task. Closes when the dead guards are gone and the gate is
  green.
