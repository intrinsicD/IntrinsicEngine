# Harmonic fields

`Geometry.HarmonicField` interpolates scalar or vector signals from constrained
rows on a nonnegative weighted graph, solves Poisson and pure-Neumann problems,
and propagates seed labels with the random walker. Runtime binds it to every canonical element domain through
**View → Harmonic Field** (also under Mesh/Graph/PointCloud → Processing) and
the `sandbox.harmonic_field` config section. It uses the same sample graphs as
[property smoothing](property-smoothing.md): kNN on any domain, and uniform or
nonnegative cotangent mesh edges on mesh vertices.

## Formulation

For edge weights `W` with row sums `D`, `L = D - W`. The field `x` minimizes

```
E(x) = x^T A x + sum_soft w_i |x_i - t_i|^2 - 2 b^T x    subject to x_h = t_h on hard rows,
```

so every non-hard row satisfies `(A + W) x = W t + b`. The order `k` selects
`A = L (M^-1 L)^(k-1)`: **harmonic** (`k = 1`, Dirichlet energy), **biharmonic**
(`k = 2`, Laplacian energy) or **triharmonic** (`k = 3`, curvature-variation
energy, C2 blending at constraints). `M` holds positive row masses; it defaults to
identity and can be the DEC lumped vertex area on meshes. Mass only changes
orders above one, because `M^-1 L x = 0` and `L x = 0` have the same solutions.
The optional source `b` has the values' shape; for `-Δu = f` on a mesh use
`b = M f` (runtime multiplies its source density by the lumped area when lumped
mass is on). Hard rows are eliminated from the system; soft rows add their
weight to the diagonal and `w_i t_i` to the right-hand side. Unconstrained rows
ignore their input values.

The reduced system is symmetric positive definite exactly when every connected
component (positive-weight edges) contains a hard or soft row. Each component
without one is reported; the solve then fails, keeps that component's input
values (`UnconstrainedPolicy::KeepInput`), or solves the **pure-Neumann** problem
(`ZeroMean`). A pure-Neumann problem `A x = b` needs `sum b = 0`: the defect is
removed in proportion to mass and reported as `MaxCompatibilityDefect`
(`|sum b| / sum |b|`), the smallest row is grounded, and the mass-weighted mean
is subtracted afterwards. The reduced matrix is factored
once with sparse Cholesky and every channel is a pair of triangular solves.
Results report free/hard/soft row counts, components and the maximum relative
residual; failure publishes nothing.

**Label propagation** (random walker) turns every distinct seed label into one
harmonic (or biharmonic) indicator field with the seeds as hard rows. All
labels share one factorization. Each row takes the label with the largest value,
ties to the smallest label, and its value is reported as the confidence
(harmonic values are probabilities; higher orders may leave `[0, 1]`). The
per-label fields are also returned (and published by runtime as one float
property per seed label); harmonic ones form a partition of unity.

The CPU reference is verified against an independent dense solve on random
graphs for all three orders with and without sources, against linear fields on
paths and cotangent planar meshes (linear precision), cubics for the biharmonic
and quintics for the triharmonic order, quadratics for the Poisson source, the
zero-mean flux line and mass-weighted projection for the Neumann mode,
closed-form soft constraints, and the random-walker midpoint split and weight
partition of unity. The
[smoke benchmark](../../benchmarks/geometry/manifests/harmonic_field_smoke.yaml)
checks these analytic workloads and records runtime.

## Use cases

Sources span this repository, Framework24, the geometry-processing literature
and production tools. Everything marked *Supported* is reachable through the
kernel and the runtime operation; bounded, complex or volumetric variants are
tracked in GEOM-100.

