# Correctness Tests

The package-level correctness tests live in
[`tests/unit/physics/Test.ParticleSpringReference.cpp`](../../../../tests/unit/physics/Test.ParticleSpringReference.cpp).
They cover analytic free-fall and two-particle spring fixtures, rest-length
equilibrium, exact momentum/center-of-mass conservation, bounded harmonic
energy drift, damped hanging-spring equilibrium, pinned-particle behavior,
degenerate and invalid-input validation, instability fail-closed fallback,
and deterministic repeated-step regression coverage.
