# GEOM-008 — Geometry linear algebra and solver infrastructure

## Goal
- Establish the CPU linear-algebra infrastructure for geometry methods using GLM for public storage and Eigen3 for numerical kernels behind geometry-owned adapters.

## Non-goals
- No conversion of public geometry containers from GLM to Eigen types.
- No optional SuiteSparse/CHOLMOD/BLAS/LAPACK/MKL/CUDA solver backend in the first implementation.
- No broad rewrite of existing DEC/geodesic/parameterization algorithms in the dependency-introduction patch.
- No performance claims without benchmark baselines.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` plus declared third-party numerical dependency).
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- Current dependency configuration wires GLM but not Eigen3. The review recommends a hybrid policy: GLM remains engine-facing storage; Eigen3 provides mature dense/sparse numerical kernels internally.
- `Geometry.DEC` currently has a narrow CSR type and CG solver; future geometry papers need reusable sparse builders, decompositions, solver diagnostics, and dense decomposition utilities.

## Required changes
- [ ] Add Eigen3 through centralized dependency management in `cmake/Dependencies.cmake`, with offline-cache validation consistent with existing FetchContent dependencies.
- [ ] Define Eigen compile configuration, including `EIGEN_MPL2_ONLY`, in an appropriate target scope.
- [ ] Add a geometry-owned linear algebra module or backend namespace that hides Eigen details from the broad `Geometry` umbrella unless intentionally exported.
- [ ] Add GLM ↔ Eigen adapter utilities for fixed-size vectors/matrices and explicit `Eigen::Map` helpers for contiguous scalar buffers where alignment/stride rules are documented.
- [ ] Generalize or bridge the current DEC CSR representation to reusable sparse builders and matrix diagnostics without breaking existing DEC callers.
- [ ] Add solver result diagnostics for residuals, iteration counts, convergence reasons, conditioning warnings, and invalid input.
- [ ] Add dense utilities for SVD, QR, symmetric eigensolver, polar decomposition, covariance accumulation, and least-squares helpers as scoped wrappers around Eigen.
- [ ] Document later optional backend seams for Spectra and SuiteSparse/CHOLMOD after CPU reference parity and benchmark manifests exist.
- [ ] Update `src/geometry/CMakeLists.txt`, `Geometry.cppm`, and generated module inventory for any new module surfaces.

## Tests
- [ ] Add unit tests for GLM/Eigen adapters, alignment/stride assumptions, and fixed-size conversion round-trips.
- [ ] Add dense numerical tests for SVD/QR/eigen/least-squares on deterministic small systems.
- [ ] Add sparse builder/multiply/transpose/solver diagnostics tests, including singular and ill-conditioned cases.
- [ ] Add regression tests proving existing DEC/geodesic/parameterization tests still pass after any CSR bridge work.

## Docs
- [ ] Update `docs/architecture/geometry.md` with the GLM + Eigen3 policy and public/private type boundary.
- [ ] Update `docs/api/generated/module_inventory.md` after module surface changes.
- [ ] Update dependency/offline-cache notes if adding Eigen changes setup expectations.

## Acceptance criteria
- [ ] GLM remains the public storage vocabulary for existing geometry containers and renderer-facing data.
- [ ] Eigen3 is available to geometry CPU kernels through a documented, layer-safe adapter/backend boundary.
- [ ] New numerical utilities return structured diagnostics and do not depend on higher engine layers.
- [ ] Existing focused geometry tests continue to pass.
- [ ] Dependency validation works in online and offline-cache configurations.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests
ctest --test-dir build/ci --output-on-failure -R 'DEC|Geodesic|VectorHeatMethod|Parameterization|LinearAlgebra' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not expose Eigen types through existing broad public geometry APIs without a separate API-review task.
- Do not add optional heavyweight solver backends in the first Eigen infrastructure patch.
- Do not replace GLM in renderer/runtime-facing storage.
- Do not claim numerical or performance superiority without tests and benchmarks.

