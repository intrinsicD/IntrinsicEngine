# Physics Architecture

`physics` is the canonical owner for runtime-independent physical simulation
world/state. The architecture decision is recorded in
[ADR-0019](../adr/0019-physics-layer-ownership-and-ecs-integration.md).

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

## Implemented World/State Surface

`PHYSICS-001` adds the first promoted physics source root and module:

- `src/physics/Physics.World.cppm` exports `Extrinsic.Physics.World`.
- `BodyHandle` is a generation-checked `Extrinsic.Core.StrongHandle` value.
- `BodyDescriptor` records motion type, pose, linear/angular velocity, mass,
  damping, gravity scale, enabled/contact participation flags, and child shape
  descriptors.
- `ShapeDescriptor` currently supports first-phase primitive descriptors for
  sphere, capsule, and box/OBB-local-pose authoring.
- `World` owns create/destroy/update/get/contains/clear lifecycle APIs and
  rejects invalid descriptors or stale handles with diagnostics.
- `Step(StepInput)` runs a deterministic CPU-only unconstrained integration:
  static bodies are skipped, kinematic bodies integrate authored velocities,
  and dynamic bodies integrate gravity, damping, linear velocity, and angular
  velocity.
- `ComputeCollisionContacts()` emits physics-owned broadphase candidate pairs,
  contact records, and diagnostics for first-phase sphere, capsule, and box/OBB
  shapes. It wraps geometry-owned primitive/contact kernels without exposing
  geometry ownership as the physics public contract.

World diagnostics currently report body counts, create/destroy/update counts,
invalid descriptor rejects, stale handle rejects, executed step count, and the
last step's static/kinematic/dynamic/disabled body counters. Collision result
diagnostics report visited/skipped bodies and shapes, deterministic broadphase
pair count, generated contact count, trigger contact count, invalid body/shape
rejects, dynamic non-uniform scale rejects, and unsupported pair count.
`LastRejectReason` records the most recent skip/reject category for focused
diagnostics; counters remain the authoritative aggregate.

## Constraint Solver, Islands, and Sleep (`PHYSICS-003`)

`PHYSICS-003` adds the first CPU-only constraint/island/sleep layer at
`CPUContracted` maturity, all owned by `Extrinsic.Physics.World`:

- `SolveStep(StepInput, SolverSettings)` is the sleep-aware step: it integrates
  awake bodies (same integrator as `Step`, which remains the raw PHYSICS-001
  path and ignores sleep), computes contacts, builds islands, runs the
  iterative linear contact solver per awake island, and applies the sleep
  policy. Repeated identical inputs produce bitwise-identical evolution
  (slot-index iteration order, deterministic islands).
- `BuildIslands(CollisionResult)` groups dynamic bodies transitively connected
  through non-trigger contacts using union-find over slot indices.
  Static/kinematic endpoints anchor islands but never merge them; trigger
  contacts are excluded. Deterministic ordering contract: islands ordered by
  smallest member index, members sorted, contact indices ascending.
- The contact solve mirrors the canonical `METHOD-001` reference
  (`methods/physics/rigid_body_reference`): per-iteration linear normal
  impulses (restitution-scaled, applied only to approaching contacts) plus
  positional Baumgarte projection (`PositionCorrectionPercent` of the
  slop-adjusted penetration) on the captured manifold. The world's damping
  factor was aligned to the reference (`max(0, 1 - c·dt)`) so parity fixtures
  track the reference exactly. Angular inertia is not modeled in contact
  response (linear-only, like the reference); reported kinetic energy is
  linear-only and documented as such.
- Sleep policy: a dynamic body accumulates low-motion time while both velocity
  magnitudes stay below thresholds; it sleeps (velocities zeroed) when the
  accumulated time reaches `TimeToSleepSeconds`, is skipped by `SolveStep`
  integration while asleep, and wakes when its island gains an awake member or
  its descriptor is updated (`UpdateBody`/`WakeBody`). Sleep state lives in
  world slots, never in ECS.
