# ADR 0019 - Physics Layer Ownership and ECS Integration

- **Status:** Accepted
- **Date:** 2026-06-05
- **Owners:** Architecture, Physics, ECS, Runtime, Methods
- **Related tasks:** [`tasks/done/ARCH-001`](../../tasks/archive/ARCH-001-physics-layer-ownership-and-ecs-integration.md), [`tasks/done/HARDEN-064`](../../tasks/archive/HARDEN-064-ecs-collider-rigidbody-authoring-contract.md), [`tasks/done/METHOD-001`](../../tasks/archive/METHOD-001-rigid-body-dynamics-reference-backend.md)
- **Related docs:** [`docs/architecture/physics.md`](../architecture/physics.md), [`docs/architecture/layering.md`](../architecture/layering.md), [`docs/architecture/ecs.md`](../architecture/ecs.md), [`docs/architecture/runtime.md`](../architecture/runtime.md)

## Context

IntrinsicEngine has geometry collision primitives (`GJK`, `EPA`, overlap tests,
contact manifolds, SDF contact helpers) and ECS collider authoring seeds, but it
has no canonical physics layer. `AGENTS.md` did not list `src/physics`, so any
rigid-body implementation would otherwise have to choose between putting solver
state into ECS, hiding it in runtime, or treating geometry collision kernels as a
full dynamics engine. All three options would blur ownership and make later
runtime/ECS integration hard to review.

The backlog already records two immediate consumers of this decision:

- `HARDEN-064` needs the ECS authoring contract for colliders and rigid bodies.
- `METHOD-001` needs the CPU reference dynamics package and parity baseline.

The decision must let those tasks proceed without authorizing solver code in this
architecture slice.

## Decision

### 1. Introduce `src/physics` as the physics-world layer

`src/physics` is accepted as the canonical home for runtime-independent physics
world/state code once implementation begins. The layer owns:

- physics world/state containers;
- solver body handles, broadphase proxies, islands, contacts, constraints,
  warm-start caches, sleep state, and integration caches;
- deterministic step contracts and physics diagnostics;
- physics-owned cooking records that are not ECS authoring data.

Allowed dependencies are intentionally narrow:

- `physics` -> `core`, `geometry`.

Forbidden dependencies from `physics`:

- no `ecs`, `runtime`, `graphics`, `graphics/rhi`, `platform`, `app`, or live
  asset-service imports;
- no method-package dependency from production `src/physics` code.

Method packages remain references and parity baselines. Physics implementations
may be compared against method results in tests and benchmarks, but production
physics source does not import method packages as engine wiring.

### 2. ECS owns authoring intent only

ECS collider/body components describe scene intent, not live simulation state.
`HARDEN-064` owns the concrete component shape, but it must follow these rules:

- `Collider` describes shape descriptors, local poses, material/filtering,
  trigger state, contact/rest offsets, and enabled state.
- `RigidBody` describes static/kinematic/dynamic intent, mass policy,
  velocity, damping, gravity scale, sleep, and CCD intent.
- ECS may store CPU-only geometry descriptors when explicitly justified by the
  `ecs -> geometry` contract.
- ECS must not store `PhysicsBodyHandle`, broadphase proxies, contact caches,
  island IDs, solver indices, runtime sidecars, graphics handles, or RHI handles.

Scene hierarchy remains an authoring/transform hierarchy. Compound collider
shape trees must be represented explicitly by collider child-shape descriptors
with local poses, not inferred from ECS parent/child relationships.

### 3. Runtime owns bridge and scheduling

Runtime composes ECS, physics, geometry, assets, platform, and graphics. It owns:

- fixed-step scheduling and accumulator policy;
- ECS-to-physics synchronization from authoring descriptors into physics world
  state;
- runtime-owned sidecar maps from stable entity IDs to physics handles;
- physics-to-ECS transform writeback for simulated bodies;
- collision/contact event routing into runtime/editor/application command
  surfaces;
- diagnostics publication and frame-lifecycle ordering.

