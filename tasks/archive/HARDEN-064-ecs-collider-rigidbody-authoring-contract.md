# HARDEN-064 — Define ECS collider and rigid-body authoring contracts

## Status
- Completed: 2026-06-05.
- Maturity: `CPUContracted`.
- Commit/PR: this task retirement commit.
- Summary: expanded ECS collider authoring from a sphere-only payload to
  CPU-only shape descriptors for sphere, capsule, and box/OBB with explicit
  child local poses, material/filtering/trigger/contact-offset metadata, and
  added a separate `RigidBody` authoring component for static/kinematic/dynamic
  body intent without solver/runtime handles.
- CPU-only runtime/world follow-up is retired in
  [`PHYSICS-001`](PHYSICS-001-physics-world-state-and-runtime-sync.md)
  after retired [`METHOD-001`](METHOD-001-rigid-body-dynamics-reference-backend.md)
  supplied enough CPU reference dynamics coverage.

## Goal
- Define promoted ECS authoring components for colliders and rigid bodies that can support future physics integration without storing physics-world internals.

## Non-goals
- No rigid-body solver, broadphase, narrowphase, constraint solver, or runtime sync implementation; CPU reference dynamics are owned by retired [`METHOD-001`](METHOD-001-rigid-body-dynamics-reference-backend.md).
- No `src/physics` source additions, physics-world implementation, or runtime
  bridge in this ECS authoring task. [`ARCH-001`](ARCH-001-physics-layer-ownership-and-ecs-integration.md)
  accepted the layer contract; this task stays in `src/ecs`.
- No graphics/RHI handle storage and no live runtime sidecars in canonical ECS components.
- No dynamic concave mesh simulation support in the first collider contract.

## Context
- Owner/layer: `ecs`, following the accepted physics ownership decision
  [`ARCH-001`](ARCH-001-physics-layer-ownership-and-ecs-integration.md)
  / [ADR-0019](../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md).
- The prior promoted `src/ecs/Components/ECS.Component.Collider.cppm` stored only `std::vector<Geometry::Sphere>`.
- Ideal model: `Collider` describes collision shape/material/filtering/trigger intent; `RigidBody` describes motion/mass/velocity/damping/sleep/CCD intent; physics world/runtime sidecars own solver handles, broadphase proxies, islands, contacts, warm-start caches, and writeback scheduling.
- ECS hierarchy is scene/authoring hierarchy and must not be the implicit physics compound-collider representation.
- Convergence: part of **Theme C — Physics readiness** and **Theme D — ECS hardening parity** in [`tasks/backlog/README.md`](../backlog/README.md). The CPU reference dynamics this contract enables live in retired [`METHOD-001`](METHOD-001-rigid-body-dynamics-reference-backend.md).

## Required changes
- [x] Define separate ECS authoring components for collision and body motion intent:
  - [x] `Collider` for shape descriptors, local pose, material, filtering, trigger state, contact/rest offsets, and enabled state.
  - [x] `RigidBody` for `Static`/`Kinematic`/`Dynamic` motion type, mass policy, velocity, damping, gravity scale, sleep, and continuous-collision-detection intent.
- [x] Document valid component combinations:
  - [x] `Collider` only: static collision/trigger authoring by default unless runtime policy says otherwise.
  - [x] `Collider + RigidBody{Static}`: explicit static body.
  - [x] `Collider + RigidBody{Kinematic}`: externally driven body that can participate in contacts.
  - [x] `Collider + RigidBody{Dynamic}`: simulated body.
  - [x] `RigidBody` only: allowed only if explicitly documented as non-contacting body state, otherwise diagnose.
- [x] Replace sphere-only authoring with a shape descriptor model that can represent at least first-phase primitives: sphere, capsule, and box/OBB.
- [x] Plan later shape support without committing it to the first implementation: convex hull, static/kinematic triangle mesh, height field, SDF, and convex decomposition.
- [x] Define compound collider authoring explicitly as child shapes with local poses under one `Collider`; do not infer physical compounds from ECS parent/child hierarchy.
- [x] Define mesh collider policy: triangle meshes are static or kinematic only at first; dynamic concave mesh bodies require later convex decomposition or a specialized method task.
- [x] Define transform/scale policy: entity/world transform provides body pose, collider local pose offsets the shape, uniform scale may be supported for simple primitives, non-uniform scale must be baked/cooked or rejected for dynamic bodies with diagnostics.
- [x] Ensure canonical ECS stores only CPU-only descriptors or stable asset/geometry identifiers; no `PhysicsBodyHandle`, broadphase proxy, contact cache, island ID, solver body index, graphics handle, or RHI handle may be stored in ECS components.
- [x] Cross-link runtime ECS-to-physics synchronization and physics-world
      cooking to [`PHYSICS-001`](PHYSICS-001-physics-world-state-and-runtime-sync.md).

## Tests
- [x] Add or update `tests/unit/ecs/Test.ECS.ColliderAuthoring.cpp` for descriptor defaults, primitive shape construction, compound child local poses, trigger/filter/material defaults, invalid shape diagnostics, and body/collider combination rules that are enforceable in ECS.
- [x] Add contract or structural coverage under `tests/contract/ecs/` if needed to prevent physics-world/runtime/graphics handles from entering canonical ECS components.
- [x] Keep all tests CPU-only and labeled `unit;ecs` or `contract;ecs` as appropriate.

## Docs
- [x] Update `src/ecs/README.md` and `src/ecs/Components/README.md` with collider/rigid-body ownership and shape policy.
- [x] Update `docs/migration/nonlegacy-parity-matrix.md` if the ECS public module surface or retirement blockers change.
- [x] Cross-reference [ADR-0019](../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md)
      and [`tasks/done/ARCH-001`](ARCH-001-physics-layer-ownership-and-ecs-integration.md).

## Acceptance criteria
- [x] ECS has a documented authoring contract that separates collider shape intent from rigid-body dynamics intent.
- [x] The first implementation path supports sphere, capsule, and box/OBB descriptors and allows explicit compound colliders.
- [x] Dynamic, kinematic, static, trigger, and non-contacting combinations are documented with diagnostics expectations.
- [x] No solver-owned or runtime-owned physics state is stored in canonical ECS components.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicECSTests IntrinsicEcsContractTests IntrinsicContractBuildTests
ctest --test-dir build/ci -L 'ecs|contract' --output-on-failure --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/check_pr_contract.py
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
```

## Forbidden changes
- Implementing solver integration, collision detection passes, broadphase structures, or runtime fixed-step sync in this ECS authoring task.
- Inferring physical compound colliders implicitly from ECS scene hierarchy.
- Supporting dynamic concave triangle mesh bodies in the initial collider contract.
- Adding physics-world handles, runtime sidecars, graphics handles, or RHI handles to canonical ECS components.
