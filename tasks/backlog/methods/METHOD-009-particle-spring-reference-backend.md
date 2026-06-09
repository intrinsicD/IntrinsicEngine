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
- Roadmap source: [`ARCH-002`](../../done/ARCH-002-physics-phenomena-roadmap.md).
- This is the first non-rigid physics method package because particles and springs provide the smallest deterministic state model that later XPBD cloth and soft-body work can reuse as fixtures.

## Required changes
- [ ] Create `methods/physics/particle_spring_reference/` from the method template.
- [ ] Fill `method.yaml`, `paper.md`, and `README.md` with backend identity `cpu_reference`, units, input/output records, diagnostics, and limitations.
- [ ] Define particle state, inverse masses, spring records, force parameters, fixed timestep, and deterministic integrator policy.
- [ ] Report diagnostics for invalid particles/springs, non-finite state, residual spring error, energy drift, and stability/fallback status.
- [ ] Add a smoke benchmark manifest with a stable `benchmark_id` and quality metrics, not runtime-only metrics.

## Tests
- [ ] Add analytic two-particle spring fixtures with expected rest-length behavior.
- [ ] Add pinned-particle and invalid-input tests.
- [ ] Add deterministic repeated-step regression coverage.
- [ ] Validate benchmark manifests if any are added.

## Docs
- [ ] Update `methods/physics/README.md` with package status once implemented.
- [ ] Update `docs/methods/index.md` if this becomes the next physics pathfinder package.

## Acceptance criteria
- [ ] CPU reference backend is the correctness oracle for future particle/spring optimized or GPU work.
- [ ] Diagnostics distinguish invalid inputs, instability, and convergence/residual quality.
- [ ] No GPU/optimized backend task is opened from this task before reference parity exists.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Particle|Spring|Physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding GPU or optimized CPU backends.
- Adding runtime/ECS/graphics integration.
- Treating visual output as correctness evidence.
