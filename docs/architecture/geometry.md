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

As of RORG-093, canonical Geometry code is promoted to `src/geometry`. Remaining `src/legacy` geometry shims (if any) must be temporary, tracked, and removed via follow-up migration tasks.
