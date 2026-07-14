# GEOM-006 — Indexed mesh/soup container and conversion contracts

## Goal
- Add a lightweight indexed mesh/polygon-soup data model and conversion contracts that bridge import, reconstruction, halfedge topology, point clouds, and renderer upload staging without requiring halfedge connectivity for every algorithm.

## Non-goals
- No renderer or GPU residency implementation.
- No asset-service integration.
- No broad replacement of `Geometry::HalfedgeMesh::Mesh`, `Geometry::PointCloud::Cloud`, or `Geometry::Graph::Graph`.
- No advanced repair/remeshing algorithms beyond validation/conversion needed by this container.

## Status
- Status: done.
- Completed: 2026-05-21.
- Commit: TBD (filled in when the landing commit is created).
- PR: TBD (filled in when the landing PR is opened).
- Owner/agent: agent-onboarding session on branch `claude/intrinsicengine-agent-onboarding-jQaPD`.

## Context
- Status: done; Slice 3 retired the task on 2026-05-21.
- Owning subsystem/layer: `geometry` (`geometry -> core` only).
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Started after retiring [`GEOIO-002`](GEOIO-002-geometry-io-parity-hardening.md) from geometry backlog to done on 2026-05-21.
- Many paper methods operate on indexed triangle soups or polygon soups before topology is built, after reconstruction, or during GPU staging.
- Existing IO code can load/export meshes and point clouds, but there is no canonical lightweight soup container with validation and conversion diagnostics.
- This task should align with GEOM-012 domain-view semantics so conversion APIs distinguish borrowed views from owning hard copies.

## Slice plan

- **Slice 1 (this slice).** Add the owning `Geometry.MeshSoup` container, borrowed view naming, `PropertySet`-backed vertex/face/corner attribute domains, validation diagnostics, unit tests, architecture docs, and module inventory updates. Defers halfedge and point-cloud conversions to Slice 2.
- **Slice 2 (2026-05-21).** Add soup ↔ halfedge mesh owning conversions in `Geometry.Mesh.Conversion` with structured conversion diagnostics and round-trip tests. The public `.cppm` declares the conversion surface and the non-trivial bodies live in `Geometry.Mesh.Conversion.cpp`. Default CMake/CTest verification remains blocked by the local dependency-cache state recorded below.
- **Slice 3 (2026-05-21, this slice).** Add the explicit MeshSoup ↔ PointCloud owning conversion surface in a new `Geometry.PointCloud.Conversion` module with structured conversion diagnostics (`DeletedPointsOmitted`, `FacesDropped`, `AttributeRemapSkipped`, `ValidationDiagnostic`), round-trip tests, architecture docs, and module inventory updates. Cloud → soup copies positions while skipping deleted points and emits remap-skipped warnings for `p:normal`/`p:color`/`p:radius` and any generic vertex property; soup → cloud copies positions while dropping topology and emits remap-skipped warnings for face/corner property domains. Defers future IO/reconstruction/render-staging callers to follow-up tasks.

## Required changes
- [x] Define an indexed mesh/soup module in `src/geometry` with positions, face/index buffers, optional polygon support, and attribute domains.
- [x] Provide validation diagnostics for duplicate vertices, invalid indices, degenerate faces, non-manifold edges, inconsistent winding, and attribute arity mismatches.
- [x] Add conversion from soup to `Geometry::HalfedgeMesh::Mesh` with explicit failure diagnostics for unsupported topology.
- [x] Add conversion from `Geometry::HalfedgeMesh::Mesh` to indexed triangle/polygon soup while preserving supported vertex/face attributes.
- [x] Add conversion between point-cloud positions and soup vertices where appropriate (Slice 3: `Geometry.PointCloud.Conversion::ToIndexedMesh` / `ToPointCloud`).
- [x] Ensure conversion APIs are named to distinguish no-copy views from hard-copy owning conversions.
- [x] Document renderer upload staging as a data-shape compatibility goal without importing graphics or runtime layers.
- [x] Update `src/geometry/CMakeLists.txt`, `Geometry.cppm`, and generated module inventory if a new module surface is added.

## Tests
- [x] Add `tests/unit/geometry/Test.MeshSoup.cpp` using the `Test.<Name>.cpp` naming style.
- [x] Cover empty input, valid triangle soup, polygon soup, duplicate vertices, invalid indices, degenerate faces, non-manifold edge detection, winding diagnostics, and attribute size mismatches.
- [x] Cover round-trip conversions for simple halfedge meshes.
- [x] Cover round-trip conversions for point-cloud-derived vertices (Slice 3: `tests/unit/geometry/Test.PointCloudConversion.cpp`).
- [x] Run focused geometry tests (Slice 3 verification notes below).

## Docs
- [x] Update `docs/architecture/geometry.md` with the soup/container role and conversion boundaries.
- [x] Update `docs/api/generated/module_inventory.md` after module surface changes.
- [x] Document the `Geometry.PointCloud.Conversion` surface in `docs/architecture/geometry.md` (Slice 3).

Future IO/reconstruction/render-staging callers cross-referencing this
container are tracked in their own follow-up tasks per the slice plan, not in
this retired task file.

