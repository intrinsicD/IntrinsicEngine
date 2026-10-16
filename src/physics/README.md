# Physics

`Physics.World.cppm` exposes the `Extrinsic.Physics.World` module;
`Physics.World.cpp` owns its CPU implementation. The world owns validated body
and shape state, collision contacts, stepping, linear contact solving and sleep.
It depends only on Core and Geometry.

Runtime owns fixed-step scheduling, ECS synchronization and transform writeback.
Physics does not borrow live ECS, graphics or application state. Use the
[physics architecture](../../docs/architecture/physics.md) for supported shapes,
solver limits and integration contracts, and
[ADR-0019](../../docs/adr/0019-physics-layer-ownership-and-ecs-integration.md) for
the ownership boundary.
