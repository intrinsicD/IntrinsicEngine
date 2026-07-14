---
id: GEOM-044
theme: none
depends_on: []
maturity_target: CPUContracted
---

# GEOM-044 — Sqrt-3 (Kobbelt) subdivision and Loop feature/crease masks

## Goal

- [x] Add a new sqrt(3) (Kobbelt) subdivision operator for triangle halfedge meshes as a sibling of the existing Loop operator.
- [x] Extend the existing Loop subdivision operator with feature/crease-edge masks so that tagged feature edges and feature vertices are preserved while the rest of the surface is smoothed.

The repository today ships only Loop (`Geometry.Subdivision`) and Catmull-Clark (`Geometry.CatmullClark`) subdivision. There is zero coverage of the sqrt(3) (Kobbelt) scheme, and Loop currently treats only mesh boundary as a sharp feature (boundary-only crease handling) with no support for interior tagged feature/crease edges. This task closes both gaps to the `CPUContracted` maturity level: a deterministic, fail-closed CPU implementation with contract tests, no GPU/runtime/UI surface.

## Non-goals

- No changes to Catmull-Clark subdivision (`Geometry.CatmullClark`); it is already present and general.
- No GPU backend or GPU parity backend for either operator.
- No UI / editor work; the editor subdivide window is owned by UI-025 and is out of scope here.
- No new general "tagging" subsystem; feature-edge tags are consumed from existing mesh edge properties (`Geometry.Properties`) and not redesigned.
- No performance claims, tuning, or optimized backend; correctness only.

## Context
- Status: done.
- Owner/agent: Codex.
- Completed: 2026-06-28.
- Commit: this commit (`Migrate geometry property and topology utilities`).
- Maturity: `CPUContracted`.

- The existing Loop operator lives in `src/geometry/Geometry.HalfedgeMesh.Subdivision.cppm` (module `Geometry.Subdivision`) with the body in `src/geometry/Geometry.HalfedgeMesh.Subdivision.cpp`. Public API today: `Geometry::Subdivision::Subdivide(const HalfedgeMesh::Mesh&, HalfedgeMesh::Mesh&, const SubdivisionParams&)` returning `std::optional<SubdivisionResult>`.
- Loop currently uses Warren weights and handles only mesh boundary as sharp; there is no interior feature/crease-edge preservation.
- Catmull-Clark lives in `src/geometry/Geometry.HalfedgeMesh.CatmullClark.cppm` (module `Geometry.CatmullClark`) and is a useful structural reference for how a second operator and its `.cpp` body are organized in this layer.
- The mesh type is `Geometry::HalfedgeMesh::Mesh` (module `Geometry.HalfedgeMesh`); per-element attributes come from `Geometry.Properties`.
- A grep over `src` confirms zero matches for `sqrt3` / `kobbelt`: the operator is genuinely absent.
- The sqrt(3) (Kobbelt, "√3-Subdivision", SIGGRAPH 2000) scheme: split each face by inserting its centroid (1 face → 3), relax every original (even) vertex toward its 1-ring with weight `β(n) = (4 - 2*cos(2*π/n)) / 9` for valence `n`, then flip every original (pre-split) interior edge. Boundary requires special handling (boundary vertices use a 1D boundary stencil and boundary edges are not flipped; the standard treatment applies the boundary smoothing rule every other step).
- Loop feature variant (bcg `SubdivisionLoop` feature path): a feature vertex stays fixed unless it has exactly 2 incident feature edges, in which case it follows the crease (1-3-...) rule along those two feature edges; a feature edge's new midpoint vertex uses the crease midpoint rule (1/2, 1/2) and stays a feature edge; non-feature elements use the standard Loop rules.
- Existing tests live in `tests/unit/geometry/Test_Subdivision.cpp` and carry CTest labels of the form `unit;geometry`. No new label is introduced by this task.

## Slice plan

