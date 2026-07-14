---
id: GEOIO-003
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---

# GEOIO-003 — Mesh and point-cloud IO breadth — OFF writer and point-cloud readers

## Goal

- [x] Broaden geometry IO format coverage by adding an OFF writer to `Geometry.HalfedgeMesh.IO` so OFF is no longer read-only, round-tripping cleanly with the existing OFF reader (`LoadOFF`).
- [x] Add additional ASCII point-cloud readers to `Geometry.PointCloud.IO` for `.pts`, `.pwn`, `.csv`, `.3d`, and `.txt`, reusing the existing hardened ASCII parsing path.
- [x] Preserve IntrinsicEngine's stronger input hardening (deterministic, fail-closed on malformed/empty/non-finite input with explicit diagnostics) across all new code paths.

## Non-goals

- No new binary formats (no binary OFF, no binary point-cloud variants beyond the existing PLY/PCD binary writers).
- No GPU backend and no renderer/runtime/ECS/assets/app integration.
- No UI, editor, or visualization surface for the new IO.
- No changes to the existing PLY/PCD/XYZ read or write paths (the IntrinsicEngine writers there are already better than the upstream `bcg` reference and stay as-is).
- No new geometry algorithms beyond serialization/parsing.

## Context

- IntrinsicEngine already has working ASCII and binary PLY/PCD/XYZ point-cloud IO and OBJ/OFF/PLY/STL mesh IO, but the OFF format in `Geometry.HalfedgeMesh.IO` is read-only (`LoadOFF` exists; there is no `WriteOFF`), and the point-cloud reader breadth in `Geometry.PointCloud.IO` is narrower than the upstream `bcg` reference.
- The mesh IO module surface is `export namespace Geometry::MeshIO` in `src/geometry/Geometry.HalfedgeMesh.IO.cppm`, with `Expected<MeshIOResult> LoadOFF(std::string_view)`, the `enum class MeshIOWriteStatus`, and writers `WriteOBJ`/`WritePLY`/`WritePLYBinary`/`WriteSTL`/`WriteSTLBinary` that this task mirrors for `WriteOFF`.
- The point-cloud IO module surface is `export namespace Geometry::PointCloudIO` in `src/geometry/Geometry.PointCloud.IO.cppm`, with `Expected<PointCloudIOResult> LoadXYZ/LoadPCD/LoadPLY(std::string_view)` and `PointCloudIOResult { PointCloud::Cloud Cloud; std::string SourcePath; std::string BasePath; }`. New readers return the same `Expected<PointCloudIOResult>` type.
- This is a geometry-layer task. Geometry depends only on core (`Extrinsic.Core.Error` for `Expected`); it must not pull in assets/runtime/graphics/rhi/ecs/app.
- Upstream references being ported: `bcg_mesh_io` (OFF writer) and `bcg_point_cloud_io` (additional ASCII readers); IntrinsicEngine keeps its own hardening rather than copying upstream parsing leniency.
- Status: retired 2026-06-28. Commit: this commit (`Broaden geometry IO ASCII coverage`).
- `Geometry::MeshIO::WriteOFF` now exports deterministic ASCII OFF writing and fails closed on empty meshes, invalid face topology, invalid paths, and non-finite positions.
- `Geometry::PointCloudIO` now exports `LoadPTS`, `LoadPWN`, `LoadCSV`, `Load3D`, and `LoadTXT`; the new readers share a strict ASCII scanner, validate count/column layouts, and populate normals/colors for supported layouts.

## Slice plan

- [x] Slice A — OFF writer: add `WriteOFF` to `Geometry.HalfedgeMesh.IO` and prove round-tripping with `LoadOFF` (vertex/face data within tolerance), with fail-closed write diagnostics.
- [x] Slice B — Point-cloud ASCII readers: add `.pts`, `.pwn`, `.csv`, `.3d`, `.txt` readers to `Geometry.PointCloud.IO`, each routed through the existing hardened ASCII parsing path, with fixtures and fail-closed tests.

## Required changes

- [x] In `src/geometry/Geometry.HalfedgeMesh.IO.cppm`, declare `MeshIOWriteStatus WriteOFF(std::string_view absolute_path, const MeshIOResult& mesh);` in `export namespace Geometry::MeshIO`, alongside the existing writers, reusing the existing `MeshIOWriteStatus` enum (`Success`, `InvalidPath`, `FileWriteError`, and the existing empty/degenerate status values — do not add new enum values unless a genuinely new failure mode requires it).
- [x] In `src/geometry/Geometry.HalfedgeMesh.IO.cpp`, implement `WriteOFF`: emit the canonical ASCII `OFF` header, vertex count / face count / edge-count line, then deterministic vertex lines followed by per-face `n i0 i1 ... in-1` index lines. Fail closed (return the appropriate `MeshIOWriteStatus`) on empty mesh, invalid/unwritable path, and non-finite vertex coordinates; do not emit NaN/Inf tokens.
- [x] Ensure `WriteOFF` output is byte-for-byte deterministic for a given mesh (stable vertex/face ordering, fixed numeric formatting consistent with the existing `Geometry.IOText` helpers / the other ASCII writers).
- [x] In `src/geometry/Geometry.PointCloud.IO.cppm`, declare ASCII reader entry points in `export namespace Geometry::PointCloudIO`, each returning `Extrinsic::Core::Expected<PointCloudIOResult>`: `LoadPTS`, `LoadPWN`, `LoadCSV`, `Load3D`, `LoadTXT` (taking `std::string_view absolute_path`). Populate positions, and normals for the formats that carry them (`.pwn` carries point+normal; `.pts`/`.csv`/`.3d`/`.txt` carry positions and may carry normals/intensity per the column layout).
- [x] In `src/geometry/Geometry.PointCloud.IO.cpp`, implement the new readers by routing through the existing hardened ASCII parsing path used by `LoadXYZ` (shared tokenizer/line-scanner), rather than duplicating per-format parsing. Centralize column-layout handling so position/normal extraction is shared.
- [x] Each new reader must fail closed with an explicit `Extrinsic::Core::Expected` error diagnostic on: missing/unopenable file, empty file, malformed lines (wrong column count, non-numeric tokens), and non-finite (NaN/Inf) coordinate or normal values. No asserts, no silent skipping that masks corruption.
- [x] If file extension dispatch exists anywhere in the point-cloud IO surface, wire the new extensions to their readers; otherwise leave dispatch untouched (out of scope).
- [x] Do not modify the existing `LoadXYZ`/`LoadPCD`/`LoadPLY` or any `Write*` point-cloud functions, nor the existing mesh load functions.

