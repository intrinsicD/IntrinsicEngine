# Spatial acceleration consumer inventory

This is the implementation-planning inventory for the shared
[point LBVH](spatial-indices.md). It covers current geometry, runtime, graphics,
physics and method consumers, Framework24 counterparts, and open work reviewed
on 2026-09-10. **Candidate means a place to evaluate integration, not a shipped
backend or a measured improvement.** The shared cache, Vulkan k-means, ICP and CPU/Vulkan normal/outlier/density/spacing/bilateral/keypoint/descriptor-neighborhood
rows below identify implemented LBVH consumers. Existing KD-tree, octree, grid and primitive
BVH paths remain in place.

## Choose the query and owner first

| Need | Current point LBVH fit |
| --- | --- |
| Euclidean nearest point, including a point-valued face/edge property | Supported; the result identifies a sample, not the closest point on the underlying primitive. |
| Inclusive radius neighbors | Supported with explicit capacity and total-hit count. GPU capacity is at most 1024. A truncated result is not a complete neighborhood. |
| Nearest point other than self and k-nearest | CPU and Vulkan queries support original-ID exclusion; coincident distinct points remain eligible. CPU k=0 is empty; GPU k is 1..64. |
| Nearest eligible subset | Needs an arbitrary predicate or a separately indexed eligible subset; single-ID exclusion does not express arbitrary membership. |
| Distance-ordered or unlimited radius neighborhoods | Needs caller sorting and a complete collection/reduction strategy. Current retained hits are ordered by source ID, not distance. |
| Triangle/segment closest point, intersections, containment, ray hits, object overlap | Needs conservative primitive bounds and exact primitive predicates/traversal. A point tree over centroids is insufficient. |
| High-dimensional descriptor distance, anisotropic metric, Gaussian far field | Needs a different metric/representation or a separately validated approximation. Current traversal uses 3D Euclidean distance. |
| Connectivity, geodesic distance, PDE/linear algebra, hierarchy error bounds | These structures encode algorithm semantics beyond proximity; retain them. |

Stable entity inputs use `Runtime::SpatialIndexCache`, keyed by world, full
entity identity, canonical position property, cardinality and relevant
revisions. Pure CPU kernels use geometry indices/spans without importing runtime.
Runtime resolves the property and arranges borrowing/snapshots; existing
KD-tree-specific APIs need an explicit adapter or scoped overload before they
can consume an LBVH. A cache alone does not accelerate those callers.

Moving runtime samples can use `SpatialIndexCache::CreateWorkspace`, retaining
its immutable snapshot lease while querying. The handle expires when the last
caller lease is released; pending GPU batches keep resources alive to safe
completion. Bilateral filtering rebuilds these private indices after each pass.
Lower-layer kernels and dedicated compute pipelines can continue using a
method-owned `Graphics::PointLbvhWorkspace` or geometry index; neither requires
an ECS component or unconditional per-frame build. Device-thread submission,
barriers and retirement rules still apply. Physics owns its CPU simulation
index; graphics owns scene-snapshot indices without importing runtime.

