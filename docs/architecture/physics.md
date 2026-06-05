# Physics Architecture

`physics` is the canonical owner for runtime-independent physical simulation
world/state once `src/physics` implementation begins. The architecture decision
is recorded in [ADR-0019](../adr/0019-physics-layer-ownership-and-ecs-integration.md).

## Responsibilities

- Physics world/state containers.
- Solver body handles, broadphase proxies, contact caches, islands,
  constraints, warm-start data, sleep state, and integration caches.
- Deterministic fixed-step simulation contracts and diagnostics.
- Physics-owned cooked data that is not ECS authoring state.

## Non-responsibilities

- ECS authoring components and scene hierarchy ownership.
- Runtime fixed-step scheduling, ECS-to-physics synchronization, transform
  writeback, event routing, or editor command surfaces.
- Graphics/RHI resources, rendering visualization, platform input, or app
  policy.
- Method-package paper intake and reference-backend ownership.

## Dependencies

Allowed:

- `core`.
- `geometry` for CPU collision shapes, support functions, contact helpers,
  overlap tests, and spatial/numeric kernels.

Disallowed:

- `ecs`, `runtime`, `graphics`, `graphics/rhi`, `platform`, `app`, live asset
  services, and production imports from `methods/**`.

`runtime` is the only layer that may compose live ECS registries with physics
world handles.

## ECS and Runtime Split

ECS stores authoring intent only:

- collider shape descriptors, local poses, material/filtering, triggers,
  contact/rest offsets, and enabled state;
- rigid-body static/kinematic/dynamic intent, mass policy, velocities, damping,
  gravity scale, sleep, and CCD intent.

The ECS descriptor surface is present in
[`HARDEN-064`](../../tasks/done/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md)
as `Extrinsic.ECS.Component.Collider` and
`Extrinsic.ECS.Component.RigidBody`. It is a CPU authoring contract only; live
physics-world state and runtime synchronization remain follow-up work.

ECS must not store physics-world handles, broadphase proxies, contacts, islands,
solver indices, runtime sidecars, graphics handles, or RHI handles.

Runtime owns the bridge:

- maps stable ECS identity to physics handles in runtime sidecars;
- syncs ECS authoring descriptors into physics state on fixed-step boundaries;
- steps physics with deterministic fixed `dt`;
- writes simulated transforms back to ECS;
- routes contacts/collision events to runtime/editor/app command surfaces.

## First-Phase Collider Policy

Dynamic-capable primitive shapes for the first implementation phase are:

- sphere;
- capsule;
- box/OBB.

Compound colliders are explicit child-shape descriptors with local poses under a
single collider/body. ECS scene hierarchy is not a compound-collider tree.

Later or separately gated shapes:

- convex hull;
- static/kinematic triangle mesh;
- height field;
- SDF collider;
- convex decomposition for dynamic concave content.

Dynamic concave triangle mesh simulation is forbidden in the first phase.

## Transform, Units, and Diagnostics

- ECS world transform provides the body pose.
- Collider child shapes carry local poses.
- Uniform scale may be supported for simple primitives when explicitly
  documented.
- Unsupported non-uniform dynamic scaling is rejected or cooked/baked with an
  explicit diagnostic.
- Physics-local units are meters, seconds, kilograms, and radians.
- The coordinate basis follows engine world-space ECS/geometry transforms;
  view-space and clip-space conventions remain graphics-owned.

Diagnostics must report invalid authoring, unsupported shape/scale choices,
filtering decisions, broadphase/narrowphase rejection reasons, solver iteration
counts, non-convergence, penetration tolerance, energy drift, sleep/island state,
and contact/event counts where applicable.

## Follow-Ups

- [`HARDEN-064`](../../tasks/done/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md) defines ECS collider and rigid-body authoring descriptors.
- [`METHOD-001`](../../tasks/done/METHOD-001-rigid-body-dynamics-reference-backend.md) defines the CPU reference rigid-body method backend.
- [`PHYSICS-001`](../../tasks/backlog/physics/PHYSICS-001-physics-world-state-and-runtime-sync.md) owns the first physics world/state module and runtime bridge.
- [`PHYSICS-002`](../../tasks/backlog/physics/PHYSICS-002-collision-broadphase-narrowphase-contract.md) owns collision broadphase/narrowphase contracts.
- [`PHYSICS-003`](../../tasks/backlog/physics/PHYSICS-003-constraints-islands-and-solver-diagnostics.md) owns constraints, islands, sleep, and solver diagnostics.
- [`ARCH-002`](../../tasks/backlog/physics/ARCH-002-physics-phenomena-roadmap.md) owns non-rigid and multi-phenomena roadmap decisions.
