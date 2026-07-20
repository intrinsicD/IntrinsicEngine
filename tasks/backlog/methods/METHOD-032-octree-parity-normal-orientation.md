---
id: METHOD-032
theme: I
depends_on: []
maturity_target: CPUContracted
---
# METHOD-032 — Octree parity normal orientation reference backend

## Goal
- Determine whether octree-corner parity constraints derived from local tangent
  patches can orient closed sampled surfaces more reliably than the existing
  MST baseline on thin/nested/adversarial geometry while detecting unsupported
  open scans. Promote a deterministic CPU reference only after a private
  killing slice passes frozen correctness, non-regression, conflict, and
  resource gates.

## Non-goals
- No changes to normal *estimation* — `Geometry.PointCloud.Normals` stays untouched; this method consumes its unoriented output (`OrientationMode::None`) or any caller-supplied unoriented normals.
- No normal computation of any kind inside the method — not even a per-voxel refit. Orientation takes precomputed unoriented normals as pure input regardless of their origin (PCA estimation, another method, imported data); callers wanting PCA normals precompute them via `Geometry.PointCloud.Normals` first.
- No refactor of the existing embedded MST orientation path; it remains as-is and serves as the comparison baseline.
- No general-purpose rework of `Geometry.Octree`. Its BVH-flavored behavior (tight children, mean/median split points, no corner identity, no 2:1 balance) is wrong for a corner lattice, and per research pragmatism (P1) the parity lattice stays method-local until a second consumer exists; promotion to a shared module is a follow-up decision, not this task.
- No optimized CPU or GPU backend before reference parity evidence, and no runtime/config/UI integration (e.g. wiring as a new `OrientationMode`) — those open as follow-up tasks only after `CPUContracted` evidence exists.
- No best-effort orientation of open scans: when the watertight-barrier assumption fails, the method reports it and fails closed instead of silently orienting garbage.

## Context
- Paper/method: original in-house method (intrinsicD, 2026-07-19 design session); no upstream paper. `paper.md` in the package records the formulation as the authoritative claim source.
- No novelty is presumed. Intake must audit the closest prior formulations for
  parity/flood-fill normal orientation, winding-number/sign propagation,
  octree corner labeling, graph-cut orientation, and reverse marching-cubes
  sign recovery, and state the irreducible delta or downgrade the task to a
  known-method implementation.
- Method package: `methods/geometry/octree_parity_orientation/`.
- Formulation: inputs are point positions plus **precomputed** unoriented per-point normals — estimation is a separate upstream step (see Non-goals), never part of this method. The method estimates local sample spacing via kNN, builds a center-split octree over the inflated AABB (OBB frame optional later) whose leaf size tracks that spacing; each populated voxel clusters its points by unsigned supplied-normal direction + plane offset into one tangent-plane patch per local sheet, clipped to the voxel; a lattice edge's crossing count is the number of patch planes cutting it, giving the parity constraint above; corner labels solve by union–find with parity, seeded outside at the root corners, processed in fixed Morton order; a verification sweep computes per-corner constraint-satisfaction and the global conflict rate; each point's normal sign is chosen by a weighted vote of its leaf-corner labels (`sign(n·(c − p))` agreement), vote margin becomes per-point confidence. This is marching cubes run in reverse — crossings on edges determine signs at corners — so the output sign field is directly consumable by `Geometry.MarchingCubes` for a reconstruction cross-check.
- Operating envelope (documented, enforced by diagnostics): closed sampled surface; leaf size ≳ local sample spacing (else the flood fill leaks between samples through empty voxels) and ≪ thinnest feature separation (else one voxel sees two sheets); noise well below local spacing. Violations surface as parity conflicts, not silent misorientation.
- Reuse: `Geometry.KDTree` + `Geometry.PointCloud.QualityMetrics` nearest-neighbor statistics for the spacing estimate; patch planes derive exclusively from the supplied precomputed normals (signs aligned within each cluster, plane through the cluster centroid along the mean direction) — the method never computes or refits normals itself; `Geometry.PointCloud.SurfaceSampling` for ground-truth-normal fixtures; `Geometry.MarchingCubes` on a `Grid::DenseGrid` sampling of the corner sign field for the benchmark-level reconstruction check; existing `Geometry.PointCloud.Normals` MST mode as baseline.
- Method-local structure: hashed integer-lattice octree (Morton keys per level), strict center splits, 2:1 balanced so hanging nodes reduce to sub-edge constraints on the finer side; corner identity via integer lattice coordinates, never floating-point comparison.
- Baseline: `Geometry.PointCloud.Normals` `OrientationMode::MinimumSpanningTree` on identical fixtures; the known MST failure cases (thin plates, nearby opposing sheets, around-hole propagation) are exactly where this method must show its value.
- Publication track: modern competitor baselines and evidence are seeded as `METHOD-033` (screened Poisson reconstruction reference — the inner solver iPSR needs), `METHOD-034` (iPSR baseline, blocked by `METHOD-033`), `METHOD-035` (PGR winding-number baseline), and `METHOD-036` (shared-protocol comparison evidence report, blocked by this task and the baselines); the sandbox parity-diagnostics debug-draw view is `RUNTIME-189` (blocked by retired `RUNTIME-177` and this task).

