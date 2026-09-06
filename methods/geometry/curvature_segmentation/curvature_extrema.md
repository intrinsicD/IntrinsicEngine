# Curvature-extremum curve inspection

METHOD-041 extracts candidate curves on an unchanged triangle surface. It does
not partition faces or change METHOD-040. The native CPU reference and offline
viewer are the inspection surface; [UI-053](../../../tasks/active/UI-053-curvature-extremum-engine-overlay.md)
owns Sandbox integration. The selected engineering formulation is described
below, not claimed to reproduce every stage of either cited paper.

## Formulation and sources

[Hildebrandt, Polthier and Wardetzky (2005), Smooth Feature Lines on Surface
Meshes](https://doi.org/10.2312/SGP/SGP05/085-090) supplies directional extremality,
locally consistent principal-line signs, and sign-covariant derivative filtering.
[Rusinkiewicz (2004), Estimating Curvatures and Their Derivatives on Triangle
Meshes](https://gfx.cs.princeton.edu/pubs/Rusinkiewicz_2004_ECA/) supplies the
normal-difference least-squares precedent. This implementation uses vertex
neighborhood fits instead of his per-face fits and tensor transport.

At each support scale, fit a symmetric tangent-plane operator `S = -dN` from
area-weighted normal differences and displacements. Eigenvalues/eigenvectors
are geometric principal curvatures/directions. This avoids assuming that the
existing Framework24-compatible hinge-dyad eigenvectors are geometric bending
directions. No geometry optimization or L0 preprocessing runs.

For a principal direction `t_i`, compute `e_i = grad(k_i) dot t_i` using
area-averaged piecewise-linear face gradients. A compact physical-radius
average transports neighboring extremalities with `sign(t_i dot t_j)`.
In a regular triangle, align directions so all pairwise dot products are
positive, then interpolate the zero set of `e_i`. Positive dominant maxima
and negative dominant minima use the corresponding sign of
`grad(e_i) dot t_i`; umbilic or poorly conditioned support is excluded.

The mean-curvature comparator fits a local quadratic to
`H = (k_max + k_min)/2`, takes a transverse Hessian eigendirection, and traces
zeros of its directional gradient. Positive mean crests require a negative
transverse second derivative; negative mean crests require a positive one.
This is a signed-crest comparator, not every possible scalar-field ridge.
Its Hessian directions are distinct from principal surface directions.

## Selected numerical choices

- Internally center and normalize by the bounding-box diagonal `D`. Source
  positions remain unchanged; output positions are float vectors in caller
  coordinates, with double edge fractions as interpolation provenance.
- Support radii default to `0.01 D`, `0.02 D`, `0.04 D`. Weighted edge-path
  distances approximate surface distance; sampling anisotropy can still bias
  support. The viewer shows the fraction of vertices with curvature support.
- Area-weighted vertex normals and compact weights `(1-(d/r)^2)^2` feed the
  symmetric least-squares fit. This is not a robust M-estimator or a calibrated
  noise model. Shape and quadratic fits reject ill-conditioned normal equations
  and insufficient samples rather than inventing directions.
- Mesh boundaries and vertices on strict dihedral features above 45 degrees
  block smooth-field support. Sharp edges are shown separately. This prevents
  smoothing across an exact sharp crease from producing parallel smooth curves;
  it also leaves deliberate gaps near sharp junctions.
- Strength is `r * abs(curvature)`. Sharpness is the magnitude of the transverse
  derivative of extremality multiplied by `r^3`. Defaults are `0.01` and
  `0.0001`; anisotropy floor is `0.1`. Eligibility is sampled at each candidate
  segment midpoint, not analytically clipped along its entire interval.
- Intersections are interned by source edge and shared by adjacent faces within
  each signal and scale. Crossings within `4 * float epsilon * D` of an endpoint
  coalesce to that source vertex. Constant-zero triangles and zero-length
  published segments are omitted. Singular triangles are reported and left
  open; no invented bridge through an umbilic is added.
- Confidence combines strength, sharpness and normal-fit residual. Cross-scale
  agreement checks nearby same-kind segments in connected surface neighborhoods,
  tangent alignment at least `0.7`, and face-normal alignment above `0.5`.
  These are diagnostic heuristics, not probabilities or topological persistence.
  Filtering segments can fragment an otherwise connected source curve.
- Defaults cap each neighborhood at 2,048 samples and the sum of neighborhood
  edge visits and cross-scale candidate visits at 80,000,000. Work-limit failure
  returns diagnostics without partial curves. Storage includes cached bounded
  neighborhoods; this is not a constant-memory or linear-time claim.

## Inspect local meshes

```bash
cmake --preset ci-release
cmake --build --preset ci-release --target IntrinsicCurvatureExtremaMesh
python3 benchmarks/runners/curvature_extrema_viewer.py \
  --runner build/ci-release/bin/IntrinsicCurvatureExtremaMesh \
  --mesh /path/to/frog.obj --mesh /path/to/sculpt.obj --mesh /path/to/trim-star.obj \
  --output build/curve-inspection/index.html
```

Open the generated HTML locally. Both views share a camera. Select principal
or mean curvature at each scale; adjust strength, confidence and scale agreement to
inspect supported and fragmented curves. Optional `--comparison-cohort`
accepts an existing validated METHOD-040 cohort and overlays its partition on
identical geometry. The viewer has no network dependencies.

The native invocation is `IntrinsicCurvatureExtremaMesh input.obj output.json
[params.json]`. Parameter files accept `radius_ratio`, `scale_factors`,
`minimum_strength`, `minimum_sharpness`, `minimum_anisotropy`,
`hard_dihedral_degrees`, `maximum_neighbors`, and `maximum_work_items`.
Unknown, duplicate, malformed, nonfinite or mistyped values fail closed.
Native output paths must be fresh. Reopen a saved inspection with `--cohort`
without re-running extraction.

The frozen local-cohort manifest produces separate schema-v2 measurements for
its exact default parameters. Those measurements are explicitly non-claim-
eligible. Their geometric checks cover source interpolation and graph references,
not whether a crease is a good part boundary. Custom parameters remain ordinary
inspection records rather than being mislabeled as the frozen benchmark.

## Validation scope and limitations

The analytic suite covers a Gaussian extrusion's central crease, mean-curvature
comparison, plane/sphere/cylinder negatives, winding reversal, retriangulation,
refinement, small positional noise, scale/translation, an exact sharp fold,
source preservation, malformed input and bounded work. These fixtures are the
CPU contract; they are not semantic-part ground truth or a corpus-wide stability
proof. Local mesh observations and reviewed artifacts belong to the
[METHOD-041 task](../../../tasks/done/METHOD-041-curvature-extremum-curve-inspection.md).

Missing or weak curves may reflect unsupported sampling, unreliable directions,
thresholds, genuine flatness or detector limitations. Large-coordinate float
publication has finite precision. The detector does not repair input topology,
close open curves, select part boundaries, perform merges, choose a part count,
or establish that mean-curvature curves are preferable to principal curves.
Those decisions require the next partitioning experiment and operator feedback.
The [inspection record](curvature_extrema_report.md) binds the executed checks
and their limits.
