# Particle and Mass-Spring Dynamics Reference Backend

`physics.particle_spring_reference` is the deterministic CPU reference backend
for particle dynamics and mass-spring systems. It is a method package, not an
engine system: it imports no ECS, runtime, platform, graphics, or RHI code.
Later XPBD cloth (METHOD-010), soft-body, and runtime particle tasks validate
against this backend as the correctness oracle.

## Contract

- Backend identity: `cpu_reference`.
- Units: meters, kilograms, seconds.
- State: particle positions, velocities, and inverse masses; spring records
  with particle index pairs, rest lengths, Hooke stiffness, and axial damping.
- Pinning: inverse mass `0` pins a particle. Pinned particles never integrate
  and discard forces; callers may reposition them between steps.
- Forces: gravity, linear Hooke spring force, axial spring damping
  (Provot-style), and an optional global linear drag.
- Integration: fixed-step semi-implicit (symplectic) Euler — velocity from
  pre-step forces, then position from the updated velocity. Deterministic for
  identical inputs.

## Diagnostics

`Step(...)` returns machine-checkable diagnostics:

- `Code`: validation result for timestep, particle state (finite, non-negative
  inverse mass), and spring records (indices, rest length, stiffness,
  damping).
- `ParticleCount`, `SpringCount`, `PinnedParticleCount`.
- `DegenerateSpringCount`: springs skipped this step because both endpoints
  coincide (undefined force direction).
- `MaxSpringResidual`, `SpringResidualL2`: post-step |length − rest| residuals.
- `KineticEnergyBefore/After`, `TotalEnergyBefore/After`, `EnergyDrift`: total
  mechanical energy is kinetic + elastic + gravitational potential.
- `MaxStiffnessDtRatio`, `StabilityLimitExceeded`: the semi-implicit Euler
  stability ratio `omega * dt` (limit 2) over all springs, reported as a
  warning rather than an error.
- `Stable`, `FallbackApplied`: a non-finite post-step state fails closed to
  the unchanged input state with `NonFiniteState`.

## Limitations

- No optimized CPU backend and no GPU backend (forbidden until reference
  parity fixtures exist; future tasks must name this package as the oracle).
- No collision handling, no constraint projection, no bending/area/volume
  models — cloth-specific constraints are owned by METHOD-010.
- No runtime particle system, ECS authoring component, renderer pass, or
  editor UI; engine integration requires separate `src/physics`/`runtime`
  tasks per the methods/physics roadmap.
- Conditionally stable explicit integration; stiff springs need smaller
  timesteps, and the stability ratio diagnostic makes the limit observable.

## Verification

The reference backend is covered by
[`tests/unit/physics/Test.ParticleSpringReference.cpp`](../../../tests/unit/physics/Test.ParticleSpringReference.cpp).
The PR-fast smoke benchmark manifest is
[`benchmarks/physics/manifests/particle_spring_reference_smoke.yaml`](../../../benchmarks/physics/manifests/particle_spring_reference_smoke.yaml).

See [`paper.md`](paper.md) for the method intake notes.