## Slice plan

- **Slice A — formulation and prior-art audit.** Freeze the claim boundary,
  closest-prior-art table, normalized units, input/output contract, patch and
  crossing conventions, fixture train/held-out split, baseline, tolerances,
  caps, diagnostics, and kill rule before prototype code.
- **Slice B — private killing prototype.** Implement method-local records/free
  functions over the fixed analytic fixtures. Do not add a public module,
  strategy token, or change `Geometry.PointCloud.Normals`.
- **Slice C — CPU reference.** Only after Slice B passes, add the narrow public
  span/`Cloud&` surface, deterministic balanced lattice, parity solve, and
  fail-closed diagnostics.
- **Slice D — comparison evidence.** Add the executable correctness smoke and
  schema-valid result, compare against MST at identical samples/sign scrambles,
  and document a positive or negative result.

## Right-sizing

- Keep the Morton lattice, tangent-patch clustering, parity union-find, and
  verification sweep in one method implementation unit using plain records and
  free functions. Do not modify or generalize `Geometry.Octree`.
- The span/`Cloud&` overload pair is justified by the existing
  `Geometry.PointCloud.Normals` house pattern. No orientation backend interface,
  registry, service, or generic lattice framework is justified.
- A failed killing slice leaves only durable method evidence and fixtures; it
  does not leave a public scaffold or open optimized/runtime follow-ups.

## Backends

