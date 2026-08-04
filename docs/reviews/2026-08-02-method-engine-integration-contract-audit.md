# Method Engine-Integration Contract Audit — 2026-08-02

## Scope

This audit applies the stable contracts `geometry.element-domain-sources` and
`method.engine-integration` to:

1. every production geometry package with a `methods/geometry/**/method.yaml`;
2. every callable geometry method registered in the Sandbox editor on
   `main`; and
3. public point-set and graph algorithm seams whose container/property types
   narrow that same substitutability contract; and
4. the LOP-family integration on `feature/lop-consolidation-e2e` at
   `33930efab13764cbbd0887bfc8c726948a480479`, because that integration exposed
   the omission that triggered this audit.

The 2026-08-03 follow-up clarified that the first audit's “shared `Vertices`”
wording was itself too narrow. The semantic unit is a compatible typed
property on an element domain, not the `Vertices` component and not a
handle-indexed `VertexProperty`. This revision re-audits the inventory under
that stronger rule and opens follow-ups rather than rewriting retired task
history.

Physics method packages are outside the geometry element-source contract. Their
method records already distinguish CPU-reference packages from separately owned
physics/runtime/ECS integration; this audit does not reinterpret particle,
cloth, fluid, or rigid-body state as geometry `Vertices`.

Enum-only availability entries without a callable runtime command and Sandbox
panel are not counted as existing engine integrations. They make no
`Operational` method claim here; when implemented or materially changed, the
prospective task contract requires their own complete integration matrix.

## Contract used

The canonical target element-source mapping is:

| Required element source | Compatible entity provenance |
| --- | --- |
| `Vertices` | point cloud, graph, mesh |
| `Halfedges` | graph, mesh |
| `Edges` | graph, mesh |
| `Faces` | mesh |

This is the physical source matrix, not a restriction that point samples live
only in `Vertices`. Eligibility follows the least structured typed data and
topology required by the method. A point-set method accepts a compatible
`Property<T>`/span on any row (for example, mesh face centers); a graph method
adds its named node/edge/halfedge adjacency and therefore also accepts a mesh
satisfying that contract. `VertexProperty`, `FaceProperty`, and related
handle-indexed wrappers are conveniences, not eligibility types.

Publication is a separate decision: same-cardinality named results return to
the originating property domain, while topology/cardinality changes require an
explicit owning operation and must not silently discard richer data.

`HARDEN-087` closed foundational violation **V0** after this audit identified
it. Graph population now exposes the existing graph vertex/halfedge/edge
property sets as shared `Vertices + Halfedges + Edges` sources, with
`HasGraphTopology` retained as separate provenance, a canonical
`GraphHalfedge` property-discovery domain, and no fabricated faces.
The method findings below remain valid, but the clarified property rule exposes
additional restrictions left behind by the earlier vertex-only remediation.

## Inventory and disposition

