# Correctness Tests

The package-level correctness tests live in
[`tests/unit/physics/Test.XpbdClothReference.cpp`](../../../../tests/unit/physics/Test.XpbdClothReference.cpp).
They cover deterministic pinned-patch stretch/bend fixtures, rigid
zero-compliance projection, topology builder output, pinned-vertex behavior,
half-space collision projection, degenerate triangle/constraint diagnostics,
invalid-input validation, unsupported collider reporting, instability
fail-closed fallback, and repeated-step determinism.
