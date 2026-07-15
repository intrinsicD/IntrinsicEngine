# Source surface

This package contains no parallel executable source. The CPU reference is
owned by `METHOD-023` and lives in
`src/geometry/Geometry.Parameterization.Bff.cppm` plus its implementation unit,
re-exported by the existing `Geometry.HalfedgeMesh.Parameterization` module so
it can reuse geometry-owned boundary, cotangent-assembly,
sparse-factorization, and diagnostics facilities.

No method-library wrapper, runtime adapter, optimized backend, or GPU backend
is planned without a later measured need.
