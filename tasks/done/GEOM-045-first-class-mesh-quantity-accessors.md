---
id: GEOM-045
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---

# GEOM-045 — First-class mesh geometric-quantity accessors

## Goal
- Promote several private/internal/legacy mesh-quantity helpers into first-class, exported, property-backed geometry APIs on `Geometry.HalfedgeMesh.Utils`, and de-duplicate the scattered private copies currently embedded in `Geometry.UvAtlas`, `Geometry.HalfedgeMesh.Analysis`, `Geometry.HalfedgeMesh.Geodesic`, and `Geometry.HalfedgeMesh.Builder`.
- Provide stable, deterministic, fail-closed accessors for: per-face area (`f:area`), per-face oriented area-vector (`f:area_vector`), per-face own-corner centroid (`f:centroid`), barycentric (lumped) vertex area (`v:barycentric_area`), a public unnormalized per-face gradient of an arbitrary vertex scalar, a public `ProjectToUnitSphere`, an `area*angle` vertex-normal weighting mode, and a per-vertex 1-ring PCA helper (`v:pca`).
- Each accessor is exported, polygon-capable where the spec calls for it, stores its result in a stably-named mesh property, and is the single canonical implementation that all other algorithms reuse.

## Non-goals
- No change to Heat-Method numerical results: the face-gradient promotion must keep `Geometry.HalfedgeMesh.Geodesic` behavior bit-for-bit identical (the public API is unnormalized; the Heat-Method caller continues to apply its own normalize-and-negate step).
- No GPU/Vulkan backend and no compute-shader port of any accessor.
- No UI, editor, or visualization work.
- No new mesh data structure, no change to the halfedge connectivity representation, and no change to property-set storage semantics.
- No performance-optimization claims; this is foundation/de-duplication work, not a speedup.

## Context
- Status: retired to `tasks/done/` on 2026-06-28 at `CPUContracted`.
  Commit: this commit (`Complete mesh quantity accessor contract`).
- `Geometry.HalfedgeMesh.Utils` now exports the quantity accessors and
  property publishers, `Geometry.HalfedgeMesh.Builder` exports
  `ProjectToUnitSphere`, geodesics consume the public unnormalized gradient,
  and `Geometry.HalfedgeMesh.Vertices.Normals` includes `AreaAngleWeighted`.
- These quantities exist today only as private/static/legacy helpers, so callers re-derive them inconsistently:
  - `Geometry.HalfedgeMesh.Utils` already exports `TriangleArea`, `ComputeMixedVoronoiAreas`, and `ComputeOneRingCentroid` (`Geometry::MeshUtils` namespace), but has no polygon-capable `f:area`, no `f:area_vector`, and no own-corner `f:centroid`.
  - `Geometry.UvAtlas` and `Geometry.HalfedgeMesh.Analysis` carry private per-face area copies that should collapse onto the new accessor.
  - `Geometry.HalfedgeMesh.Geodesic` computes a per-face gradient inline (`Geometry.HalfedgeMesh.Geodesic.cpp` ~lines 42-93): `grad = (1/2A) sum u_i (N x e_i)`, then stores the *normalized negative* `-grad/|grad|`. The general-purpose API must expose the *unnormalized* `grad`.
  - `Geometry.HalfedgeMesh.Builder.cpp` has a file-private `ProjectVerticesToUnitSphere(Mesh&)` (~line 109) used by `MakeUnitSphereMesh`; promote a clean public `ProjectToUnitSphere`.
  - `Geometry.HalfedgeMesh.Vertices.Normals` exposes `AveragingMode { UniformFace, AreaWeighted, AngleWeighted, MaxWeighted }`; add an `AreaAngleWeighted` mode.
  - A correct own-corner `BuildMeshFaceCentroids` exists only in the legacy graphics layer; geometry must NOT depend on graphics, so a clean reimplementation is brought into `Geometry.MeshUtils`.
  - `Geometry.PCA` exposes `ToPCA(std::span<const glm::vec3>) -> PCAResult` with `Eigenvectors`/`Eigenvalues`; the 1-ring PCA helper reuses it.
- Promoting these is low-risk: each is a pure read of positions/connectivity that many downstream algorithms (curvature, parameterization, smoothing, geodesics) already need.

## Slice plan
- [x] Slice A — Face area / area-vector / own-corner centroid + barycentric lumped vertex area: add property accessors and de-duplicate the private area copies (`f:area`, `f:area_vector`, `f:centroid`, `v:barycentric_area`).
- [x] Slice B — Public unnormalized per-face gradient of a vertex scalar + public `ProjectToUnitSphere`: promotions only; keep the Heat-Method caller behavior identical.
- [x] Slice C — `area*angle` vertex-normal weighting mode + per-vertex 1-ring PCA helper (`v:pca`).

