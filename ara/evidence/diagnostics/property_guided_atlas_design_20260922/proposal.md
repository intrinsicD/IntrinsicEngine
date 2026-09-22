# Property-guided mesh atlas and baking workflow — design proposal

Status: proposal from a two-model design discussion; not an adopted algorithm, implementation, performance claim, or runtime verification. User requested a discussion with Claude Opus 5.5 at max effort. The source review targets IntrinsicEngine commit e156d3a15. No engine code was changed and no engine build, benchmark, or GPU execution was performed in this discussion.

## Discussion outcome and remaining disagreements

Two completed Claude Opus 5.5 calls used `--effort max`: an independent source review and a final adversarial discussion supplied with that review plus Codex objections. The first resumed follow-up timed out after 480 seconds with no final answer; the successful follow-up used a fresh self-contained packet. The record preserves this failed attempt without counting it as a completed review.

Claude revised the first answer: withdrew the floating-window camera-offset shortcut, source-value-as-baked-value readout, per-chart normalization as the quality gate, predicted raster coverage, a blanket mip-padding formula, and splitting-only area mode. It agreed on frozen hard regions, explicit atlas identity, global pixel-metric diagnostics, target-gated success and a real scene rectangle. Read the [first response](claude-response-1.md), [Codex objections](codex-follow-up.txt), and [revised response](claude-response-2.md) as discussion evidence, not verified engine results.

Codex retains these qualifications to the revised response:
- Face-material IDs preserve region separation but cannot encode every non-separating hard seam. Such seams require detached corner/edge cuts and a corresponding post-check; do not claim that xatlas label binding solves this alone.
- The proposed AMIPS-style area energy is an additional candidate, not a verified implementation of the cited paper. A genuine area-priority formulation, its gradient, solver and conditioning still require exact paper intake and CPU-reference tests. The simpler log-area candidate below is likewise unimplemented.
- Position edits do not universally make only metrics stale. Revalidate atlas acceptance and invalidate any position-dependent property/bake recipe; retain a position-independent bake only when its exact correspondence and recipe dependencies establish validity.
- Absorbing tiny generated regions by longest shared boundary is a candidate heuristic, not a selected default; it can erase the part boundary the user wants. Never apply it to frozen labels, and make its area threshold and guidance policy explicit.
- Source-value fidelity at texel centres does not prove reconstruction fidelity under runtime filtering. Representability, encoding, seams and filter footprints need separate tests.
- Claude prefers audit/backend reporting as the first slice; Codex recommends corner-UV view support as the first visible slice, with audit/reporting immediately alongside the constrained-atlas work. Neither estimate is a completion claim.

## Recommendation

Deliver one source-preserving workflow: property binding → meaningful connected regions → topology cuts and UV charts → parameterization and packing → audited atlas → property bakes → a genuine mesh/atlas split workspace. Keep semantic regions, UV charts, and duplicated UV corners distinct. A region may contain several charts and interior seams without changing its semantic identity.

First integrate the existing arbitrary-feature segmentation with region-constrained xatlas and an independent acceptance report. This is the initial usable baseline, not evidence that xatlas satisfies every requested distortion objective. Develop the proposed connected growth/validated split-and-merge chart construction and objective-specific solvers behind the same data contract. Compare on fixed inputs before changing production defaults. Global multicut is an experimental alternative, not the default product path.

## Source-backed gaps (inspection only)

- `CurvatureSegmentationConfig::Features` and `SegmentFaceFeatures` already accept arbitrary scalar/feature bindings. Reuse their checked domain binding and publication paths.
- `UvAtlasInput` lacks region labels. `UvAtlasOptions` defaults to FastStaged. The editor worker sets `BackendName` to xatlas without setting `Method`, while its request validation says only xatlas is supported. Expose actual/requested/fallback values end to end.
- `BuildEditorParameterizationViewModel` explicitly cannot draw corner-domain UVs, although seam-preserving atlas publication uses them. Fix this before polishing the UV window.
- `DrawParameterizationWindow` currently implements controls | UV. It is not the requested 3D mesh | UV workspace.
- `AcceptSolverUvs` checks local diagnostics/orientation; this alone is not global injectivity. Diagnostics currently use raw UV/source area ratios, which must be interpreted with scale and target-image aspect before setting quality thresholds.
- The bake path is GPU-based. Raw float padding is rejected, output textures have one mip level, and bake records do not carry explicit atlas identity. The appearance binding path does not check an atlas revision. Add a small CPU raster oracle for correctness tests, not a competing runtime service.
- Bake input implicitly prefers literal `h:texcoord`. Explicit selected-atlas and property bindings must replace this implicit selection when supporting multiple layouts and bake tabs.

