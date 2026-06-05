# Rigid Body Dynamics Reference Intake

## Citation

- **Title:** Rigid Body Simulation
- **Authors:** David Baraff
- **Venue / Year:** SIGGRAPH course notes, 1997
- **DOI:**
- **URL:** https://www.cs.cmu.edu/~baraff/sigcourse/

## Core Claim

Rigid-body motion can be represented by body pose, velocity, mass/inertia, and
constraint/contact impulses. For this first reference slice, IntrinsicEngine
captures the deterministic fixed-step integration and simple normal-impulse
contact contract that future physics implementations must match on analytic
cases before optimization.

## Mathematical Formulation

Dynamic bodies advance with semi-implicit Euler:

```text
v(t + dt) = damping * (v(t) + g * dt)
x(t + dt) = x(t) + v(t + dt) * dt
q(t + dt) = normalize(angle_axis(|omega| * dt, normalize(omega)) * q(t))
```

Supported contacts use a normal-only impulse:

```text
j = -(1 + restitution) * dot(v_b - v_a, n) / (inv_mass_a + inv_mass_b)
v_a -= n * j * inv_mass_a
v_b += n * j * inv_mass_b
```

Positional correction is applied along the same normal using the configured
penetration slop and correction percentage.

## Inputs and Outputs

- Inputs: body states, fixed-step parameters, and explicit child shapes for
  spheres, capsules, and boxes.
- Outputs: stepped body states, generated contacts, validation diagnostics,
  penetration residuals, and energy drift.

## Degenerate / Edge Cases

- Invalid timestep, zero solver iterations, NaN/Inf state, invalid mass,
  invalid inertia, and invalid shapes return explicit validation codes.
- Overlapping static/static starts report penetration and fail convergence
  rather than mutating immovable bodies.
- Unsupported dynamic shape pairs are reported in diagnostics and do not fake
  contacts.

## Implementation Notes

This package intentionally uses scalar CPU code and deterministic containers.
It does not import engine runtime, ECS, graphics, platform, or RHI layers.