## Acceptance criteria
- [x] Algorithms that do not require halfedge connectivity can use a canonical lightweight geometry container.
- [x] Conversion failures return structured diagnostics rather than silent `bool`/`std::optional` failures.
- [x] The implementation preserves `geometry -> core` layering and does not import assets, graphics, runtime, ECS, platform, or app.
- [x] Focused tests and structural checks pass (Slice 3 verification notes below).

## Verification
```bash
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'MeshSoup|GeometryIO|MeshBuilder|HalfedgeMesh' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Slice 1 verification notes (2026-05-21):

- Passed: direct `clang++-22` module precompile/object compile for `Geometry.MeshSoup.cppm` with cached GLM include path.
- Passed: direct `clang++-22` smoke executable importing `Geometry.MeshSoup` and validating a triangle soup.
- Passed: direct `clang++-22` diagnostic smoke executable covering invalid index, degenerate face, inconsistent winding, non-manifold edge, and attribute arity diagnostics.
- Passed: `python3 tools/repo/check_layering.py --root src --strict`.
- Passed: `python3 tools/repo/check_test_layout.py --root . --strict`.
- Passed: `python3 tools/docs/check_doc_links.py --root .`.
- Passed: `python3 tools/agents/check_task_policy.py --root . --strict`.
- Blocked: `cmake --preset ci` with default compiler names because `clang-20`/`clang++-20` are not on PATH in this shell.
- Blocked: `cmake --preset ci -D CMAKE_C_COMPILER=/usr/bin/clang-22 -D CMAKE_CXX_COMPILER=/usr/bin/clang++-22 -D CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22` during generate by cached Draco targets with empty source lists; focused `IntrinsicGeometryTests`/CTest remain pending until the build tree/dependency cache is repaired.

Slice 2 verification notes (2026-05-21):

- Passed: direct `clang++-22` module interface compile for `Geometry.Properties`, `Geometry.Graph.Fwd`, `Geometry.Circulators`, `Geometry.HalfedgeMesh.Fwd`, `Geometry.HalfedgeMesh`, `Geometry.PointCloud.Fwd`, `Geometry.PointCloud`, `Geometry.MeshSoup`, and `Geometry.Mesh.Conversion` with cached GLM include path.
- Passed: direct `clang++-22` implementation compile for `Geometry.Properties.cpp`, `Geometry.HalfedgeMesh.cpp`, and `Geometry.Mesh.Conversion.cpp` against the prebuilt module interfaces.
- Passed: direct `clang++-22` smoke executable covering soup → halfedge → soup conversions.
- Passed: `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`.
- Passed: `python3 tools/repo/check_layering.py --root src --strict`.
- Passed: `python3 tools/repo/check_test_layout.py --root . --strict`.
- Passed: `python3 tools/docs/check_doc_links.py --root .`.
- Passed: `python3 tools/agents/check_task_policy.py --root . --strict`.
- Passed: `cmake --preset ci -D CMAKE_C_COMPILER=/usr/bin/clang-22 -D CMAKE_CXX_COMPILER=/usr/bin/clang++-22 -D CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS=/usr/bin/clang-scan-deps-22` regenerated `build/ci`.
- Passed: `cmake --build --preset ci --target IntrinsicGeometryTests` linked `bin/IntrinsicGeometryTests` (user rerun) and a follow-up retry reported `ninja: no work to do`.
- Passed: `ctest --test-dir build/ci --output-on-failure -R 'MeshSoup|MeshConversion|GeometryIO|MeshBuilder|HalfedgeMesh' --timeout 60` (`278/278` tests passed).

Slice 3 verification notes (2026-05-21):

- Passed: `cmake --preset ci` (clang-20 toolchain installed in this environment).
- Passed: `cmake --build --preset ci --target IntrinsicGeometryTests` (438 modules; new `Geometry.PointCloud.Conversion` interface + implementation compiled clean).
- Passed: `ctest --test-dir build/ci --output-on-failure -R 'PointCloudConversion' --timeout 60` (`8/8` tests passed).
- Passed: `ctest --test-dir build/ci --output-on-failure -R 'PointCloudConversion|MeshConversion|MeshSoup|PointCloud|HalfedgeMesh' --timeout 60` (`195/195` tests passed).
- Passed: `python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md` (438 modules; new entry committed).
- Passed: `python3 tools/repo/check_layering.py --root src --strict`.
- Passed: `python3 tools/repo/check_test_layout.py --root . --strict`.
- Passed: `python3 tools/docs/check_doc_links.py --root .`.
- Passed: `python3 tools/agents/check_task_policy.py --root . --strict`.
- Default CPU gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) reports `2073/2074` passing; the sole failure `CoreTaskGraph.MainThreadReadyQueueUsesPriorityAndCostOrdering` (Test.Core.TaskGraphLegacy.cpp) is a pre-existing sleep-based timing flake (passes on rerun) unrelated to this slice's geometry surface.

## Forbidden changes
- Do not add renderer, runtime, ECS, assets, platform, or app dependencies.
- Do not mix this semantic container addition with mechanical module renames.
- Do not claim performance improvements without benchmark evidence.


