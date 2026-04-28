# Methods

This directory contains scientific method packages used to implement and validate papers in geometry processing, rendering, and physics.

## Paper-to-benchmark pipeline

Each method package should follow this lifecycle:

1. Intake paper metadata and define the method contract (`Input`, `Params`, `Result`, diagnostics).
2. Implement a CPU reference backend first.
3. Add correctness tests for analytic and regression cases.
4. Add benchmark manifests and smoke runners.
5. Add optimized CPU backend and compare against reference output.
6. Add GPU backend only after reference parity is demonstrated.
7. Document numerical limitations, degenerate-case behavior, and diagnostics.

## Directory layout

- `_template/`: starter package that can be copied for a new method.
- `geometry/`: geometry processing methods.
- `rendering/`: rendering and light transport methods.
- `physics/`: simulation and dynamics methods.
- `papers/`: paper notes, intake records, and cross-method references.

## Package expectations

Each concrete method package should include:

- `README.md`: problem statement, implementation scope, and backend status.
- `method.yaml`: machine-readable method manifest.
- `paper.md`: paper summary and citation metadata.
- `tests/`: correctness and regression test assets/instructions.
- `benchmarks/`: benchmark manifests and runner notes.
- `reports/`: validation and performance reports.

The `_template` package in this directory is intentionally complete enough that agents can clone it and begin implementation without additional structure decisions.

## Method documentation

Method policy and onboarding docs live in `docs/methods/`:

- `docs/methods/index.md`
- `docs/methods/method-template.md`
- `docs/methods/reference-implementation-policy.md`
- `docs/methods/backend-policy.md`
- `docs/methods/numerical-robustness-policy.md`
- `docs/methods/dataset-policy.md`
- `docs/methods/report-template.md`