- [x] Slice A — sqrt(3) (Kobbelt) subdivision operator: add the new module `Geometry.HalfedgeMesh.SubdivisionSqrt3.{cppm,cpp}` (module name `Geometry.SubdivisionSqrt3`), register it in `src/geometry/CMakeLists.txt`, and add Slice A tests. Defers all Loop feature-mask work.
- [x] Slice B — Loop feature/crease masks: extend `Geometry.Subdivision` (`Geometry.HalfedgeMesh.Subdivision.{cppm,cpp}`) with feature-vertex / feature-edge masks driven by a tagged feature-edge input, and add Slice B tests. Does not touch sqrt(3).

## Required changes

- [x] Add `src/geometry/Geometry.HalfedgeMesh.SubdivisionSqrt3.cppm` exporting `export module Geometry.SubdivisionSqrt3;`, importing `Geometry.Properties` and `Geometry.HalfedgeMesh` only. Export namespace `Geometry::SubdivisionSqrt3` with: `struct Sqrt3Params { std::size_t Iterations{1}; std::size_t MaxOutputFaces{0}; };`, `struct Sqrt3Result { std::size_t IterationsPerformed{0}; std::size_t FinalVertexCount{0}; std::size_t FinalFaceCount{0}; };`, and `[[nodiscard]] std::optional<Sqrt3Result> Subdivide(const HalfedgeMesh::Mesh& input, HalfedgeMesh::Mesh& output, const Sqrt3Params& params = {});`. Keep only declarations / small inline in the interface unit.
- [x] Add `src/geometry/Geometry.HalfedgeMesh.SubdivisionSqrt3.cpp` implementing the sqrt(3) step: (1) validate input is a pure, non-empty triangle mesh with finite positions (else fail closed); (2) centroid 1→3 face split; (3) even-vertex relaxation with `β(n) = (4 - 2*cos(2*π/n)) / 9`; (4) flip every original interior edge; (5) correct boundary handling (boundary vertices use the boundary stencil, boundary edges are not flipped). Honor `MaxOutputFaces` by clamping `Iterations` (a single step triples face count: `n_faces_out = n_faces_in * 3^Iterations`).
- [x] Register the new interface unit in `src/geometry/CMakeLists.txt` by adding `Geometry.HalfedgeMesh.SubdivisionSqrt3.cppm` to the `FILE_SET CXX_MODULES` list for `IntrinsicGeometry` (alphabetically after `Geometry.HalfedgeMesh.Subdivision.cppm`). The `.cpp` body builds via the module library's normal source globbing/listing convention already used by the sibling operators.
- [x] Extend `src/geometry/Geometry.HalfedgeMesh.Subdivision.cppm`: add feature inputs to `SubdivisionParams` (e.g. a non-owning view / property key naming the boolean feature-edge tag, plus an opt-in flag) without changing the existing default behavior, and document the feature/crease semantics in the header comment. Do not break the existing `Subdivide` signature for callers that pass `{}`.
- [x] Extend `src/geometry/Geometry.HalfedgeMesh.Subdivision.cpp`: implement Loop feature masks — a feature vertex stays fixed unless it has exactly 2 incident feature edges (then crease rule along those edges); a feature edge's inserted midpoint uses the (1/2, 1/2) crease midpoint rule and remains tagged as a feature edge on output; all non-feature elements keep the current Loop/Warren rules and current boundary handling. Feature handling must be deterministic and independent of edge/vertex iteration order.
- [x] Both operators must follow GEOM-005 (API/numeric policy) and GEOM-007 (robust-predicate/tolerance policy): explicit diagnostics on degenerate/empty/non-finite/non-triangle input, no asserts, no NaNs produced.

## Tests

