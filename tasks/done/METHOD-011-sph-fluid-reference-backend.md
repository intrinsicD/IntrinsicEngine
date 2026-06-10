---
id: METHOD-011
theme: C
depends_on: [ARCH-002]
---
# METHOD-011 — SPH fluid reference backend

## Goal
- Add a deterministic CPU reference backend for particle-based SPH fluid simulation with explicit diagnostics and benchmark-quality metrics.

## Non-goals
- No GPU backend.
- No optimized CPU backend before CPU reference parity and benchmark manifests pass.
- No runtime fluid system, ECS component, renderer pass, or editor UI.
- No grid-based FLIP/APIC, pressure-projection grid solver, shallow-water solver, or multiphase material model.

## Context
- Owning area: `methods/physics` method package, with future engine integration deferred to separate `physics`/`runtime` tasks.
- Roadmap source: [`ARCH-002`](ARCH-002-physics-phenomena-roadmap.md).
- SPH is ranked after particle/spring and cloth because it has higher engine value but stricter stability and neighbor-search diagnostics.
- Landed (2026-06-10): `methods/physics/sph_fluid_reference/` ships the deterministic WCSPH `cpu_reference` backend (Mueller 2003: Poly6 density with self-contribution, clamped ideal-gas pressure, symmetric Spiky-gradient pressure force, viscosity-Laplacian force, semi-implicit Euler, half-space boundary planes with restitution-scaled reflection, deterministic O(N^2) index-ordered neighbors with advisory `MaxNeighborLimit` overflow reporting), diagnostics (validation codes, total mass, density stats with `MaxCompression` incompressibility proxy and mean relative density error, neighbor counts, max velocity, kinetic energy drift, non-finite fail-closed fallback), 13 `unit;physics` tests (kernel closed forms + numeric normalization, uniform-grid density recovery, exact symmetric-pair momentum conservation, viscosity smoothing, free-fall closed form, toy column drop, overflow reporting, invalid inputs, determinism, fallback), and the `physics.sph_fluid_reference.smoke` benchmark (static 5^3-grid interior density error ~0.0098 vs 0.05 threshold as quality metric plus dynamic toy-column stability diagnostics).
- Completed: 2026-06-10.
- PR/commit: branch `claude/pensive-albattani-pu2t14` (pending local commit).

## Maturity

- Closes at `CPUContracted`: the reference backend is the correctness oracle, covered by the default CPU gate and a validated smoke benchmark.
- Optimized CPU/GPU backends are explicitly forbidden by this task; per the methods/physics roadmap they require a future task naming this package as the oracle, so no `Operational` follow-up is owed by this task.

## Required changes
- [x] Create `methods/physics/sph_fluid_reference/` from the method template.
- [x] Fill `method.yaml`, `paper.md`, and `README.md` with backend identity `cpu_reference`, kernel equations, units, inputs/outputs, diagnostics, and limitations.
- [x] Define particle state, smoothing length, mass/density/pressure/viscosity parameters, boundary handling, timestep policy, and deterministic neighbor ordering.
- [x] Report diagnostics for invalid particles, non-finite state, density error, pressure residuals or incompressibility proxy, neighbor overflow, and stability/fallback status.
- [x] Add a smoke benchmark manifest with quality metrics such as density error and conservation drift.

## Tests
- [x] Add small static-neighborhood kernel normalization fixtures.
- [x] Add toy dam-break or fluid-column smoke fixtures with deterministic diagnostics.
- [x] Add invalid input and repeated-step determinism tests.
- [x] Validate benchmark manifests if any are added.

## Docs
- [x] Update `methods/physics/README.md` with package status once implemented.
- [x] Update architecture docs only when a later task promotes runtime/engine integration.

## Acceptance criteria
- [x] CPU reference backend defines the parity oracle for future SPH optimization or GPU work.
- [x] Diagnostics report stability and density/conservation quality, not just runtime.
- [x] No GPU/optimized backend task is opened before reference parity exists.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SPH|Fluid|Physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding GPU or optimized CPU backends.
- Adding runtime/ECS/graphics integration.
- Treating rendered particles as correctness evidence.
