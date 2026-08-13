# 3x3 eigensolver and live curvature diagnostic — 2026-08-12

This is local-development diagnostic evidence. It is not a benchmark result,
is not claim-eligible, and does not authorize a curvature estimator or solver
change. Temporary probes and GUI processes were removed after the run; the
screenshots, CSVs, and binaries named below remain under `/tmp` only.

## Source identities

- IntrinsicEngine: `77a243edf` (dirty state: clean before ARA recording).
- Archived framework24 checkout: `81c54ad4294280fc034d39e46eafc1a29d598b81`.
- eig3x3: `d0f2118705cf30ce7d585302c832829497e143f5`.
- Geometric Tools reference checkout:
  `3e43b79905a1db3dae1fb3e42d1759b3a2522d9b`.
- Asset: `tests/data/sculpt.obj`, 3,669 vertices, 11,013 edges, 7,342
  triangular faces, no boundary, no rejected support.
- Host: 11th Gen Intel Core i9-11900KF, Linux 6.14 x86-64, Clang 23.

## eig3x3 scope and compatibility

The reviewed paper and code implement closed-form **eigenvalues** for real 3x3
matrices using stable evaluations of `J2`, `J3`, and the discriminant. The
symmetric entry point is `eigvalss`; the repository has no eigenvector routine.
Curvature directions, PCA frames, and OBB axes require a full orthonormal
eigensystem, so this is not a drop-in replacement.

The upstream header is C11. A direct `clang++ -std=c++23` include failed with
18 errors because declarations such as
`const double A[static restrict 3][3]` are not C++ syntax; the header also uses
C99 array designators. A C++ integration therefore needs a maintained port or
a separately compiled C adapter.

The upstream routine does not pre-scale its input. For the rotated and diagonal
controls below, max-absolute pre-scaling and post-rescaling restores the expected
homogeneity; the raw routine does not preserve it at extreme scales.

| Matrix eigenvalues | Raw eig3x3 | Pre-scaled eig3x3 |
|---|---|---|
| `1e-200 * [1,2,3]` | `[2e-200,2e-200,2e-200]` | `[1e-200,2e-200,3e-200]` |
| `1e-100 * [1,2,3]` | `[1.42265e-100,1.42265e-100,3.15470e-100]` | `[1e-100,2e-100,3e-100]` |
| `[1,2,3]` | `[1,2,3]` | `[1,2,3]` |
| `1e100 * [1,2,3]` | `[1e100,2e100,3e100]` | `[1e100,2e100,3e100]` |
| `1e200 * [1,2,3]` | non-finite | `[1e200,2e200,3e200]` |

A throwaway `-O3 -march=native` microbenchmark ran 250,000 deterministic
random symmetric matrices, seven timed repetitions, and reported the median.
It was not CPU-pinned, frequency-controlled, or retained as a benchmark
manifest. Habera is values-only; every other row computes full eigenpairs, so
the rows are intentionally not a performance-parity claim.

| Local implementation | Median ns / matrix |
|---|---:|
| Current Intrinsic maximal-pivot Jacobi, full eigenpairs | 254.94 |
| Fixed-size Eigen symmetric QR, full eigenpairs | 259.93 |
| Eigen closed-form `computeDirect`, full eigenpairs | 136.37 |
| Eberly iterative symmetric solver, full eigenpairs | 208.51 |
| Habera eig3x3, eigenvalues only | 61.14 |

On a rotated `[1,2,3]` spectrum scaled by `1e-200` through `1e200`, the current
Intrinsic Jacobi and Eigen symmetric QR returned the expected values at every
scale. Raw Eberly lost accuracy at `1e-200` and failed at `1e200`; raw eig3x3
failed at both extremes. Both external candidates need explicit input
pre-scaling before they can enter a reusable engine contract.

## Isolated live comparison

Two software-rendered Xephyr servers isolated the applications from the
physical monitor: BCG on `:91` and ExtrinsicSandbox on `:92`, both at
1600x1000. The archived BCG source required a temporary copy under `/tmp` to
remove unrelated missing Torch/CUDA demos, initialize two missing header-only
submodules, and add an environment-driven startup probe. Its curvature
algorithm, UI defaults, material path, and renderer remained the exact
framework24 checkout implementation. Xephyr, BCG, and Sandbox were stopped
after capture.

Both applications loaded the exact same `tests/data/sculpt.obj` payload.

| Surface | BCG default live view | BCG matched-support view | Sandbox live view |
|---|---|---|---|
| Scalar displayed | minimum principal `kmin` | mean `H` | mean `H` |
| Tensor support | two-ring | one-ring | one-ring |
| Post-smoothing | 3 damped passes | 0 | 0 |
| Eigensolver | Eigen fixed 3x3 `SelfAdjointEigenSolver` | same | private maximal-pivot Jacobi |
| Colormap | Jet | Jet | Viridis |
| Automatic range | exact min/max | exact min/max | 2nd/98th percentiles |

BCG selects `kmin` initially because its curvature-name list begins with
minimum principal curvature and its UI index defaults to zero. Intrinsic's
geometry kernel computes `kmin` and `kmax`, but the runtime editor state and
property publication surface retain only `v:mean_curvature`,
`v:gaussian_curvature`, and the two direction fields. The live Sandbox
therefore cannot select the scalar BCG shows by default.

## Scalar differential

Quantiles are `[min, q02, median, q98, max]` over all 3,669 slots.