Neither ECS nor physics imports runtime. Runtime is the only layer that sees both
live ECS registries and physics-world handles.

### 4. First-phase collider and transform policy

The first implementation phase supports these dynamic-capable primitive shapes:

- sphere;
- capsule;
- box/OBB.

Convex hulls, static/kinematic triangle meshes, height fields, SDF shapes, and
convex decomposition are later tasks. Dynamic concave triangle mesh simulation is
not part of the first-phase contract.

Transform policy:

- ECS world transform supplies the body pose.
- Each collider child shape carries its own local pose under the body.
- Uniform scale may be supported for simple primitives when the implementation
  documents the exact conversion.
- Unsupported non-uniform dynamic scale must be rejected or baked/cooked with an
  explicit diagnostic.

Physics-local units are meters, seconds, kilograms, and radians. The coordinate
basis follows the engine world-space basis used by ECS/geometry transforms;
view-space or clip-space conventions remain graphics-owned.

### 5. Determinism and diagnostics policy

Physics stepping is fixed-step only at the physics API boundary. Runtime may
accumulate variable frame time, but it submits deterministic fixed `dt` steps to
physics. Physics diagnostics must cover at least:

- invalid authoring or cooked input;
- unsupported shape/scale combinations;
- broadphase/narrowphase rejection reasons;
- solver iteration counts and non-convergence;
- penetration tolerance, energy drift, and sleep/island state where applicable;
- collision filtering decisions and event counts.

### 6. Follow-up ownership

`ARCH-001` does not add source code. Follow-up tasks own implementation:

- `HARDEN-064`: ECS collider and rigid-body authoring descriptors.
- `METHOD-001`: CPU reference rigid-body dynamics backend.
- `PHYSICS-001`: physics world/state module and runtime fixed-step bridge.
- `PHYSICS-002`: collision broadphase/narrowphase contract and diagnostics.
- `PHYSICS-003`: constraints, islands, sleep, and solver diagnostics.
- `ARCH-002`: non-rigid and multi-phenomena roadmap.

## Consequences

Positive:

- Solver state has a clear home and no longer pressures ECS/runtime into owning
  physics internals.
- ECS authoring can proceed with CPU descriptors while keeping physics handles
  out of canonical components.
- Runtime integration remains reviewable because the only live ECS plus physics
  handle bridge is in `runtime`.
- Layering tooling can reject future upward imports from `src/physics`.

Trade-offs and risks:

- `src/physics` introduces another promoted layer and must stay small until a
  CPU reference and runtime bridge justify growth.
- Physics cannot directly use asset services or ECS queries; runtime must adapt
  those inputs into stable descriptors or handles.
- Method reference code is not production physics code, so parity comparisons
  require explicit tests rather than shared implementation shortcuts.

## Alternatives Considered

### Keep physics inside `runtime`

Rejected. Runtime should compose lower layers, not become the owner of solver
state, broadphase proxies, contacts, and constraints. Putting physics internals
there would make the layer less reusable and harder to test independently.

### Store live physics handles in ECS components

Rejected. ECS is canonical authoring data and already forbids runtime/graphics
sidecars. Live solver handles and contact caches are frame-lifecycle state, not
portable scene descriptors.

### Treat `geometry` as the physics layer

Rejected. Geometry owns collision/math kernels and shape queries. It should not
own solver worlds, integration caches, fixed-step scheduling, sleep, contacts,
or event routing.

### Put rigid-body dynamics only under `methods/`

Rejected. Methods provide reference algorithms and parity baselines. A runtime
physics world needs lifecycle, handles, events, and scheduling contracts that are
engine integration concerns, not paper/method package ownership.

## Validation

- `tools/repo/check_layering.py` recognizes `physics` and permits only
  `core`/`geometry` dependencies from it.
- `tests/regression/tooling/Test.CheckLayering.py` includes a clean physics
  fixture and a negative fixture that rejects `physics -> ecs/runtime/graphics/platform/app` imports.
- Future source additions under `src/physics` must pass
  `python3 tools/repo/check_layering.py --root src --strict`.
