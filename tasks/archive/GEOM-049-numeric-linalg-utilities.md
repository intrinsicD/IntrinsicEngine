---
id: GEOM-049
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---

# GEOM-049 — Numeric / linear-algebra utilities: RPCA and Eigen map adapters

## Status

- Completed at `CPUContracted`. Commit: this commit (`Complete RPCA and Eigen map utilities`).
- Slice A shipped aliasing strided Eigen maps for scalar buffers, fixed-size GLM vector arrays, and typed property columns, with a copying numeric fallback for `bool` property columns.
- Slice B shipped deterministic Principal Component Pursuit / ADMM `RobustPCA` on top of `ComputeSVD`, including synthetic recovery, determinism, and fail-closed input coverage.

## Goal

- Add the linear-algebra utilities the engine lacked: a robust PCA / Principal Component Pursuit (low-rank + sparse) decomposition built on the existing `ComputeSVD`, strided Eigen maps over an array of fixed-size vectors viewed as an `N x dim` matrix, and a property-column-to-`Eigen::Map` adapter so property storage can be viewed (and mutated) as an Eigen matrix without copying.
- Land the work in two slices: a small adapter slice (strided/property Eigen maps) and a large algorithm slice (robust PCA via ADMM / augmented Lagrangian), both reaching `CPUContracted` (deterministic, fail-closed, contract-tested) for the surfaces they introduce.

## Non-goals

- No new sparse-solver seams or factorization backends; GEOM-020 / GEOM-023 / GEOM-024 own sparse-solver infrastructure and this task must not pre-empt those interfaces.
- No GPU backend, no offload path, and no performance/throughput claims for any new routine.
- No UI, visualization, or editor surface for PCA results.
- No replacement or reimplementation of `ComputeSVD`; RPCA consumes it as-is.
- No changes to the public numeric policy (`GEOM-005`) or robust-predicate/tolerance policy (`GEOM-007`); new code conforms to them.

## Context

- Before GEOM-049, `Geometry.Linalg` (`src/geometry/Geometry.Linalg.cppm` / `.cpp`) exported `DenseMatrix`, `SVDResult`, `NumericStatus`, `NumericDiagnostics`, and `ComputeSVD`, plus a contiguous scalar-buffer Eigen view via `EigenDenseMap` / `ConstEigenDenseMap`, but no strided fixed-vector map or robust-PCA routine.
- Before GEOM-049, `Geometry.Properties` (`src/geometry/Geometry.Properties.cppm` / `.cpp`) exposed `PropertyRegistry`, `PropertyBuffer<T>`, and `ConstPropertyBuffer<T>`, but property columns could not be handed to Eigen without a copy.
- `ComputeSVD` remains the only RPCA SVD building block; the PCP/ADMM loop reuses it for the singular-value soft-thresholding (low-rank) step, and applies element-wise L1 soft-thresholding for the sparse step.
- The geometry linalg and property tests stay under the existing `unit;geometry` label; GEOM-049 introduced no new CTest labels.

## Slice plan

- [x] Slice A (small): strided Eigen-map adapters over arrays of fixed-size vectors (`MapAsMatrix` / `MapVectorAsMatrix`) using `Eigen::Stride`, plus the property-column-to-`Eigen::Map` adapter (`MapProperty`) with dims-aware stride, Eigen-vs-fundamental element dispatch, and a `bool` fallback; including aliasing/stride/fallback tests. Closes the adapter surface at `CPUContracted`.
- [x] Slice B (large): robust PCA / Principal Component Pursuit (`RobustPCA`) via ADMM / augmented Lagrangian using singular-value and L1 soft-thresholding on top of `ComputeSVD`; including synthetic low-rank-plus-sparse recovery, determinism, and degenerate-input fail-closed tests. Closes the RPCA surface at `CPUContracted`.

## Required changes