## Tests

- [x] Add `WriteOFF` round-trip test (`unit;geometry`): build a mesh, `WriteOFF` to a temp path, `LoadOFF` it back, and assert vertex positions and face index sets match the original within tolerance (per GEOM-005 numeric policy).
- [x] Add `WriteOFF` fail-closed tests: empty mesh returns the empty/degenerate status; an invalid/unwritable path returns `InvalidPath`/`FileWriteError`; a mesh with a non-finite vertex returns a write-failure status without emitting NaN/Inf.
- [x] Add `WriteOFF` determinism test: writing the same mesh twice yields byte-identical files.
- [x] Add a parsing test per new point-cloud reader (`LoadPTS`, `LoadPWN`, `LoadCSV`, `Load3D`, `LoadTXT`) over a known committed fixture, asserting parsed positions match expected values and, for formats that carry them, parsed normals match expected values (notably `.pwn`).
- [x] Add fail-closed tests for each new reader: empty file and malformed file (wrong column count and non-numeric token) each return an `Expected` error with an explicit diagnostic message; a non-finite token causes a fail-closed error.
- [x] Add a determinism test: loading the same fixture twice yields identical `PointCloud::Cloud` contents (positions and normals in stable order).
- [x] Place tests under `tests/unit/geometry/` (e.g. extend `Test.GeometryIO.cpp` or add a sibling), and commit minimal ASCII fixtures (one valid + the malformed/empty cases) under the geometry test fixtures location. Use the existing `unit;geometry` label only — do not introduce a new CTest label.

## Docs

- [x] Update the geometry IO documentation to record that OFF is now read/write and to list the newly supported point-cloud read extensions (`.pts`, `.pwn`, `.csv`, `.3d`, `.txt`), including which formats carry normals.
- [x] Regenerate `docs/api/generated/module_inventory.md` so the new exported symbols (`WriteOFF`, `LoadPTS`, `LoadPWN`, `LoadCSV`, `Load3D`, `LoadTXT`) appear.
- [x] Note in the IO docs that the readers reuse the existing hardened ASCII parsing path and that all new paths are fail-closed with explicit diagnostics.

## Acceptance criteria

- [x] `Geometry::MeshIO::WriteOFF` exists and is exported; `WriteOFF` then `LoadOFF` round-trips vertex positions and face connectivity within the GEOM-005 tolerance.
- [x] `Geometry::PointCloudIO::LoadPTS`, `LoadPWN`, `LoadCSV`, `Load3D`, and `LoadTXT` exist, are exported, and each parses its valid fixture into the expected positions (and normals where applicable).
- [x] Every new read/write path fails closed (returns an error status / `Expected` error with an explicit diagnostic) on empty, malformed, or non-finite input, and never emits or stores NaN/Inf.
- [x] All new IO is deterministic: repeated writes are byte-identical and repeated reads produce identical cloud/mesh contents.
- [x] The existing PLY/PCD/XYZ point-cloud paths and existing mesh IO functions are unchanged.
- [x] `module_inventory.md`, layering, test-layout, doc-link, and task-policy validators all pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'GeometryIO|PointCloud.*IO|MeshIO' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
git diff --check
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R '^Dimensions/ProgressivePoissonReferenceDim\\.PoissonGuaranteeHoldsAtEveryLevelBoundary/2$' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

Session note: the focused GEOIO filter passed 208/208. The full default CPU
gate completed with one unrelated timeout in
`Dimensions/ProgressivePoissonReferenceDim.PoissonGuaranteeHoldsAtEveryLevelBoundary/2`;
the isolated rerun of that exact test under the same 60s timeout passed in
58.38s.

## Forbidden changes

- Mixing mechanical file moves with semantic refactors in the same change.
- Introducing unrelated feature work (new geometry algorithms, format converters, or tooling beyond OFF writing and the five ASCII point-cloud readers).
- Introducing renderer/runtime/ECS/assets/platform/app dependencies from the geometry layer.
- Adding new binary formats or modifying the existing PLY/PCD/XYZ read/write paths.
- Introducing a new CTest label without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.
- Claiming performance improvements without a baseline comparison.

## Maturity

- Stop-state pin: this task closes at `CPUContracted`. The OFF writer and the five point-cloud readers must be fully implemented (not stubbed), deterministic, fail-closed with explicit diagnostics, and covered by the round-trip / fixture / fail-closed / determinism tests above. No GPU/runtime/parity work is in scope; advancing beyond `CPUContracted` is out of scope for this task.

- Closure: no `Operational` follow-up is owed for this task.
