---
id: GEOM-048
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-28
---

# GEOM-048 — Statistics accumulators and robust estimation kernels

## Goal

- [x] Add a new geometry-layer module `Geometry.Statistics` providing a mergeable streaming scalar accumulator (Welford with Pébay/Terriberry parallel-combine), higher moments (skewness/kurtosis via M2/M3/M4), and a running median, plus free functions `Median(std::vector<T>)` and `Quantile(std::vector<T>, double q)`.
- [x] Add a new geometry-layer module `Geometry.Robust` providing a family of robust loss/weight kernels (L2, L1, Huber, Tukey biweight, Welsch, Lorentzian, Cauchy), each exposing `Rho`/`Psi`/`Weight` functions suitable for IRLS / M-estimation.
- [x] Add shared domain-clamping inverse-trig utilities `SafeAcos`/`SafeAsin` (in `Geometry.Statistics`) so call sites stop using ad-hoc inline clamps.
- [x] Wire an optional IRLS robust-weighting path into `Geometry.Registration` ICP that computes per-residual weights from a selected `Geometry.Robust` kernel, augmenting the existing hard percentile trimming (`RejectOutliers`) without removing it as the default.
- [x] All new code is deterministic and fail-closed on degenerate/empty/non-finite input with explicit diagnostics, per GEOM-005 and GEOM-007 (no asserts, no NaN propagation).

## Non-goals

- No GPU backend, compute shaders, or RHI usage; this is pure CPU geometry-layer math.
- No UI, visualization, editor, or runtime wiring.
- Do not remove or disable the existing `RejectOutliers` percentile-trimming path; it remains the ICP default.
- No rotation/registration solver redesign beyond inserting the optional per-residual weighting; the Kabsch/Umeyama core stays as-is.
- No new CTest labels; reuse the existing `unit;geometry` label.

## Context

- Status: completed at `CPUContracted`. `Geometry.Statistics` and
  `Geometry.Robust` are promoted CPU geometry modules, and
  `Geometry.Registration` has an explicit default-off robust weighting path that
  preserves the existing percentile-trimming default. Commit: this commit
  ("Complete robust statistics and ICP weighting").
- `CovarianceAccumulator` lives in `Geometry.Linalg` (`src/geometry/Geometry.Linalg.cppm:100`, impl `Geometry.Linalg.cpp:141`) and is 3D-covariance-specific: it has `Reset`/`Add(glm::dvec3)`/`Result()` but no `merge`/`operator+` and no scalar M3/M4 moments. The new scalar accumulator is complementary and does not modify `CovarianceAccumulator`.
- Before GEOM-048, `Geometry.Registration` performed only hard outlier handling via `RejectOutliers(std::vector<CorrespondencePair>&, double inlierRatio)`, called from the ICP loop using `params.InlierRatio`.
- Before GEOM-048, the only nearby kernel family was KDE for probability density, not robust loss/weighting.
- Before GEOM-048, generic order statistics were absent: only spatial-split `nth_element` heuristics (KD/BVH builders) and legacy frame-time helpers existed.
- Before GEOM-048, `SafeAcos`/`SafeAsin` clamping was duplicated ad-hoc at call sites.
- Layering: `Geometry.Statistics` and `Geometry.Robust` must depend only on `core` (and standard library / glm as already used in geometry); they must not import assets/runtime/graphics/rhi/ecs/app/platform. `Geometry.Registration` already lives in the geometry layer and may import both new modules.

## Slice plan

- [x] Slice A (CPUContracted): `Geometry.Statistics` scalar accumulator (mean/variance/M2/M3/M4, skewness/kurtosis), `merge`/`operator+`, running median, `Median`/`Quantile` free functions, and `SafeAcos`/`SafeAsin`, with full unit tests. No registration changes in this slice.
- [x] Slice B (CPUContracted): `Geometry.Robust` kernel family and the optional ICP IRLS weighting path in `Geometry.Registration`, with kernel unit tests and an ICP-with-outliers convergence test. Depends on Slice A for `SafeAcos`/`SafeAsin` reuse where applicable.

