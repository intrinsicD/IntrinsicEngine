# HARDEN-064 — Define ECS collider and rigid-body authoring contracts

## Goal
- Define promoted ECS authoring components for colliders and rigid bodies that can support future physics integration without storing physics-world internals.

## Non-goals
- No rigid-body solver, broadphase, narrowphase, constraint solver, or runtime sync implementation; CPU reference dynamics are owned by [`METHOD-001`](../methods/METHOD-001-rigid-body-dynamics-reference-backend.md).
- No `src/physics` source layout change before [`ARCH-001`](../physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md) is accepted.
- No graphics/RHI handle storage and no live runtime sidecars in canonical ECS components.
- No dynamic concave mesh simulation support in the first collider contract.

## Context
- Owner/layer: `ecs`, with architecture dependency on [`ARCH-001`](../physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md) for any future `src/physics` layer.
- Current promoted `src/ecs/Components/ECS.Component.Collider.cppm` stores only `std::vector<Geometry::Sphere>`.
- Ideal model: `Collider` describes collision shape/material/filtering/trigger intent; `RigidBody` describes motion/mass/velocity/damping/sleep/CCD intent; physics world/runtime sidecars own solver handles, broadphase proxies, islands, contacts, warm-start caches, and writeback scheduling.
- ECS hierarchy is scene/authoring hierarchy and must not be the implicit physics compound-collider representation.
- Convergence: part of **Theme C — Physics readiness** and **Theme D — ECS hardening parity** in [`tasks/backlog/README.md`](../README.md). The CPU reference dynamics this contract enables live in [`METHOD-001`](../methods/METHOD-001-rigid-body-dynamics-reference-backend.md).

## Required changes
- [ ] Define separate ECS authoring components for collision and body motion intent:
  - [ ] `Collider` for shape descriptors, local pose, material, filtering, trigger state, contact/rest offsets, and enabled state.
  - [ ] `RigidBody` for `Static`/`Kinematic`/`Dynamic` motion type, mass policy, velocity, damping, gravity scale, sleep, and continuous-collision-detection intent.
- [ ] Document valid component combinations:
  - [ ] `Collider` only: static collision/trigger authoring by default unless runtime policy says otherwise.
  - [ ] `Collider + RigidBody{Static}`: explicit static body.
  - [ ] `Collider + RigidBody{Kinematic}`: externally driven body that can participate in contacts.
  - [ ] `Collider + RigidBody{Dynamic}`: simulated body.
  - [ ] `RigidBody` only: allowed only if explicitly documented as non-contacting body state, otherwise diagnose.
- [ ] Replace sphere-only authoring with a shape descriptor model that can represent at least first-phase primitives: sphere, capsule, and box/OBB.
- [ ] Plan later shape support without committing it to the first implementation: convex hull, static/kinematic triangle mesh, height field, SDF, and convex decomposition.
- [ ] Define compound collider authoring explicitly as child shapes with local poses under one `Collider`; do not infer physical compounds from ECS parent/child hierarchy.
- [ ] Define mesh collider policy: triangle meshes are static or kinematic only at first; dynamic concave mesh bodies require later convex decomposition or a specialized method task.
- [ ] Define transform/scale policy: entity/world transform provides body pose, collider local pose offsets the shape, uniform scale may be supported for simple primitives, non-uniform scale must be baked/cooked or rejected for dynamic bodies with diagnostics.
- [ ] Ensure canonical ECS stores only CPU-only descriptors or stable asset/geometry identifiers; no `PhysicsBodyHandle`, broadphase proxy, contact cache, island ID, solver body index, graphics handle, or RHI handle may be stored in ECS components.
- [ ] Add follow-up tasks for runtime ECS-to-physics synchronization and physics-world cooking after `ARCH-001` decides layer ownership.

## Tests
- [ ] Add or update `tests/unit/ecs/Test.ECS.ColliderAuthoring.cpp` for descriptor defaults, primitive shape construction, compound child local poses, trigger/filter/material defaults, invalid shape diagnostics, and body/collider combination rules that are enforceable in ECS.
- [ ] Add contract or structural coverage under `tests/contract/ecs/` if needed to prevent physics-world/runtime/graphics handles from entering canonical ECS components.
- [ ] Keep all tests CPU-only and labeled `unit;ecs` or `contract;ecs` as appropriate.

## Docs
- [ ] Update `src/ecs/README.md` and `src/ecs/Components/README.md` with collider/rigid-body ownership and shape policy.
- [ ] Update `docs/migration/nonlegacy-parity-matrix.md` if the ECS public module surface or retirement blockers change.
- [ ] Cross-reference the architecture decision from `tasks/backlog/physics/ARCH-001-physics-layer-ownership-and-ecs-integration.md` once accepted.

## Acceptance criteria
- [ ] ECS has a documented authoring contract that separates collider shape intent from rigid-body dynamics intent.
- [ ] The first implementation path supports sphere, capsule, and box/OBB descriptors and allows explicit compound colliders.
- [ ] Dynamic, kinematic, static, trigger, and non-contacting combinations are documented with diagnostics expectations.
- [ ] No solver-owned or runtime-owned physics state is stored in canonical ECS components.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicContractBuildTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Implementing solver integration, collision detection passes, broadphase structures, or runtime fixed-step sync in this ECS authoring task.
- Inferring physical compound colliders implicitly from ECS scene hierarchy.
- Supporting dynamic concave triangle mesh bodies in the initial collider contract.
- Adding physics-world handles, runtime sidecars, graphics handles, or RHI handles to canonical ECS components.

