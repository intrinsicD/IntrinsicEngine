---
id: METHOD-032
theme: I
depends_on: []
maturity_target: CPUContracted
---
# METHOD-032 — Octree parity normal orientation reference backend

## Goal
- Add a CPU reference method package that globally orients an inconsistently signed point-cloud normal field by labeling octree corner-lattice points inside/outside: the corners of an inflated bounding box are known-outside seeds, every lattice edge carries the parity constraint `label(a) XOR label(b) = (crossings of edge a–b) mod 2` with crossings taken from the tangent planes of the points in each populated voxel, and the resulting corner sign field picks the sign of every point normal. Deterministic and solver-free, with per-point confidence and fail-closed open-surface detection.

## Non-goals
- No changes to normal *estimation* — `Geometry.PointCloud.Normals` stays untouched; this method consumes its unoriented output (`OrientationMode::None`) or any caller-supplied unoriented normals.
- No normal computation of any kind inside the method — not even a per-voxel refit. Orientation takes precomputed unoriented normals as pure input regardless of their origin (PCA estimation, another method, imported data); callers wanting PCA normals precompute them via `Geometry.PointCloud.Normals` first.
- No refactor of the existing embedded MST orientation path; it remains as-is and serves as the comparison baseline.
- No general-purpose rework of `Geometry.Octree`. Its BVH-flavored behavior (tight children, mean/median split points, no corner identity, no 2:1 balance) is wrong for a corner lattice, and per research pragmatism (P1) the parity lattice stays method-local until a second consumer exists; promotion to a shared module is a follow-up decision, not this task.
- No optimized CPU or GPU backend before reference parity evidence, and no runtime/config/UI integration (e.g. wiring as a new `OrientationMode`) — those open as follow-up tasks only after `CPUContracted` evidence exists.
- No best-effort orientation of open scans: when the watertight-barrier assumption fails, the method reports it and fails closed instead of silently orienting garbage.