Preserve original source slots, deletion masks, deterministic ties, units and
query membership when integrating. Local-space caches need explicit treatment
of world-space distances under nonuniform scale. Current finite-coordinate,
point-count, query-count and radius-capacity limits are documented in
[spatial-indices.md](spatial-indices.md#construction-and-limits). Each consumer
must validate those bounds and report unsupported execution or its actual
fallback. Runtime config, agents and UI continue through one validated method
path; selecting Vulkan must not imply every stage uses an LBVH.

## Point-query consumers

| Consumer and source | Existing mechanism / useful LBVH work | Ownership and integration condition |
| --- | --- | --- |
| [Entity spatial queries](../../src/runtime/GeometryIntegration/Runtime.SpatialIndexCache.cpp) | **Wired:** CPU nearest/k-nearest/radius and recorded/framed GPU batched queries over canonical float3 properties. | Shared runtime cache; all compatible mesh/graph/point-cloud element domains retain source IDs. |
| [K-means assignment](../../src/runtime/Modules/Clustering/Runtime.ClusteringGpuState.cpp), [CPU kernel](../../src/geometry/Geometry.KMeans.cpp) | **Wired on Vulkan:** nearest moving centroid each iteration. CPU reference/optimized paths retain their scan/KD-tree behavior. | Private centroid workspace, rebuilt per iteration with reused storage. Any CPU migration needs its own comparison; small cluster counts can favor scans. |
| [ICP registration](../../src/geometry/Geometry.Registration.cpp) and Framework24 point correspondence | CPU KD-tree reference, cached CPU LBVH, or framed Vulkan LBVH nearest correspondences; all use the shared CPU solve. | Integrated through `SpatialIndexCache` immutable target snapshots and reusable batches. Canonical domains, deletion mapping, entity-transform metric and stale-result checks are preserved; see [registration](registration.md). |
| [PCA point normals and MST orientation](../../src/geometry/Geometry.PointCloud.Normals.cpp) | **Integrated CPU and Vulkan LBVH neighborhood consumer** through shared normal config/commands, alongside existing KD-tree/octree kernel paths; radius or kNN neighborhoods feed local PCA and orientation edges. | Preserve complete radius support and existing k+1-then-filter policy. [Normal estimation](normal-estimation.md) uses canonical-domain cache leases and named publication (RUNTIME-213/UI-045, RUNTIME-219). Vulkan queries use bounded chunks, at most 64 kNN candidates including the extra self candidate, and reject radius support above 1024 hits. MST orientation and PCA solve remain CPU work; topology normal methods do not use proximity indices. |
| [ISS-style keypoints](keypoint-analysis.md) | Reference KD-tree, cached CPU LBVH and framed Vulkan complete-radius support; CPU scale/PCA/suppression. | Reuse the selected-property index on all eight domains. Preserve centroid covariance, source-ID ordering, inclusive support and tie suppression. Vulkan overflow rejects the operation; it cannot truncate support. |
| [FPFH](descriptor-analysis.md) | Reference KD-tree, cached CPU LBVH and framed Vulkan radius support; CPU spacing/SPFH/FPFH. RUNTIME-226 binds verification. | Reuse the selected-position property index on all eight domains. Validate finite nonzero normals, preserve complete support or the exact required lowest-ID prefix, and publish 33 float columns atomically. Uncapped overflow rejects; capped support can use the existing lowest-ID retention rule. FPFH **descriptor matching** uses descriptor-space distance, so a 3D position LBVH cannot replace it. |
| [Point statistics and splat radii](point-spacing.md) | CPU octree reference, cached CPU LBVH and framed Vulkan kNN feed CPU radius/nearest-spacing reductions. | RUNTIME-221 binds all eight domains with config/UI and named radius publication; preserve k+1-then-self-filter, duplicates and sampled statistics stride. RUNTIME-222 owns model-space radius rendering; current point sizes are pixels. |
| [Bilateral point filtering](bilateral-point-filter.md) | CPU octree, first-pass cached CPU LBVH and framed Vulkan kNN feed fixed-normal CPU updates. | RUNTIME-224 uses private working-set leases for later passes; rebuild each iteration, preserve k+1-then-self-filter and joint weights. Named or in-place same-domain output publishes only after all passes. |
| [Statistical/radius outlier analysis](outlier-analysis.md) | CPU octree reference, cached CPU LBVH and framed Vulkan LBVH queries feed CPU classification. | RUNTIME-209/UI-041: named same-domain mask/score on all eight domains, strict population threshold or inclusive radius count, exact source exclusion. Radius consumes complete counts with capacity one. Separate current-mask removal only for point clouds. |
| [Local distance-ratio score](outlier-analysis.md) | Existing mean-neighbor-distance ratio, with span and supplied-neighbor reducers. | RUNTIME-223 extends Outlier Analysis with shared CPU/framed Vulkan LBVH. Query k+1 candidates before self filtering; GPU k<=63. Distinct from Framework24 covariance probability, full LOF and LoOP. |
| [Kernel density estimation](kernel-density.md) | CPU octree reference, cached CPU LBVH and framed Vulkan LBVH candidates feed CPU spacing bandwidth and local Gaussian averaging. | RUNTIME-220: all eight domains, named scalar publication/config/UI; preserves k+1-then-self-filter, duplicates, and automatic/manual bandwidth. Vulkan k<=63; no Gaussian radius cutoff or full-sample KDE claim. |
| [Compact density weights](density-weights.md) | KD-tree reference, cached CPU LBVH and framed Vulkan complete-radius candidates feed the existing double kernel reduction. | RUNTIME-227: all eight domains, named scalar config/UI/publication/history. Conservative float query radius preserves strict double support; capacity overflow rejects. Vulkan density preflight rejects subnormal coordinates. Shared radius pagination also serves keypoints and FPFH. |
| [LOP / WLOP / CLOP / EAR](../../src/geometry/Geometry.PointCloud.Consolidation.cpp), [GPU path](../../src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp) | LOP offers cached CPU or framed Vulkan LBVH source attraction and private moving-sample indices through shared CPU projection steps; other CPU strategies use radius KD-trees and existing GPU LOP/WLOP uses grid passes. Source attraction, density and moving-sample repulsion query compact support; conservative broad-phase radius and strict double filtering protect tiny/boundary support. | LOP leases a stable source cache plus a separate evolving `CreateWorkspace` for each iteration; complete radius overflow rejects publication. WLOP/CLOP/EAR still need framed Vulkan adapters using the same lease and supplied-neighborhood seams. Preserve the selected variant's support, weights, neighbor limits and reduction order; reuse the supplied-neighborhood density reducer and compare against the grid before replacing it. EAR insertions change cardinality. |
| [Automatic support radius and occupancy guards](../../src/geometry/Geometry.SupportRadius.cpp) | KD-tree kNN chooses scale, then conservative radius candidates and strict double-distance filtering estimate occupancy/workload. BUG-185 corrects tiny-support undercounts; self remains included. | Radius/counting fits; automatic rank selection can use available kNN/exclusion through an adapter. Never truncate occupancy and thereby understate the work budget. |
| [Hoppe surface reconstruction](../../src/geometry/Geometry.SurfaceReconstruction.cpp) | Octree nearest or kNN oriented samples for grid signed-distance values. | Stable oriented-point index: nearest mode fits now; multi-neighbor mode can use available kNN after adapter parity. This accelerates field evaluation, not grid storage or Marching Cubes. |
| [Progressive Poisson sampling](../../methods/geometry/progressive_poisson/src/ProgressivePoissonReference.cpp), [GPU acceptance](../../assets/shaders/progressive_poisson_accept_phase.comp) | Phase-specific spatial hashing and exact conflict checks; nearest-other distances support hierarchy construction. | Candidate only after comparing the paper's grid strategy. Accepted-set membership and phase order matter: indexing all input points as blockers changes the sampler. Needs an active subset/predicate or private rebuilt tree, and exclusion for spacing. |
| [Nearest-neighbor histograms, coverage and pair correlation](../../src/geometry/Geometry.PointCloud.QualityMetrics.cpp) | Exhaustive nearest/pair loops. Coverage is nearest to a separate sample set; pair correlation has a bounded distance range. | Coverage fits nearest; spacing needs exclusion; pair correlation needs complete radius pairs, self filtering and once-only counting. Preserve double-precision oracle/tolerance semantics and existing all-pairs diameter/count diagnostics, which nearest queries alone do not compute; retain independent exhaustive benchmark truth. Periodograms/global Fourier sums do not become local radius queries. |
| [Point-set kNN graph construction](../../src/geometry/Geometry.Graph.Utils.cpp) | Octree kNN generates graph edges. | Available kNN/exclusion still needs an adapter and deterministic duplicate-edge policy. Building a graph is an explicit topology operation; existing graph traversal still uses adjacency. |
| [Htex patch point-to-centroid classification](../../src/geometry/Geometry.HtexPatch.cpp) | Exhaustive nearest centroid. | Nearest fits for large batches/patch sets; use a small local CPU tree or private GPU workspace only when build/query cost justifies it. A three/few-centroid scan should stay simple. |
| [Selection and primitive refinement](../../src/runtime/GeometryIntegration/Runtime.PrimitiveSelectionRefinement.cpp) | Raster primitive IDs, local face/edge refinement, and a full scan for the nearest point to a pick ray. | Future brush/radius selection and snapping to a known 3D point can use the shared cache. The existing half-ray-distance fallback needs conservative ray/capsule traversal, not nearest to the ray origin. Keep visible-ID, depth, input-capture and stale-source checks. |

Framework24's [nearest-neighbor system](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_nearest_neighbors.cpp)
shares a CPU property KD-tree across
[correspondence](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_correspondence.cpp),
[PCA](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_point_cloud_vertex_pca.cpp),
[selection](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_selection.cpp),
[saliency](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_point_cloud_saliency.cpp),
[point analysis](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_point_cloud.cpp),
[subsampling](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_subsampling.cpp)
and [Gaussian-mixture workflows](../../experimental/framework24/lib_bcg_viewer/src/bcg_system_point_cloud_gaussian_mixture.cpp).
These are discovery references for shared query inputs, not evidence that every
Framework24 method uses a GPU LBVH. Port local PCA/saliency/neighborhood outcomes
through canonical properties; keep dense Gaussian responsibilities exact unless
a later method explicitly contracts an approximation.

## Consumers requiring other primitives or traversal

| Consumer and source / planned work | Useful acceleration | Required boundary |
| --- | --- | --- |
| [Closest face](../../src/geometry/Geometry.MeshClosestFace.cpp), [adaptive remeshing projection](../../src/geometry/Geometry.HalfedgeMesh.AdaptiveRemeshing.cpp), [implicit plane field](../../src/geometry/Geometry.ImplicitPlaneField.cpp) | Existing CPU BVH over face bounds; potential reusable triangle LBVH for repeated closest-surface evaluation. | Conservative face/triangle bounds, exact point-to-triangle/polygon distance, barycentric/normal/source-face result. Nearest centroid or vertex is insufficient. |
| [Closest graph edge / edges within radius](../../src/geometry/Geometry.Graph.Utils.cpp) | Existing segment AABB BVH; a persistent segment index could avoid per-call rebuilds. | Segment bounds plus exact distance and original edge IDs. Endpoint/edge-center proximity is a different query. |
| [Mesh booleans](../../src/geometry/Geometry.HalfedgeMesh.Boolean.cpp), METHOD-005 and future self-intersection/repair validation | Conservative primitive-pair candidate enumeration and ray classification. | Triangle AABB overlap/ray traversal, exact intersection/containment predicates, duplicate-pair and degenerate-case policy. A hierarchy does not supply robust predicates. |
| METHOD-003 closest-point PDE and METHOD-004/028 Walk on Stars | Repeated closest boundary, distance and visibility queries against fixed geometry. | Triangle/segment oracle plus signed/inside and boundary-condition semantics. Point LBVH is useful only for explicitly sampled point-oracle parts; retain analytic oracles. Guiding cells encode distributions, not nearest points. |
| METHOD-007 constrained tetrahedralization and METHOD-027 implicit meshing | Candidate constraints, surface-distance/field queries, or nearest sample seeds for point location. | Simplex/primitive bounds and exact predicates/topology still own containment and insertion. Coordinate changes/refinement need rebuilding or a separately implemented update strategy. |
| METHOD-034 iPSR normal transfer | Repeated nearest reconstructed face queries. | Rebuild the triangle index after each reconstructed mesh. Use `MeshClosestFaceIndex` for exact nearest-face semantics; a KD-tree/LBVH over face centroids is only an approximate method if explicitly chosen during intake. |
| METHOD-043 shape diameter / thickness and future ambient curve-distance evidence | Many rays against one fixed surface; segment queries against detached curves. | Triangle ray traversal with self-hit/epsilon, sidedness and interior rules; segment distance for curves. Point nearest/radius cannot measure thickness. Existing experiment results remain unchanged. |
| METHOD-044/045 and GEOM-076 UV validation | Possible broad phase for large UV triangle-overlap or boundary-segment checks. | Conservative bounds in the UV metric plus exact 2D predicates. Chart connectivity, seam selection, geodesic seeds and distortion solves remain topology/numerics work. |
| [Physics contact candidates](../../src/physics/Physics.World.cpp) | Current `ComputeCollisionContacts` enumerates shape pairs. An AABB hierarchy could cull separated finite shapes. | CPU physics-owned moving shape bounds, collision filters, deterministic pairs and exact narrow phase; handle unbounded shapes separately. No graphics/runtime imports into physics. No active implementation task currently owns this optimization. |
| [Renderer instance culling](../../src/graphics/renderer/Graphics.CullingSystem.cpp), [culling shader](../../assets/shaders/culling/instance_cull.comp), future scene/cluster visibility | Conservative scene/instance/cluster hierarchy for sufficiently large scenes. | Graphics snapshot ownership, transform/bounds revisions, frustum/HZB traversal, indirect outputs and recipe scheduling. Point LBVH is not a scene hierarchy or a meshlet/LOD error hierarchy. GRAPHICS-125 remains offline evidence. |
| Future ray picking, shadows, AO, path tracing and contact/particle neighborhood work | Triangle/object ray traversal; point radius neighborhoods for a separately contracted particle solver. | No such backend is provided by the point cache. Hardware BLAS/TLAS remains a distinct RHI capability. Compare grids for fixed-support particles; no active particle-method task is implied. |

## Keep the existing mathematical structure

- Geodesics, Dijkstra/A*, heat/signed heat, curvature segmentation, extrema,
  region/part adjacency and geodesic farthest-point seeds use graph or surface
  connectivity. Ambient proximity may assist authoring or a separately named
  feature, but must not connect nearby disconnected sheets or cross barriers.
- Parameterization, ARAP/SLIM, DEC/FEM, sparse solves and eigensolvers use their
  operators and topology. An LBVH is relevant only to an actual geometric
  query around those solves, such as overlap checking or constraint snapping.
- Voxel downsampling and fixed-grid splatting have direct cell addressing.
  Selecting a representative closest to a cell centroid is a per-cell
  reduction, not a global nearest-point query.
- Dense CPD/Gaussian-mixture responsibilities, kernel matrices/GPs and PGR
  winding-number sums have nonlocal contributions. Spatial cutoffs or
  aggregate nodes need explicit error contracts and new parity evidence.
  Permutohedral filtering operates in its declared feature dimension; a 3D
  tree does not replace the lattice. METHOD-032 needs actual shared octree
  corners and parity connectivity, which the LBVH does not represent.
- Small one-off scans, known primitive-ID lookup, one-ring mesh operators,
  random sampling, noise injection and unrelated IO/config/rendering work
  have no demonstrated hierarchy need.

## Open-work reminders

The linked tasks carry a local **Spatial acceleration consideration** note.
These notes preserve current non-goals, backend progression and research gates;
they request a reuse decision when the relevant implementation/adoption slice
starts, not an unconditional GPU port. Entries without an active task above
remain discoverable here; allocate a scoped task only when work is selected.

| Work area | Open task owners with local reminders |
| --- | --- |
| Point analysis | [RUNTIME-220](../../tasks/active/RUNTIME-220-kernel-density-spatial-backends.md), [RUNTIME-209](../../tasks/active/RUNTIME-209-point-set-outlier-analysis-publication.md), [UI-041](../../tasks/active/UI-041-point-set-outlier-multi-domain-panel.md), [UI-051](../../tasks/backlog/ui/UI-051-domain-agnostic-appearance-properties-selection-windows.md), [GEOM-073](../../tasks/backlog/geometry/GEOM-073-point-analysis-property-span-contracts.md) |
| Clustering | [RUNTIME-211](../../tasks/backlog/runtime/RUNTIME-211-kmeans-property-domain-integration.md), [UI-043](../../tasks/backlog/ui/UI-043-kmeans-property-domain-panel.md) |
| Sampling | [RUNTIME-212](../../tasks/backlog/runtime/RUNTIME-212-progressive-poisson-property-domain-publication.md), [UI-044](../../tasks/backlog/ui/UI-044-progressive-poisson-property-domain-panel.md), [GEOM-061](../../tasks/backlog/geometry/GEOM-061-grid-downsampling-reduction-strategies.md), [METHOD-014](../../tasks/backlog/methods/METHOD-014-progressive-poisson-gpu-operational-parity.md) |
| Graphs | [GEOM-074](../../tasks/backlog/geometry/GEOM-074-graph-property-adjacency-contracts.md) |
| Nonlocal methods | [GEOM-059](../../tasks/backlog/geometry/GEOM-059-kernel-matrices-nystroem-gaussian-process.md), [GEOM-060](../../tasks/backlog/geometry/GEOM-060-permutohedral-lattice-highdim-filtering.md), [METHOD-015](../../tasks/backlog/methods/METHOD-015-coherent-point-drift-family-reference-backend.md), [METHOD-035](../../tasks/backlog/methods/METHOD-035-pgr-winding-number-orientation-baseline.md) |
| Primitive hierarchies | [GEOM-067](../../tasks/backlog/geometry/GEOM-067-memory-aware-bvh-merged-node-evidence.md), [GRAPHICS-125](../../tasks/backlog/rendering/GRAPHICS-125-memory-priced-cluster-hierarchy-evidence.md) |
| Surface/atlas work | [GEOM-076](../../tasks/backlog/geometry/GEOM-076-curvature-region-guided-uv-atlas-cuts.md), [METHOD-043](../../tasks/active/METHOD-043-thickness-and-curve-parts-comparison.md), [METHOD-044](../../tasks/active/METHOD-044-feature-aware-atlas-merge-experiment.md), [METHOD-045](../../tasks/active/METHOD-045-baseline-preserving-atlas-cuts.md) |
| Boundary queries | [METHOD-003](../../tasks/backlog/methods/METHOD-003-closest-point-method-pde-reference-backend.md), [METHOD-004](../../tasks/backlog/methods/METHOD-004-walk-on-spheres-reference-backend.md), [METHOD-005](../../tasks/backlog/methods/METHOD-005-robust-mesh-boolean-reference-backend.md), [METHOD-007](../../tasks/backlog/methods/METHOD-007-constrained-delaunay-tetrahedralization-reference-backend.md), [METHOD-007A](../../tasks/backlog/methods/METHOD-007A-cdt-engine-integration-intake.md), [METHOD-027](../../tasks/backlog/methods/METHOD-027-adaptive-delaunay-qef-implicit-meshing.md), [METHOD-028](../../tasks/backlog/methods/METHOD-028-confidence-driven-walk-on-stars-guiding.md) |
| Orientation/reconstruction | [METHOD-032](../../tasks/backlog/methods/METHOD-032-octree-parity-normal-orientation.md), [METHOD-033](../../tasks/backlog/methods/METHOD-033-screened-poisson-reconstruction-reference.md), [METHOD-033A](../../tasks/backlog/methods/METHOD-033A-screened-poisson-engine-integration-intake.md), [METHOD-034](../../tasks/backlog/methods/METHOD-034-ipsr-orientation-baseline.md), [METHOD-036](../../tasks/backlog/methods/METHOD-036-orientation-comparison-evidence.md) |
| Reference adoption intake | [METHOD-003A](../../tasks/backlog/methods/METHOD-003A-spatial-query-reference-integration-intake.md) owns the named integration decisions required when enrolling older reference tasks. |
| Evidence | [BENCH-001](../../tasks/backlog/benchmarks/BENCH-001-framework24-golden-workflow-comparison-harness.md), [REVIEW-004](../../tasks/backlog/architecture/REVIEW-004-framework24-product-convergence-audit.md) |

## Integration and evidence checklist

At method intake, follow the [method workflow](../agent/method-workflow.md) and
record the query, metric, membership rules, owner, update frequency and reuse
decision alongside the existing engine-integration matrix. Preserve the CPU
oracle. Test duplicates, self exclusion, ties, deleted source slots, stale
handles, transformed inputs and dense neighborhoods where relevant. GPU tests
must exercise actual traversal and readback, including overflow/unavailable
states; a composed cache alone does not prove consumer integration.

Measure cold build/upload, warm reuse, query/reduction, per-iteration rebuild,
readback and complete method cost separately, including memory. Compare with
the existing scan/KD-tree/octree/grid/BVH on small and large, uniform and
clustered inputs. Keep oracle computations independent of the index under
test. The current bitonic build and range-bound unions are a correctness
baseline; choose migration priority from measured consumer workloads, not
from the name “GPU LBVH.”

## Integrated registration consumer

ICP now exposes CPU KD-tree reference, cached CPU LBVH and framed Vulkan LBVH
correspondences through one canonical-domain config/command path. Target leases
and batched queries are owned by `SpatialIndexCache`; the CPU solve remains
shared. See [registration](registration.md) for metric, invalidation, fallback,
limits and end-to-end comparison scope. This closes the point-nearest adapter
opportunity. Shared kNN/exclusion/radius batches and a supplied-neighborhood CPU PCA adapter
also serve normal estimation; covariance/GICP and triangle-surface correspondence remain
separate future methods.

Shared LBVH follow-up: audit subnormal coordinate handling in GPU AABB unions/clamp independently of density weights, whose adapter now rejects those inputs. Reuse the density conservative-radius helper only for methods whose narrow phase implements the same double Euclidean support contract; it is not a replacement for every existing float-radius rule.
