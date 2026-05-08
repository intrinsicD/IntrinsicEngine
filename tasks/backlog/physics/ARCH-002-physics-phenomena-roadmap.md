# ARCH-002 — Physics phenomena roadmap and method selection

## Goal
- Build a prioritized roadmap for physical phenomena beyond rigid bodies, with ownership, method selection criteria, and verification expectations.

## Non-goals
- No implementation of soft bodies, fluids, cloth, FEM, particles, fracture, thermal, acoustic, or electromagnetic simulation.
- No source-layout changes; those belong to `ARCH-001`.
- No performance claims or backend commitments.

## Context
- Owner/layer: architecture planning with method workflow implications.
- `methods/physics/README.md` currently reserves space for simulation and dynamics methods but has no concrete packages.
- IntrinsicEngine’s geometry algorithms and rendering systems can support visualization and collision queries, but each physical domain needs an explicit numerical method contract before engine integration.
- The method protocol requires CPU reference parity, tests, benchmark manifests, optimized CPU later, and GPU only after reference parity.

## Required changes
- Inventory candidate phenomena and classify likely implementation path:
  - Rigid bodies and articulated constraints.
  - Particles and mass-spring systems.
  - Cloth and shells.
  - Soft bodies/FEM/PBD/XPBD.
  - Fluids: SPH, grid-based incompressible flow, FLIP/APIC, shallow water.
  - Fracture/damage and contact-rich granular materials.
  - Thermal diffusion or other scalar/vector PDE fields.
- For each phenomenon, record required data model, coupling to ECS/runtime, geometry prerequisites, expected diagnostics, benchmark shape, and visualization hooks.
- Rank the first three method packages by engine value, complexity, and verification tractability.
- Define which phenomena are research-method packages only versus candidates for engine runtime systems.
- Create follow-up `METHOD-*`, `ARCH-*`, or `HARDEN-*` tasks for the top-ranked items.

## Tests
- No code tests required unless tooling/docs change.
- Validate task policy and documentation links.

## Docs
- Update `methods/physics/README.md` with the roadmap and selection criteria.
- Add a docs page under `docs/methods/` or `docs/architecture/` if the roadmap affects engine architecture.

## Acceptance criteria
- The repository has a factual physics roadmap separating rigid-body dynamics from other phenomena.
- Each candidate phenomenon has an explicit reference-first verification path.
- No phenomenon is approved for GPU/backend optimization before a CPU reference plan exists.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding simulation code in this roadmap task.
- Treating rendering visualizations as physics correctness evidence.
- Adding GPU or optimized-backend tasks without a corresponding CPU reference task.

