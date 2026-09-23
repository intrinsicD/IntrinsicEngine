# Property-guided UV atlas

`Geometry.UvAtlas` owns CPU chart construction, parameterization, packing and
acceptance. Runtime supplies an optional scalar guide and publishes UVs on the
original mesh. The editor and property baker consume that same UV layout.
Implementation and verification are tracked by
[METHOD-047](../../tasks/done/METHOD-047-property-guided-atlas-editor.md).

## Input and segmentation

The geometry API takes finite positions, non-degenerate indexed triangles and
optional face region labels. Labels are constraints: charts cannot cross region
boundaries. Disconnected components of the same label remain separate. Boundary,
non-manifold and inconsistent-orientation incidences form chart barriers; the
method does not silently weld or repair the source mesh.

Runtime accepts a typed scalar property on vertices or faces, independent of its
name and provenance. Vertex values are averaged onto incident triangles; face
values are mapped through the existing source-face correspondence. Numeric
conversion must preserve integer values exactly and reject non-finite data.
The existing `CurvatureSegmentation::SegmentFaceFeatures` performs bounded,
deterministic one-channel GMM segmentation and connected-region extraction.
Region count zero selects its automatic model-selection path. This uses an
existing curvature, saliency or other scalar field; it does not implement a new
saliency estimator or claim semantic object-part recognition.

## Chart parameterization

`FastStaged` grows connected charts within these constraints, requires an
acceptable disk embedding, and refines rejected charts within explicit chart and
iteration budgets. A single triangle has an exact isometric layout. Such
fallback charts and all refinement splits are counted in diagnostics.

| Objective | Implemented solve |
| --- | --- |
| None | Uniform Tutte initialization, without distortion optimization. Quality limits still apply. |
| Angle | Least Squares Conformal Maps (LSCM). A non-injective or unsuccessful solve causes refinement. |
| Area | Injective LSCM/Tutte initialization, then Laplacian-preconditioned descent on the area-priority energy below. |
| Both | Injective initialization followed by SLIM reweighted-proxy iterations on symmetric Dirichlet energy. |

For positive-determinant Jacobian `J`, the area-priority energy per source-area
weight is `log(det J)^2 + 0.1 * (||J||_F^2 / (2 det J) - 1)`.
This is an explicitly defined area-priority objective with an anisotropy
regularizer, not an implementation of AMIPS. Both uses
`||J||_F^2 + ||J^-1||_F^2`. Injective line searches bound accepted steps.
Iteration exhaustion is reported separately from atlas quality acceptance;
an accepted bounded iterate is not a claim of optimizer convergence.

The implementation reuses the existing
[LSCM formulation](https://people.engr.tamu.edu/schaefer/teaching/689_Fall2006/p362-levy.pdf)
and [SLIM optimization kernels](https://igl.ethz.ch/projects/slim/).
Each chart is normalized to its source surface area before deterministic shelf
packing with uniform density and texel padding. Packing cannot hide area error
by independently rescaling each triangle.
Each native shelf cell also reserves one texel of translation space per axis.
The packer places an interior triangle centroid on a texel center, so a thin
island with adequate area cannot miss every sample solely through translation.
The extra space preserves the requested gutters and a uniform density across
charts; reserving that space reduces the absolute fitted texels-per-unit.

The repository-pinned [xatlas backend](https://github.com/jpcy/xatlas) supports
the Angle objective. Other objective requests fail explicitly. A fallback from
FastStaged is allowed only when xatlas can honor the requested objective;
requested method, actual method and fallback reason remain visible.

## Acceptance and publication

`Geometry.UvAtlas.Validation` independently checks published source-corner
coverage, finite coordinates, tile bounds, orientation, region preservation,
positive-area triangle overlap, common texel density, distortion and chart
resolution. Its BVH supplies candidate overlap pairs. The narrow phase uses
exact binary32 orientation expansions; the existing filtered predicate can
return `Uncertain` and therefore cannot establish the required acceptance.
Positive-area overlap is invalid; a shared edge or point alone is not overlap.
Generated charts must have at least one texel center strictly inside a triangle.
This conservative check can reject a chart covered only at exact raster-edge
ties; increasing atlas resolution can resolve that failure.

Distortion is measured from each triangle's surface-to-UV Jacobian using one
global density. The conformal ratio is `sigma_max / sigma_min`; area distortion
is the larger of normalized area scale and its reciprocal. Limits, non-finite
values, under-resolution and resource exhaustion produce typed failures.
An input can require more charts or texels than the chosen budgets permit.
No fixed-size atlas can guarantee low distortion and useful resolution for
every possible mesh.

Runtime publishes canonical `h:texcoord` at seams and `v:texcoord` where
compatible, plus `f:atlas_region` and `f:atlas_chart` (`uint32`) on original
source face slots. It preserves topology and unrelated properties. Existing
job cancellation, stale-result rejection and history transactions guard
publication and undo. Bakes record the resolved UV binding and fingerprints of
UVs, topology, positions and source values; changed inputs make old outputs
unavailable for binding and identify them as stale in the editor. Generated atlas
dimensions follow their UVs through undo/redo and scene save/load. Automatic
appearance bakes use those exact dimensions; they do not enlarge textures
implicitly. Authored UVs with no known extent use 1024. The bake limit is 8192
per axis, with an explicit rejection above it.

The import path can preserve finite authored UVs without imposing the generated
atlas quality contract. Vertex-UV preservation checks guide-region compatibility;
a requested guide regenerates corner UVs so the region constraints are applied.
Regeneration runs the full atlas validator; baking separately checks raster
coverage and overlap for the chosen texture dimensions.

## Evidence and limits

The diagnostic campaign is
[`geometry.uv_atlas.property_guided.reference`](../../benchmarks/geometry/manifests/geometry_property_guided_atlas_reference.yaml),
run by `tools/diagnostics/atlas/check_property_atlas.py` using the public
`IntrinsicUvAtlasMeshDiagnostic` runner. It compares generated controls and the
frozen METHOD-045 corpus with independent NumPy Jacobian measurements and the
native overlap audit. Timing from this correctness campaign is descriptive;
it does not establish a performance improvement.

These are CPU reference solvers. There is no GPU parameterization backend,
general mesh repair, UDIM packing or unrestricted mip-chain guarantee. Property
bakes retain one mip and an explicit coverage policy; padding must match the
intended filtering footprint. The bounded CPU and Vulkan results are recorded as C109/C110 in the
[evidence report](../../ara/evidence/diagnostics/method047_property_atlas/report.md),
with exact test counts, source identity and readback scope.
Generated bake records and GPU texels are session state and must be recomputed
after loading a scene; UVs and their accepted atlas dimensions are persisted.

### Import and publication boundaries

Runtime UV publication currently accepts triangular source faces. Polygon faces
are rejected in shared preview/apply preflight with a triangulation diagnostic:
a source polygon cannot represent distinct UVs for the same corner when its
internal fan triangles enter different charts. This keeps source topology and
attributes intact instead of reporting a corrupt atlas as success. Regeneration
never treats shadow `v:texcoord` data as preserved authored UVs while canonical
`h:texcoord` exists.

Automatic mesh/model import and reconstruction attempt atlas generation but preserve renderable,
selectable geometry and normals when UV quality admission fails. Direct-mesh
enrichment reports the absence of usable UVs. Explicit materialization callers
can still require UV success. Missing UVs cannot be used for a bake; users must
repair the input or generate an accepted atlas first. No synthetic UVs or dropped
faces conceal a rejected atlas.
