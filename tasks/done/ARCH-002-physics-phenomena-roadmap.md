# ARCH-002 — Physics phenomena roadmap and method selection

- Status: completed / retired.
- Completion date: 2026-06-06.
- Maturity: `Scaffolded` planning roadmap; implementation remains in follow-up tasks.

## Goal
- Build a prioritized roadmap for physical phenomena beyond rigid bodies, with ownership, method selection criteria, and verification expectations.

## Non-goals
- No implementation of soft bodies, fluids, cloth, FEM, particles, fracture, thermal, acoustic, or electromagnetic simulation.
- No source-layout changes; those belong to `ARCH-001`.
- No performance claims or backend commitments.

## Context
- Owner/layer: architecture planning with method workflow implications.
- `methods/physics/README.md` now records concrete package ordering and selection criteria.
- IntrinsicEngine's geometry algorithms and rendering systems can support visualization and collision queries, but each physical domain needs an explicit numerical method contract before engine integration.
- The method protocol requires CPU reference parity, tests, benchmark manifests, optimized CPU later, and GPU only after reference parity.

## Required changes
- [x] Inventory candidate phenomena and classify likely implementation path:
  - [x] Rigid bodies and articulated constraints.
  - [x] Particles and mass-spring systems.
  - [x] Cloth and shells.
  - [x] Soft bodies/FEM/PBD/XPBD.
  - [x] Fluids: SPH, grid-based incompressible flow, FLIP/APIC, shallow water.
  - [x] Fracture/damage and contact-rich granular materials.
  - [x] Thermal diffusion or other scalar/vector PDE fields.
- [x] For each phenomenon, record required data model, coupling to ECS/runtime, geometry prerequisites, expected diagnostics, benchmark shape, and visualization hooks.
- [x] Rank the first three method packages by engine value, complexity, and verification tractability.
- [x] Define which phenomena are research-method packages only versus candidates for engine runtime systems.
- [x] Create follow-up `METHOD-*`, `ARCH-*`, or `HARDEN-*` tasks for the top-ranked items.

## Follow-up Tasks
- [`METHOD-009`](../backlog/methods/METHOD-009-particle-spring-reference-backend.md) — particle and mass-spring CPU reference backend.
- [`METHOD-010`](../backlog/methods/METHOD-010-xpbd-cloth-shell-reference-backend.md) — XPBD cloth and shell CPU reference backend.
- [`METHOD-011`](../backlog/methods/METHOD-011-sph-fluid-reference-backend.md) — SPH fluid CPU reference backend.
- [`PHYSICS-002`](PHYSICS-002-collision-broadphase-narrowphase-contract.md) and [`PHYSICS-003`](../backlog/physics/PHYSICS-003-constraints-islands-and-solver-diagnostics.md) remain the engine-side rigid-body collision/solver follow-ups.

## Tests
- [x] No code tests required unless tooling/docs change.
- [x] Validate task policy and documentation links.

## Docs
- [x] Update [`methods/physics/README.md`](../../methods/physics/README.md) with the roadmap and selection criteria.
- [x] Update [`docs/architecture/physics.md`](../../docs/architecture/physics.md) and [`docs/methods/index.md`](../../docs/methods/index.md) with roadmap pointers.

## Acceptance criteria
- [x] The repository has a factual physics roadmap separating rigid-body dynamics from other phenomena.
- [x] Each candidate phenomenon has an explicit reference-first verification path.
- [x] No phenomenon is approved for GPU/backend optimization before a CPU reference plan exists.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
```

## Completion
- Completed: 2026-06-06.
- Commit reference: this task-retirement commit.
- Notes: this is a planning-only roadmap. It opens method-package tasks but no GPU, optimized backend, runtime, ECS, or solver implementation work.

## Forbidden changes
- Adding simulation code in this roadmap task.
- Treating rendering visualizations as physics correctness evidence.
- Adding GPU or optimized-backend tasks without a corresponding CPU reference task.
