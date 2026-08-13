# Curvature support/smoothing defect analysis — 2026-08-12

> Superseded product decision (BUG-156 reopen): the measurements below compare
> the former Intrinsic/PMP configuration with the independent Meyer scalar
> operator. They remain a record of that accuracy trade-off, but they do not
> define the shipped interoperability contract. The reporter requires exact
> Framework24 `CurvatureTaubin` behavior, so the engine now follows its
> deterministic sequential two-ring/three-pass result. The local,
> non-claim-eligible direct-parity observation is recorded in
> [`ara/evidence/tables/curvature_framework24_parity_2026-08-12.md`](../../ara/evidence/tables/curvature_framework24_parity_2026-08-12.md).

## Verdict

The PMP-parameter defaults restored by BUG-153/BUG-154 — two-ring hinge
support plus three damped eigenvalue-smoothing passes — destroy genuine
curvature on meshes with sharp creases adjacent to smooth regions. On the
reporting asset `tests/data/sculpt.obj` the published mean curvature had a
median relative error of 62% against the independent Meyer cotan operator,
with 65 vertices reduced to near zero and 917 vertices with flipped sign on a
surface whose true |H| is at least ~1.9 everywhere. Restricting support to the
vertex's own incident hinges (one-ring) and publishing unsmoothed eigenvalues
reduces the median error to 0.07% with zero cancelled or sign-flipped
vertices. Both corrected parameters remain expressible in PMP's own API
(`two_ring_neighborhood = false`, zero post-smoothing steps), so the engine
stays on the same Cohen-Steiner–Morvan hinge formulation.

This defect class was structurally invisible to the BUG-154 corpus
differential: that experiment compared Intrinsic against PMP configured with
identical parameters, so a shared parameter defect could not appear. Parity
and accuracy are different claims; the 2026-08-12 estimator study measured
the former.

## What was ruled out first

The reported symptom ("parts correct, other parts all zeros or too small") was
first checked against every structural hypothesis:

| Hypothesis | Result |
| --- | --- |
| OBJ parsing / corner-tuple topology destruction | Ruled out — the asset is position-only; the loader emits one 3,669-vertex soup. |
| Mesh construction (`AddFace`) | Ruled out — the engine builds V=3669, E=11013, F=7342, zero rejected triangles. |
| Manifoldness / winding / connectivity | Ruled out — closed genus-2 manifold (χ=−2), single component, consistent winding, zero boundary vertices, no duplicate or degenerate faces. |
| Circulators / vertex iteration | Ruled out — engine per-vertex output equals an independent NumPy replica (no engine data structures) to 1.4e-14. |
| Triangle-quality gating | Ruled out — minimum quality 0.46, ~1,300× above the 3.5e-4 floor; nothing is rejected. |

The independent replica reproducing the identical defective field localized
the defect to the algorithm parameters themselves.

## Root cause

Two stacked effects:

1. **Two-ring support bleeds crease bending.** The hinge tensor of a vertex
   one ring away from a sharp convex crease integrates the crease's large
   positive dihedrals over its support and publishes raw κ_max ≈ +11 where the
   local surface is a concave groove with H ≈ −2 (Meyer). The support radius,
   not the estimator, is at fault: with one-ring support the same vertex
   estimates H within a few percent.
2. **Rank-channel eigenvalue smoothing cancels across transitions.** The
   three damped passes average the algebraically sorted κ_max and κ_min
   channels independently. Around convex/concave transitions (a groove beside
   a ridge), channel averaging mixes values that belong to different physical
   directions and signs, driving H toward zero across a band roughly five
   rings wide (two support rings plus three smoothing rings). This also broke
   the field-coherence invariant at boundary vertices with no interior
   neighbour, which received nonzero smoothed κ while their H/K stayed zero
   (BUG-154 independent-review finding).

## Measurements

All errors are relative mean-curvature deviations against the independent
Meyer cotan operator on the clean asset; zero-band counts use |H| below 5% of
the field median where the Meyer field is unambiguous (|H_meyer| above half
its median); sign flips require |H| above 10% of the field median.

`tests/data/sculpt.obj` (3,669 vertices, clean, closed):

| Variant | median | p90 | zero-band | sign flips |
| --- | ---: | ---: | ---: | ---: |
| two-ring + 3 damped passes (previous default) | 0.616 | 3.39 | 65 | 917 |
| two-ring raw | 0.007 | 4.06 | 0 | 500 |
| one-ring + 3 damped passes | 0.448 | 3.35 | 64 | 605 |
| one-ring + tensor-space smoothing ×3 | 0.447 | 3.34 | 55 | 607 |
| **one-ring raw (corrected default)** | **0.001** | **0.035** | **0** | **0** |

Smoothing in tensor space instead of eigenvalue space does not rescue the
previous default: the smoothing radius itself is the dominant damage on this
geometry.

Normal-direction noise perturbations (σ as a fraction of the median edge
length), same metrics:

| σ | one-ring raw | previous default |
| --- | --- | --- |
| 0.5% | med 0.103, zero-band 0, flips 0 | med 0.616, zero-band 64, flips 915 |
| 2.0% | med 0.336, zero-band 59, flips 149 | med 0.616, zero-band 60, flips 921 |

The previous default's bias exceeds its noise suppression at every measured
noise level: it is dominated on every aggregate, so no accuracy/robustness
trade-off is being given up by removing it. Callers that want stabilized
fields on genuinely noisy input can apply the reusable
`Geometry::Smoothing::CotanSmoothVertexProperty` to the published properties
explicitly, choosing their own iteration count.

Crease fixture (tent ridge `z = −0.6|x| + 0.55x²`, 21×21 grid): the previous
default loses up to 79% of |H| two rings from the crease (7 of 14 probe
vertices fail a 30%-retention criterion); the corrected path deviates at most
0.05% from the Meyer cross-check on the same probes.

Fixed-surface confirmations: the corrected engine matches the independent
replica oracle on the frozen 5×5 fixture to 3e-16, and on the sculpt asset
publishes 3,669/3,669 supported, nonzero vertices with zero zero-band and
zero sign-flip vertices.

## Historical decision (superseded)

- Former default = one-ring hinge support, unsmoothed eigenvalues, unchanged quality
  gating, boundary interpolation, orientation conventions, and diagnostics.
- Complexity drops to O(V+E+F); the estimator no longer revisits neighbour
  edge sets.
- The reusable smoothing operation keeps its public contract; stabilization is
  now an explicit caller decision instead of an irreversible default.
- The Rusinkiewicz / corrected-curvature-measures evaluation recommended by
  the 2026-08-12 estimator study remains open; this repair fixes the shipped
  formulation's parameters and does not select a new estimator family.

## Evidence identity

- Analysis scripts: session-local NumPy replica validated against the engine
  probe (`IntrinsicCurvatureCorpusProbe`) to 1.4e-14 pre-fix and 3e-16
  post-fix on the fixture grid; probe binary built from `ci-release` with
  `INTRINSIC_BUILD_DIAGNOSTIC_TOOLS=ON`.
- Regression anchors checked into the tree:
  `tests/unit/geometry/Test.CurvatureTensor.cpp`
  (`MatchesEdgeDihedralReferenceOracle`, `CreaseFlanksKeepGenuineCurvature`,
  `SculptAssetHasNoZeroCurvatureBands`,
  `OpenMeshFullFieldKeepsPrincipalInvariants`).
- Asset: `tests/data/sculpt.obj` (BUG-137 acceptance asset, unchanged).
