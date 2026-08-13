# Framework24 curvature parity — 2026-08-12

> Development observation only: this dirty-worktree differential is not
> claim-eligible. BUG-156 retains the high-risk implementation/publication
> contract; BENCH-001 owns the clean frozen claim-grade protocol/run, portable
> bundle, exact-revision review, and independent audit needed to promote or
> refute the bounded parity candidate.

## Scope

This local CPU differential compares IntrinsicEngine
`ComputeCurvatureTensor` with the actual Framework24
`CurvatureTaubin(mesh, 3, true, Policy::Sequential)` implementation. Both paths
received identical float vertex coordinates and triangle order. Framework24's
separate `MeshIo` AABB normalization was not counted as part of the curvature
algorithm; all checked-in fixtures have maximum AABB extent one, including
`tests/data/sculpt.obj`.

The comparison is local parity evidence, not claim-eligible evidence or a
universal geometric-accuracy/performance claim. Framework24's default
`Policy::ParallelUnsequential`
in-place smoothing is not a stable oracle because it concurrently reads and
writes the principal-value arrays. The deterministic sequential policy keeps
the same loops and vertex ordering without that race.

Each Framework24 reference mesh used a fresh/default `v_feature` property, so
all mask entries were false. Framework24 can consume a private, pre-populated
`v_feature` mask to freeze or omit selected vertices during smoothing; the
Intrinsic public operation has no corresponding semantic input and this
differential does not claim parity for that separate masked mode.

## Results

| Fixture | Topology purpose | Vertices | Maximum absolute `kmin` error | Maximum absolute `kmax` error |
| --- | --- | ---: | ---: | ---: |
| `framework24-acute-tetrahedron.obj` | closed, all-acute area branch | 4 | `0` | `0` |
| `framework24-obtuse-tetrahedron.obj` | closed, obtuse area branch | 4 | `4.44e-16` | `3.55e-15` |
| `framework24-open-patch.obj` | direct open-boundary tensor | 9 | `2.78e-17` | `8.33e-17` |
| `sculpt.obj` | closed full-field regression | 3,669 | `6.22e-15` | `2.78e-15` |

On sculpt, the corresponding maximum pointwise relative errors, using
`abs(intrinsic-reference) / max(abs(reference), 1e-15)`, were `4.65e-14` for
`kmin` and `1.29e-13` for `kmax`; mean absolute errors were `6.14e-16` and
`2.46e-16`.

The comparison used raw algebraically ordered Framework24 scalar fields without
sign, scalar-slot, or scale remapping. Framework24 `min_direction` maps to the
engine's established `v:principal_dir2` minimum-direction slot, while
Framework24 `max_direction` maps to `v:principal_dir1`; this is an API-name
adapter, not a numerical transformation. The sculpt regression additionally
freezes seven readable vertices and an FNV-1a hash of all 7,338 principal values
after rounding to `1e-5`; the hash is `0x1ed455aa30053cf6`. Since eigenvector sign
is arbitrary, directions were compared as lines using absolute dot products.
On sculpt, the minimum absolute line agreement was `0.9999999595` for the
minimum field and `0.9999999591` for the maximum field; the open fixture freezes
the center's actual Framework24 direction pairing in the CPU regression.

## Reproduction identity

- Framework24 source root: `/home/alex/Documents/framework24`.
- Reference source: `lib_bcg_framework/src/bcg_mesh_curvature_taubin.cpp` plus
  its vertex-area and edge-cotan helpers.
- Reference invocation: `CurvatureTaubin(mesh, 3, true, Policy::Sequential)`.
- Reference adapter:
  `tools/diagnostics/curvature/Framework24CurvatureParityProbe.cpp`.
- Intrinsic probe: `IntrinsicCurvatureCorpusProbe`, built from the `ci` preset
  with `INTRINSIC_BUILD_DIAGNOSTIC_TOOLS=ON`.
- Intrinsic build compiler: Clang 23.0.0 with Eigen 5.0.1; the Framework24
  reference binary uses its vendored Eigen. Both paths call
  `Eigen::SelfAdjointEigenSolver` directly, and the residual scalar differences
  above are roundoff-level library-version differences.
- Evidence class: local CPU parity differential on a dirty task worktree; the
  checked-in fixtures, scalar anchors, and hash are the durable regressions.

The task report records the remaining verification and review gates. Exact
parity is intentionally bounded to finite triangle support above Intrinsic's
documented dimensionless quality threshold; malformed or ill-conditioned input
fails closed instead of inheriting Framework24's unchecked behavior. It is also
bounded to Framework24's default all-false `v_feature` smoothing mask.
