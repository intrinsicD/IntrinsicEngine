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
- Roadmap source: [`ARCH-002`](../../done/ARCH-002-physics-phenomena-roadmap.md).
- SPH is ranked after particle/spring and cloth because it has higher engine value but stricter stability and neighbor-search diagnostics.

## Required changes
- [ ] Create `methods/physics/sph_fluid_reference/` from the method template.
- [ ] Fill `method.yaml`, `paper.md`, and `README.md` with backend identity `cpu_reference`, kernel equations, units, inputs/outputs, diagnostics, and limitations.
- [ ] Define particle state, smoothing length, mass/density/pressure/viscosity parameters, boundary handling, timestep policy, and deterministic neighbor ordering.
- [ ] Report diagnostics for invalid particles, non-finite state, density error, pressure residuals or incompressibility proxy, neighbor overflow, and stability/fallback status.
- [ ] Add a smoke benchmark manifest with quality metrics such as density error and conservation drift.

## Tests
- [ ] Add small static-neighborhood kernel normalization fixtures.
- [ ] Add toy dam-break or fluid-column smoke fixtures with deterministic diagnostics.
- [ ] Add invalid input and repeated-step determinism tests.
- [ ] Validate benchmark manifests if any are added.

## Docs
- [ ] Update `methods/physics/README.md` with package status once implemented.
- [ ] Update architecture docs only when a later task promotes runtime/engine integration.

## Acceptance criteria
- [ ] CPU reference backend defines the parity oracle for future SPH optimization or GPU work.
- [ ] Diagnostics report stability and density/conservation quality, not just runtime.
- [ ] No GPU/optimized backend task is opened before reference parity exists.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SPH|Fluid|Physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding GPU or optimized CPU backends.
- Adding runtime/ECS/graphics integration.
- Treating rendered particles as correctness evidence.
