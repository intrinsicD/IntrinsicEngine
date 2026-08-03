# Method Engine-Integration Contract Audit — 2026-08-02

## Scope

This audit applies the stable contracts `geometry.element-domain-sources` and
`method.engine-integration` to:

1. every production geometry package with a `methods/geometry/**/method.yaml`;
2. every callable geometry method registered in the Sandbox editor on
   `main`; and
3. the LOP-family integration on `feature/lop-consolidation-e2e` at
   `33930efab13764cbbd0887bfc8c726948a480479`, because that integration exposed
   the omission that triggered this audit.

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

Eligibility follows the least-structured source required by the method.
Publication is a separate decision: same-cardinality results return to the
originating element source, while topology/cardinality changes require an
explicit owning operation and must not silently discard richer data.

`HARDEN-087` closed foundational violation **V0** after this audit identified
it. Graph population now exposes the existing graph vertex/halfedge/edge
property sets as shared `Vertices + Halfedges + Edges` sources, with
`HasGraphTopology` retained as separate provenance, a canonical
`GraphHalfedge` property-discovery domain, and no fabricated faces.
The method findings below remain valid because exact point-cloud provenance is
still narrower than the shared `Vertices` capability.

## Inventory and disposition

| Method/integration | Evidence inspected | Disposition | Follow-up |
| --- | --- | --- | --- |
| ECS geometry materialization | `ECS.Component.GeometrySourcesPopulate.cpp`; `ECS.Component.GeometrySources.cppm`; retired `HARDEN-065`; `Geometry.Graph` | **V0 resolved by `HARDEN-087`.** Graph and mesh share the physical vertex/halfedge/edge source types; graph provenance remains explicit, graph halfedges preserve real `h:connectivity` without faces, and runtime/UI expose them through the canonical `GraphHalfedge` property domain. | None; V1–V4 runtime tasks may proceed against the unified source contract. |
| Boundary First Flattening / parameterization | `methods/geometry/boundary_first_flattening/method.yaml`; `src/runtime/Runtime.ParameterizationOperations.cpp`; `src/app/Sandbox/Editor/Sandbox.MethodPanels.cpp` | Conforming. The public input genuinely requires connected triangle-mesh/disk topology; runtime and UI use the Mesh domain and publish same-count UVs. | None. |
| K-Means | `GetEditorSupportedGeometryProcessingDomains`; three registrations in `Sandbox.MethodPanels.cpp`; runtime clustering contracts | Conforming. Mesh vertices, graph nodes, and point-cloud points are all advertised and registered. | None. |
| Vertex normal estimation family | `GetEditorSupportedGeometryProcessingDomains`; Mesh/Graph/PointCloud normal registrations in `Sandbox.MeshProcessingPanels.cpp` | Conforming at the engine-integration level. Domain-specific kernels intentionally differ, but all compatible entity sources have a method path. | None. |
| Mesh denoise, curvature, remesh, simplify, smooth, subdivide, repair, and parameterization operations | Runtime domain table plus Mesh Processing registrations | Conforming to stronger topology contracts. These operations use mesh adjacency/faces or mutate mesh topology; a point-span substitution is not claimed. | None. |
| Progressive Poisson | `methods/geometry/progressive_poisson/method.yaml`; `ApplyEditorProgressivePoissonCommand`; two registrations in `Sandbox.MethodPanels.cpp` | **V1 runtime resolved by `RUNTIME-208`.** Mesh, graph, and point-cloud `Vertices` now enter one non-destructive runtime/config/publication path with source-cardinality attributes. The Graph panel remains intentionally unregistered. | Re-scoped `UI-038` owns the final three-domain UI registration. |
| LOP/WLOP/CLOP/EAR feature integration | `Runtime.PointCloudConsolidationModule.cpp` at feature revision `33930efa` (exact `Domain::PointCloud` checks and replacement `Vertices` state); PointCloud-only registration in `Sandbox.MethodPanels.cpp` | **Violation V2.** Point-set kernels are gated by point-cloud provenance. Compatible mesh/graph vertex sources are hidden, and publication does not distinguish topology-safe same-cardinality output from count-changing point-cloud output. | `RUNTIME-206` + `UI-039`. |
| ICP registration | `ApplyEditorRegistrationCommand`; `view.registration` panel in `Sandbox.MeshProcessingPanels.cpp` | **Violation V3.** Both operands are point spans, but runtime and entity selectors require exact point-cloud provenance. Tunable parameters are also panel-local instead of a co-equal config lane. | `RUNTIME-207` + `UI-040` (after `BUG-096`). |
| Statistical/radius outlier processing | `ApplyEditorPointCloudOutlierRemovalCommand`; PointCloud-only “Remove Outliers” controls in `Sandbox.DomainPanels.cpp` | **Violation V4.** Detection indices are meaningful for every vertex source, but eligibility is point-cloud-only and detection is inseparable from destructive compaction. | `RUNTIME-209` + `UI-041`. |
| Signed Heat | `methods/geometry/signed_heat/method.yaml`; `Geometry.SignedHeatMethod`; no runtime/Sandbox binding | **Violation V5.** The mesh-only input restriction is legitimate, but the existing production method package has no RuntimeModule, config/agent, ECS publication/history, visualization, UI, or end-to-end integration owner. | `RUNTIME-210` + `UI-042`. |

The five method-integration violation rows are exhaustive for the stated scope;
resolved V0 was their shared physical-source prerequisite, not a sixth method.
`RUNTIME-208` resolves Progressive Poisson capability/publication while
`UI-038` retains its Graph-panel closure. LOP still needs
capability/publication correction; ICP and outlier processing need capability
plus control-surface correction; Signed Heat needs the missing end-to-end
integration while retaining its real topology contract.

## Literature cross-check

The refactor tasks require a fresh original-paper plus extensions/improvements
review before source changes. The intake used for this audit confirms only the
input/publication distinctions needed to slice the work:

- LOP originates in Lipman et al., “Parameterization-free Projection for
  Geometry Reconstruction” (DOI `10.1145/1275808.1276405`); WLOP (DOI
  `10.1145/1618452.1618522`), CLOP (DOI `10.1145/2601097.2601172`), and EAR
  (DOI `10.1145/2421636.2421645`) extend point-set consolidation without adding
  an ECS provenance requirement.
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
