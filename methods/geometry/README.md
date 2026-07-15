# Geometry Methods

Method packages for geometry processing research (remeshing, parameterization, distance fields, etc.).

- [`signed_heat`](signed_heat/) — `geometry.signed_heat` CPU reference for
  surface signed geodesic distance from oriented halfedge curves.
- [`progressive_poisson`](progressive_poisson/) — `geometry.progressive_poisson`
  CPU reference for progressive Poisson-disk subsampling.
- [`boundary_first_flattening`](boundary_first_flattening/) —
  `geometry.boundary_first_flattening` CPU reference for Boundary First
  Flattening on an already-cut triangle disk.

Implemented CPU parameterization strategies enter the sandbox through
`EngineConfig.sandbox.parameterization` and the existing runtime editor facade.
The stable config tokens are `lscm`, `harmonic_cotangent`, `tutte_uniform`, and
`bff`; the configured command writes undoable `v:texcoord` values and produces
the pointer-free UV model consumed by the downstream editor. See
[engine config](../../docs/architecture/engine-config.md) and
[runtime config control](../../docs/architecture/runtime-config-control.md).