- Backend axis: deterministic `cpu_reference` only after the killing gate
  passes. MST is a comparison baseline, not canonical truth. No optimized CPU,
  GPU, runtime, or UI backend is owed.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/octree_parity_orientation/`.
- [ ] Fill `method.yaml` (`id: geometry.octree_parity_orientation`; initial
      status `planned`; paper block marked in-house/unpublished; metrics:
      `oriented_correct_fraction`, `parity_conflict_rate`,
      `low_confidence_fraction`, `reconstruction_mean_distance`,
      `runtime_ms`). Promote status to `reference` only after the killing gate
      passes and the CPU reference/tests/benchmark exist.
- [ ] Fill `paper.md` with the formulation, the parity-constraint equation, complexity (O(n log n), no linear solve), the operating envelope, and the failure-mode taxonomy.
- [ ] Add a dated prior-art audit and freeze position/spacing/plane-offset
      units, lattice/patch/crossing conventions, outside seeding, confidence
      definition, fixture split, numeric tolerances, caps, and no-novelty
      fallback before prototype implementation.
- [ ] Freeze the killing corpus before execution: analytic sphere, torus,
      nested hollow shell, separated thin double sheet, nearby opposing sheets,
      and open hemisphere; fixed sampling densities, noise-to-spacing levels,
      and at least eight seeded sign scrambles per closed fixture. Reserve at
      least one density/noise combination per fixture as held out.
- [ ] Apply the fixed gate on held-out cases: every clean closed case reaches
      at least `0.99` oriented-correct fraction; sphere/torus regress by no more
      than one percentage point versus MST; on both thin/nested challenge
      classes parity improves over MST by at least ten percentage points or
      reaches `0.99` when MST already does; every run satisfies the frozen
      conflict/resource caps; and every open-hemisphere run returns the
      declared fail-closed status with no successful oriented output.
- [ ] If any required gate fails, record fixture/seed/params/diagnostics,
      preserve the negative result, remove prototype-only promoted surface, and
      retire with no implementation follow-up.

### Public API in `src/geometry`
- [ ] Add module `Geometry.PointCloud.NormalOrientation` (`.cppm` + `.cpp`): an `OrientParams` (position/normal property names; leaf-size factor relative to estimated spacing plus optional explicit leaf size; spacing-estimate k; root-box inflation factor; normal-angle and plane-offset clustering epsilons; conflict-rate fail-closed threshold; max octree depth cap — no RNG/seed: the method itself is fully deterministic, test fixtures own their sign-scramble seeds) and `Orient(...)` overloads for `std::span` positions+normals and for `Cloud&` in-place property update — the `Cloud&` overload also writes per-point confidence as a vertex property (default `v:normal_confidence`, name param-controllable) — returning a `Result` with `Status`, flip mask / oriented normals, per-point confidence, and `Diagnostics` (corner/edge/constraint counts, patch-cluster count, crossing counts, parity conflict rate, per-corner satisfaction summary, flipped count, undecided/low-confidence count, leaf-depth and spacing statistics).
- [ ] Implement the method-local balanced Morton-lattice octree and union–find-with-parity solver in the `.cpp` implementation unit (not exported); fixed processing order; no floating-point corner identity.
- [ ] Deterministic on-plane tie-break: corner-vs-patch-plane classifications within epsilon of zero resolve by one documented consistent rule (e.g. always the positive side) so adjacent edge segments can never double- or zero-count a crossing; the epsilon is a documented param.
- [ ] Deterministic: identical `(input, params)` produce bitwise-identical outputs across runs and thread counts.
- [ ] Fail-closed with explicit statuses on: empty/too-small input, missing or count-mismatched normals, non-finite positions/normals, spacing-estimate failure, depth-cap overflow, and conflict rate above threshold (`OpenSurfaceSuspected`-style status; no oriented output claimed as success).
- [ ] Register the module in `src/geometry/CMakeLists.txt` (single `IntrinsicGeometry` target, alphabetical placement, no new link dependency).

### Benchmarks
- [ ] After the killing gate passes, add executable manifest
      `benchmarks/geometry/manifests/octree_parity_orientation_reference_smoke.yaml`
      with stable ID `geometry.octree_parity_orientation.reference.smoke`,
      built-in deterministic held-out fixtures, `intent: correctness`, fixed
      seeds/densities/noise, explicit warmup/measured counts, and allowed
      metrics `runtime_ms`, `quality_error_l2`, and `quality_error_linf`.
- [ ] Emit schema-valid `cpu_reference` result JSON. Encode orientation and
      reconstruction error in quality metrics; put MST deltas, confidence,
      parity conflicts, lattice/patch/resource counts, fixture/seed, open-scan
      status, and termination diagnostics in the payload.
- [ ] Keep method-local measures (`oriented_correct_fraction`,
      `parity_conflict_rate`, `low_confidence_fraction`, and
      `reconstruction_mean_distance`) in method metadata/result diagnostics;
      do not widen the global benchmark metric allowlist for one consumer.

## Tests
- [ ] `tests/unit/geometry/Test.PointCloudNormalOrientation.cpp` with `unit;geometry` labels.
- [ ] Closed analytic fixtures via `SampleTriangleMeshSurface` (ground-truth normals kept, signs scrambled with a seeded RNG): sphere and torus reach the documented oriented-correct fraction (torus proves around-the-hole lateral propagation); hollow shell (nested cavity) proves parity handles nesting — inner-surface normals point into the cavity; thin plate at documented sheet separation stays correctly oriented on both sides.
- [ ] Baseline comparison: oriented-correct fraction ≥ the MST baseline on the torus and thin-plate fixtures, with deltas recorded in the method README.
- [ ] Open-surface detection: an open hemisphere fixture triggers the conflict-rate fail-closed status rather than returning success.
- [ ] Determinism and all fail-closed input cases listed above.
- [ ] Noise regression: documented correctness bound at a stated noise-to-spacing fraction on the sphere fixture.
- [ ] Assert the frozen eight-seed held-out killing rule exactly, including the
      `0.99`, one-point non-regression, ten-point challenge improvement (or
      `0.99` ceiling), resource/conflict, and open-surface gates.
- [ ] Run and strictly validate the smoke manifest and emitted result after a
      positive killing verdict.

## Docs
- [ ] `methods/geometry/octree_parity_orientation/README.md` with parameter-selection guidance (leaf-size factor, clustering epsilons, conflict threshold) and known limitations (open surfaces, noise approaching spacing, nonmanifold junctions, density starvation).
- [ ] Record the prior-art boundary, frozen killing protocol, all positive or
      negative outcomes, MST comparison, and exact maturity. Do not describe a
      killed prototype as an engine capability.
- [ ] Register the stable benchmark ID/dataset in
      `benchmarks/geometry/README.md`.
- [ ] Regenerate the module inventory (`python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md`).

## Acceptance criteria
- [ ] Slice A freezes the formulation/prior-art/fixture/tolerance/cap contract
      before implementation.
- [ ] Slice B produces a reproducible positive or negative verdict under the
      exact held-out killing rule.
- [ ] After a positive verdict, the CPU reference is present, deterministic,
      and fail-closed per the listed statuses; after a negative verdict, no
      public implementation scaffold remains.
- [ ] Positive path: all correctness tests pass in the default CPU gate,
      including torus, hollow-shell, thin-sheet, and open-surface fail-closed
      cases; every frozen held-out gate passes; and benchmark manifest/result
      validate with quality plus MST/conflict/resource diagnostics.
- [ ] Positive path: `method.yaml` validates and the exported surface uses only
      `std`/`glm`/scalar plus engine-owned point-cloud types. The `Cloud&`
      overload follows the existing `Geometry.PointCloud.Normals` span +
      `Cloud&` pattern; no Morton-lattice/union-find/third-party types escape.
- [ ] Negative path: preserve the validated intake, fixtures, protocol, and
      failure evidence, remove prototype-only promoted surface, make no
      capability/maturity claim, and open no implementation follow-up.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'NormalOrientation|PointCloud|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No optimized/GPU backend before reference parity; no performance claims without baseline comparison.
- No behavior changes to `Geometry.Octree`, `Geometry.PointCloud.Normals`, or the embedded MST orientation path.
- No public module or integration surface before the killing gate passes; no
  tuning thresholds after inspecting held-out results.
- No `std::rand` or global RNG state; seeded determinism only.
- No external datasets in smoke tests or benchmarks.

## Maturity
- A failed killing slice retires with negative evidence and no promoted method
  surface. A positive result targets `CPUContracted`: reference backend,
  analytic/regression tests, and validated correctness smoke under the default
  CPU gate.
- No `Operational` follow-up is owed. Optimized CPU, GPU, or runtime/UI
  integration requires a separately reviewed task after positive evidence.
