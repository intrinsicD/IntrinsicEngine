# Benchmarks

The executable correctness smoke is
`benchmarks/geometry/manifests/locally_optimal_projection_reference_smoke.yaml`.
It uses deterministic built-in noisy plane and sphere fixtures and makes no
performance claim.

The paired reference/candidate comparison is declared by
`benchmarks/geometry/manifests/lop_family_comparison_smoke.yaml`. Its frozen
five-pair median rule produced no adopted optimized strategy; see
`../reports/METHOD-019-result.md`.
