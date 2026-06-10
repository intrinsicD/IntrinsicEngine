# XPBD Cloth and Shell Reference Backend

`physics.xpbd_cloth_reference` is the deterministic CPU reference backend for
cloth and thin-shell simulation using XPBD-style compliant constraints over
triangle mesh state. It is a method package, not an engine system: it imports
no ECS, runtime, platform, graphics, or RHI code. Later optimized cloth,
dihedral bending, and runtime cloth tasks validate against this backend as the
correctness oracle.

## Contract

- Backend identity: `cpu_reference`.
- Units: meters, kilograms, seconds.
- State: particle positions/velocities/inverse masses over triangle topology,
  with structural (unique edge) and bending (opposite-vertex pair across each
  interior edge) XPBD distance constraints carrying rest length + compliance.
- Pinning: inverse mass `0` pins a vertex (never integrates, ignores
  corrections; callers may reposition between steps).
- Solver: predict positions (gravity), iterate XPBD projection with
  per-constraint Lagrange multipliers (compliance `alpha`, `alpha~ =
  alpha/dt^2`), project half-space colliders, then derive velocities from
  position change and apply global damping. Deterministic constraint order.
- Collision-query inputs are method parameters only: static half-space planes
  are supported; sphere colliders are declared but unsupported and counted in
  diagnostics. No self-collision, no friction.
- `BuildClothFromTriangles(...)` derives the constraint sets deterministically
  from triangle topology and input positions.

## Diagnostics

`Step(...)` returns machine-checkable diagnostics:

- `Code`: validation result for timestep, iterations, particle state,
  constraints, triangle topology (out-of-range or repeated indices), and
  colliders.
- `ParticleCount`, `TriangleCount`, `StretchConstraintCount`,
  `BendConstraintCount`, `PinnedParticleCount`.
- `DegenerateTriangleCount` (zero-area triangles, reported not fatal) and
  `DegenerateConstraintCount` (coincident endpoints skipped this step).
- `UnsupportedColliderCount` for declared-but-unsupported collider kinds.
- `MaxStretchResidual`, `StretchResidualL2`, `MaxBendResidual` post-step, and
  `Converged`/`IterationsUsed` against `ResidualTolerance`.
- `KineticEnergyBefore/After`, `MechanicalEnergyBefore/After`, `EnergyDrift`
  (kinetic + gravitational; PBD dissipates energy by construction).
- `Stable`, `FallbackApplied`: a non-finite post-step state fails closed to
  the unchanged input state with `NonFiniteState`.

## Limitations

- No optimized CPU backend and no GPU backend (forbidden until reference
  parity fixtures exist; future tasks must name this package as the oracle).
- Bending is opposite-vertex distance, not dihedral angle.
- No self-collision, friction, strain limiting, or volumetric FEM.
- No runtime cloth component, editor tool, renderer pass, or collision event
  routing; engine integration requires separate `physics`/`runtime` tasks.

## Verification

The reference backend is covered by
[`tests/unit/physics/Test.XpbdClothReference.cpp`](../../../tests/unit/physics/Test.XpbdClothReference.cpp).
The PR-fast smoke benchmark manifest is
[`benchmarks/physics/manifests/xpbd_cloth_reference_smoke.yaml`](../../../benchmarks/physics/manifests/xpbd_cloth_reference_smoke.yaml).

See [`paper.md`](paper.md) for the method intake notes.