## Required changes
- [x] In `src/geometry/Geometry.HalfedgeMesh.Utils.cppm` export, in `Geometry::MeshUtils`, a polygon-capable per-face area accessor (e.g. `double FaceArea(const HalfedgeMesh::Mesh&, FaceHandle)` plus `std::vector<double> ComputeFaceAreas(const HalfedgeMesh::Mesh&)`), using a stable Heron-style triangle accumulation over the face's corner fan with the result clamped to be non-negative; declare the property-populating variant that writes the `f:area` property.
- [x] In `src/geometry/Geometry.HalfedgeMesh.Utils.cpp` implement the area accessor with deterministic corner-fan summation; reject degenerate/empty/non-finite input fail-closed with an explicit diagnostic (no asserts, no NaN propagation) per GEOM-005/GEOM-007.
- [x] In `src/geometry/Geometry.HalfedgeMesh.Utils.cppm`/`.cpp` export a per-face oriented area-vector accessor (e.g. `glm::dvec3 FaceAreaVector(const HalfedgeMesh::Mesh&, FaceHandle)`) by promoting the existing file-local `ComputeAreaVector`; polygon-capable; populate the `f:area_vector` property.
- [x] In `src/geometry/Geometry.HalfedgeMesh.Utils.cppm`/`.cpp` export a per-face own-corner centroid accessor (e.g. `glm::dvec3 FaceCentroid(const HalfedgeMesh::Mesh&, FaceHandle)`) computed as the average of the face's OWN corner positions (a clean reimplementation of the legacy-graphics `BuildMeshFaceCentroids`, with no graphics dependency); populate `f:centroid`. Document that this is distinct from the existing `ComputeOneRingCentroid`.
- [x] In `src/geometry/Geometry.HalfedgeMesh.Utils.cppm`/`.cpp` export a barycentric (lumped) vertex-area accessor (e.g. `std::vector<double> ComputeBarycentricVertexAreas(const HalfedgeMesh::Mesh&)`) defined as `v:barycentric_area = sum over incident faces of FaceArea / 3`, reusing the new `FaceArea`; populate `v:barycentric_area`. Keep it as a distinct, cheaper alternative to `ComputeMixedVoronoiAreas`.
- [x] De-duplicate: replace the private per-face area copies in `src/geometry/Geometry.UvAtlas.cpp` and `src/geometry/Geometry.HalfedgeMesh.Analysis.cpp` with calls to `Geometry::MeshUtils::FaceArea`/`ComputeFaceAreas`; remove the now-dead private helpers.
- [x] In `src/geometry/Geometry.HalfedgeMesh.Utils.cppm`/`.cpp` export a public general-purpose per-face gradient of an arbitrary vertex scalar field (e.g. `glm::dvec3 FaceScalarGradient(const HalfedgeMesh::Mesh&, FaceHandle, std::span<const double> vertexValues)` plus a whole-mesh batch variant) implementing `grad = (1/2A) sum_i u_i (N x e_i)`, UNNORMALIZED; fail-closed when `A` is below tolerance.
- [x] In `src/geometry/Geometry.HalfedgeMesh.Geodesic.cpp` (~lines 42-93) replace the inline gradient loop with a call to the new `FaceScalarGradient`, then apply the existing normalize-and-negate (`-grad/|grad|`) locally so Heat-Method output is unchanged.
- [x] In `src/geometry/Geometry.HalfedgeMesh.Builder.cppm`/`.cpp` export a public `ProjectToUnitSphere(Mesh&)` promoted from the file-private `ProjectVerticesToUnitSphere` (~line 109), normalizing each vertex position with a zero-norm guard (vertices at/near the origin are left unchanged, no NaN); update `MakeUnitSphereMesh` to call the public API.
- [x] In `src/geometry/Geometry.HalfedgeMesh.Vertices.Normals.cppm` add an `AreaAngleWeighted` value to `AveragingMode` and update `DebugName`; in `Geometry.HalfedgeMesh.Vertices.Normals.cpp` implement the `area*angle` weighting (per-corner face contribution scaled by face area times the corner interior angle).
- [x] In `src/geometry/Geometry.HalfedgeMesh.Utils.cppm`/`.cpp` export a per-vertex 1-ring PCA helper (e.g. `Geometry::PCAResult VertexOneRingPCA(const HalfedgeMesh::Mesh&, VertexHandle)`) that gathers the 1-ring neighbor positions and calls `Geometry::ToPCA`; populate a `v:pca` property (frame/eigen data) with fail-closed handling of under-determined neighborhoods.
- [x] Use the established `f:`/`v:` property-naming convention for `f:area`, `f:area_vector`, `f:centroid`, `v:barycentric_area`, `v:pca` consistent with the property-name lifetime contract (GEOM-027); do not invent a new naming scheme.