## Required changes

- [x] Create `src/geometry/Geometry.Statistics.cppm` exporting `export module Geometry.Statistics;` with: a mergeable scalar accumulator type (e.g. `StreamingMoments`) exposing `Add(double)`, `Count()`, `Mean()`, `Variance()` (sample + population), `Skewness()`, `Kurtosis()` (excess), `Reset()`, `Merge(const StreamingMoments&)` and `operator+`; a `RunningMedian` type with `Add`/`Median`; and free function templates `Median`/`Quantile`. Keep only exported decls, small inline accessors, and templates in the interface.
- [x] Create `src/geometry/Geometry.Statistics.cpp` (`module Geometry.Statistics;`) holding non-trivial bodies: the Pébay/Terriberry parallel-combine update and merge math for M2/M3/M4, the running-median heap maintenance, and the diagnostic-returning quantile interpolation. Fail closed (explicit diagnostic, no NaN) on empty input, `Count()==0` moment queries, non-finite samples, and `q` outside `[0,1]`.
- [x] In `Geometry.Statistics.cppm`/`.cpp` add `SafeAcos(double)` and `SafeAsin(double)` that clamp the argument to `[-1, 1]` before calling `std::acos`/`std::asin`, returning a finite result for any finite input and a defined fail-closed result/diagnostic for non-finite input.
- [x] Create `src/geometry/Geometry.Robust.cppm` exporting `export module Geometry.Robust;` with an enum of kernel kinds (`L2`, `L1`, `Huber`, `Tukey`, `Welsch`, `Lorentzian`, `Cauchy`) and, for each, `Rho(r, scale)`, `Psi(r, scale)`, and `Weight(r, scale)` (where `Weight(r) = Psi(r)/r` with the correct `r→0` limit). Provide a dispatch entry point (e.g. `RobustWeight(kind, r, scale)`).
- [x] Create `src/geometry/Geometry.Robust.cpp` (`module Geometry.Robust;`) with the analytic kernel bodies; fail closed on non-positive/non-finite `scale` and non-finite `r` (explicit diagnostic, no NaN/Inf returned).
- [x] Register both modules in `src/geometry/CMakeLists.txt` via the existing `intrinsic_add_module_library` target using `target_sources(... FILE_SET CXX_MODULES FILES ...)` for the `.cppm` interfaces and listing the `.cpp` implementation units.
- [x] Re-export the new modules from the geometry umbrella `src/geometry/Geometry.cppm` if and only if existing sibling modules are aggregated there; otherwise leave the umbrella unchanged.
- [x] In `Geometry.Registration.cppm`, add an optional robust-weighting configuration to the ICP params (e.g. a `RobustKernel` kind selector + `RobustScale`, defaulting to a "none / use RejectOutliers" mode that preserves current behavior).
- [x] In `Geometry.Registration.cpp`, import `Geometry.Robust` and, when a robust kernel is selected, compute per-residual weights for each `CorrespondencePair` and fold them into the weighted point-to-point/point-to-plane normal-equation assembly; keep the `RejectOutliers(pairs, params.InlierRatio)` call (`Geometry.Registration.cpp:575`) as the default path when no kernel is selected.

## Tests

