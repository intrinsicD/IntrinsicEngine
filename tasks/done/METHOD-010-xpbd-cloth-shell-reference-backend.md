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
- Roadmap source: [`ARCH-002`](ARCH-002-physics-phenomena-roadmap.md).
- Depends conceptually on the particle/spring diagnostics shape from [`METHOD-009`](METHOD-009-particle-spring-reference-backend.md), but may be implemented independently if it duplicates the minimal particle state locally.
- Landed (2026-06-10): `methods/physics/xpbd_cloth_reference/` ships the deterministic XPBD `cpu_reference` backend (predict, per-constraint Lagrange multipliers with compliance, structural unique-edge + opposite-vertex bending distance constraints built deterministically by `BuildClothFromTriangles`, half-space collision projection as method parameters with sphere colliders declared-but-unsupported, position-derived velocities), diagnostics (validation codes incl. `InvalidTopology`, degenerate triangle/constraint counts, stretch/bend residuals max/L2, convergence vs tolerance, kinetic/mechanical energy drift, fail-closed non-finite fallback), 14 `unit;physics` tests, and the `physics.xpbd_cloth_reference.smoke` benchmark (pinned hanging 3x3 patch; final max stretch residual as quality metric, observed ~6.2e-4 vs 5e-3 threshold).
- Completed: 2026-06-10.
- PR/commit: branch `claude/pensive-albattani-pu2t14` (pending local commit).

## Maturity

- Closes at `CPUContracted`: the reference backend is the correctness oracle, covered by the default CPU gate and a validated smoke benchmark.
- Optimized CPU/GPU backends are explicitly forbidden by this task; per the methods/physics roadmap they require a future task naming this package as the oracle, so no `Operational` follow-up is owed by this task.

## Required changes
- [x] Create `methods/physics/xpbd_cloth_reference/` from the method template.
- [x] Fill `method.yaml`, `paper.md`, and `README.md` with backend identity `cpu_reference`, constraint equations, inputs/outputs, diagnostics, and limitations.
- [x] Define cloth state over triangle meshes: particle positions/velocities, inverse masses, pinned vertices, structural constraints, bending or dihedral constraints, and timestep/iteration parameters.
- [x] Define collision-query inputs as method parameters only; do not import runtime, ECS, or graphics.
- [x] Report diagnostics for invalid topology, degenerate triangles, constraint residuals, non-convergence, energy drift, and unsupported collision inputs.
- [x] Add a smoke benchmark manifest with quality metrics such as residual stretch/bend error and deterministic step count.

## Tests
- [x] Add pinned patch stretch and bend fixtures with deterministic outputs.
- [x] Add degenerate mesh/topology diagnostics tests.
- [x] Add repeated-step determinism tests.
- [x] Validate benchmark manifests if any are added.

## Docs
- [x] Update `methods/physics/README.md` with package status once implemented.
- [x] Update architecture docs only when a later task promotes runtime/engine integration.

## Acceptance criteria
- [x] CPU reference backend defines the parity oracle for future cloth/shell optimization.
- [x] Diagnostics identify topology errors, constraint residuals, and non-convergence.
- [x] No GPU/optimized backend task is opened before reference parity exists.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Cloth|XPBD|Physics' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding GPU or optimized CPU backends.
- Adding runtime/ECS/graphics integration.
- Treating rendered cloth as correctness evidence.
