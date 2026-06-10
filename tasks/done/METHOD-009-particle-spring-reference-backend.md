---
id: METHOD-009
theme: C
depends_on: [ARCH-002]
---
# METHOD-009 — Particle and mass-spring reference backend

## Goal
- Add a deterministic CPU reference backend for particle dynamics and mass-spring systems suitable for later cloth, soft-body, and particle-runtime validation.

## Non-goals
- No GPU backend.
- No optimized CPU backend before reference parity tests and benchmark manifests pass.
- No runtime particle system, ECS component, renderer pass, or editor UI.
- No cloth-specific bending/area model beyond simple spring fixtures.

## Context
- Owning area: `methods/physics` method package, with future `src/physics` integration only through separate tasks.
- Roadmap source: [`ARCH-002`](ARCH-002-physics-phenomena-roadmap.md).
- This is the first non-rigid physics method package because particles and springs provide the smallest deterministic state model that later XPBD cloth and soft-body work can reuse as fixtures.
- Landed (2026-06-10): `methods/physics/particle_spring_reference/` ships the deterministic `cpu_reference` backend (semi-implicit Euler; Hooke springs with axial damping; pinning via zero inverse mass; global drag), machine-checkable diagnostics (validation codes, pinned/degenerate counts, post-step spring residuals max/L2, kinetic/total energy drift, `omega*dt` stability ratio with limit flag, non-finite fail-closed fallback), 12 `unit;physics` correctness tests in `tests/unit/physics/Test.ParticleSpringReference.cpp`, and the `physics.particle_spring_reference.smoke` benchmark (manifest + `IntrinsicBenchmarkSmoke` workload with exact momentum/center-of-mass conservation as the quality metric).
- Completed: 2026-06-10.
- PR/commit: branch `claude/pensive-albattani-pu2t14` (pending local commit).

## Maturity

- Closes at `CPUContracted`: the reference backend is the correctness oracle, covered by the default CPU gate and a validated smoke benchmark.
- Optimized CPU/GPU backends are explicitly forbidden by this task; per the methods/physics roadmap they require a future task naming this package as the oracle, so no `Operational` follow-up is owed by this task.

## Required changes
- [x] Create `methods/physics/particle_spring_reference/` from the method template.
- [x] Fill `method.yaml`, `paper.md`, and `README.md` with backend identity `cpu_reference`, units, input/output records, diagnostics, and limitations.
- [x] Define particle state, inverse masses, spring records, force parameters, fixed timestep, and deterministic integrator policy.
- [x] Report diagnostics for invalid particles/springs, non-finite state, residual spring error, energy drift, and stability/fallback status.
- [x] Add a smoke benchmark manifest with a stable `benchmark_id` and quality metrics, not runtime-only metrics.

## Tests
- [x] Add analytic two-particle spring fixtures with expected rest-length behavior.
- [x] Add pinned-particle and invalid-input tests.
- [x] Add deterministic repeated-step regression coverage.
- [x] Validate benchmark manifests if any are added.

## Docs
- [x] Update `methods/physics/README.md` with package status once implemented.
- [x] Update `docs/methods/index.md` if this becomes the next physics pathfinder package.

## Acceptance criteria
- [x] CPU reference backend is the correctness oracle for future particle/spring optimized or GPU work.
- [x] Diagnostics distinguish invalid inputs, instability, and convergence/residual quality.
- [x] No GPU/optimized backend task is opened from this task before reference parity exists.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Particle|Spring|Physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

(The manifest validator takes the benchmarks root, not the repo root; the
command above reflects the CI invocation actually run.)

## Forbidden changes
- Adding GPU or optimized CPU backends.
- Adding runtime/ECS/graphics integration.
- Treating visual output as correctness evidence.
