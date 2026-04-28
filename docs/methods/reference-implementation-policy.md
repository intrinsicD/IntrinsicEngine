# Reference Implementation Policy

Every method starts with a deterministic CPU reference backend.

## Rules

- Reference backend is correctness-first and fully documented.
- Degenerate cases must produce explicit diagnostics/status.
- No optimized/GPU backend may be treated as canonical truth.
- Correctness tests must target reference backend first.

## Required evidence before optimization

- Passing analytic/simple-case tests.
- Passing regression tests.
- Manifest and benchmark IDs committed.
- Numerical limitations documented.
