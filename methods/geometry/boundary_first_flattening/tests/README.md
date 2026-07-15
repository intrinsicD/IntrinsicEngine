# Tests

Correctness coverage is at
`tests/unit/geometry/Test.BoundaryFirstFlattening.cpp` in the `unit;geometry`
suite and lands with the `METHOD-023` CPU reference implementation.

The bounded contract requires:

- `AutomaticConformal` finite UVs and conformal diagnostics on a triangle disk;
- approximate target-length fitting with explicit length and closure residuals;
- `TargetAngles` fitting in radians with an explicit angle residual and a
  required total of `2*pi` within tolerance;
- deterministic normalization and repeated-run output;
- a closed-form irregular-triangle regression for the paper's per-edge
  best-fit closure weights, asymmetric target angles, and zero-interior path;
- automatic-mode quality invariance under small and large uniform input scales;
- fail-closed coverage for non-disk topology, malformed arrays, non-finite
  data, and inconsistent or overflowing angle totals; singular-solve and
  unusable-diagnostics outcomes remain explicit fail-closed status branches;
- closed and non-disk inputs fail closed; no cone/cut payload exists on the
  public surface to be mistaken for topology repair.