| Application | Literature / industry | How to set it up | Status |
| --- | --- | --- | --- |
| Interpolation, inpainting or attribute transfer from sparse known values | Scattered-data interpolation; texture/attribute transfer in DCC tools | Values + bool hard mask of known rows | Supported on all domains |
| Boundary-value problems (Dirichlet data on the boundary) | Laplace equation, Pinkall–Polthier cotangent weights | Pin mesh boundary, cotangent weights | Supported |
| Membrane or thin-plate data fitting with per-row confidence | Screened fitting; least-squares meshes (Sorkine–Cohen-Or 2004) | Soft weights, optionally with hard rows | Supported |
| Screened Poisson / one implicit heat step `(M + tL) u = M u0` | Heat method step (Crane et al. 2013), screened Poisson | Soft weights `m_i / t` on every row with the input as target | Supported (expressed through soft weights) |
| Robust, total-variation or tolerance-bounded fitting of a noisy signal (e.g. mean curvature) | Rudin–Osher–Fatemi 1992; Huber 1964; Morozov discrepancy principle | `FitProperty`, exposed as **Variational fit** in View → Smooth Property ([property smoothing](property-smoothing.md)) | Supported (IRLS and active set over `Solve`, or ADMM with one factorization) |
| Bone-heat skinning weights `(-Δ + H) w = H p` | Baran–Popović 2007 (Pinocchio), Blender "automatic weights" | Soft weights `H_i` with target 1 near the bone, 0 elsewhere, one run per bone | Supported (expressed through soft weights) |
| Poisson problems `-Δu = f` (sources, sinks, potentials) | Poisson surface editing, divergence-driven fields | Source density property (lumped mass on meshes) plus hard rows | Supported |
| Pure-Neumann Poisson (fields defined up to a constant) | Boundary First Flattening Neumann solve, potential flows, gradient integration | Source + `unconstrained = ZeroMean`, no constraints | Supported |
| Handle-based deformation and fairing | Botsch–Kobbelt 2004; Jacobson et al. 2010; Maya/Blender smooth falloff | Positions as input and output, hard handles, biharmonic/triharmonic | Supported in absolute-position form; see limitations |
| Hole or patch fairing | Biharmonic/triharmonic fairing (Botsch et al., *Polygon Mesh Processing*) | Hard rows outside the patch, order 2 or 3 | Supported once the patch vertices exist; refinement tracked in GEOM-100 |
| Scribble/seed segmentation (random walker) | Grady 2006; dental tooth partition (Zou et al. 2015, Framework24) | Label mode with Int32 seeds, optional confidence | Supported on vertices, graphs and point clouds; faces use kNN on face centers |
| Harmonic / biharmonic skinning or blending weights | Joshi et al. 2007 (harmonic coordinates), bounded biharmonic weights without bounds | Label mode with a weights prefix: one field per handle label | Supported (unbounded) |
| Semi-supervised label propagation on graphs | Zhu–Ghahramani–Lafferty 2003 (harmonic functions) | Label mode on a graph or kNN point cloud | Supported |
| Harmonic / Tutte parameterization | Floater 1997; Eck et al. 1995 | Two channels with a pinned boundary | Provided by `Geometry.Parameterization.Harmonic`; consolidation in GEOM-100 |
| Smooth scalar functions for gradients, extrema, watershed or Morse layouts | Ni et al. 2004 (fair Morse functions) | Sources and sinks as hard rows; feed the output to gradient/extrema | Supported |
| Vector-field or cross-field design, connection Laplacian | Knöppel et al. 2013 (globally optimal direction fields) | Needs complex/connection weights | Deferred (GEOM-100) |
| Bounded biharmonic weights, inequality constraints | Jacobson et al. 2011 | Needs a QP solver | Deferred (GEOM-100) |
| Volumetric harmonic fields, harmonic coordinates in cages | Joshi et al. 2007 | Needs a tetrahedral Laplacian source | Deferred (GEOM-100) |

## Engine integration

