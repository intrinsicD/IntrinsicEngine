---
id: METHOD-010
theme: C
depends_on: [ARCH-002]
---
# METHOD-010 — XPBD cloth and shell reference backend

## Goal
- Add a deterministic CPU reference backend for cloth and thin-shell simulation using XPBD-style constraints over triangle mesh state.

## Non-goals
- No GPU backend.
- No optimized CPU backend before CPU reference parity and benchmark manifests pass.
- No runtime cloth component, editor tool, renderer pass, or collision event routing.
- No volumetric soft-body FEM; this task is cloth/shell only.

## Context
- Owning area: `methods/physics` method package, with any engine-runtime integration deferred to later `physics`/`runtime` tasks.
- Roadmap source: [`ARCH-002`](../../done/ARCH-002-physics-phenomena-roadmap.md).
- Depends conceptually on the particle/spring diagnostics shape from [`METHOD-009`](../../done/METHOD-009-particle-spring-reference-backend.md), but may be implemented independently if it duplicates the minimal particle state locally.

## Required changes
- [ ] Create `methods/physics/xpbd_cloth_reference/` from the method template.
- [ ] Fill `method.yaml`, `paper.md`, and `README.md` with backend identity `cpu_reference`, constraint equations, inputs/outputs, diagnostics, and limitations.
- [ ] Define cloth state over triangle meshes: particle positions/velocities, inverse masses, pinned vertices, structural constraints, bending or dihedral constraints, and timestep/iteration parameters.
- [ ] Define collision-query inputs as method parameters only; do not import runtime, ECS, or graphics.
- [ ] Report diagnostics for invalid topology, degenerate triangles, constraint residuals, non-convergence, energy drift, and unsupported collision inputs.
- [ ] Add a smoke benchmark manifest with quality metrics such as residual stretch/bend error and deterministic step count.

## Tests
- [ ] Add pinned patch stretch and bend fixtures with deterministic outputs.
- [ ] Add degenerate mesh/topology diagnostics tests.
- [ ] Add repeated-step determinism tests.
- [ ] Validate benchmark manifests if any are added.

## Docs
- [ ] Update `methods/physics/README.md` with package status once implemented.
- [ ] Update architecture docs only when a later task promotes runtime/engine integration.

## Acceptance criteria
- [ ] CPU reference backend defines the parity oracle for future cloth/shell optimization.
- [ ] Diagnostics identify topology errors, constraint residuals, and non-convergence.
- [ ] No GPU/optimized backend task is opened before reference parity exists.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Cloth|XPBD|Physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding GPU or optimized CPU backends.
- Adding runtime/ECS/graphics integration.
- Treating rendered cloth as correctness evidence.
