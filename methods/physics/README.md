# Physics Methods

Method packages for simulation and dynamics methods. Physics methods provide
paper-backed reference backends and validation fixtures; they do not own the
engine physics world, ECS authoring state, runtime scheduling, rendering, or
platform integration.

## Selection Rules

- CPU reference first. Optimized CPU and GPU backends are forbidden until the
  reference backend, diagnostics, correctness tests, and benchmark manifest pass.
- Method packages report backend identity and parity deltas against the CPU
  reference; optimized/GPU backends are not compared against each other as the
  source of truth.
- Runtime integration follows only after a method contract is stable and a
  `src/physics` or `runtime` task records the owning layer.
- Rendering visualization is useful for demos and debugging, but it is not
  physics correctness evidence.
- Diagnostics must be machine-checkable: invalid input, unsupported shape or
  topology, convergence state, residual error, energy drift or conserved-quantity
  drift, and benchmark-quality metrics where applicable.

## Packages

- [`rigid_body_reference/`](rigid_body_reference/) — deterministic
  `cpu_reference` backend for fixed-step rigid-body integration and first-phase
  sphere/capsule/box contact contracts.
- [`particle_spring_reference/`](particle_spring_reference/) — deterministic
  `cpu_reference` backend for particle dynamics and mass-spring systems
  (semi-implicit Euler, Hooke springs with axial damping, pinning, stability
  and energy-drift diagnostics); the fixture base for later XPBD cloth and
  soft-body work.
- [`xpbd_cloth_reference/`](xpbd_cloth_reference/) — deterministic XPBD
  `cpu_reference` backend for cloth/thin shells over triangle meshes
  (compliant distance + opposite-vertex bending constraints, half-space
  collision parameters, residual/convergence and energy diagnostics).
- [`sph_fluid_reference/`](sph_fluid_reference/) — deterministic WCSPH
  `cpu_reference` backend for particle fluids (Poly6/Spiky/viscosity
  kernels, clamped pressure, half-space boundaries, density/compression
  and neighbor diagnostics).

## Roadmap

This roadmap was established by
[`ARCH-002`](../../tasks/archive/ARCH-002-physics-phenomena-roadmap.md). It keeps
non-rigid physics work reference-first and separates method packages from engine
runtime systems.

| Rank | Phenomenon | Default path | Data model | Verification shape | Follow-up |
| --- | --- | --- | --- | --- | --- |
| 0 | Rigid bodies and first contacts | Existing CPU reference plus `src/physics` world contracts | Bodies with primitive child shapes, masses, velocities, contacts, and solver diagnostics | Fixed-step deterministic fixtures, analytic primitive contacts, residual penetration / energy drift diagnostics | [`METHOD-001`](../../tasks/archive/METHOD-001-rigid-body-dynamics-reference-backend.md), [`PHYSICS-002`](../../tasks/archive/PHYSICS-002-collision-broadphase-narrowphase-contract.md), [`PHYSICS-003`](../../tasks/archive/PHYSICS-003-constraints-islands-and-solver-diagnostics.md) |
| 1 | Particles and mass-spring systems | Method package first, then optional runtime particle system | Particle arrays, inverse masses, springs, damping, pins, external forces | Two-particle spring analytic fixtures, conserved quantity drift, stability diagnostics, smoke benchmark | [`METHOD-009`](../../tasks/archive/METHOD-009-particle-spring-reference-backend.md) |
| 2 | Cloth and shells | XPBD cloth method package first; runtime/editor integration later | Triangle mesh cloth state, distance/bending/area constraints, collision query inputs | Patch stretch/bend fixtures, pinned cloth determinism, constraint residuals, degeneracy diagnostics | [`METHOD-010`](../../tasks/archive/METHOD-010-xpbd-cloth-shell-reference-backend.md) |
| 3 | SPH particle fluids | Method package first; no realtime/runtime fluid claim until reference fixtures pass | Fluid particles, neighbor search, smoothing kernels, pressure/density/viscosity parameters | Dam-break/toy column smoke, density error, divergence or incompressibility proxy, stability diagnostics | [`METHOD-011`](../../tasks/archive/METHOD-011-sph-fluid-reference-backend.md) |
| 4 | Soft bodies, FEM, PBD/XPBD solids | Defer until cloth/particles establish constraint diagnostics and collision coupling | Tetrahedral or surface volume state, material parameters, constraints or element energies | Patch/tet analytic cases, convergence and inversion diagnostics | Future `METHOD-*` after METHOD-010 |
| 5 | Grid fluids, FLIP/APIC, shallow water | Defer until particle-fluid reference and grid storage policy exist | Grid/marker particles, pressure projection, boundary conditions | Manufactured solutions, mass conservation, pressure residuals | Future `METHOD-*` / `ARCH-*` |
| 6 | Fracture, damage, granular materials | Defer; needs robust contact and material failure contracts first | Damage fields, fracture graph/mesh updates, granular particles | Conservation and failure-mode diagnostics; topology-change regressions | Future `METHOD-*` after PHYSICS-002/003 |
| 7 | Thermal diffusion and scalar/vector PDE fields | Research method package unless a runtime use case appears | Mesh/grid scalar fields, boundary/initial conditions, time integrator | Manufactured solution, residual norms, conservation/monotonicity diagnostics | Future `METHOD-*` after sparse/direct solver gates |

## Engine Integration Gates

- `src/physics` may consume method fixtures or parity expectations, but method
  packages do not become production engine systems by default.
- `runtime` owns fixed-step scheduling, ECS synchronization, events, and editor
  command surfaces for any promoted physics behavior.
- GPU acceleration is never opened from this roadmap alone. A future GPU backend
  task must name the completed CPU reference backend, parity metrics, benchmark
  manifest, and host capability gate.
