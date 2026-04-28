# Geometry Architecture

`geometry` is the canonical home for geometry-processing algorithms and mesh-domain operations.

## Responsibilities

- Deterministic geometric kernels and data transformations.
- Robust handling of degenerate/non-ideal input cases.
- Integration seams for method packages and benchmark harnesses.

## Dependencies

- Allowed: `core`.
- Disallowed: runtime/app-specific ownership and rendering backend internals.

## Migration note

During reorganization, legacy geometry implementations may temporarily remain elsewhere; canonical placement target is `src/geometry`.