| Method/integration | Evidence inspected | Disposition | Follow-up |
| --- | --- | --- | --- |
| ECS geometry materialization | `ECS.Component.GeometrySourcesPopulate.cpp`; `ECS.Component.GeometrySources.cppm`; retired `HARDEN-065`; `Geometry.Graph` | **V0 resolved by `HARDEN-087`.** Graph and mesh share the physical vertex/halfedge/edge source types; graph provenance remains explicit, graph halfedges preserve real `h:connectivity` without faces, and runtime/UI expose them through the canonical `GraphHalfedge` property domain. | None; V1–V4 runtime tasks may proceed against the unified source contract. |
| Boundary First Flattening / parameterization | `methods/geometry/boundary_first_flattening/method.yaml`; `src/runtime/Runtime.ParameterizationOperations.cpp`; `src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp` | Conforming. The public input genuinely requires connected triangle-mesh/disk topology; runtime and UI use the Mesh domain and publish same-count UVs. | None. |
| K-Means | `Runtime.ClusteringModule.cpp` (`IsExecutionDomain`, source resolver, publisher); three registrations in `Sandbox.MethodPanels.cpp`; span-based `Geometry.KMeans` | **Violation V6.** The kernel consumes vectors, but runtime/publication/UI admit only the three vertex-like domains. Edge, halfedge, face, and arbitrary named `vec3` properties are excluded. | `RUNTIME-211` + `UI-043`. |
| Vertex/point normal estimation family | `GetEditorSupportedGeometryProcessingDomains`; Mesh/Graph/PointCloud normal registrations; span-based `Geometry.PointCloud.Normals::Estimate` | **Violation V7 for the point-set variant.** Mesh- and graph-topology methods may retain their real requirements, but the generic PCA point estimator is hidden from non-vertex properties such as face centers. | `RUNTIME-213` + `UI-045`. |
| Mesh denoise, curvature, remesh, simplify, smooth, subdivide, repair, and parameterization operations | Runtime domain table plus Mesh Processing registrations | Conforming to stronger topology contracts. These operations use mesh adjacency/faces or mutate mesh topology; a point-span substitution is not claimed. | None. |
| Progressive Poisson | `methods/geometry/progressive_poisson/method.yaml`; `ApplyEditorProgressivePoissonCommand`; retired `RUNTIME-208`/`UI-038` | **Residual V1.** The retired tasks correctly unified mesh/graph/point-cloud provenance and non-destructive publication, but the request still hardcodes `Vertices`/`v:position`; other typed element-domain properties remain unavailable. | `RUNTIME-212` + `UI-044`; retired records stay frozen. |
| LOP/WLOP/CLOP/EAR feature integration | `Runtime.PointCloudConsolidationModule`; span-based consolidation kernels; shared Sandbox panel under Mesh/Graph/PointCloud Processing | **V2 resolved by `RUNTIME-206` and `UI-039`.** Requests carry arbitrary same-domain `vec3` property refs, all eight domains share one preflight/job/history path, all three stable registrations open one catalog-backed panel/state that binds compatible properties without conversion, topology-bearing count changes reject before queueing, and canonical point-cloud replacement remains explicit. | None; GPU/Vulkan parity remains separately owned by `METHOD-020`. |
| ICP registration | `ApplyEditorRegistrationCommand`; `view.registration` panel in `Sandbox.MeshProcessingPanels.cpp` | **Violation V3.** Both operands are point spans, but runtime/entity selectors require exact point-cloud provenance and expose no property identity. Tunable parameters are also panel-local instead of a co-equal config lane. | `RUNTIME-207` + `UI-040` (after `BUG-096`), broadened to arbitrary property pairs. |
| Statistical/radius outlier processing | `ApplyEditorPointCloudOutlierRemovalCommand`; PointCloud-only “Remove Outliers” controls in `Sandbox.DomainPanels.cpp` | **Violation V4.** Detection indices are meaningful for every typed sample property, but eligibility is point-cloud-only and detection is inseparable from destructive compaction. | `RUNTIME-209` + `UI-041`, broadened to arbitrary input/output property domains. |
| Signed Heat | `methods/geometry/signed_heat/method.yaml`; `Geometry.SignedHeatMethod`; no runtime/Sandbox binding | **Violation V5.** The mesh-only input restriction is legitimate, but the existing production method package has no RuntimeModule, config/agent, ECS publication/history, visualization, UI, or end-to-end integration owner. | `RUNTIME-210` + `UI-042`. |
| Point-set utility/feature APIs | `Geometry.PointCloud.Utils` and `Geometry.PointCloud.Features` public `Cloud` parameters; span-based quality/kernels/consolidation comparators | **Violation V8.** Several read-only and same-cardinality analyses semantically consume positions/normals/indices but require the owning point-cloud container. | `GEOM-073`; retain `Cloud` convenience adapters and explicit count-changing results. |
| Graph analysis/layout APIs | `Geometry.Graph.ShortestPath` result/property types and `Graph` parameter; `Geometry.Graph.Utils` layout/query APIs | **Violation V9.** These algorithms require adjacency plus named properties, but public container/`VertexProperty` types prevent an equivalent mesh primal graph from entering without an owning adaptation. | `GEOM-074`; no mesh-to-graph conversion or universal interface. |

