---
id: METHOD-025
theme: I
depends_on: [METHOD-021, METHOD-022]
maturity_target: Operational
---
# METHOD-025 — Parameterization family optimized CPU backend and comparison benchmark

## Goal
- Add a `cpu_optimized` backend for the iterative parameterization strategies (ARAP, SLIM) that reaches the same solution as the reference within a documented tolerance but far faster, using progressive-parameterization acceleration, and prove the speedup with a reference-vs-optimized comparison benchmark — the first backend that may make a performance claim, and only against a measured baseline.

## Non-goals
- No new strategy or algorithm change — this backend must converge to the same map the `METHOD-021`/`METHOD-022` reference produces (within tolerance); it changes *how fast*, not *what*.
- No GPU backend — that is `METHOD-026`; this task is CPU-only.
- No acceleration of the linear strategies (LSCM/SCP/BFF) — those are single-solve and already fast; the optimized backend targets the iterative local/global family.

## Context
- Paper/method: Liu, Ye, Chai, Zhao, Wang & Liu, "Progressive Parameterizations", ACM TOG 37(4), SIGGRAPH 2018 — a progressive penalty / reference-update scheme that dramatically reduces the iteration count of ARAP/SLIM-style local/global solves while preserving the target energy, plus optional Anderson acceleration of the fixed-point iteration.
- Owner/layer: `src/geometry` optimized path behind the same `Geometry.Parameterization` surface (`Backend` selects `cpu_reference` vs `cpu_optimized`); the reference bodies from `METHOD-021`/`METHOD-022` stay the parity oracle. Reuses the `GEOM-064` kernels (local step, proxy, injective line search) — the optimized backend changes the schedule, not the primitives.
- Backend policy: per `docs/methods/backend-policy.md` and `docs/architecture/algorithm-variant-dispatch.md`, the result must report `RequestedBackend`/`ActualBackend`; `cpu_optimized` is an explicit token distinct from `cpu_reference`.
- Benchmark policy: per the benchmark workflow, a speedup claim requires a baseline comparison on declared fixtures; the comparison benchmark records reference and optimized runtime and the parity delta.

## Control surfaces
- Config/UI/Agent: none new — `cpu_optimized` is selectable through the existing `Backend`/policy token the `RUNTIME-176` config lane and `UI-036` panel already expose; this task makes that token do real work for the iterative strategies.

## Backends
- Backend axis: adds `cpu_optimized` with parity to `cpu_reference`; `gpu_vulkan_compute` deferred to `METHOD-026`.

## Required changes
- [ ] Add the progressive/accelerated optimized path for the `Arap` and `Slim` strategies behind the `Backend` axis, reusing `GEOM-064` primitives; keep the reference path unchanged and selectable.
- [ ] Parity: on the shared fixtures the optimized result matches the reference symmetric-Dirichlet energy and UVs within a documented tolerance, and preserves injectivity for SLIM.
- [ ] Deterministic: identical `(mesh, params, backend)` produce bitwise-identical output across runs and thread counts.
- [ ] Report `ActualBackend == cpu_optimized` (and honest fallback to `cpu_reference` when the optimized path declines an input, e.g. a mesh below a size threshold).

## Tests
- [ ] Extend `tests/unit/geometry/Test.ArapParameterization.cpp` and `Test.SlimParameterization.cpp` (`unit;geometry`) with `cpu_optimized` parity cases: same energy/UVs within tolerance, SLIM injectivity preserved, backend telemetry asserted.
- [ ] Determinism of the optimized path.
- [ ] Fewer iterations: the optimized path reports strictly fewer local/global iterations than the reference to reach the same energy tolerance on a standard fixture (recorded in diagnostics, not a wall-clock assertion in the unit test).

## Docs
- [ ] Comparison benchmark manifest `benchmarks/geometry/manifests/parameterization_optimized_vs_reference_smoke.yaml` (`benchmark_id: geometry.parameterization.optimized_vs_reference.smoke`) recording reference and optimized `runtime_ms` and `quality_error_l2` on deterministic fixtures; the speedup is reported from this baseline, never asserted without it.
- [ ] Update the ARAP/SLIM method READMEs' backend-status tables (`cpu_optimized` → `METHOD-025`) and note the progressive-acceleration limitations (crossover size, tolerance).
- [ ] Update `docs/methods/index.md` and the parameterization roadmap optimized-backend note.

## Acceptance criteria
- [ ] `cpu_optimized` produces reference-parity maps for ARAP and SLIM within tolerance, with honest backend telemetry and preserved SLIM injectivity.
- [ ] The comparison benchmark validates, runs, and records reference-vs-optimized runtime and parity; any speedup statement cites it.
- [ ] Default CPU gate passes; layering holds (`geometry -> core`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ArapParameterization|SlimParameterization|Parameterization' --timeout 120
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No solution change versus the reference beyond documented tolerance; no performance claim without the comparison benchmark baseline.
- No GPU work in this task; no `std::rand` or global RNG state.

## Maturity
- Target: `Operational` (CPU) — the optimized backend runs behind the `Backend` axis with parity tests and a baseline benchmark. `gpu_vulkan_compute` is owned by `METHOD-026`.
