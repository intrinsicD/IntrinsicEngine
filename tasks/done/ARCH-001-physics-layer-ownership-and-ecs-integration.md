# ARCH-001 — Define physics layer ownership and ECS integration

## Status

- Status: complete 2026-06-05.
- Maturity: `Retired` for the architecture decision. No solver/runtime implementation is claimed in this task.
- Commit/PR: this task retirement commit.
- Decision: [ADR-0019](../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md) accepts `src/physics` as the future physics-world layer with `physics -> core, geometry` dependencies.

## Goal
- Decide and document where rigid-body dynamics and other physical simulation systems live, and how they integrate with ECS/runtime without violating layer boundaries.

## Non-goals
- No rigid-body solver implementation.
- No soft-body, fluid, cloth, FEM, particle, or GPU simulation implementation.
- No `src/physics` source code in this slice; only the canonical source-layout contract is accepted.

## Context
- Owner/layer: architecture planning across `core`, `geometry`, `ecs`, `physics`, `runtime`, and `methods`.
- Convergence: anchor of Theme C - Physics readiness in [`tasks/backlog/README.md`](../backlog/README.md). This decision gates [`ecs/HARDEN-064`](../backlog/ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) and [`methods/METHOD-001`](../backlog/methods/METHOD-001-rigid-body-dynamics-reference-backend.md).
- `AGENTS.md` now includes `src/physics/` in the target source layout and records the `physics -> core, geometry` dependency edge.
- Geometry already provides collision/math building blocks such as `Geometry.GJK`, `Geometry.EPA`, `Geometry.ContactManifold`, `Geometry.Overlap`, `Geometry.SDF`, and `Geometry.SDFContact`.
- `docs/architecture/patterns.md` already sketches a `PhysicsWorld` owner and `PhysicsTickJob` worker split, but it is only an architectural pattern example.
- ECS holds authoritative scene intent/components; runtime owns composition/wiring; physics simulation state avoids back-importing ECS, runtime, graphics, platform, app, assets services, or method packages.
- Collider and rigid-body authoring are separate concerns: collider components describe collision shape/material/filtering/trigger intent, while rigid-body components describe static/kinematic/dynamic motion, mass, velocity, damping, sleep, and CCD intent.
- Compound colliders are explicit collider-owned child-shape descriptors with local poses; ECS scene hierarchy must not implicitly define physics compound shape topology.

## Required changes
- [x] Write an architecture note or ADR deciding whether to introduce `src/physics/` and its allowed dependency edges.
- [x] Define the split between:
  - [x] ECS physics components: body descriptors, collider references, material IDs, simulation intent.
  - [x] Physics world/state: solver islands, broadphase, contacts, constraints, integration caches.
  - [x] Runtime bridge: fixed-step scheduling, ECS-to-physics synchronization, physics-to-ECS transform writeback.
  - [x] Methods packages: reference algorithms, parity baselines, diagnostics, and papers.
- [x] Decide the canonical collider/body ownership contract before implementation, using [`tasks/backlog/ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md`](../backlog/ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) as the ECS authoring follow-up and [`tasks/backlog/methods/METHOD-001-rigid-body-dynamics-reference-backend.md`](../backlog/methods/METHOD-001-rigid-body-dynamics-reference-backend.md) as the CPU reference dynamics follow-up.
- [x] Define first-phase collider shape policy: sphere, capsule, and box/OBB as dynamic-capable primitives; convex hull and static/kinematic triangle mesh as later or separately gated work; dynamic concave triangle mesh only via future convex decomposition or specialized method work.
- [x] Define transform/scale policy for physics: body pose from ECS transform, collider local pose per shape, uniform scale policy for simple primitives, and diagnostics/rejection for unsupported non-uniform dynamic scaling.
- [x] Decide whether physics can depend on `geometry` directly and whether ECS may store geometry collider primitives or only handles/descriptors.
- [x] Define deterministic fixed-step policy, units, coordinate convention, sleep/island policy, collision filtering, event ownership, and diagnostics/reporting expectations.
- [x] Add follow-up implementation tasks for ECS-to-physics runtime sync, rigid-body world/state, collision broadphase/narrowphase, constraints, and non-rigid phenomena after the layer decision.

## Tests
- [x] Add/update architecture/contract tests only if source layout or dependency checks change.
- [x] If a `src/physics` layer is approved, update layering tooling and add a failing-first contract test that prevents physics from importing runtime/graphics/platform/app.

## Docs
- [x] Update `AGENTS.md` only if the canonical source layout or dependency invariant table changes.
- [x] Update `docs/architecture/` with the physics layer contract and ECS/runtime integration diagram.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` only if promoted source roots or retirement gates change.

## Acceptance criteria
- [x] The repository has an explicit, reviewed answer for where rigid-body dynamics and other physics systems belong.
- [x] ECS, runtime, geometry, methods, and any new physics layer each have clear ownership and dependency constraints.
- [x] Collider descriptors, rigid-body descriptors, runtime sync, and physics-world solver state have separate ownership and no handle leakage into ECS.
- [x] Follow-up tasks can implement physics without inventing cross-layer shortcuts.

## Verification
```bash
python3 tests/regression/tooling/Test.CheckLayering.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/repo/check_pr_contract.py
```

## Forbidden changes
- Adding `src/physics` code in this architecture decision task.
- Letting physics systems mutate graphics state or own runtime composition.
- Treating geometry collision algorithms as a full dynamics engine without a physics ownership boundary.
