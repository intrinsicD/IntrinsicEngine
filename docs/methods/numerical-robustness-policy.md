# Numerical Robustness Policy

Method implementations must define stable behavior for non-ideal inputs.

## Required handling

- Degenerate geometry (zero-area faces, duplicate vertices, non-manifold edges).
- Ill-conditioned solves and failed convergence.
- Empty/undersized input domains.
- Precision mode assumptions and tolerances.

## Diagnostics requirements

Results must report:

- Convergence or failure reason.
- Iteration counts or stopping criteria where applicable.
- Applied fallback path.
- Quality/error metrics used by tests and benchmarks.