## Context
- Paper/method: original in-house method (intrinsicD, 2026-07-19 design session); no upstream paper. `paper.md` in the package records the formulation as the authoritative claim source.
- Method package: `methods/geometry/octree_parity_orientation/`.
- Formulation: estimate per-point unoriented normals and local sample spacing; build a center-split octree over the inflated AABB (OBB frame optional later) whose leaf size tracks local spacing; each populated voxel clusters its points by unsigned normal direction + plane offset into one tangent-plane patch per local sheet, clipped to the voxel; a lattice edge's crossing count is the number of patch planes cutting it, giving the parity constraint above; corner labels solve by union–find with parity, seeded outside at the root corners, processed in fixed Morton order; a verification sweep computes per-corner constraint-satisfaction and the global conflict rate; each point's normal sign is chosen by a weighted vote of its leaf-corner labels (`sign(n·(c − p))` agreement), vote margin becomes per-point confidence. This is marching cubes run in reverse — crossings on edges determine signs at corners — so the output sign field is directly consumable by `Geometry.MarchingCubes` for a reconstruction cross-check.
- Operating envelope (documented, enforced by diagnostics): closed sampled surface; leaf size ≳ local sample spacing (else the flood fill leaks between samples through empty voxels) and ≪ thinnest feature separation (else one voxel sees two sheets); noise well below local spacing. Violations surface as parity conflicts, not silent misorientation.
- Reuse: `Geometry.KDTree` + `Geometry.PointCloud.QualityMetrics` nearest-neighbor statistics for the spacing estimate; patch planes derive exclusively from the supplied precomputed normals (signs aligned within each cluster, plane through the cluster centroid along the mean direction) — the method never computes or refits normals itself; `Geometry.PointCloud.SurfaceSampling` for ground-truth-normal fixtures; `Geometry.MarchingCubes` on a `Grid::DenseGrid` sampling of the corner sign field for the benchmark-level reconstruction check; existing `Geometry.PointCloud.Normals` MST mode as baseline.
- Method-local structure: hashed integer-lattice octree (Morton keys per level), strict center splits, 2:1 balanced so hanging nodes reduce to sub-edge constraints on the finer side; corner identity via integer lattice coordinates, never floating-point comparison.
- Baseline: `Geometry.PointCloud.Normals` `OrientationMode::MinimumSpanningTree` on identical fixtures; the known MST failure cases (thin plates, nearby opposing sheets, around-hole propagation) are exactly where this method must show its value.
- Publication track: modern competitor baselines and evidence are seeded as `METHOD-033` (screened Poisson reconstruction reference — the inner solver iPSR needs), `METHOD-034` (iPSR baseline, blocked by `METHOD-033`), `METHOD-035` (PGR winding-number baseline), and `METHOD-036` (shared-protocol comparison evidence report, blocked by this task and the baselines); the sandbox parity-diagnostics debug-draw view is `RUNTIME-179` (blocked by `RUNTIME-177` and this task).

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/octree_parity_orientation/`.
- [ ] Fill `method.yaml` (`id: geometry.octree_parity_orientation`; status `reference`; paper block marked in-house/unpublished; metrics: `oriented_correct_fraction`, `parity_conflict_rate`, `low_confidence_fraction`, `reconstruction_mean_distance`, `runtime_ms`).
- [ ] Fill `paper.md` with the formulation, the parity-constraint equation, complexity (O(n log n), no linear solve), the operating envelope, and the failure-mode taxonomy.

### Public API in `src/geometry`
- [ ] Add module `Geometry.PointCloud.NormalOrientation` (`.cppm` + `.cpp`): an `OrientParams` (position/normal property names; leaf-size factor relative to estimated spacing plus optional explicit leaf size; spacing-estimate k; root-box inflation factor; normal-angle and plane-offset clustering epsilons; conflict-rate fail-closed threshold; max octree depth cap — no RNG/seed: the method itself is fully deterministic, test fixtures own their sign-scramble seeds) and `Orient(...)` overloads for `std::span` positions+normals and for `Cloud&` in-place property update — the `Cloud&` overload also writes per-point confidence as a vertex property (default `v:normal_confidence`, name param-controllable) — returning a `Result` with `Status`, flip mask / oriented normals, per-point confidence, and `Diagnostics` (corner/edge/constraint counts, patch-cluster count, crossing counts, parity conflict rate, per-corner satisfaction summary, flipped count, undecided/low-confidence count, leaf-depth and spacing statistics).
- [ ] Implement the method-local balanced Morton-lattice octree and union–find-with-parity solver in the `.cpp` implementation unit (not exported); fixed processing order; no floating-point corner identity.
- [ ] Deterministic on-plane tie-break: corner-vs-patch-plane classifications within epsilon of zero resolve by one documented consistent rule (e.g. always the positive side) so adjacent edge segments can never double- or zero-count a crossing; the epsilon is a documented param.
- [ ] Deterministic: identical `(input, params)` produce bitwise-identical outputs across runs and thread counts.
- [ ] Fail-closed with explicit statuses on: empty/too-small input, missing or count-mismatched normals, non-finite positions/normals, spacing-estimate failure, depth-cap overflow, and conflict rate above threshold (`OpenSurfaceSuspected`-style status; no oriented output claimed as success).
- [ ] Register the module in `src/geometry/CMakeLists.txt` (single `IntrinsicGeometry` target, alphabetical placement, no new link dependency).

### Benchmarks
- [ ] Smoke benchmark manifest on deterministic synthetic fixtures (sphere, torus, thin plate, hollow shell; seeded sign scramble; no external datasets) reporting the metrics above — quality metrics included, not runtime-only.

## Tests
- [ ] `tests/unit/geometry/Test.PointCloudNormalOrientation.cpp` with `unit;geometry` labels.
- [ ] Closed analytic fixtures via `SampleTriangleMeshSurface` (ground-truth normals kept, signs scrambled with a seeded RNG): sphere and torus reach the documented oriented-correct fraction (torus proves around-the-hole lateral propagation); hollow shell (nested cavity) proves parity handles nesting — inner-surface normals point into the cavity; thin plate at documented sheet separation stays correctly oriented on both sides.
- [ ] Baseline comparison: oriented-correct fraction ≥ the MST baseline on the torus and thin-plate fixtures, with deltas recorded in the method README.
- [ ] Open-surface detection: an open hemisphere fixture triggers the conflict-rate fail-closed status rather than returning success.
- [ ] Determinism and all fail-closed input cases listed above.
- [ ] Noise regression: documented correctness bound at a stated noise-to-spacing fraction on the sphere fixture.

## Docs
- [ ] `methods/geometry/octree_parity_orientation/README.md` with parameter-selection guidance (leaf-size factor, clustering epsilons, conflict threshold) and known limitations (open surfaces, noise approaching spacing, nonmanifold junctions, density starvation).
- [ ] Regenerate the module inventory (`python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`).

## Acceptance criteria
- [ ] CPU reference implementation present, deterministic, fail-closed per the listed statuses.
- [ ] All correctness tests pass in the default CPU gate, including torus, hollow-shell, thin-plate, and open-surface fail-closed cases.
- [ ] Oriented-correct fraction meets documented bounds on clean fixtures and is ≥ the MST baseline on torus and thin plate (or the delta is documented with analysis).
- [ ] Benchmark smoke manifest validates and runs with quality metrics.
- [ ] `method.yaml` validates.
- [ ] Public API type discipline: the exported surface uses only `std`/`glm`/scalar types plus the engine's own point-cloud types — the `Cloud&` overload is explicitly in-contract (it captures any vertex `PropertySet`, matching the `Geometry.PointCloud.Normals` span + `Cloud&` house pattern), and vertex-`Property` handles may appear in results; no third-party types and no method-internal types (Morton lattice, union–find state) are exported.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'NormalOrientation|PointCloud' --timeout 120
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No optimized/GPU backend before reference parity; no performance claims without baseline comparison.
- No behavior changes to `Geometry.Octree`, `Geometry.PointCloud.Normals`, or the embedded MST orientation path.
- No `std::rand` or global RNG state; seeded determinism only.
- No external datasets in smoke tests or benchmarks.

## Maturity
- Target: `CPUContracted` — reference backend plus correctness tests under the default CPU gate (correctness-first per the method workflow).
- No `Operational` follow-up is owed by this task; optimized CPU, GPU, and runtime/UI `OrientationMode` integration open as separate tasks only after `CPUContracted` evidence exists.