| Field | Intrinsic current | BCG one-ring, no smoothing |
|---|---|---|
| `kmin` | `[-4.71735,-4.28094,-1.53256,1.99866,2.00332]` | `[-46.4272,-35.8214,-0.999214,1.59536,2.02548]` |
| `kmax` | `[-3.19473,-3.19029,1.99843,71.2283,79.3482]` | `[-1.20001,-0.999370,0.767458,2.14936,2.63553]` |
| `H` | `[-3.28224,-3.19572,1.99429,35.5550,39.1143]` | `[-23.5559,-17.8343,-0.997154,1.59790,2.15603]` |
| `K` | `[-107.364,-96.3293,3.98908,63.8737,85.9522]` | `[-34.1242,-24.1632,0.997305,16.1884,31.7846]` |

Most vertices follow one sign/scale transform to near machine precision:

| Intrinsic channel predicted from BCG | Median absolute error | Relative L2 | Correlation |
|---|---:|---:|---:|
| `kmin = -2 * BCG(kmax)` | `9.02e-7` | `0.02475` | `0.999660` |
| `kmax = -2 * BCG(kmin)` | `2.74e-6` | `0.04362` | `0.998982` |
| `H = -2 * BCG(H)` | `9.67e-7` | `0.04323` | `0.999034` |
| `K = 4 * BCG(K)` | `1.31e-6` | `0.08594` | `0.996434` |

The sign is deliberate: Intrinsic negates the BCG/PMP-style signed dihedral to
publish outward-convex-positive curvature. The magnitude difference localizes
to framework24's mixed Voronoi area routine, before eigendecomposition:

- On acute triangles it divides corner dot products by triangle area instead
  of twice the area, doubling the cotangents and therefore doubling the mixed
  area denominator.
- On obtuse triangles it assigns `A/4` to the obtuse vertex and `A/8` to the
  other vertices, half the Meyer `A/2` and `A/4` allocations.
- `sculpt.obj` contains 7,317 acute and only 25 obtuse triangles (99.66% acute),
  explaining the almost-everywhere factor of two and the localized tail.

The like-for-like fields therefore agree structurally after accounting for a
known sign convention and the legacy area defect. BCG itself uses Eigen's
self-adjoint eigensolver, independently ruling out eig3x3 or Intrinsic's Jacobi
routine as the source of the live visual discrepancy.

## Visualization differential

Intrinsic mean curvature has robust automatic range `[-3.19572, 35.5550]`.
Zero maps to `0.0825` and the median to `0.1339` in that interval, so most
vertices occupy a small dark/magenta portion of sequential Viridis. BCG uses
Jet over each field's exact extrema, producing much greater apparent contrast.
This range/colormap difference changes appearance without changing stored
values.

## Temporary artifact identities

| Artifact | SHA-256 |
|---|---|
| `/tmp/bcg-default-min.png` | `c48ae97e6d694899bad77af3113c7c53a4a54b5447f29d5e0bb986a58646917e` |
| `/tmp/bcg-one-ring-min.png` | `136d77484e85fd0eb65e5ab20b0e3546a7ab121983e9bd9ef95e872865d5d246` |
| `/tmp/bcg-one-ring-mean.png` | `ea9d6ac3dde478b476bb61b0fcf37bff3b0c253f986352af71cb711e242b1b46` |
| `/tmp/sandbox-curvature-result.png` | `c694c01142069e782a0fc2139c78bebd5d0a1ba43a83fcc2fea11f53a5cbb737` |
| `/tmp/sandbox-mean-curvature.png` | `d9810dccbade593517c9129f392a477333f518322688f333e1cd8c370f963fd0` |
| `/tmp/bcg-curvature-default.csv` | `44433702a937eadbf49ec05a18caeea4226000f761125eaf12b8aecb39b3823d` |
| `/tmp/bcg-curvature-one-ring.csv` | `119d92677095e0868d156ab7f236f21a938a36e489cf5c4f6eb019e38e2d09e4` |
| `/tmp/intrinsic-curvature-sculpt.bin` | `c190556c11f4eaed7ea27618ac2cb654e163be059018ba745a39982e2b203bb9` |

## Source anchors

- `src/geometry/Geometry.HalfedgeMesh.Curvature.cpp`: current Jacobi,
  one-ring hinge tensor, sign convention, and principal fields.
- `src/geometry/Geometry.HalfedgeMesh.Utils.cpp`: corrected Meyer mixed area.
- `src/geometry/Geometry.PCA.cpp`: separate closed-form solver with an absolute
  scale floor and row-cross-product eigenvectors.
- `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp`:
  editor publication retains only mean, Gaussian, and direction properties.
- `src/runtime/Visualization/Runtime.VisualizationRecipes.cpp`: generic
  2nd/98th-percentile automatic scalar range.
- `experimental/framework24/lib_bcg_framework/src/bcg_mesh_curvature_taubin.cpp`:
  Eigen eigendecomposition and legacy tensor formulation.
- `experimental/framework24/lib_bcg_framework/src/bcg_mesh_vertex_voronoi_area.cpp`:
  inconsistent acute/obtuse mixed-area factors.
- `experimental/framework24/lib_bcg_viewer/src/bcg_system_mesh_curvature.cpp`:
  two-ring, three-pass, minimum-curvature defaults.
- `experimental/framework24/lib_bcg_viewer/src/bcg_system_mesh_material.cpp` and
  `experimental/framework24/lib_bcg_viewer/include/bcg_component_material.h`:
  exact-extrema range and Jet default.