The revised inventory distinguishes provenance fixes from property-domain
closure. `RUNTIME-208`/`UI-038` remain valid completed provenance work, while
`RUNTIME-212`/`UI-044` own the newly explicit property gap. The retired
`RUNTIME-206`/`UI-039` pair closes LOP runtime publication and property-aware
UI discovery. ICP and
outlier tasks now carry property identities, Signed Heat retains its legitimate
surface contract, and V6–V9 cover restrictions the original vertex-only audit
misclassified as conforming or left outside the engine-facing inventory.

## Literature cross-check

The refactor tasks require a fresh original-paper plus extensions/improvements
review before source changes. The intake used for this audit confirms only the
input/publication distinctions needed to slice the work:

- LOP originates in Lipman et al., “Parameterization-free Projection for
  Geometry Reconstruction” (DOI `10.1145/1275808.1276405`); WLOP (DOI
  `10.1145/1618452.1618522`), CLOP (DOI `10.1145/2601097.2601172`), and EAR
  (DOI `10.1145/2421636.2421645`) extend point-set consolidation without adding
  an ECS provenance requirement.
- Lloyd's least-squares quantization (DOI `10.1109/TIT.1982.1056489`), Arthur
  and Vassilvitskii's k-means++, and Bahmani et al.'s scalable k-means++ define
  clustering over sample vectors. Initialization and scale extensions do not
  introduce a vertex-handle or element-provenance requirement.
- Hoppe et al.'s unorganized-point reconstruction (DOI
  `10.1145/133994.134011`) and Mitra–Nguyen's noisy normal analysis use local
  sample neighborhoods. They support a generic property/span point estimator;
  they do not erase the distinct semantics of mesh face-weighted or graph
  connectivity-aware normal methods.
- ICP (DOI `10.1109/34.121791`) and its point-to-plane (DOI
  `10.1016/0262-8856(92)90066-C`), Generalized ICP (DOI
  `10.15607/RSS.2009.V.021`), trimmed (DOI
  `10.1109/ICPR.2002.1047997`), and robust (DOI
  `10.1109/TPAMI.2021.3054619`) extensions formulate registration over
  point/normal sets; ECS mesh/graph provenance is not an algorithmic
  precondition.
- The repository Progressive Poisson contract explicitly accepts a contiguous
  point buffer and identifies an unpublished working draft at
  `https://github.com/intrinsicD/GPU-Accelerated-Progressive-Poisson-Disk-Sampling-via-Phase-Parallel-Spatial-Hashing`.
  Brandt et al.'s visibility-aware progressive farthest-point sampler (DOI
  `10.1111/cgf.13848`) and Yuksel's weighted sample elimination (DOI
  `10.1111/cgf.12538`) are comparative adjacent methods; progressive surface
  generation must not be smuggled into this method's mesh binding.
- Statistical/radius filters classify input points before optional removal;
  exposing classification separately is consistent with Rusu et al.'s
  statistical formulation (DOI `10.1016/j.robot.2008.08.005`) and the official
  PCL StatisticalOutlierRemoval/RadiusOutlierRemoval behavior, and enables
  topology-safe analysis.
- Zhong's ISS and Rusu et al.'s FPFH (DOI
  `10.1109/ROBOT.2009.5152473`) consume positions, neighborhoods, normals, and
  indices; their point-cloud wording does not make an owning `Cloud` container
  part of the mathematical input. Count-changing utility results still need an
  explicit owning/publication decision.
- Dijkstra's shortest-path note (DOI `10.1007/BF01386390`) and
  Fruchterman–Reingold layout (DOI `10.1002/spe.4380211102`) operate on graph
  adjacency and weights/positions. Later weighted/A*/multilevel improvements
  change costs or solve strategy, not whether the adjacency came from a graph
  entity or a mesh primal graph.
- Feng and Crane's Signed Heat method (DOI `10.1145/3658220`) extends the
  scalar Heat Method lineage (DOI `10.1145/2516971.2516977`) and includes
  surface, point-cloud, and volumetric formulations. The repository package
  explicitly implements only surface Variant A, so this audit preserves a
  mesh-only UI and does not advertise unimplemented variants.

This is a bounded bibliographic cross-check for integration slicing, not the
sealed literature-intake artifact required by A24 and not a new numerical or
performance claim. Each follow-up must perform and record its fresh search,
exact formulation, later improvements, and selection/exclusion rationale in
its method/package documentation before implementation changes.
