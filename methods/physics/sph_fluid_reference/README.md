# SPH Particle Fluid Reference Backend

`physics.sph_fluid_reference` is the deterministic CPU reference backend for
weakly compressible SPH particle fluids (Müller et al. 2003). It is a method
package, not an engine system: it imports no ECS, runtime, platform,
graphics, or RHI code. Later optimized CPU/GPU SPH and any runtime fluid
work validate against this backend as the correctness oracle.

## Contract

- Backend identity: `cpu_reference`.
- Units: meters, kilograms, seconds; density in kg/m^3.
- State: particle positions and velocities with uniform particle mass.
- Fields: Poly6 density (with self-contribution), clamped ideal-gas pressure
  `p = max(0, k (rho - rho_0))`, symmetric Spiky-gradient pressure force,
  viscosity-Laplacian force, gravity.
- Integration: fixed-step semi-implicit Euler.
- Neighbors: deterministic O(N^2) all-pairs enumeration in index order. The
  `MaxNeighborLimit` parameter is advisory — overflow is reported, physics
  is never truncated.
- Boundaries: static half-space planes; penetrating particles are projected
  to the surface and their inward normal velocity is reflected scaled by
  `(1 + BoundaryRestitution)` (0 = inelastic).

## Diagnostics

`Step(...)` returns machine-checkable diagnostics:

- `Code`: validation result for timestep, kernel/material parameters,
  boundaries, and particle state.
- `ParticleCount`, `TotalMass` (exactly `mass * count`, conserved).
- `AverageDensity`, `MinDensity`, `MaxDensity`, `MaxCompression`
  (`max(0, (rho - rho_0)/rho_0)` incompressibility proxy), and
  `AverageDensityError` (mean relative |rho - rho_0|).
- `MaxNeighborCount` and advisory `NeighborOverflowCount`.
- `MaxVelocity`, `KineticEnergyBefore/After`, `EnergyDrift`.
- `Stable`, `FallbackApplied`: a non-finite post-step state fails closed to
  the unchanged input state with `NonFiniteState`.

## Limitations

- No optimized CPU backend and no GPU backend (forbidden until reference
  parity fixtures exist; future tasks must name this package as the oracle).
- No neighbor acceleration structure, no surface tension, no solid
  coupling, no multiphase model, no grid-based FLIP/APIC or
  pressure-projection solver.
- Explicit weakly compressible stepping is conditionally stable; stiff
  parameters require smaller timesteps.
- No runtime fluid system, ECS component, renderer pass, or editor UI.

## Verification

The reference backend is covered by
[`tests/unit/physics/Test.SphFluidReference.cpp`](../../../tests/unit/physics/Test.SphFluidReference.cpp).
The PR-fast smoke benchmark manifest is
[`benchmarks/physics/manifests/sph_fluid_reference_smoke.yaml`](../../../benchmarks/physics/manifests/sph_fluid_reference_smoke.yaml).

See [`paper.md`](paper.md) for the method intake notes.