- [x] In `tests/unit/geometry/Test_Subdivision.cpp` (label `unit;geometry`), add Slice A sqrt(3) cases: one sqrt(3) step on a regular triangle mesh triples the face count (`out_faces == 3 * in_faces`) and increases vertex count consistently.
- [x] Add a sqrt(3) convergence case: iterating sqrt(3) on a regular mesh moves the surface toward the analytic sqrt(3) limit (decreasing residual against the analytic limit position on a regular interior vertex over successive iterations).
- [x] Add a sqrt(3) planarity case: sqrt(3) applied to a flat planar triangulated patch stays planar (all output vertices remain within tolerance of the input plane).
- [x] Add a sqrt(3) determinism case: two runs on the same input produce byte-identical output vertex positions and identical connectivity.
- [x] Add sqrt(3) fail-closed cases: empty mesh, non-triangle (quad-containing) input, and non-finite (NaN/Inf) input each return `std::nullopt` and leave `output` untouched (no asserts, no NaNs).
- [x] Add a sqrt(3) boundary case: a triangle mesh with boundary subdivides without leaking NaNs and boundary vertices follow the boundary stencil (boundary edges remain boundary; not flipped).
- [x] Add Slice B Loop feature cases: a mesh with a tagged closed feature loop preserves the crease — feature vertices and feature edges keep their position class (feature vertices with exactly 2 incident feature edges stay on the crease; isolated feature vertices stay fixed) while interior non-feature regions are smoothed as before.
- [x] Add a Loop feature regression case: with no feature edges tagged, output is identical to the pre-existing Loop behavior (guards against behavior drift for existing callers).
- [x] Add a Loop feature determinism case: feature-masked subdivision is order-independent and reproducible across runs.

## Docs

- [x] Document the sqrt(3) operator semantics, the `β(n)` weight, the 1→3 triple-face-count invariant, and boundary handling in the `Geometry.HalfedgeMesh.SubdivisionSqrt3.cppm` header comment block (matching the style of the existing Loop header).
- [x] Document the new Loop feature/crease mask semantics (feature vertex fixed unless exactly 2 incident feature edges; feature edge crease midpoint and tag preservation) in the `Geometry.HalfedgeMesh.Subdivision.cppm` header comment block.
- [x] Regenerate `docs/api/generated/module_inventory.md` so the new `Geometry.SubdivisionSqrt3` module appears.

## Acceptance criteria

- [x] `Geometry.SubdivisionSqrt3` module builds as part of `IntrinsicGeometry` and is registered in `src/geometry/CMakeLists.txt`.
- [x] One sqrt(3) step yields exactly `3 * in_faces` output faces on a valid triangle mesh.
- [x] sqrt(3) on a flat patch keeps every output vertex within numeric tolerance of the original plane.
- [x] sqrt(3) convergence test shows monotonically decreasing residual to the analytic limit at a regular interior vertex across iterations.
- [x] sqrt(3) fail-closed: empty, non-triangle, and non-finite inputs each return `std::nullopt` with `output` unchanged; no asserts fire and no NaN/Inf is ever written.
- [x] Loop with a tagged feature loop leaves feature vertices/edges in their expected position class (crease preserved) while smoothing non-feature regions.
- [x] Loop with no feature tags reproduces the existing Loop output exactly (no regression for current callers).
- [x] Both operators are deterministic: repeated runs on identical input produce identical connectivity and bit-identical positions.
- [x] The full Verification block passes, including layering, test-layout, doc-link, and task-policy checks.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Subdivision' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing mechanical file moves/renames with the semantic subdivision work in the same change.
- Introducing unrelated feature work (no Catmull-Clark edits, no new tagging subsystem, no remeshing/simplification changes).
- Introducing any renderer/runtime/ECS/assets/platform/app dependency from `src/geometry/*`; geometry imports only geometry and core.
- Claiming any performance improvement without a recorded baseline comparison (this task makes no perf claim).
- Adding new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change (this task reuses `unit;geometry`).
- Using asserts, producing NaNs, or silently succeeding on degenerate/non-triangle/non-finite input instead of failing closed with diagnostics.

## Maturity

- Stop-state for this task is `CPUContracted`: deterministic, fail-closed CPU implementations of both the sqrt(3) operator and the Loop feature/crease masks, covered by the contract tests above. No GPU backend, no optimized backend, no UI surface, and no performance claims are in scope. Slice A may close independently at `CPUContracted` for the sqrt(3) operator; Slice B closes the Loop feature-mask extension at `CPUContracted`.

- Closure: no `Operational` follow-up is owed for this task.
