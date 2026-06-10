# Correctness Tests

The package-level correctness tests live in
[`tests/unit/physics/Test.SphFluidReference.cpp`](../../../../tests/unit/physics/Test.SphFluidReference.cpp).
They cover kernel normalization and closed-form kernel values, uniform-grid
density recovery, exact momentum conservation of the symmetric pressure
force, viscosity smoothing, a toy fluid-column drop with boundary planes and
stability diagnostics, advisory neighbor-overflow reporting, invalid-input
validation, instability fail-closed fallback, and repeated-step determinism.
