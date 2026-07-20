---
id: METHOD-034
theme: I
depends_on: [METHOD-033]
maturity_target: CPUContracted
---
# METHOD-034 — iPSR normal orientation baseline (reference backend)

## Goal
- Add a CPU reference implementation of Iterative Poisson Surface Reconstruction (iPSR) as a modern competitor baseline for the `METHOD-032` publication track: initialize normals with seeded random directions, then iterate — screened Poisson reconstruction from the current normals, transfer of reconstructed-surface normals back to the input points via nearest faces — until the per-iteration flip fraction drops below a threshold or the iteration cap is reached. Output oriented normals plus convergence diagnostics.

## Non-goals
- Not a replacement for the engine's default orientation paths — this is comparison infrastructure; engine-facing selection stays out of scope.
- No optimized CPU or GPU backend; no neural components.
- No new reconstruction work — the inner solver is `METHOD-033`'s `Geometry.SurfaceReconstruction.Poisson`, consumed as-is.
- The cross-method comparison protocol and report are owned by `METHOD-036`, not this task.

## Context
- Paper/method: Hou, Wang, Bao, et al. — "Iterative Poisson Surface Reconstruction (iPSR) for Unoriented Points", SIGGRAPH 2022.
- Method package: `methods/geometry/ipsr/`; implementation is package-local (`include/` + `src/`, the `progressive_poisson` pattern) — a research baseline does not warrant a `src/geometry` module surface.
- Reuse: `Geometry.SurfaceReconstruction.Poisson` (`METHOD-033`) for the inner solve; `Geometry.KDTree` for point-to-face normal transfer; `Geometry.PointCloud.SurfaceSampling` for fixtures.
- Seeding: iPSR legitimately requires an RNG for the initial normals; the seed is an explicit param, and the `METHOD-036` comparison protocol pins it.
- Metrics use the same names as `METHOD-032` (`oriented_correct_fraction`, `runtime_ms`) so results are directly comparable, plus `iterations` and `final_flip_fraction`.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/ipsr/`; fill `method.yaml` (`id: geometry.ipsr`; status `reference`; metrics above; paper block) and `paper.md`.
- [ ] Package-local reference implementation: params (seed, max iterations, flip-fraction convergence threshold, inner Poisson params passthrough), result (oriented normals, per-iteration flip-fraction history, converged flag, status), diagnostics assembly.
- [ ] Deterministic given `(input, params, seed)`: fixed iteration order and transfer tie-breaking; bitwise-identical outputs across runs.
- [ ] Fail-closed with explicit statuses: empty/too-small input, non-finite data, inner reconstruction failure (propagated, not swallowed), iteration cap reached without convergence (distinct non-success status).
- [ ] Smoke benchmark manifest; extend the benchmark metric schema in the same change (`ALLOWED_METRICS` in `tools/benchmark/validate_benchmark_manifests.py` plus `docs/benchmarking/metrics.md` / `benchmark-manifest-schema.md`) with any of this task's declared metrics not yet allowed (`oriented_correct_fraction`, `iterations`, `final_flip_fraction`).

## Tests
- [ ] `tests/unit/geometry/Test.IPSROrientationBaseline.cpp` with `unit;geometry` labels.
- [ ] Scrambled-sign sphere and torus fixtures reach a documented `oriented_correct_fraction` bound within the iteration cap.
- [ ] Determinism given a fixed seed; cap-without-convergence returns its distinct status; fail-closed cases above.

## Docs
- [ ] `methods/geometry/ipsr/README.md` — parameter guidance (seed, iteration cap, inner grid resolution) and known limitations (cost per iteration, sensitivity to inner reconstruction quality on sparse/noisy input).

## Acceptance criteria
- [ ] Reference implementation present and tested in the default CPU gate.
- [ ] `method.yaml` validates; benchmark smoke manifest validates and runs with quality metrics shared with `METHOD-032`.
- [ ] Public surface type discipline: `std`/`glm`/scalar plus engine point-cloud types; nothing method-internal exported.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'IPSR' --timeout 300
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No modifications to `METHOD-033`'s solver beyond consuming its public API.
- No optimized/GPU work; no performance claims without baseline comparison.
- No external datasets in smoke tests.

## Maturity
- Target: `CPUContracted` (baseline-grade reference for the comparison protocol).
- No `Operational` follow-up is owed — this baseline exists for `METHOD-036` evidence, not for engine promotion.