Owners inspected: `src/geometry/Geometry.UvAtlas.cpp[m]`, `Geometry.Parameterization.*`, `Geometry.HalfedgeMesh.CurvatureSegmentation.cppm`; `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Uv.cpp`, `Runtime.ParameterizationOperations.cpp`; `src/runtime/Modules/TextureBake/Runtime.TextureBakeModule.cpp[m]`; `src/runtime/Rendering/Runtime.RenderExtraction.cpp`; `src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp`; and ADR-0025.

## Mesh and region contract

Accept triangulated embedded surfaces or a documented source-preserving triangulation with original face/corner correspondence. Check indexing, finite positions, degeneracy, winding, connected components and manifold sheets before solving. Nonmanifold edges and bowtie fans can be separated in a detached working representation. High-genus or closed regions need cuts before disk solvers. Preserve the source topology, sparse property slots and unrelated fields.

Invalid or zero-area faces cannot be silently dropped under a complete-success label. Return identified invalid faces and a typed failure, or an explicitly requested partial result with an exclusion mask. A finite time/memory/chart/texture budget may make the requested quality unattainable. A per-triangle emergency layout is a disclosed highly fragmented result, not proof of useful decomposition.

Support explicit field roles:
- region similarity: group nearby faces with compatible values;
- boundary preference: attract cuts to scalar discontinuities or explicitly chosen ridges/valleys;
- importance: avoid salient areas for seams or allocate additional sampling there.

Do not infer these roles from a property name. Saliency maxima are not inherently anatomical boundaries. Preserve signed curvature unless the user chooses an absolute-value or other transform. Use finite masks, robust normalization, a constant-field fallback, and area/length-weighted terms. Geometric scale and property units must not accidentally change the result; test unit changes and remeshing rather than assuming invariance.

The existing GMM/spatial segmentation is the first reference for region generation. A candidate scalable alternative uses a face-dual adjacency graph, deterministic seeds, connected growth and local merges with a dimensionless regional data term and edge-length-weighted boundary cost. Candidate chart construction minimizes seam cost plus chart complexity, subject to fixed region boundaries, disk validity and measured distortion thresholds. A property can influence seam placement without redefining the semantic regions. Cheap normal/curvature predictors can prioritize work but never replace a solved-map acceptance test.

Clean or merge tiny generated regions explicitly before freezing the region map. User/imported frozen labels and authored hard seams remain hard. Once frozen, charting cannot merge across them. Preserve seam reasons: semantic boundary, user/hard feature, topology cut, guidance, or distortion refinement.

## Parameterization objectives and validity

Objective and solver are separate controls; only supported combinations are selectable. Proposed final modes:

| User mode | Meaning | Candidate solver path |
|---|---|---|
| None | Construct a valid map without an additional distortion objective | Positive-weight Tutte with a strictly convex boundary after disk validation; validated planar projection when applicable |
| Angle | Minimize angular/conformal distortion and meet the selected bound | Existing LSCM or BFF, with measured fallback/refinement |
| Area | Minimize variation from a declared target area/density, with an explicit anisotropy safety bound | A genuine authalic reference objective and validity-preserving iteration; evaluate established discrete/iterative authalic methods as comparators |
| Both | Minimize metric/stretch distortion, balancing angle and area | SLIM symmetric-Dirichlet outer loop using the existing optimization kernels |

"None" means no additional optimization, not zero distortion. General curved multi-face charts cannot preserve both angles and areas exactly. Independent triangle maps can preserve each triangle's metric, at the cost of maximal seams and finite-resolution problems. No mode promises meaningful parts, zero distortion, minimal seams and arbitrarily fast completion simultaneously.

For diagnostics, construct each triangle's 3D-to-2D Jacobian in a stable local frame. Evaluate the final map in pixel coordinates `(W*u, H*v)`, divide by one declared global target texels-per-unit scale, and inspect its singular values s1 >= s2 > 0. Report conformal ratio s1/s2, area ratio s1*s2 and bidirectional stretch max(s1, 1/s2), with area-weighted distributions and worst cases. Keep scale-free within-chart shape diagnostics separate from between-chart density differences; per-chart normalization must not hide density errors.

