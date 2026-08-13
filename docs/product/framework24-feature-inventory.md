# Framework24 Registered-Feature Inventory

## Scope

This inventory starts from the systems registered by Framework24's default
viewer at comparison revision `6dd50a8289c64b5054bc9601beb5647f459d7969`:
`experimental/framework24/lib_bcg_viewer/src/bcg_viewer.cpp` in the local,
comparison-only Framework24 checkout (not part of this repository).
That is the reproducible user-facing feature surface. Low-level helper headers
are not each separate product gates unless a registered system or an accepted
IntrinsicEngine workflow consumes them. Previously identified library port
gaps remain in the structured backlog, but they do not silently expand this
viewer-replacement matrix.

The inventory describes current source evidence, not final parity. `Kernel`
means the CPU capability exists but its runtime/config/UI workflow still needs
the final audit. `Foundation proven` cites accepted ARA evidence for a bounded
lower-level path. `Open` names a known repair. `Disposition required` means
`REVIEW-004` must either accept an operational IntrinsicEngine equivalent or
strict superset that preserves every user outcome, or open a scoped
implementation owner; it may not omit the feature.

## Inventory

| Framework24 registered systems | IntrinsicEngine source/equivalent | Current state | Gate owner |
| --- | --- | --- | --- |
| Viewer window, time, FPS, keyboard, mouse, logger, colors | `platform`, runtime frame loop/input actions, Sandbox, frame-pacing diagnostics | Foundation exists; first-run usability and steady-state frame cost are open | W1; `UI-048`, `UI-049`, `GRAPHICS-135` |
| GUI and application GUI | App-owned registered Sandbox windows over runtime models; generic property widgets adopted by retired `UI-034` | Architecture foundation exists; layout, discoverability, clipping, and domain discovery are open | W1/W5; `UI-048..051` |
| Scene, entity, name, relationship, transform | ECS scene/hierarchy plus runtime document, history, and editing operations | Foundation proven for presentation and mutation by ARA C12 and C16; full save/edit workflow open | W4; `UI-046..048`, `REVIEW-004` |
| Camera, picker, selection, AABB, overlays | Runtime camera/selection/gizmo/spatial-query paths and graphics selection/debug packets | Foundation present; representative interaction parity needs exact workflow audit | W1/W5; `REVIEW-004` |
| Buffers, textures, generic material, render | RHI/Vulkan renderer, geometry residency, property texture bake, frame recipes | Foundation proven by ARA C12-C15; lighting and frame cost remain open | W1/W5; `RUNTIME-218`, `GRAPHICS-135` |
| Mesh, point-cloud, graph, picking, and vector-field materials | Domain-aware presentation/visualization recipes and Vulkan property residency | Scalar/color/label/isoline foundation proven by ARA C14; vector/domain usability open | W5; `UI-050`, `UI-051` |
| Entity-file, mesh, and point-cloud load | Geometry IO plus AssetWorkflow staged import, visible/selectable geometry-first materialization | Functional foundation exists; file choice, method readiness, latency, and enrichment cost open | W2; `UI-047`, `BUG-158..160`, `BENCH-001` |
| Geometry save/export | Geometry writers for OBJ/OFF/STL/PLY/PCD/TGF/edge-list | Writers exist but no Sandbox export workflow | W4; `ASSETIO-012`, `UI-046` |
| Point-cloud base, nearest neighbors | `Geometry.PointCloud`, KDTree, octree, property-domain sources | Kernel; generic source/runtime/UI equivalence requires audit | `REVIEW-004` |
| Point-cloud PCA, PCA geometric features, saliency | `Geometry.PCA`, point-cloud normals/features including ISS/FPFH | Kernel; complete property publication and generic visualization workflow not accepted | W5; `UI-051`, `REVIEW-004` |
| Point-cloud Gaussian noise | Deterministic sampling primitives exist; no accepted matching editor operation found | Disposition required | `REVIEW-004` |
| Point-cloud outlier probability/removal | Statistical/radius outlier operation with runtime publication, stale rejection, undo/redo, and Sandbox controls | Intrinsic workflow exists; matched semantic/output audit pending | W4/W6; `REVIEW-004` |
| K-Means clustering | One CPU/Vulkan `ClusteringService` operation with shared config/UI and truthful fallback | Foundation/parity proven in bounded fixtures by ARA C11; product timing pending | W6; `BENCH-001`, `REVIEW-004` |
| Point-cloud Gaussian mixture | `Geometry.GaussianMixture` with tested deterministic CPU numerics | Kernel; standalone registered-workflow equivalence not accepted | `REVIEW-004` |
| Grid, sampler, subsampling, octree sampling | `Geometry.Grid`, `Geometry.Sampling`, `Geometry.Octree`, point-cloud surface/consolidation operations | Mixed kernel/workflow coverage; strategy-level equivalence needs audit | `REVIEW-004` |
| Graph shortest path | `Geometry.Graph.ShortestPath` Dijkstra with published distance/predecessor properties | Kernel; Framework24's interactive source/target/path workflow needs audit | `REVIEW-004` |
| Mesh factory | Geometry primitive modules and runtime reference/procedural geometry paths | Kernel/foundation; create-and-edit UI equivalence needs audit | `REVIEW-004` |
| Mesh vertex/face normals | Halfedge/point/graph normal kernels plus runtime editor operations | Kernel and selected editor paths exist; domain-generic panel work remains outside the accepted product gate | W5; `UI-051`, `REVIEW-004` |
| Mesh vertex/face gradients and mesh vertex PCA | DEC/analysis/PCA kernels and canonical property containers | Kernel; registered runtime/config/UI workflow equivalence not accepted | `REVIEW-004` |
| Mesh connected components | `Geometry.HalfedgeMesh.Repair` component labeling/splitting/largest-component operations | Kernel; registered editor workflow equivalence not accepted | `REVIEW-004` |
| Mesh geodesics | Heat-method geodesics plus graph shortest-path kernels; not the same virtual-source formulation | Potentially better/different replacement; output and interaction equivalence must be demonstrated | `REVIEW-004` |
| Mesh curvature | Signed edge-dihedral principal curvature plus Meyer cross-check, runtime publication, and visualization recipes | Correctness repairs active; resident topology/output/visualization performance still open | W3; `BUG-154`, `BUG-156`, `UI-050`, `BENCH-001` |
| Mesh Laplacian | DEC/Laplacian/smoothing kernels used by geometry operations | Kernel; Framework24 field/smoothing controls and visible output need audit | `REVIEW-004` |
| Mesh simplification, remeshing, subdivision | QEM/FA-QEM, uniform/adaptive remeshing, Loop/sqrt3/Catmull-Clark operations with editor controls | Intrinsic workflows exist; matched result, undo, visibility, and latency audit pending | W4; `REVIEW-004` |
| Correspondence and rigid registration | Feature/correspondence kernels plus point-to-point/point-to-plane ICP editor workflow | Intrinsic workflow exists; compatible-domain and matched behavior work remains in existing runtime/UI backlog | `REVIEW-004` |
| Coherent Point Drift | `METHOD-015` owns the CPU reference family; no accepted Intrinsic package/runtime/UI path yet | Open Framework24 feature gap | `METHOD-015`, then `REVIEW-004` assigns integration if the reference passes |
| Implicit system | SDF/implicit-plane, marching-cubes, and surface-reconstruction kernels | Kernel; registered interactive workflow equivalence not accepted | `REVIEW-004` |
| Orthodontic system | No accepted general IntrinsicEngine equivalent identified | Open Framework24 feature gap; an operational equivalent or strict superset needs a scoped owner | `REVIEW-004` |
| Compute-shader test | Promoted Vulkan compute/readback/parity tests and real K-Means/LOP compute paths | Superseded by stronger operational Vulkan evidence; verify no user workflow is lost | W6; ARA C08-C11 and C34-C36, `REVIEW-004` |
| Statistics and eigendecomposition | `Geometry.Statistics`, `Geometry.Linalg`, PCA, property plots/metadata | Kernel; generic inspectability is part of W5 | `UI-051`, `REVIEW-004` |

## Audit rule

`REVIEW-004` must walk every row. A row closes only when the registered
Framework24 user outcomes are represented by an operational, tested
IntrinsicEngine equivalent or strict superset. Different architecture,
interaction design, algorithms, APIs, or rendering technology are allowed;
loss of a user-facing capability is not. The six golden workflows do not
waive inventory rows, and a source file, module name, or unit test alone does
not prove product parity.
