# Particle and Mass-Spring Dynamics Reference Intake

## Citation

- **Title:** Physically Based Modeling: Principles and Practice — Particle
  System Dynamics
- **Authors:** Andrew Witkin, David Baraff
- **Venue / Year:** SIGGRAPH course notes, 2001 (originally 1997)
- **DOI:**
- **URL:** https://www.cs.cmu.edu/~baraff/pbm/pbm.html

Secondary stability/strain context: Xavier Provot, "Deformation Constraints in
a Mass-Spring Model to Describe Rigid Cloth Behavior", Graphics Interface 1995.

## Core Claim

Particle systems with pairwise spring forces are the smallest deterministic
state model for deformable dynamics: each particle carries position, velocity,
and inverse mass; springs contribute equal-and-opposite Hooke forces with
axial damping. This package captures that contract as a fixed-step CPU
reference so later XPBD cloth (METHOD-010), soft-body, and runtime particle
work can validate against a canonical oracle.

## Mathematical Formulation

Spring force on particle A for a spring (A, B) with rest length `L0`,
stiffness `k`, and axial damping `c`:

```text
d    = x_B - x_A
dhat = d / |d|
F_A  = (k * (|d| - L0) + c * dot(v_B - v_A, dhat)) * dhat
F_B  = -F_A
```

Non-pinned particles advance with semi-implicit (symplectic) Euler:

```text
v(t + dt) = drag * (v(t) + (g + F / m) * dt)
x(t + dt) = x(t) + v(t + dt) * dt
drag      = max(0, 1 - global_damping * dt)
```

Pinned particles (`inverse mass = 0`) never integrate and discard forces.

Total mechanical energy used for drift diagnostics:

```text
E = sum_particles 1/2 m |v|^2  -  sum_particles m * dot(g, x)
  + sum_springs   1/2 k (|d| - L0)^2
```

## Inputs and Outputs

- Inputs: particle array (position, velocity, inverse mass), spring records
  (index pair, rest length, stiffness, damping), and step parameters (fixed
  delta time, gravity, global damping).
- Outputs: stepped particle states plus diagnostics: validation code,
  pinned/degenerate counts, post-step spring residuals (max and L2), kinetic
  and total energy before/after with drift, stiffness-vs-timestep stability
  ratio, and fail-closed fallback status.

## Degenerate / Edge Cases

- Invalid timestep, non-finite particle state, negative inverse mass, bad
  spring indices, self-springs, and negative/non-finite spring parameters
  return explicit validation codes and leave the input state unchanged.
- Coincident particles joined by a spring have no defined force direction; the
  spring is skipped for the step and counted in `DegenerateSpringCount`.
- Semi-implicit Euler on an undamped spring is stable for `omega * dt < 2`
  with `omega = sqrt(k * (1/m_A + 1/m_B))`; the maximum ratio is reported and
  `StabilityLimitExceeded` flags configurations beyond the limit.
- A non-finite post-step state (numerical explosion) fails closed: the input
  state is returned unchanged with `NonFiniteState`/`FallbackApplied` set.

## Why This Slice First

Particles and springs provide the smallest deterministic state model that
later cloth, soft-body, and particle-runtime work can reuse as fixtures
(ARCH-002 phenomena roadmap, rank 1). The reference stays collision-free and
constraint-free on purpose; XPBD constraint projection enters with METHOD-010.