An example area-priority research candidate is area-weighted squared log determinant with a hard anisotropy cap and positive-determinant/global-validity constraints. Its gradient/solver and feasibility behavior require their own reference tests; calling symmetric Dirichlet 'area only' would be incorrect. Uniform density is the default. Optional importance-weighted density changes the declared target and must be reported as intentional allocation, including a separate uniform-area comparison.

Require a valid start, positive published float triangle areas, and chart/global non-overlap. Local flip prevention is insufficient. Validate topology, boundary simplicity, and triangle intersections using a correct broad phase plus robust predicates; validate chart packing independently too. Recheck after float conversion and packing. Do not clamp coordinates to conceal packing failures.

Retries have explicit solver-iteration, split, chart-size, chart-count, memory and time budgets. Only split failing charts; optionally try bounded validated merges. Every solver and fallback reports requested/actual identity and reasons. xatlas can respect group boundaries using `faceMaterialData`, or pack accepted charts via `AddUvMesh`/`PackCharts`, but its output must pass the same audit. A fallback cannot silently relax region or quality constraints.

## Baking and shared atlas identity

One atlas serves all selected bake properties; changing a tab never recomputes chart placement. Publish a source-bound atlas revision containing corner UVs, face/chart/region correspondence, seam reasons, packing extent, density target, config and validation report. Each bake binds its atlas revision, geometry revisions, canonical property reference/generation, encoding, extent and filtering recipe.

Rasterize using exact source triangle/corner correspondence. Vertex scalars interpolate barycentrically; face fields are constant per face; labels retain discrete semantics. Unsupported domains need a declared reconstruction rule. No-data values use an explicit mask; zero is a legitimate scalar value.

Keep raw float property data separate from its displayed colormap/range. Add chart-aware coverage masks, float gutters and a defined mip/filter policy. Dilation must not mix unrelated islands. Label textures use discrete sampling/reduction policy. Quantify coverage with the actual raster mask and CPU oracle: texel density predicts problems but does not prove coverage. Tiny/subtexel charts and insufficient gutter capacity produce a resolution/budget diagnostic distinct from UV geometry failure.

Measure both geometric quality and recovered signal error on held-out surface samples. Source-property hover values and actual baked-pixel values are distinct readouts; do not label a source interpolation as a sampled baked pixel.

## Requested UI

```text
Mesh: selected object     Guide: curvature/saliency/property     Compute Atlas
+--------------------------------+-------------------------------------------+
|                                | Atlas | Curvature | Saliency | ...       |
|  3D mesh                       |                                           |
|  regions and seam overlays     |  same packed UV layout                    |
|  linked face/chart selection   |  wireframe over selected baked texture    |
|                                |  legend, range, fit, zoom, pan            |
+--------------------------------+-------------------------------------------+
Stage/progress   Regions / Charts / Cuts   Distortion   Coverage   Backend
```

The UV side is selectable left/right with a draggable divider. Successful publication automatically opens the split for the still-selected entity. Preserve the single scene camera; establish a real scene rectangle and use it consistently for presentation, aspect ratio, picking, gizmos and input capture. Reuse the existing derived UV target/CPU fallback. A camera offset behind an overlapping floating window is not the final requested split.

Use one Atlas tab plus one tab per baked output, including pending/failed/stale states. Show raw scalar data through an adjustable display colormap, optional wireframe, chart/region selection, seam reasons, distortion and coverage overlays. Share pan/zoom across tabs and retain per-entity view state. Controls live in a compact toolbar/inspector rather than consuming the mesh half. GPU rendering/culling is needed for dense wireframes.

Retain the last valid atlas during recomputation. Cancellation, changed selection, deleted/rebound entities and stale job completions must be safe. Atlas/property edits mark affected bakes stale in both tabs and 3D material binding; old records remain inspectable against their original atlas snapshot, never overlaid on a new layout. Undo restores a matching revision or recomputes freshness. Persist config, region/atlas identity and bake recipes/catalog through existing scene/asset conventions; do not silently lose semantic labels on load.