- [x] In `src/geometry/Geometry.Linalg.cppm`, export a strided Eigen-map alias and adapter for an array of fixed-size vectors: add a `StridedEigenMap` / `ConstStridedEigenMap` alias built on `Eigen::Map<..., Eigen::OuterStride<>>` (or an explicit `Eigen::Stride`) and `MapAsMatrix(std::span<const double> data, std::size_t rows, std::size_t dim, std::ptrdiff_t strideElements)` plus a writable overload, returning an `N x dim` view that aliases the underlying buffer.
- [x] In `src/geometry/Geometry.Linalg.cppm`, export `MapVectorAsMatrix` overloads accepting a `std::span` of a fixed-size vector type (the engine's `Vec2`/`Vec3`/`VecN` element layout) and producing the same strided `N x dim` matrix view, deriving the stride from the element size in scalars.
- [x] In `src/geometry/Geometry.Linalg.cppm`, declare a `RobustPCAResult` struct (low-rank matrix `L`, sparse matrix `S`, recovered rank, iteration count, achieved residual, plus a `NumericStatus` / `NumericDiagnostics`) and a `RobustPCAOptions` struct (lambda / sparsity weight with a documented default of `1/sqrt(max(rows,cols))`, ADMM penalty `mu`, max iterations, convergence tolerance) and the entry point `RobustPCAResult RobustPCA(const DenseMatrix& matrix, const RobustPCAOptions& options = {});`.
- [x] In `src/geometry/Geometry.Linalg.cpp`, implement concrete `MapAsMatrix` bodies; keep `MapVectorAsMatrix` as interface-visible template shims because the vector element type is part of the return type. Validate `rows`, `dim`, and stride against the span extent, failing closed (no out-of-bounds view) on inconsistent sizes.
- [x] In `src/geometry/Geometry.Linalg.cpp`, implement `RobustPCA` as a deterministic PCP/ADMM (augmented-Lagrangian) loop: initialize `L=0`, `S=0`, dual `Y`; per iteration compute the low-rank update by `ComputeSVD` followed by singular-value soft-thresholding at `1/mu`, the sparse update by element-wise L1 soft-thresholding at `lambda/mu`, update the dual, and terminate on the relative Frobenius residual `||M - L - S||_F / ||M||_F <= tolerance` or `maxIterations`. Fail closed (`NumericStatus` non-OK with a diagnostic, no NaNs, no asserts) on empty, non-finite, or zero-norm input, and propagate hard `ComputeSVD` failures while accepting rank-deficient SVDs as the expected low-rank path.
- [x] In `src/geometry/Geometry.Properties.cppm`, export a `MapProperty` adapter (`Map(PropertyBuffer<T>&)` and a `ConstPropertyBuffer<T>` overload) returning a dims-aware strided `Eigen::Map` over the column's contiguous storage: dispatch on element type so that fundamental scalar elements yield an `N x 1` map and fixed-size Eigen/vector elements yield an `N x dim` strided map, with a `bool` column fallback (a copying view or an explicitly diagnosed unsupported path — never a reinterpret of `std::vector<bool>` storage).
- [x] Keep the `MapProperty` adapter and `bool` fallback inline in `src/geometry/Geometry.Properties.cppm` because the return type depends on the property element type, while preserving the dependency direction `Geometry.Properties` -> `Geometry.Linalg` (adapter pulls in `Geometry.Linalg`; `Geometry.Linalg` does not import `Geometry.Properties`).
- [x] Update `src/geometry/CMakeLists.txt` only if new translation units are added; reuse the existing `Geometry.Linalg` / `Geometry.Properties` module targets (declared via `intrinsic_add_module_library` with `FILE_SET CXX_MODULES`) rather than creating new module libraries.

## Tests

- [x] In `tests/unit/geometry/Test.LinearAlgebra.cpp`, add a case proving `MapAsMatrix` / `MapVectorAsMatrix` alias the underlying buffer: a write through the Eigen view is observable in the source array, with correct strided placement for `dim > 1` (verify off-diagonal/interleaved elements land at the right offsets, not just `dim == 1`).
- [x] Add a case proving `MapProperty` over a fundamental scalar column yields an `N x 1` aliasing map, and over a fixed-size vector column yields an `N x dim` strided aliasing map, with writes propagating back into the `PropertyBuffer`.
- [x] Add a `bool`-column fallback case asserting the documented behavior (copying view round-trips values, or unsupported path returns the explicit diagnostic) and that no undefined `std::vector<bool>` aliasing occurs.
- [x] Add a `RobustPCA` recovery case on a synthetic `M = L0 + S0` where `L0` is a known low rank `r` (outer product of random-but-fixed-seed factors) and `S0` is a sparse sign matrix: assert the recovered low-rank part has numerical rank `r` (singular-value gap) and that the sparse support is recovered within tolerance (support agreement fraction and `||L - L0||_F / ||L0||_F`, `||S - S0||_F / ||S0||_F` below documented thresholds).
- [x] Add a determinism case: `RobustPCA` on identical input (and identical options) returns bitwise-identical `L`, `S`, rank, and iteration count across repeated runs.
- [x] Add degenerate / fail-closed cases: empty matrix, zero-size dimension, non-finite (NaN/Inf) entries, and zero matrix each return a non-OK `NumericStatus` with a diagnostic and produce no NaNs; assert `RobustPCA` propagates a non-OK status when `ComputeSVD` fails.
- [x] Keep all new tests under the existing `unit;geometry` label (no new CTest label); do not add GPU/Vulkan/slow markers.

## Docs

- [x] Regenerate `docs/api/generated/module_inventory.md` via `tools/repo/generate_module_inventory.py` so the new exported symbols (`MapAsMatrix`, `MapVectorAsMatrix`, `RobustPCA`, `RobustPCAResult`, `RobustPCAOptions`, `MapProperty`) appear.
- [x] Document the RPCA contract (PCP/ADMM formulation, default `lambda`, convergence criterion, fail-closed behavior, determinism guarantee) and the Eigen-map aliasing/stride/bool-fallback semantics in the appropriate `docs/architecture/*` or geometry numeric reference, conforming to `GEOM-005` / `GEOM-007` language.
- [x] If any cross-references are added, ensure `tools/docs/check_doc_links.py` passes (no dangling links).

## Acceptance criteria

- [x] `MapAsMatrix`, `MapVectorAsMatrix`, and `MapProperty` produce strided Eigen views that alias the source buffer; a write through the view mutates the source for `dim == 1` and `dim > 1`, proven by the aliasing tests.
- [x] `MapProperty` correctly dispatches fundamental vs fixed-size element types and provides the defined `bool` fallback without undefined `std::vector<bool>` aliasing.
- [x] `RobustPCA` recovers the synthetic low-rank-plus-sparse matrix within the documented thresholds (low-rank rank `== r`, sparse-support agreement and Frobenius residuals below threshold) on the seeded test input.
- [x] `RobustPCA` is deterministic: repeated runs on identical input/options yield bitwise-identical results.
- [x] All degenerate inputs (empty, zero-size, non-finite, zero matrix) and `ComputeSVD` failure cause `RobustPCA` to fail closed with a non-OK `NumericStatus` and a diagnostic, producing no NaNs and triggering no asserts.
- [x] No new module library or CTest label is introduced; `Geometry.Linalg` does not import `Geometry.Properties`, and neither imports any layer above geometry.
- [x] All listed verification commands pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'geometry.*(LinearAlgebra|Linalg|Propert|RobustPCA|EigenMap)' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Introducing renderer / runtime / ECS / assets / platform / app dependencies into `src/geometry/*`; `Geometry.Linalg` and `Geometry.Properties` stay geometry -> core only, and `Geometry.Linalg` must not import `Geometry.Properties`.
- Adding a GPU backend, offload seam, or any new sparse-solver interface (those belong to GEOM-020 / GEOM-023 / GEOM-024).
- Claiming any performance or speed improvement for `RobustPCA` or the map adapters without a recorded baseline comparison.
- Mixing mechanical file moves or renames with the semantic additions in this task.
- Introducing unrelated feature work, new CTest labels (without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change), asserts, NaN-producing paths, or non-deterministic behavior.

## Maturity

- Stop-state pin: `CPUContracted`. Both slices close when the new surfaces (`MapAsMatrix` / `MapVectorAsMatrix` / `MapProperty` in Slice A; `RobustPCA` in Slice B) are fully implemented, deterministic, fail-closed on degenerate input per `GEOM-005` / `GEOM-007`, and covered by the contract tests above — not merely scaffolded. Promotion beyond `CPUContracted` (Operational / ParityProven) is out of scope for this task.
