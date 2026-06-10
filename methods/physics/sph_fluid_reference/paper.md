# SPH Particle Fluid Reference Intake

## Citation

- **Title:** Particle-Based Fluid Simulation for Interactive Applications
- **Authors:** Matthias Müller, David Charypar, Markus Gross
- **Venue / Year:** ACM SIGGRAPH/Eurographics Symposium on Computer
  Animation (SCA), 2003
- **DOI:** 10.2312/SCA03/154-159
- **URL:** https://matthias-research.github.io/pages/publications/sca03.pdf

## Core Claim

Smoothed particle hydrodynamics evaluates fluid fields with radially
symmetric smoothing kernels over particle neighborhoods. With the Poly6
kernel for density, the Spiky kernel gradient for pressure forces (avoiding
the vanishing-gradient clustering problem), and a positive-Laplacian
viscosity kernel, a weakly compressible particle fluid runs stably at
interactive rates. This package captures that formulation as a fixed-step
deterministic CPU reference for later optimized/GPU SPH work.

## Mathematical Formulation

Kernels with support radius `h` (zero for `r >= h`):

```text
W_poly6(r)      = 315 / (64 pi h^9) * (h^2 - r^2)^3
|grad W_spiky|  =  45 / (pi h^6)    * (h - r)^2
lap W_visc(r)   =  45 / (pi h^6)    * (h - r)
```

Per particle `i` with uniform mass `m`:

```text
rho_i = sum_j m * W_poly6(|x_i - x_j|)            (includes self term)
p_i   = max(0, k * (rho_i - rho_0))               (clamped ideal gas)
F_p   = sum_{j != i} m * (p_i + p_j) / (2 rho_j) * |grad W_spiky| * dir_ij
F_v   = sum_{j != i} mu * m * (v_j - v_i) / rho_j * lap W_visc
a_i   = (F_p + F_v) / rho_i + g
v_i  += a_i * dt;  x_i += v_i * dt                (semi-implicit Euler)
```

Boundary half-spaces project penetrating particles back to the surface and
reflect the inward normal velocity scaled by `(1 + restitution)`.

## Inputs and Outputs

- Inputs: particle positions/velocities; smoothing length, particle mass,
  rest density, stiffness, viscosity, gravity, boundary restitution,
  advisory neighbor limit, and static half-space boundary planes.
- Outputs: stepped particle states plus diagnostics: validation code, total
  mass, density statistics (average/min/max, compression proxy, mean
  relative density error), neighbor counts with advisory overflow, max
  velocity, kinetic energy drift, and fail-closed fallback status.

## Degenerate / Edge Cases

- Invalid timestep, non-positive smoothing length/mass/rest density,
  negative stiffness/viscosity, out-of-range restitution, non-finite
  gravity, degenerate boundary normals, and non-finite particle state
  return explicit validation codes with the input unchanged.
- Coincident particles contribute density but no pressure direction (the
  direction is undefined below the distance epsilon); viscosity still
  applies. Deterministic index-ordered accumulation keeps repeated runs
  bitwise identical.
- The pressure clamp removes tensile instability at free surfaces; surface
  particles legitimately report `rho < rho_0`.
- A non-finite post-step state (stiffness/timestep explosion) fails closed:
  the input state is returned unchanged with `NonFiniteState` and
  `FallbackApplied`.

## Why This Slice First

SPH is rank 3 of the ARCH-002 phenomena roadmap, after particles/springs
and cloth established deterministic particle state and constraint
diagnostics. The reference stays O(N^2) and grid-free on purpose: neighbor
acceleration structures are an optimized-backend concern that must prove
parity against this oracle first.
