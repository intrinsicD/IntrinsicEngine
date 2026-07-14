# GEOM-021 — MeshSoup module implementation split

## Goal
- Move non-trivial, non-template `Geometry.MeshSoup` implementations out of
  `src/geometry/Geometry.MeshSoup.cppm` into a matching
  `src/geometry/Geometry.MeshSoup.cpp` implementation unit, and clean up any
  includes/imports that become implementation-only.

## Non-goals
- No MeshSoup validation behavior changes.
- No public API or data-layout changes.
- No changes to `Geometry.Mesh.Conversion` or `Geometry.PointCloud.Conversion`
  except for compile fallout caused by the implementation split.
- No broad cross-layer `.cppm` cleanup in this task; open follow-up tasks for
  other owning layers.
- No attempt to move templates or template-dependent bodies into `.cpp`.

## Context
- Status: done (retired 2026-06-07; implementation split landed in `bfcd2751`).
- Owner/agent: Codex.
- Branch / PR: current branch / TBD.
- Next verification step: none; retired after a clean no-cache rebuild, explicit benchmark-smoke target build, and default CPU gate on 2026-06-07.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- `AGENTS.md` requires `.cppm` module interfaces to stay focused on exported
  types, declarations, small inline accessors, and templates that must be
  visible to importers. Non-trivial algorithm/control-flow bodies, container
  traversal, diagnostics assembly, and implementation-only imports belong in
  matching `.cpp` implementation units.
- `src/geometry/Geometry.MeshSoup.cppm` currently has no matching
  `src/geometry/Geometry.MeshSoup.cpp`; `src/geometry/CMakeLists.txt` lists only
  `Geometry.MeshSoup.cppm`.
- Retired [`GEOM-006`](GEOM-006-indexed-mesh-soup-conversion-contracts.md)
  established MeshSoup and already uses the intended pattern in downstream
  conversion modules: public `.cppm` surfaces plus non-trivial bodies in
  `Geometry.Mesh.Conversion.cpp` and `Geometry.PointCloud.Conversion.cpp`.
- Static audit on 2026-06-06 identified these high-confidence non-template
  `Geometry.MeshSoup.cppm` move candidates:
  `IndexedMesh` special members, mutating container APIs, validation result
  queries, diagnostic string conversion, validation helpers, and the exported
  `Validate` overloads.
- Same-pattern follow-up inventory outside this task includes
  `Geometry.Quadric`, `Geometry.RobustPredicates`, `Geometry.Grid`,
  `Geometry.OBB`, `Geometry.Overlap`, `Geometry.Support`,
  `Runtime.CameraControllers`, `Platform.Backend.Glfw`, `Core.ResourcePool`,
  `RHI.TextureUpload`, and other audited interfaces. Those should be split by
  owning subsystem in separate tasks.

## Required changes
- [x] Add `src/geometry/Geometry.MeshSoup.cpp` as the implementation unit for
      `module Geometry.MeshSoup;`.
- [x] Register `Geometry.MeshSoup.cpp` as a private source in
      `src/geometry/CMakeLists.txt`.
- [x] Replace non-template `IndexedMesh` bodies in
      `src/geometry/Geometry.MeshSoup.cppm` with declarations and define them in
      `Geometry.MeshSoup.cpp`: constructors, copy/move assignment,
      `AddVertex`, `AddFace`, `AddTriangle`, `Clear`, `BorrowView`, and
      `EnsureProperties`.
- [x] Replace non-template validation and diagnostic bodies with declarations
      and define them in `Geometry.MeshSoup.cpp`:
      `ValidationResult::HasErrors`, `ValidationResult::Count`,
      `BorrowView(const IndexedMesh&)`, `ToString(AttributeDomain)`,
      `ToString(ValidationDiagnosticKind)`, `IsValid`,
      `Validate(IndexedMeshView, const ValidationOptions&)`, and
      `Validate(const IndexedMesh&, const ValidationOptions&)`.
