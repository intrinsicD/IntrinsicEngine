# Tests

Correctness and regression tests for the progressive Poisson-disk sampler.

Progression (land with METHOD-012):

1. Poisson guarantee at every level boundary (`min_dist >= r_L`).
2. Determinism for fixed `(points, config, seeds)`; within-level shuffle preserves the guarantee.
3. Degenerate inputs (empty/one-point, duplicates, non-finite, invalid `dimension`/`radius_alpha`).
4. Cross-backend parity vs the GPU backend (METHOD-013).
