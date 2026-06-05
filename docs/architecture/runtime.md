# Runtime Architecture

`runtime` is the composition root for IntrinsicEngine.

## Responsibilities

- Construct and wire subsystem boundaries.
- Own lifecycle/state transitions for engine execution.
- Mediate between platform, graphics, assets, ECS, physics, and geometry services.
- Own fixed-step ECS-to-physics synchronization, physics-world stepping, contact/event routing,
  and physics-to-ECS transform writeback.

## Non-responsibilities

- Runtime should not become a utility grab-bag for lower layers.
- Lower layers must remain reusable without runtime internals.
- Physics world/state lives in `physics`; runtime owns only bridge sidecars and scheduling.

## Physics Bridge

`Extrinsic.Runtime.PhysicsBridge` is the concrete runtime-owned ECS/physics
composition seam added by `PHYSICS-001`.

The bridge owns:

- an `Extrinsic.Physics.World` instance;
- a `StableId -> BodyHandle` sidecar keyed by
  `Extrinsic.ECS.Component.StableId`;
- fixed-step accumulator state;
- synchronization, writeback, and ordering diagnostics.

`SyncAuthoring(Registry&)` scans ECS entities with collider or rigid-body
authoring, sorts them by `entt` entity value for deterministic processing,
requires a valid `StableId`, converts ECS collider/rigid-body/transform
descriptors into `Physics::BodyDescriptor`, creates or updates world bodies,
and destroys stale sidecar/world bodies when entities disappear or authoring
becomes invalid. ECS components never receive physics handles.

`TickFixedStep(Registry&, frameDeltaSeconds, config)` runs in this order:

1. synchronize ECS authoring into the physics world;
2. clamp and accumulate frame delta;
3. execute zero or more fixed physics steps with `config.FixedDeltaSeconds`;
4. write dynamic body poses back to ECS transforms;
5. stamp `Transform::IsDirtyTag` and `Transform::WorldUpdatedTag` on dynamic
   writeback.

Static and kinematic bodies are not written back by this bridge; they are
diagnosed as skipped writebacks. Contact event routing is intentionally not
implemented here yet because broadphase/narrowphase contact records are owned
by `PHYSICS-002`.

## Related references

- Historical details: `runtime-subsystem-boundaries.md` (`legacy-background`).
- Physics bridge ownership: [physics.md](physics.md).
- Layer policy: [layering.md](layering.md).