- `SolveStepDiagnostics` reports validation status, solve status
  (`Converged` / `MaxIterationsReached` / `Degraded` fail-closed on non-finite
  state), iterations used, contacts solved, non-converged island count,
  max penetration before/after (residual recomputed from live shapes),
  max approaching normal-velocity residual, linear kinetic energy
  before/after with drift, island diagnostics, sleep transition counters, and
  the integration counters. `WorldDiagnostics` mirrors the last solve in
  `LastSolveStep`/`SolveStepsExecuted`.
- Physics-owned `ContactRecord` normals are enforced to the documented A→B
  convention by orienting against the shape-center offset. The geometry
  kernel itself honors the A→B convention on every `ComputeContact` path
  since `BUG-025` was fixed; the physics-side orientation guard stays as
  defense in depth against future kernel regressions.

Parity against the canonical reference is pinned by
`tests/unit/physics/Test.PhysicsSolverParity.cpp` (free-fall integrator
parity over 60 steps and an overlapping-sphere contact solve with matched
parameters, absolute tolerance `1e-4` for float-vs-double accumulation).

## Collision Contract Surface

`PHYSICS-002` closes the first CPU collision contract at `CPUContracted`
maturity:

- Shape references are `(BodyHandle, ShapeIndex)` pairs, so contacts can be
  mapped back to physics-owned body state without ECS/runtime imports.
- Broadphase candidates are generated deterministically in body-slot order and
  shape-index order. This first slice uses all-pairs enumeration; spatial
  acceleration is a later optimization that must preserve deterministic output
  semantics.
- Narrowphase contacts include normal, penetration depth, contact point on A,
  contact point on B, and trigger status.
- Collision filtering currently covers body-level contact participation,
  disabled bodies, disabled shapes, and trigger classification. ECS/runtime
  filtering policy remains outside `src/physics`.
- Dynamic non-uniform shape/body scale is rejected with diagnostics. Static and
  kinematic cooked-shape policy may expand later, but dynamic concave mesh
  support remains forbidden in the first phase.

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
physics-world state is stored in `Extrinsic.Physics.World`, and runtime
synchronization is stored in runtime-owned bridge sidecars.

ECS must not store physics-world handles, broadphase proxies, contacts, islands,
solver indices, runtime sidecars, graphics handles, or RHI handles.

Runtime owns the bridge:

- maps stable ECS identity to physics handles in runtime sidecars through
  `Extrinsic.Runtime.PhysicsBridge`;
- syncs ECS authoring descriptors into physics state before fixed-step stepping;
- steps physics with deterministic fixed `dt` through an accumulator;
- writes simulated dynamic-body transforms back to ECS and stamps transform
  dirty markers;
- skips static and kinematic writeback with diagnostics;
- routes future contacts/collision events to runtime/editor/app command
  surfaces once `PHYSICS-002` exposes contact data.

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
- [`PHYSICS-001`](../../tasks/done/PHYSICS-001-physics-world-state-and-runtime-sync.md) defines the first physics world/state module and runtime bridge at `CPUContracted` maturity.
- [`PHYSICS-002`](../../tasks/done/PHYSICS-002-collision-broadphase-narrowphase-contract.md) owns collision broadphase/narrowphase contracts.
- [`PHYSICS-003`](../../tasks/done/PHYSICS-003-constraints-islands-and-solver-diagnostics.md) added constraints, islands, sleep, and solver diagnostics at `CPUContracted`.
- [`ARCH-002`](../../tasks/done/ARCH-002-physics-phenomena-roadmap.md) records the non-rigid and multi-phenomena roadmap decisions.
- [`METHOD-009`](../../tasks/done/METHOD-009-particle-spring-reference-backend.md), [`METHOD-010`](../../tasks/done/METHOD-010-xpbd-cloth-shell-reference-backend.md), and [`METHOD-011`](../../tasks/backlog/methods/METHOD-011-sph-fluid-reference-backend.md) are the first non-rigid physics method-package follow-ups (`METHOD-009` and `METHOD-010` are done; `METHOD-011` remains open). They remain CPU-reference-first and open no GPU backend.