- [x] Move implementation-only helpers from the `.cppm` private namespace into
      `Geometry.MeshSoup.cpp`: `EdgeObservation`, `CornerCount`, `NearlySame`,
      `NewellNormal`, `ContainsDuplicateIndex`, `HasOnlyValidIndices`,
      `IsDegenerateFace`, `AppendDuplicateVertexDiagnostics`,
      `AppendFaceDiagnostics`, `FindEdgeObservation`,
      `AppendTopologyDiagnostics`, `AppendAttributeArityDiagnostic`, and
      `AppendAttributeDiagnostics`.
- [x] Remove `inline` from moved non-template exported declarations where it is
      no longer required by the interface.
- [x] Keep templates and genuinely tiny accessors in the `.cppm` interface when
      they must remain visible to importers.
- [x] Clean up `Geometry.MeshSoup.cppm` global-module-fragment includes so
      implementation-only headers such as `<algorithm>` and `<cmath>` move to
      `Geometry.MeshSoup.cpp` when no longer needed by declarations/templates.
- [x] Audit `Geometry.MeshSoup.cppm` imports after the move; keep public-surface
      imports such as `Geometry.Properties`, but move implementation-only imports
      to `Geometry.MeshSoup.cpp` if any appear.

## Tests
- [x] Existing `tests/unit/geometry/Test.MeshSoup.cpp` remains green.
- [x] Existing conversion coverage using MeshSoup remains green:
      `tests/unit/geometry/Test.MeshConversion.cpp` and
      `tests/unit/geometry/Test.PointCloudConversion.cpp`.
- [x] Add no new behavior tests unless the split exposes an untested contract
      seam or a bug.

## Docs
- [x] Update `docs/architecture/geometry.md` only if the public MeshSoup
      contract wording changes; the expected implementation split should not
      require an architecture-doc behavior update.
- [x] Regenerate `docs/api/generated/module_inventory.md` if the module surface
      or import inventory changes.
- [x] Update this task with completion notes before retiring it to
      `tasks/done/`.

## Acceptance criteria
- [x] `Geometry.MeshSoup.cppm` exposes declarations, data types, templates, and
      small inline accessors only; non-trivial validation/container logic lives
      in `Geometry.MeshSoup.cpp`.
- [x] `Geometry.MeshSoup.cpp` compiles as part of `IntrinsicGeometry` through
      the configured CMake preset.
- [x] `Geometry.MeshSoup.cppm` no longer includes headers needed only by moved
      implementations.
- [x] Existing MeshSoup, mesh-conversion, and point-cloud-conversion tests pass
      without changed expectations.
- [x] The change preserves `geometry -> core` layering and introduces no
      renderer/runtime/ECS/assets/platform/app dependencies.

## Progress notes
- 2026-06-07: Current implementation-split slice completed locally: moved non-trivial non-template bodies into matching `.cpp` implementation units, registered new private sources in CMake, and retained importer-visible templates, constexpr helpers, ABI structs, and small accessors in module interfaces where required.
- Focused target builds passed for the touched subsystem, and `docs/api/generated/module_inventory.md` was regenerated with `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- 2026-06-07 retirement verification: the previous rendergraph ASan blocker was reproduced as stale C++23 module layout state from ccache/incremental module artifacts, not a source defect in the implementation split. A `CCACHE_DISABLE=1` `IntrinsicTests` rebuild plus an explicit `IntrinsicBenchmarkSmoke` build restored a clean default CPU gate; final `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` passed 2816/2816.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'MeshSoup|MeshConversion|PointCloudConversion' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not change MeshSoup validation semantics, diagnostic kinds, severity
  policy, or attribute-domain cardinality rules.
- Do not move template functions or template-dependent helpers into `.cpp`.
- Do not mix this mechanical implementation split with geometry algorithm
  refactors.
- Do not add dependencies outside the allowed `geometry -> core` boundary.
- Do not broaden this task into the cross-tree `.cppm` cleanup inventory.
