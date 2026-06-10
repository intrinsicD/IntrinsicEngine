# XPBD Cloth and Shell Reference Intake

## Citation

- **Title:** XPBD: Position-Based Simulation of Compliant Constrained Dynamics
- **Authors:** Miles Macklin, Matthias Müller, Nuttapong Chentanez
- **Venue / Year:** ACM SIGGRAPH Conference on Motion in Games (MIG), 2016
- **DOI:** 10.1145/2994258.2994272
- **URL:** https://matthias-research.github.io/pages/publications/XPBD.pdf

Secondary context: Müller et al., "Position Based Dynamics", VRIPHYS 2006 /
JVCIR 2007 (constraint projection and distance-based bending pairs).

## Core Claim

XPBD extends position-based dynamics with per-constraint Lagrange
multipliers and a compliance parameter, making constraint stiffness
timestep- and iteration-count-independent. For cloth over a triangle mesh,
structural (edge) and bending (opposite-vertex) distance constraints with
compliance `alpha` converge to the implicit-Euler solution of an energy
potential `U = C^T C / (2 alpha)` instead of the iteration-dependent
stiffness of classic PBD.

## Mathematical Formulation

Per substep `dt` with gravity `g`:

```text
predict:  v_i += g * dt;  x_i += v_i * dt          (free particles)
solve (per constraint, per iteration):
  C        = |x_A - x_B| - L0
  grad     = (x_A - x_B) / |x_A - x_B|
  alpha~   = compliance / dt^2
  dlambda  = (-C - alpha~ * lambda) / (w_A + w_B + alpha~)
  lambda  += dlambda
  x_A     += w_A * grad * dlambda
  x_B     -= w_B * grad * dlambda
collide:  x_i -= N * min(0, (dot(N, x_i) - offset) / dot(N, N))   (half-space planes)
update:   v_i  = (x_i - x_i_prev) / dt;  v_i *= max(0, 1 - damping * dt)
```

Lagrange multipliers reset to zero at the start of each step. Pinned
particles (`w = 0`) never integrate, ignore corrections, and anchor
constraints.

## Inputs and Outputs

- Inputs: particle array, triangle topology, structural + bending distance
  constraints (built deterministically by `BuildClothFromTriangles` or
  supplied explicitly), and step parameters including collision half-spaces.
- Outputs: stepped cloth state plus diagnostics: validation code,
  topology/pinned/degenerate counts, stretch/bend residuals (max, L2),
  convergence against the residual tolerance, kinetic/mechanical energy
  drift, unsupported collider count, and fail-closed fallback status.

## Degenerate / Edge Cases

- Invalid timestep/iterations, non-finite particle state, bad constraint
  indices, self-constraints, negative rest length/compliance, repeated-index
  or out-of-range triangles, and non-finite or zero-normal colliders return
  explicit validation codes with the input state unchanged.
- Zero-area triangles with valid indices are reported via
  `DegenerateTriangleCount` and do not block the step (constraints operate
  on edges, which remain well-defined).
- Coincident constraint endpoints have an undefined gradient; the
  projection is skipped deterministically and counted once per step.
- A non-finite post-step state fails closed: the input state is returned
  unchanged with `NonFiniteState`/`FallbackApplied`.

## Why This Slice First

Cloth is rank 2 of the ARCH-002 phenomena roadmap, after particles/springs
(METHOD-009) established the minimal deterministic particle state and
diagnostics shape. XPBD distance+bend constraints over triangle meshes are
the smallest cloth contract that later optimized CPU/GPU cloth, dihedral
bending, and runtime integration can validate against.