| Surface | Contract |
| --- | --- |
| Least-structured input | A floating signal with 1–N channels (runtime: float/double/vec2..4) or Int32 labels, and a nonnegative weighted undirected graph. |
| Entity/domain sources | All eight canonical domains; positions on the input domain, or vertex/node positions for derived face centers and edge/halfedge midpoints. |
| Runtime owner | `Runtime.MeshFieldOperations.HarmonicField.cpp`; the sample capture and graph construction are shared with property smoothing in `Runtime.MeshFieldOperations.PropertyGraph.cpp`. |
| Config/agent | `sandbox.harmonic_field`; serialization and preview/apply share one validator. Unused optional bindings are `null`. |
| UI | View → Harmonic Field and the three domain Processing redirects. |
| Publication | Same domain and cardinality. Field mode writes one output; label mode writes labels and optional confidence in one guarded undo/redo transaction. Inputs and constraint properties are publication guards. |
| Verification | `Test.HarmonicField.cpp` (kernel), `Test.HarmonicFieldOperations.cpp` (binding, config, history). |

## Limitations

- Boundaries are Dirichlet (hard), penalty (soft) or natural Neumann; a
  prescribed nonzero Neumann flux is expressed through the source on boundary
  rows. There are no inequality bounds (bounded biharmonic weights).
- Label mode rejects `ZeroMean`: a component without seeds has no label
  information.
- Cotangent weights are clamped to be nonnegative, as in property smoothing; this
  is exact on Delaunay-like meshes and an approximation otherwise.
- Deformation in absolute-position form interpolates positions and therefore
  flattens detail between handles. Detail-preserving editing solves for a
  displacement field; bind a displacement property and add it to the rest
  positions for that.
- Faces have no dual (face-adjacency) Laplacian; they use kNN on face centers.
- Complex or direction fields (cross fields), edge 1-forms and volumetric
  domains are out of scope.
- Orders above three are not offered; each order squares the stencil width and
  fills the Cholesky factor accordingly.
- Execution is CPU-only and synchronous; the direct solver needs memory for the
  Cholesky factor.

Follow-ups for these limitations and for moving the existing harmonic
parameterization, BFF and LSCM constraint elimination onto this kernel are
tracked in [GEOM-100](../../tasks/backlog/geometry/GEOM-100-harmonic-field-follow-ups.md).

## Sources

- Framework24 `bcg_laplacian_harmonic_field.h`: hard-constrained harmonic fields
  with multi-column right-hand sides; this implementation adds explicit
  constraint rows (not nonzero values), soft constraints, the biharmonic order
  and component diagnostics.
- [Grady, *Random Walks for Image Segmentation*, IEEE TPAMI 2006](https://doi.org/10.1109/TPAMI.2006.233): label propagation by harmonic indicator fields.
- Zhu, Ghahramani and Lafferty, *Semi-Supervised Learning Using Gaussian Fields and Harmonic Functions*, ICML 2003: harmonic label propagation on graphs.
- Baran and Popović, *Automatic Rigging and Animation of 3D Characters*, SIGGRAPH 2007: bone-heat skinning weights.
- Joshi et al., *Harmonic Coordinates for Character Articulation*, SIGGRAPH 2007, and Jacobson et al., *Bounded Biharmonic Weights for Real-Time Deformation*, SIGGRAPH 2011: blending weights.
- Crane, Weischedel and Wardetzky, *Geodesics in Heat*, ACM TOG 2013: the screened heat step.
- Sawhney and Crane, *Boundary First Flattening*, ACM TOG 2017: grounded Neumann Poisson solves.
- Botsch et al., *Polygon Mesh Processing*, AK Peters 2010: fairing with harmonic, biharmonic and triharmonic energies.
- Zou et al., *Interactive tooth partition of dental mesh base on tooth-target harmonic field*, Computers in Biology and Medicine 2015: harmonic fields from tooth seeds, the Framework24 orthodontic use case.
- [Pinkall and Polthier, *Computing Discrete Minimal Surfaces and Their Conjugates*, 1993](https://doi.org/10.1080/10586458.1993.10504266): cotangent Laplacian.
- Botsch and Kobbelt, *An Intuitive Framework for Real-Time Freeform Modeling*, SIGGRAPH 2004, and Jacobson et al., *Mixed Finite Elements for Variational Surface Modeling*, SGP 2010: biharmonic interpolation for deformation and fairing.
