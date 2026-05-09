# ARCH-001 — Define physics layer ownership and ECS integration

## Goal
- Decide and document where rigid-body dynamics and other physical simulation systems live, and how they integrate with ECS/runtime without violating layer boundaries.

## Non-goals
- No rigid-body solver implementation.
- No soft-body, fluid, cloth, FEM, particle, or GPU simulation implementation.
- No changes to the canonical source layout before the architecture decision is recorded.

## Context
- Owner/layer: architecture planning across `core`, `geometry`, `ecs`, `runtime`, `methods`, and potential `physics` ownership.
- Convergence: anchor of **Theme C — Physics readiness** in [`tasks/backlog/README.md`](../README.md). This decision gates [`ecs/HARDEN-064`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) and [`methods/METHOD-001`](../methods/METHOD-001-rigid-body-dynamics-reference-backend.md).
- `AGENTS.md` target source layout currently does not include `src/physics`, so adding that layer requires a documented architecture/source-layout update.
- Geometry already provides collision/math building blocks such as `Geometry.GJK`, `Geometry.EPA`, `Geometry.ContactManifold`, `Geometry.Overlap`, `Geometry.SDF`, and `Geometry.SDFContact`.
- `docs/architecture/patterns.md` already sketches a `PhysicsWorld` owner and `PhysicsTickJob` worker split, but it is only an architectural pattern example.
- ECS should hold authoritative scene intent/components; runtime should own composition/wiring; physics simulation state should avoid back-importing runtime, graphics, platform, or app.
- Collider and rigid-body authoring should be separate concerns: collider components describe collision shape/material/filtering/trigger intent, while rigid-body components describe static/kinematic/dynamic motion, mass, velocity, damping, sleep, and CCD intent.
- Compound colliders should be explicit collider-owned child-shape descriptors with local poses; ECS scene hierarchy must not implicitly define physics compound shape topology.

## Required changes
- Write an architecture note or ADR deciding whether to introduce `src/physics/` and its allowed dependency edges.
- Define the split between:
  - ECS physics components: body descriptors, collider references, material IDs, simulation intent.
  - Physics world/state: solver islands, broadphase, contacts, constraints, integration caches.
  - Runtime bridge: fixed-step scheduling, ECS-to-physics synchronization, physics-to-ECS transform writeback.
  - Methods packages: reference algorithms, parity baselines, diagnostics, and papers.
- Decide the canonical collider/body ownership contract before implementation, using [`tasks/backlog/ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md`](../ecs/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) as the ECS authoring follow-up and [`tasks/backlog/methods/METHOD-001-rigid-body-dynamics-reference-backend.md`](../methods/METHOD-001-rigid-body-dynamics-reference-backend.md) as the CPU reference dynamics follow-up.
- Define first-phase collider shape policy: sphere, capsule, and box/OBB as dynamic-capable primitives; convex hull and static/kinematic triangle mesh as later or separately gated work; dynamic concave triangle mesh only via future convex decomposition or specialized method work.
- Define transform/scale policy for physics: body pose from ECS transform, collider local pose per shape, uniform scale policy for simple primitives, and diagnostics/rejection for unsupported non-uniform dynamic scaling.
- Decide whether physics can depend on `geometry` directly and whether ECS may store geometry collider primitives or only handles/descriptors.
- Define deterministic fixed-step policy, units, coordinate convention, sleep/island policy, collision filtering, event ownership, and diagnostics/reporting expectations.
- Add follow-up implementation tasks for ECS-to-physics runtime sync, rigid-body world/state, collision broadphase/narrowphase, constraints, and non-rigid phenomena after the layer decision.

## Tests
- Add/update architecture/contract tests only if source layout or dependency checks change.
- If a `src/physics` layer is approved, update layering tooling and add a failing-first contract test that prevents physics from importing runtime/graphics/platform/app.

## Docs
- Update `AGENTS.md` only if the canonical source layout or dependency invariant table changes.
- Update `docs/architecture/` with the physics layer contract and ECS/runtime integration diagram.
- Update `docs/migration/nonlegacy-parity-matrix.md` only if promoted source roots or retirement gates change.

## Acceptance criteria
- The repository has an explicit, reviewed answer for where rigid-body dynamics and other physics systems belong.
- ECS, runtime, geometry, methods, and any new physics layer each have clear ownership and dependency constraints.
- Collider descriptors, rigid-body descriptors, runtime sync, and physics-world solver state have separate ownership and no handle leakage into ECS.
- Follow-up tasks can implement physics without inventing cross-layer shortcuts.

## Verification
```bash
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Adding `src/physics` code before the architecture contract is accepted.
- Letting physics systems mutate graphics state or own runtime composition.
- Treating geometry collision algorithms as a full dynamics engine without a physics ownership boundary.