## Tests
- [x] Add `f:area` analytic tests (`unit;geometry`): unit-square face area equals `1.0` and an equilateral-triangle face area equals `sqrt(3)/4` within tolerance.
- [x] Add an `f:area_vector` closed-mesh test: summing `FaceAreaVector` over all faces of a closed mesh (e.g. a tetra/sphere mesh) equals the zero vector within tolerance.
- [x] Add an `f:centroid` test: `FaceCentroid` equals the analytically-computed average of the face's own corner positions, and differs from `ComputeOneRingCentroid` on an irregular (non-symmetric) mesh.
- [x] Add a `v:barycentric_area` conservation test: the sum of `ComputeBarycentricVertexAreas` over all vertices equals the total mesh surface area within tolerance.
- [x] Add a face-gradient correctness test: for a linear scalar field `u(x) = a·x + b` sampled at vertices, `FaceScalarGradient` returns the analytic constant gradient `a` within tolerance.
- [x] Add a Heat-Method invariance test: confirm `Geometry.HalfedgeMesh.Geodesic` distances are unchanged (within existing tolerance) after the gradient promotion, against the pre-existing geodesic expectations.
- [x] Add `ProjectToUnitSphere` tests: all non-origin vertices have unit norm afterward; a vertex placed at the origin is left unchanged with no NaN/Inf produced.
- [x] Add an `area*angle` normal test: `AreaAngleWeighted` vertex normals on a small hand-constructed fan match a hand-computed area-times-angle weighted average.
- [x] Add a 1-ring PCA normal test: on a sampled sphere mesh, the smallest-eigenvalue eigenvector from `VertexOneRingPCA` aligns (up to sign) with the analytic outward sphere normal within tolerance.
- [x] Add fail-closed tests: empty mesh, degenerate (zero-area / collinear) face, and non-finite input each return an explicit diagnostic / empty result rather than asserting, NaN, or undefined behavior.

## Docs
- [x] Update `docs/api/generated/module_inventory.md` by regenerating it after the new exports land.
- [x] Document the new `Geometry::MeshUtils` accessors and the `AreaAngleWeighted` mode in the relevant geometry API doc. The repository currently has only generated inventory under `docs/api/`, so the hand-authored contract lives in [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md) and records the `f:area`, `f:area_vector`, `f:centroid`, `v:barycentric_area`, `v:pca` property names and that `FaceCentroid` is distinct from `ComputeOneRingCentroid`.
- [x] Note in the docs that the public `FaceScalarGradient` is unnormalized and that the Heat-Method applies its own normalize/negate step.

## Acceptance criteria
- [x] `Geometry.HalfedgeMesh.Utils` exports polygon-capable `FaceArea`/`FaceAreaVector`/`FaceCentroid`/`ComputeBarycentricVertexAreas`/`FaceScalarGradient`/`VertexOneRingPCA`, each populating its documented `f:`/`v:` property.
- [x] `Geometry.UvAtlas` and `Geometry.HalfedgeMesh.Analysis` contain no private per-face area copy; both call the canonical `Geometry::MeshUtils` accessor.
- [x] `Geometry.HalfedgeMesh.Builder` exports public `ProjectToUnitSphere(Mesh&)` and `MakeUnitSphereMesh` routes through it; the private `ProjectVerticesToUnitSphere` is removed or reduced to a thin forwarder.
- [x] `Geometry.HalfedgeMesh.Vertices.Normals::AveragingMode` includes `AreaAngleWeighted` with a `DebugName` entry and a working implementation.
- [x] All new tests pass and the existing Heat-Method/geodesic tests pass unchanged (no numerical drift).
- [x] Every new accessor fails closed on empty/degenerate/non-finite input with an explicit diagnostic; no asserts and no NaN/Inf leak.
- [x] `check_layering.py --strict` passes: nothing under `src/geometry` imports assets/runtime/graphics/rhi/ecs/app.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'HalfedgeMesh.*(Utils|Geodesic|Builder|Normals|Analysis)|UvAtlas|MeshUtils' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -R 'GeometryMeshQuantities|HalfedgeMeshVertexNormals|Geodesic|UvAtlas|MeshAnalysis|MeshBuilder' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors in the same commit.
- Introducing unrelated feature work alongside the accessor promotions.
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into `src/geometry` (the clean `FaceCentroid` reimplementation must NOT pull from the legacy graphics layer).
- Changing Heat-Method / geodesic numerical output as a side effect of the gradient promotion.
- Claiming any performance improvement without a baseline comparison.
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.

## Maturity
- Stop-state: CPUContracted. The accessors are exported, property-backed, de-duplicated, and covered by deterministic analytic + fail-closed CPU tests, with the Heat-Method invariance pinned. No GPU backend and no optimized-backend parity are in scope; that would be a later Operational/ParityProven step.