- [x] Add `tests/unit/geometry/Test_Statistics.cpp` (label `unit;geometry`): accumulator `Mean`/`Variance`/`Skewness`/`Kurtosis` match a batch reference over a fixed sample set within tolerance.
- [x] In `Test_Statistics.cpp`: `Merge(A, B)` (and `A + B`) equals an accumulator built over the concatenation of A's and B's samples (commutative and associative within tolerance).
- [x] In `Test_Statistics.cpp`: `Median` and `Quantile` match a sorted-reference computation for both odd- and even-sized inputs, including `q ∈ {0, 0.25, 0.5, 0.75, 1}`.
- [x] In `Test_Statistics.cpp`: `SafeAcos`/`SafeAsin` clamp out-of-domain inputs (e.g. `1.0+eps`, `-1.0-eps`) to finite results equal to the endpoint values; degenerate inputs (empty vector, `Count()==0` moment query, non-finite sample, `q` outside `[0,1]`, non-finite trig input) fail closed with the documented diagnostic and emit no NaN/Inf.
- [x] Add `tests/unit/geometry/Test_RobustKernels.cpp` (label `unit;geometry`): each kernel's `Weight`/`Psi`/`Rho` matches its analytic closed form at sampled residuals, the `r→0` weight limit is finite, and `Weight` is monotonically non-increasing in `|r|` (large residuals are downweighted relative to small ones).
- [x] In `Test_RobustKernels.cpp`: non-positive/non-finite `scale` and non-finite `r` fail closed with diagnostics and no NaN/Inf output.
- [x] Extend `tests/unit/geometry/Test_Registration.cpp`: ICP run with a robust kernel (e.g. Huber/Tukey) converges to the ground-truth transform on a synthetic cloud with injected gross outliers, where the default `RejectOutliers` trimming path leaves a measurably larger residual/transform error; and assert the default path is unchanged when no kernel is selected.

## Docs

- [x] Add module-level doc comments to `Geometry.Statistics.cppm` and `Geometry.Robust.cppm` describing the numeric/fail-closed contract (GEOM-005/GEOM-007) and the kernel `Rho`/`Psi`/`Weight` relationship.
- [x] Regenerate `docs/api/generated/module_inventory.md` via `tools/repo/generate_module_inventory.py` so the two new modules appear.
- [x] Note the optional ICP IRLS robust-weighting path and its default-off behavior in the geometry registration docs (the doc that currently describes `Geometry.Registration` / `RejectOutliers`), if such a doc exists; otherwise add a short subsection there.

## Acceptance criteria

- [x] `Geometry.Statistics` and `Geometry.Robust` build under the `ci` preset and pass `tools/repo/check_layering.py --root src --strict` (no imports of assets/runtime/graphics/rhi/ecs/app/platform).
- [x] `Test_Statistics.cpp` passes: batch-vs-streaming moments match within tolerance; `Merge`/`operator+` equals concatenation; `Median`/`Quantile` match sorted reference for odd and even sizes.
- [x] `Test_RobustKernels.cpp` passes: every kernel matches its analytic form, has a finite `r→0` weight, and is non-increasing in `|r|`.
- [x] `Test_Registration.cpp` passes: the robust-kernel ICP achieves strictly smaller transform/residual error than the default trimming path on the gross-outlier dataset, and the default (no-kernel) path is byte-for-byte behavior-preserving versus before this change.
- [x] All degenerate/non-finite/out-of-domain inputs (empty, `Count()==0`, `q∉[0,1]`, `scale≤0`, non-finite `r`/sample/trig arg) return a fail-closed diagnostic and never emit NaN/Inf.
- [x] `docs/api/generated/module_inventory.md` lists both new modules and `tools/docs/check_doc_links.py` and `tools/agents/check_task_policy.py` pass.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Statistics|RobustKernels|Registration' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Mixing mechanical file moves/renames with the semantic work in this task.
- Introducing unrelated feature work outside statistics/robust-kernels/ICP weighting.
- Introducing renderer/runtime/ECS/assets/platform/app dependencies into `Geometry.Statistics`, `Geometry.Robust`, or the geometry layer.
- Removing, disabling, or changing the default of the existing `RejectOutliers` percentile-trimming path.
- Modifying `CovarianceAccumulator` semantics in `Geometry.Linalg` as part of this task.
- Introducing new CTest labels without updating `tests/README.md` and `tests/CMakeLists.txt` in the same change.
- Claiming any performance improvement without a baseline comparison.

## Maturity

- Stop-state for this task is `CPUContracted`: both slices land deterministic, fail-closed CPU implementations with passing correctness tests and synced docs/inventory. Both Slice A and Slice B must be merged to close at `CPUContracted`. No GPU/optimized backend or parity work is in scope here; `Operational`/`ParityProven` are out of scope.

- Closure: no `Operational` follow-up is owed for this task.