Ownership: geometry owns detached arrays, graph/cut/solve/pack/audit functions; runtime owns validated config, source capture, jobs, revision checking, publication/history and asset resolution; graphics consumes copied view/bake requests and owns GPU resources; app uses runtime commands/view models. No second ECS mesh, broad segmentation framework, layer-policy change, or unrelated rendering framework is needed.

## Implementation order and acceptance

1. First <=one-day foundation: corner-UV-aware selected-entity view data and CPU wireframe, with the existing seam-preserving atlas and a regression fixture. Keep backend truthfulness as a separate small immediate fix.
2. Region-bound atlas input, constrained xatlas, exact source correspondence and independent audit; expose selected scalar/region inputs via the existing shared config lane.
3. Atlas revisions and bake freshness, explicit texture binding, raw scalar display/masks and bake tabs.
4. Genuine left/right mesh/UV split with input/picking/resizing tests.
5. Correct scale/density diagnostics and native CPU reference for property-guided growth/cuts/validated merging, compared with existing segmentation and constrained xatlas.
6. Objective-specific solvers: angle paths, genuine area reference, SLIM balanced mode; chart-aware gutters and mip policy complete the bake target.
7. Optimize only after correctness/reference baselines: compact adjacency, cached geometry computations, bounded chart solves, parallel independent work and packing. Sparse-solve and intersection costs need measurement; they are not globally O(F log F) just because growth uses a priority queue.

Test corpus: analytic plane/fold, open and closed cylinders, spheres, annulus/holes, tori/high genus, disconnected pieces, thin triangles, nonmanifold fans, invalid data, and noisy/remeshed/subdivided variants. Retain sculpt/frog/fandisk/bunny/dolphin comparisons with fixed source identities; test 10k/100k/1M scales where practical.

Hard success checks: complete valid-face/corner correspondence; unchanged source; connected regions; no forbidden region crossing; finite float UVs; no collapsed/flipped faces; no geometric overlap; requested distortion bounds; in-bounds packing without clamp; adequate measured raster coverage and filtering separation. Explicitly partial or target-missed results do not count as full success.

Bake checks: constant and analytic linear fields, face constants, labels, invalid masks, seam reconstruction, chart-aware dilation/mips, tiny-chart coverage and post-edit/undo freshness. UI checks include automatic opening, left/right placement, resize/DPI, pan/zoom, tab identity, picking/camera/gizmo coordinates, cancellation and entity deletion. GPU claims need actual Vulkan execution/readback; source inspection and headless tests are separate evidence.

Report runtime and peak memory by stage, chart/region/seam counts, density ratios, occupancy, distortion distributions, baked signal error, fallbacks and failures. Compare matched Release runs with equal resolution, gutters and requested quality. Fix seeds/tie rules and verify repeatability for a specified build/backend/thread configuration; ordered output collection alone does not establish bitwise determinism. No reliability or speed claim is established by this design review.

## Primary sources consulted

- [LSCM, Levy et al. 2002](https://people.engr.tamu.edu/schaefer/teaching/689_Fall2006/p362-levy.pdf): conformal chart parameterization and atlas decomposition.
- [SLIM, Rabinovich et al. 2017](https://igl.ethz.ch/projects/slim/): flip-preventing distortion optimization; local injectivity does not replace global overlap checks.
- [Boundary First Flattening, Sawhney and Crane](https://www.cs.cmu.edu/~kmcrane/Projects/BoundaryFirstFlattening/paper.pdf): boundary-controlled conformal mapping.
- [CGAL parameterization manual](https://doc.cgal.org/latest/Surface_mesh_parameterization/index.html): solver families, seam meshes and conditional bijectivity.
- [xatlas public API](https://github.com/jpcy/xatlas/blob/master/source/xatlas/xatlas.h): face-label constraints and UV-only packing.
- [Mesh Saliency, Lee et al. 2005](https://www.cs.umd.edu/~varshney/papers/mesh_saliency_sig05.pdf): scale-dependent importance from center-surround curvature.
- [Advanced MIPS, Fu et al. 2015](https://www.microsoft.com/en-us/research/publication/computing-locally-injective-mappings-advanced-mips/): additional area/shape energy candidate raised in the second review; exact formulation remains to be checked before implementation.
- [Signal-Specialized Parametrization, Sander et al. 2002](https://hhoppe.com/ssp.pdf): evaluating allocation through reconstruction of the stored signal.
