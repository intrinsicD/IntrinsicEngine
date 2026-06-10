# Rigid Body Dynamics Reference Backend

`physics.rigid_body_reference` is the deterministic CPU reference backend for
first-phase rigid-body dynamics work. It is a method package, not the engine
physics world: it imports no ECS, runtime, platform, graphics, or RHI code.
Future `src/physics` and runtime bridge tasks compare against this backend when
validating fixed-step body integration and simple contact behavior.

## Contract

- Backend identity: `cpu_reference`.
- Units: meters, kilograms, seconds, radians.
- Integration: fixed-step semi-implicit Euler for dynamic bodies; kinematic
  bodies integrate author-provided linear/angular velocities; static bodies do
  not move.
- State: pose, linear/angular velocity, mass, diagonal inertia, damping, sleep
  flags, and explicit child collider shapes with local poses.
- Shapes: sphere, capsule, and oriented box child descriptors. Compound shapes
  are represented by multiple children.
- Contacts: sphere-sphere, sphere-capsule, and sphere-box are supported. Other
  pairs are valid inputs but increment `UnsupportedPairCount` instead of
  silently producing a contact.
- Solver: normal-only impulse plus positional penetration correction. The
  reference does not model friction or angular contact impulses yet.

## Reference

The intake reference for this slice is David Baraff's SIGGRAPH course material
on rigid-body simulation. The package captures the foundational fixed-step
state update and impulse-resolution contract needed before optimized CPU, GPU,
or runtime-integrated physics backends can be compared against a canonical
oracle.

See [`paper.md`](paper.md) for the method intake notes.

## Diagnostics

`Step(...)` returns machine-checkable diagnostics:

- `Code`: validation result for timestep, solver iterations, finite state,
  mass, inertia, and shape descriptors.
- `ContactCount`: supported contacts generated before solve.
- `UnsupportedPairCount`: analytic shape pairs outside this reference slice.
- `MaxPenetration`: largest pre-solve penetration.
- `ResidualPenetration`: largest supported penetration after solve.
- `KineticEnergyBefore`, `KineticEnergyAfter`, `EnergyDrift`: energy accounting
  for dynamic bodies.
- `Converged`: whether residual penetration is within the configured slop.

## Limitations

- No optimized CPU backend and no GPU backend.
- No runtime/ECS integration; that is owned by `PHYSICS-001` after this method
  task retires.
- No broadphase acceleration structure, islands, sleeping policy, friction,
  joints, angular impulse solve, continuous collision detection, or concave
  dynamic mesh support.
- Triangle mesh colliders are intentionally not part of this dynamic reference
  backend. Mesh support must remain static/kinematic or arrive through a later
  convex-decomposition/specialized method task.

## Verification

The reference backend is covered by
[`tests/unit/physics/Test.RigidBodyReference.cpp`](../../../tests/unit/physics/Test.RigidBodyReference.cpp).
The PR-fast smoke benchmark manifest is
[`benchmarks/physics/manifests/rigid_body_reference_smoke.yaml`](../../../benchmarks/physics/manifests/rigid_body_reference_smoke.yaml).

This backend is also the canonical parity target for the promoted
`Extrinsic.Physics.World` constraint solver (`PHYSICS-003`):
[`tests/unit/physics/Test.PhysicsSolverParity.cpp`](../../../tests/unit/physics/Test.PhysicsSolverParity.cpp)
runs shared free-fall and overlapping-sphere fixtures through both
implementations with matched parameters (absolute tolerance `1e-4`,
float-vs-double accumulation). The world's damping factor follows this
reference (`max(0, 1 - c·dt)`).
