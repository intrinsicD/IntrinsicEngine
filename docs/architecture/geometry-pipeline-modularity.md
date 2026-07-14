# Geometry Processing Pipeline Modularity

Status: `roadmap`. This document is a directional design/roadmap note, **not** a
claim that the described pipeline framework is implemented. Present-tense
statements about *current* code are anchored with `file:line`; everything
described as a stage/axis/slice below is planned work owned by the slice
roadmap in the final section. When any slice lands, move the corresponding
"current state" claims into [`geometry.md`](geometry.md) and demote this note.

Related canonical policy this design obeys:

- [Algorithm variant dispatch pattern](algorithm-variant-dispatch.md) — the
  `Strategy` (`std::variant`/enum) × `Backend{CPU,GPU}` seam this design reuses
  as its primary dispatch mechanism.
- [Geometry API style and numeric policy](geometry-api-style.md) —
  `Core::Expected`/structured-result-record/`std::optional` failure reporting,
  deterministic diagnostics, seeded randomness, "request the least structured
  domain you need".
- [`docs/agent/method-workflow.md`](../agent/method-workflow.md) — the
  CPU-reference-first method contract that any *named paper* stage must follow.
- Layer contract (`AGENTS.md` §2): `geometry -> core` only; `runtime`
  owns execution facades and generic editor infrastructure; `app` owns
  Sandbox presentation; `methods` consume the public method API.

## 1. Problem: geometry and UI are stitched together

Two concrete couplings motivate this work.

**Historical baseline: the algorithm catalog and its dispatch lived inside the
UI.** Before ARCH-006, the now-retired
`src/runtime/Editor/Runtime.SandboxEditorUi.cpp` (then ~16.9k lines) defined a
`SandboxEditorGeometryProcessingAlgorithm` enum spanning ~20 algorithm families
(`Registration`, `Smoothing`, `Remeshing`, `Simplification`, `Parameterization`,
`SurfaceReconstruction`, denoise/outlier families, …) and drives it through
several **parallel `switch(algorithm)` statements** — domain-compatibility
(`:2748`), per-algorithm parameter panels (`:12483`), execution (`:13286`) —
with 89 references to that enum in one file. Each panel hand-marshals ImGui
widget state into a `Command` struct and calls the geometry free function
inline, then writes results back to ECS + `Dirty::Mark*`. Adding or swapping an
algorithm requires editing the god-file in several places. (`Geometry.MeshOperator`
is even imported but only supplies a shared *result* struct
(`Geometry.MeshOperator.cppm:25`), not an operator abstraction — a signal of an
abandoned operator migration.)

ARCH-006 has since moved ImGui state, widgets, and registration into
`Extrinsic.Sandbox.Editor.Shell` and the app-owned panel modules. The
algorithm catalog, deterministic panel models, and typed command execution
remain runtime-owned in `Extrinsic.Runtime.SandboxEditorFacades`. The
remaining modularity concern is therefore the breadth of that runtime facade,
not runtime ownership of Sandbox presentation.

**Registration is monolithic.** `Geometry::Registration::AlignICP`
(`Geometry.Registration.cppm:145`) is a single rigid-only free function whose
loop (`Geometry.Registration.cpp:450`) fuses four already-named-but-anonymous
helpers — `FindCorrespondences` → `RejectOutliers` → `ApplyRobustWeights` →
`SolvePointToPoint`/`SolvePointToPlane` → an inline convergence check. It has no
seams, no initial-pose input, no global/coarse stage, and no non-rigid path.

The engine already owns the raw material a modular pipeline would assemble:
`Geometry.Robust` (seven M-estimator kernels), `Geometry.KDTree`,
`Geometry.RotationAveraging`, `Geometry.PointCloud.Features`
(keypoints → FPFH descriptors → `MatchDescriptors` → `EstimateCoarseAlignment`
RANSAC, `Geometry.PointCloud.Features.cppm:184`), `Geometry.MeshClosestFace`,
and the documented Algorithm-Variant-Dispatch idiom (`Geometry.KMeans` exemplar).
**The pipeline is assembly of existing parts, not new mathematics.**

## 2. How it is done well (the reference model)

Open3D, PCL, ITK/Elastix, and MeshLab independently converge on the same
factoring — that convergence is the design signal.

**Rigid registration = a thin driver over six orthogonal, swappable stages.**
(PCL's `pcl::Registration` base with setter injection; Open3D's
`registration_icp(...)` arguments.)

| # | Stage | Variants in the wild |
|---|-------|----------------------|
| 1 | Correspondence estimator `(src, tgt, T) → pairs` | kd-tree nearest (+ reciprocal), normal-shooting, projective, feature-match (FPFH/SHOT) |
| 2 | Rejector chain (composable, *hard* cut) | max-distance, one-to-one, percentile/variance-trim, normal-angle, geometric-consistency, RANSAC |
| 3 | Transformation estimator `(weighted pairs) → SE(3)` | point-to-point SVD (Umeyama), point-to-plane LLS, symmetric, generalized-ICP |
| 4 | Robust kernel (IRLS *soft* weighting, orthogonal to #3) | L2/None, Huber, Cauchy, Tukey, Geman-McClure, Welsch — each with a scale ≈ noise σ |
| 5 | Convergence criteria `(rmse, fitness, ΔT, iter) → stop?` | + oscillation/similar-transform guard; supplied **per pyramid scale** |
| 6 | Global/coarse stage (no prior pose) | feature-RANSAC, Fast Global Registration, TEASER++, Super4PCS |

Two load-bearing distinctions:

- **Rejection (#2) and robust weighting (#4) are separate interfaces** even
  though both fight outliers, because you routinely want *trimmed rejection +
  Tukey weighting together*. Fusing them loses that.
- Above the fine loop sits a **two-stage coarse→fine architecture** wrapped in a
  **voxel-pyramid scheduler** (a `list of (voxel_size, criteria)`, loose/coarse
  early and tight/fine late).

**Non-rigid registration adds three more orthogonal knobs.** CPD (Myronenko
2010), Amberg non-rigid ICP (2007), Sumner embedded deformation (2007), and
ARAP (Sorkine 2007) all share the skeleton *correspondence → deformation-parameter
solve → additive regularizer → annealing/coarse-to-fine schedule*, differing
only in how deformation is parameterized and how the regularizer is weighted:

| Method | Deformation model | Regularizer / smoothing | Schedule |
|--------|-------------------|-------------------------|----------|
| CPD | dense RKHS displacement field | `λ·‖v‖²` motion-coherence, kernel width β | σ² variance annealing |
| Amberg N-ICP | per-vertex 3×4 affine | stiffness `α·‖neighbor-affine-diff‖²` | explicit decreasing **α list** |
| Embedded deformation | sparse graph of affine nodes | rotation + regularization terms | graph node spacing |
| ARAP | per-cell rotations, cotan Laplacian | rigidity energy *is* the prior | (deformation solver under a correspondence layer) |
| Functional maps | low-rank matrix in LBO eigenbasis | commutativity/orthonormality | eigenbasis size *k* |

The "how deformation vectors are smoothed / a schedule" question maps directly
onto **Amberg's stiffness schedule** and **CPD's β / σ² annealing**: a
regularizer keyed on a scalar strength, driven by a coarse-to-fine schedule.
(Functional maps live in *correspondence space*, so model them as an alternative
correspondence backend feeding a spatial deformation stage, not as a peer of
CPD/ARAP.)

**"Declared parameter schema drives the UI" is the decoupling principle.**
MeshLab filters return a `RichParameterList` (typed name/default/label/range) and
the GUI dialog, the Python bindings, and the `.mlx` script format are **all
auto-generated from that one schema**, with `getPreConditions()` capability
gating validated *before* execution. Elastix expresses a whole registration as an
ordered vector of parameter maps, each selecting `Transform + Metric + Optimizer +
Interpolator + Sampler + Pyramid` **by name**. Two consequences to steal:
(1) parameters-as-data yields UI + CLI + JSON serialization + a reproducible run
record from one schema, with zero hand-written adapters; (2) the pipeline is an
ordered list of stages, so multi-stage (rigid → affine → non-rigid) falls out
naturally and each stage is independently serializable.

**What not to build.** ITK `ProcessObject`/`DataObject` and VTK
`vtkAlgorithm`/`vtkExecutive` are demand-driven multi-pass pull graphs — powerful
for streaming images larger than RAM, but far heavier than a swappable-stage
registration loop needs. YAGNI.

## 3. Design principles for IntrinsicEngine

### 3.1 Governing decision — concrete first, no speculative framework

**Do not build a general "geometry pipeline framework" up front.** `AGENTS.md`
§5 forbids introducing features during refactors, and the weekly agent-output
audit explicitly flags "premature abstraction" and "ceremony-without-shipped-value".
The reference model is a *strategy-object decomposition inside one algorithm
family*, not a universal DAG engine.

> Build the swappable-stage decomposition for **registration concretely first**,
> as plain `Geometry::Registration` sub-modules following the existing
> Algorithm-Variant-Dispatch idiom. Extract a shared `Stage`/`Pipeline`/`Schedule`
> contract **only once a second family (parameterization or smoothing)
> demonstrably needs the same seams** — the repo's own "grow on the second
> caller" rule.

### 3.2 Where each piece lives (respecting hard layering)

| Concern | Layer | Module(s) |
|---------|-------|-----------|
| Stage/param/result types, kernels, CPU-reference driver | **geometry** | `Geometry.Registration` (+ new `Geometry.Registration.*` sub-modules) |
| Reused kernels (no new math) | **geometry** | `Geometry.Robust`, `Geometry.KDTree`, `Geometry.RotationAveraging`, `Geometry.PointCloud.Features`, `Geometry.MeshClosestFace`, `Geometry.Rotation`, `Geometry.Sparse`/`Geometry.Linalg` |
| Data-driven **param schema** (key/type/default/label/range) | **geometry-local** initially; promote to `core` on a second consumer | new `Geometry.Registration` descriptor types |
| GPU-capable overloads + `IDevice::IsOperational()` fallback | **runtime** | `Runtime.RegistrationBackend` (mirrors `Runtime.KMeansBackend`) |
| Heavy/async execution (large clouds, coarse RANSAC, BVH builds) | **runtime** | `Runtime.DerivedJobGraph` (do not invent a fourth graph system) |
| Runtime panel model + typed command facade | **runtime** | `Extrinsic.Runtime.SandboxEditorFacades`; split focused private implementation units as families grow |
| UI (thin schema-reflecting adapter) | **app** | `Extrinsic.Sandbox.Editor.MeshProcessingPanels`, registered through `Extrinsic.Sandbox.Editor.Shell` |
| Serializable pipeline config (files/CLI/agent lane) | **runtime** | route through the `Engine` preview→apply facade with `RuntimeConfigControlSource` |
| Named papers (TEASER, FGR, CPD, non-rigid) | **methods** | `methods/geometry/…` + `method.yaml`, under the method contract |

`Geometry.MeshOperator` stays a shared *result* record — do not overload it into
a base class. `DerivedJobGraph` is the *execution* substrate, not the composition
contract: the pipeline driver is a plain geometry function; runtime *wraps* a
heavy invocation in a `DerivedJob` when async is needed.

### 3.3 The Stage contract — choose the mechanism per axis

Three mechanisms are in tension; pick per-axis rather than globally.

1. **Stage *selection* → `std::variant`/enum (primary).** Registration's stage
   set is closed and known, and the canonical engine idiom for "which variant
   runs" is `Strategy = std::variant<…>` or a small enum (see
   [algorithm-variant-dispatch.md](algorithm-variant-dispatch.md); `Geometry.KMeans`).
   This gives value semantics, no vtable/heap, compile-time exhaustiveness,
   deterministic dispatch, and stays `geometry -> core`. It also sidesteps a real
   hazard: static-init self-registration of a virtual registry gets stripped in
   C++23-module + static-lib builds when a translation unit is unreferenced.
2. **Runtime-virtual `IStage` → deferred.** A virtual interface buys an *open*
   set (third-party/plugin stages at runtime). Registration does not need that.
   Adopt it only if/when external method packages must inject stages the geometry
   layer cannot know about, and then via an **explicit `registerStages(Registry&)`
   entry point called from runtime** — never implicit static-init — preserving
   "geometry defines / runtime wires".
3. **Data-driven param *schema* → mandatory, orthogonal to 1 & 2.** Independent
   of how stages dispatch, parameters must be data: an ordered list of
   `ParamDesc{key, type(int/float/bool/enum/vec3/percentage), default, label,
   tooltip, min/max}`. One schema generates the ImGui widget, the CLI/config
   field, JSON (de)serialization, and validation. **This is what actually
   decouples the UI and delivers the reproducibility + agent lane.**

Illustrative shape (geometry layer, `geometry -> core` only):

```cpp
// Geometry.Registration.Stages.cppm  (illustrative — see slice roadmap)
export namespace Geometry::Registration {

enum class Domain  : uint8_t { PointCloud, Mesh, Graph };
enum class Backend : uint8_t { CPU = 0, GPU = 1 };   // matches Geometry.KMeans

// Capability contract (MeshLab pre/post-conditions), checked BEFORE compute.
struct StageCapabilities {
    Domain InputDomain{Domain::PointCloud};
    bool   RequiresTargetNormals{false};  // PointToPlane, NormalShooting
    bool   RequiresSourceNormals{false};  // FPFH descriptors, symmetric
    bool   RequiresDescriptors{false};    // feature-based correspondence
};

// Stage *selection* is a variant/enum (closed set), not a vtable.
enum class CorrespondenceKind : uint8_t { KdTreeNearest, KdTreeReciprocal,
                                          NormalShooting, FeatureMatch, MeshClosestPoint };
enum class RejectorKind       : uint8_t { MaxDistance, PercentileTrim, VarTrim,
                                          OneToOne, NormalAngle, GeometricPoly };
enum class TransformKind      : uint8_t { PointToPointSVD, PointToPlaneLLS,
                                          SymmetricPointToPlane };

struct RegistrationPipelineParams {
    CorrespondenceKind        Correspondence{CorrespondenceKind::KdTreeNearest};
    std::vector<RejectorKind> RejectorChain{RejectorKind::MaxDistance,
                                            RejectorKind::PercentileTrim}; // PCL-style chain
    TransformKind             Transform{TransformKind::PointToPlaneLLS};
    std::optional<Geometry::Robust::RobustKernel> RobustKernel{};          // IRLS, orthogonal to chain
    double                    RobustScale{1.0};
    // ... existing knobs: MaxIterations, MaxCorrespondenceDistance,
    //     InlierRatio, KDTreeLeafSize ...
    ConvergenceCriteria             Convergence{};   // now its own struct (+ oscillation guard)
    std::optional<CoarseToFineSchedule> Schedule{};  // voxel pyramid, opt-in
    Backend                   Compute{Backend::CPU};
};

struct RegistrationResult {
    glm::dmat4          Transform{1.0};
    double              FinalRMSE{0.0};
    std::vector<double> RMSEHistory{};
    std::size_t         IterationsPerformed{0};
    bool                Converged{false};
    std::size_t         FinalInlierCount{0};
    // Algorithm-Variant-Dispatch telemetry (currently MISSING from Registration):
    Backend             RequestedBackend{Backend::CPU};
    Backend             ActualBackend{Backend::CPU};
    bool                FellBackToCPU{false};
    CorrespondenceKind  ActualCorrespondence{};  // reports fallback (e.g. P2Plane → P2Point)
    StageStatus         Status{StageStatus::Success};
};
}
```

A small internal `switch` per axis dispatches the variant — exactly as `AlignICP`
already branches on `ICPVariant` at `Geometry.Registration.cpp:476`. This keeps
everything value-typed, allocation-free, and trivially deterministic (an
`AGENTS.md` §7 / geometry-api-style requirement).

### 3.4 Optional observability — the iteration-trace seam

Iterative stages should expose an **optional observer** so callers can watch the
intermediate solution (e.g. render the shape under the current estimate each
iteration) — with **zero cost when it is not used**. This is the standard
iteration-callback pattern (Open3D registration callbacks, PCL
`registerVisualizationCallback`, Ceres `IterationCallback`, ITK observers).

Why it is free when disabled: the observer is a null-by-default hook checked
**once per iteration**, not per point. The ICP hot path is the per-point KDTree
work *inside* an iteration; an `if (observer)` guard around it is O(iterations)
predictable branches — immeasurable next to O(points × iterations). And the loop
already computes everything worth observing (the cumulative transform, the
per-iteration RMSE, the inlier count), so emitting a trace is *reading existing
state*, never extra work or allocation.

For rigid registration the complete description of "the shape under the current
solution" is a single `glm::dmat4`: the observer forwards it and the renderer
applies it on the GPU — no CPU point transformation, no copies. So the v1 trace
is scalar+matrix only:

```cpp
// Geometry.Registration.cppm  (geometry -> core only; pure data)
export namespace Geometry::Registration {

// Read-only snapshot emitted at the end of each ICP iteration. Observing never
// mutates solver state (determinism). Copy fields to retain them.
struct IterationTrace {
    std::size_t Iteration{0};    // 0-based iteration index
    glm::dmat4  Transform{1.0};  // cumulative source->target estimate AFTER this iteration
    double      RMSE{0.0};        // inlier RMSE evaluated this iteration (== RMSEHistory[Iteration])
    std::size_t InlierCount{0};   // correspondences used this iteration
};

// Null by default => zero overhead. Passed SEPARATELY from RegistrationParams so
// the serializable/reproducible config stays a pure value (a std::function is not
// serializable and must not pollute the pipeline config / agent lane).
using IterationObserver = std::function<void(const IterationTrace&)>;

[[nodiscard]] std::optional<RegistrationResult> AlignICP(
    std::span<const glm::vec3> sourcePoints,
    std::span<const glm::vec3> targetPoints,
    std::span<const glm::vec3> targetNormals = {},
    const RegistrationParams& params = {},
    const IterationObserver& observer = {});   // null => zero overhead
}
```

Rules that keep it clean:

- **Config vs hook are separate.** The observer is an argument, not a
  `RegistrationParams` field — the config remains a serializable value that
  drives reproducibility and the agent lane; the observer is a non-serializable
  runtime concern.
- **Geometry defines the seam; runtime forwards it.** `IterationTrace` is pure
  geometry/core data (glm + scalars), so `geometry -> core` holds. The editor
  supplies the concrete observer that pushes each `Transform` into the
  visualization overlay, and owns any throttling (draw every Nth iteration) —
  policy lives in the observer, not the kernel.
- **Read-only.** The observer must not mutate solver state; this protects
  determinism and lets the same run be replayed headlessly.

This generalizes across the pipeline: every iterative stage (non-rigid,
smoothing, remeshing) emits progress through the same observer contract. The one
case that adds point data is **non-rigid**, where a single matrix cannot
represent the deformation — there the trace gains a deformed-positions span
(pointing at the buffer the deformation solve already fills), still with no extra
compute when observing.

The runtime-side consumer is `Extrinsic.Runtime.RegistrationAlignment`
(`runtime -> geometry`): `AlignPointClouds(...)` runs `AlignICP` with a
trace-collecting observer and returns the final result plus the full
per-iteration trajectory, and `TrajectoryPose(outcome, index)` yields the
renderer-facing `glm::mat4` to preview at each step (identity at step 0). The
Sandbox editor panel that lets a user select two point clouds, run registration,
and scrub the convergence with a slider is tracked by
[`UI-029`](../../tasks/archive/UI-029-editor-registration-convergence-visualization.md).

## 4. How registration decomposes onto existing code

The `AlignICP` loop already *is* the reference model's structure with hard-wired
choices — the four helpers map 1:1 onto reference interfaces.

| Reference interface (§2) | Existing IntrinsicEngine asset | Swappable target |
|---|---|---|
| #1 Correspondence | `FindCorrespondences` via `KDTree::QueryKNN` (k=1), `Geometry.Registration.cpp:48` | add `KdTreeReciprocal` (`Features::CorrespondenceParams.MutualBest` exists, `Features.cppm:126`), `NormalShooting`, `FeatureMatch` (delegate to `Features::MatchDescriptors`, `Features.cppm:195`), `MeshClosestPoint` (`MeshClosestFace::Query`, `MeshClosestFace.cppm:82`) |
| #2 Rejector chain | `RejectOutliers` percentile (`Geometry.Registration.cpp:114`) | `RejectorChain` vector: keep `PercentileTrim`; add `MaxDistance` (already inline as `maxDistSq`), `NormalAngle`, `GeometricPoly` (mirror `Features::EstimateCoarseAlignment` edge-length gate, `Features.cppm:203`) |
| #4 Robust kernel | `ApplyRobustWeights` via `Geometry::Robust::Weight` (7 kernels) | keep optional; surface Tukey/Welsch/Geman-McClure in the schema (present in `Geometry.Robust`, not yet in `RegistrationParams`) |
| #3 Transform | `SolvePointToPoint` (SVD via `Rotation::OptimalRotation`) / `SolvePointToPlane` (6×6 Cholesky) | wire `TransformKind`; add `SymmetricPointToPlane` (Rusinkiewicz 2019) as a later CPU-reference method |
| #5 Convergence | inline rel-RMSE (`Geometry.Registration.cpp:491`) | extract `ConvergenceCriteria` struct; add PCL oscillation/similar-transform guard |
| #6 Global/coarse | `Features` keypoint→descriptor→match→RANSAC pipeline, **disconnected from ICP** | add `RegisterCoarse(...) → dmat4` feeding a new `init` parameter on the fine path |
| Multi-scale schedule | *(absent)* | `CoarseToFineSchedule = list<(voxel_size, ConvergenceCriteria)>` over existing `PointCloud` voxel utilities |

`Geometry.RotationAveraging` (Chordal/Quaternion/Karcher/GeodesicMedian) is the
substrate for a future multi-hypothesis/voting global stage — not needed early,
but the "global stage emits candidate poses" seam leaves room for it.

## 5. The same abstraction covers parameterization / smoothing / denoising

Every family already shares the reference skeleton (`Params → std::optional<Result-with-diagnostics>`):

- **Smoothing / denoising** = the non-rigid skeleton with correspondence fixed to
  identity: a deformation model (which Laplacian/operator) + a regularizer/strength
  + an iteration/annealing schedule. `Geometry.Smoothing` already exposes
  `Uniform/Cotan/Taubin/ImplicitLaplacian/DenoiseBilateral` with `Iterations`/
  `Lambda`/`PreserveBoundary` and convergence tracking — an enum + schedule waiting
  to be named.
- **Parameterization** = a solver choice (LSCM/Harmonic-Tutte, future ARAP/SLIM)
  + a boundary policy + the already-canonical shared `ParameterizationDiagnostics`
  (conformal/authalic/stretch/flipped/seam metrics). See
  [`GEOM-019`](../../tasks/backlog/geometry/GEOM-019-harmonic-tutte-parameterization-boundary-constraints.md)
  and the [parameterization roadmap](parameterization-mapping-roadmap.md).
- **The coarse-to-fine schedule is the same object everywhere**: Amberg's
  stiffness list, CPD's σ² annealing, and a voxel/resolution pyramid all reduce to
  "drive a named scalar parameter of another stage over a list of levels". Model
  it once and reuse it across registration, smoothing, and remeshing.

**Unify by extracting a shared contract, not by predicting it.** Land
registration's decomposition → land one more family's decomposition using the
same shapes by convention → *then* factor the common `Stage`/`Schedule`/`Diagnostics`
types into a shared module. The `Domain` enum in the capability struct is the
placeholder for input-domain polymorphism, not a solution to it.

## 6. Decoupling the UI

Target: a thin schema-reflecting adapter, replacing per-algorithm hand-marshaling.

1. Geometry publishes a `ParamSchema` per pipeline (`RegistrationPipeline::Schema()`
   → ordered `ParamDesc`).
2. A single generic `DrawParamSchema(schema, values)` renders widgets by type
   (int→`DragInt`, float→`DragFloat`, enum→combo, bool→`Checkbox`,
   percentage→slider), replacing the bespoke `DrawMeshDenoiseControls` /
   `DrawMeshRemeshControls` / … marshaling. UI holds only a `ParamValues` blob
   (draft state), never a hand-built `Command`.
3. Execution goes through a typed runtime facade
   (`RegistrationEditorController::Run(entity, ParamValues)`): validate via schema,
   dispatch to the geometry pipeline (or wrap it in a `DerivedJob` for heavy
   inputs), and on completion publish results + `Dirty::Mark*` in one
   `EditorCommandHistory` transaction.
4. `ParamValues` serializes to the same JSON the CLI/config/agent lane loads,
   routed through the `Engine` preview→apply facade with `RuntimeConfigControlSource`.
   A registration run becomes a *schema id + values blob + backend identity*,
   replayable headless — the reproducibility and agent-lane payoff.
5. Capability gating becomes discoverable: the panel queries `StageCapabilities`
   up front and disables/annotates unavailable stages *before* the user clicks.

Do this for **one panel (registration) first** as the proof; leave all other
panels untouched. This is explicitly *not* a bulk decomposition of the 16.9k-line
file.

## 7. Extension cookbook (experiment fast, add methods easily)

The point of this architecture is that new research plugs in without touching the
UI or the driver. Once the slices below land, the recipes are:

**Add a new correspondence / rejector / transform variant** (same-family, closed
set):
1. Add an enumerator to the relevant `*Kind` enum in `Geometry.Registration`.
2. Implement the kernel as an internal function reusing existing geometry
   (`KDTree`, `Robust`, `Rotation`, `Features`, `MeshClosestFace`).
3. Add one `case` to that axis's dispatch `switch` (the compiler's exhaustiveness
   check flags every site to update).
4. Add its parameters to the `ParamSchema`. The UI widget, CLI flag, and JSON
   field appear automatically — **no UI code**.
5. Add a CPU-reference test; if it declares `StageCapabilities`, add a
   capability-gating test.

**Add a new schedule** (annealing / pyramid): construct a `CoarseToFineSchedule`
that drives any named scalar param over a list of levels; reuse it across
families unchanged.

**Observe / trace intermediate steps** (§3.4): pass an `IterationObserver`
(null by default, so leaving it off costs nothing). To watch convergence, apply
each trace's `Transform` to the source in the renderer — no CPU point work. The
same hook records a per-iteration trajectory for diagnostics/reproducibility.

**Add a whole new method / paper** (open set — TEASER, FGR, CPD, non-rigid,
functional maps): implement it under `methods/geometry/<method_id>/` with a
`method.yaml`, following [`docs/agent/method-workflow.md`](../agent/method-workflow.md)
(CPU reference first → correctness tests → benchmark manifest with a stable
`benchmark_id` → optional optimized/GPU with parity). Expose it to the pipeline
either as a coarse/global stage returning an init pose, or — when it needs an
open plugin seam the geometry layer cannot enumerate — via the deferred
`registerStages(Registry&)` entry point called from runtime (§3.3 mechanism 2).
Rigid-ICP composition stays in `geometry`; **named papers live in `methods`.**

**Experiment without recompiling stage code**: because the pipeline config is a
serializable `ParamValues` blob, sweeping variants/parameters (and comparing
against a baseline) is a matter of editing JSON / driving the agent lane, and
every run records its backend identity and diagnostics for reproducibility.

## 8. Slice roadmap

Each slice is independently shippable, CPU-reference-first, with tests + docs,
following the nine-section task template. Slices coordinate with the geometry
umbrella
[`RORG-031E`](../../tasks/backlog/geometry/RORG-031-geometry-method-readiness.md);
the UI slice follows the `UI-024..028` panel pattern under the UI umbrella; the
method slice cites retired
[`GEOM-017`](../../tasks/archive/GEOM-017-point-cloud-descriptors-registration-seams.md)'s
deferred "future robust/global registration method packages" edge.

| Slice | Goal | Behavior change | Task |
|-------|------|-----------------|------|
| 0 | Extract the four helpers into a named internal stage sequence (internal convergence helper; no public `.cppm` change) | **none** (bit-for-bit) | [`GEOM-054`](../../tasks/archive/GEOM-054-registration-pipeline-stage-extraction.md) |
| 0-obs | Optional per-iteration observer seam (`IterationTrace`), null by default — zero cost when off; the renderer applies each trace's transform to show the shape under the current solution (§3.4) | additive | [`GEOM-055`](../../tasks/archive/GEOM-055-registration-iteration-observer.md) |
| 1 | Swappable `CorrespondenceKind` + `RejectorChain` vector + surface existing `Geometry.Robust` kernels in params | defaults reproduce today exactly | (future) |
| 2 | Public `ConvergenceCriteria` struct + `TransformKind` enum + `RegistrationResult` backend telemetry + convergence oscillation guard | additive | (future) |
| 3 | Global/coarse glue: `RegisterCoarse(...)` running the `Features` pipeline + `init` pose param on the fine path | new capability | (future) |
| 4 | `CoarseToFineSchedule` (voxel pyramid) as a first-class, reusable object | new capability | (future) |
| 5 | Decouple the **registration UI panel** (schema publish + generic `DrawParamSchema` + `RegistrationEditorController` facade + serializable `ParamValues`) | proof of the pattern | (future, UI umbrella) |
| 6 | First **non-rigid method** (CPD or Amberg N-ICP) under `methods/geometry` with deformation-model + regularizer + annealing-schedule seams | new method (method contract) | (future, `METHOD-*` citing GEOM-017) |
| 7 | Extract the shared `Stage`/`Schedule`/`Diagnostics` contract onto a second family (smoothing or parameterization) | refactor | (future; gated on a real second consumer) |

Slice 0 (`GEOM-054`) and Slice 0-obs (`GEOM-055`) are retired. Later slices are
opened when the prior slice lands and the next is the priority. A new `GEOM-0NN`
is allocated per slice (the `GRAPHICS-072/073/074` series pattern).

## 9. Risks

1. **Over-abstraction (highest).** A speculative `IStage`/DAG framework built
   before a second consumer needs it will be flagged by the weekly audit.
   Mitigation: concrete-first (§3.1); Slice 0 changes zero behavior; the shared
   contract is extracted only on the second family's demonstrated need (Slice 7).
2. **Layering.** Keep the driver + kernels `geometry -> core`; ECS/`Dirty` and
   GPU wiring stay in `runtime`, while Sandbox UI consumes runtime facade data
   from `app`; `check_layering.py --strict` gates it. The
   `ParamSchema` must use `core`-or-geometry-local types only — no runtime/UI
   types leaking downward.
3. **Facade re-concentration.** ARCH-006 retired the runtime-owned presentation
   god-file, but the current `Runtime.SandboxEditorFacades.cpp` still carries
   a broad catalog/model/command implementation. Mitigation: keep
   `DrawParamSchema` and controller state in the app panel, keep the typed
   model/command contract in runtime, and split runtime implementation units by
   result-consumer family as they grow. The clean-workshop review gate catches
   renewed god-file accumulation.
4. **Determinism / reproducibility.** Coarse RANSAC and subsampling introduce
   randomness; geometry-api-style and `AGENTS.md` §7 require seeded,
   platform-documented, order-stable results. `Features::CoarseAlignmentParams`
   already carries an explicit `Seed` — thread it into diagnostics and never
   introduce unseeded RNG. `std::variant` dispatch (not a hash-map registry)
   keeps stage ordering deterministic.
5. **Backend-parity obligations.** Adding `Backend`/`RequestedBackend`/
   `ActualBackend`/`FellBackToCPU` commits to the Algorithm-Variant-Dispatch
   contract: every result reports the backend that actually ran; a GPU request
   resolving to CPU is valid only when telemetry says so (KMeans precedent). The
   `DerivedJobRegistry` GPU domain is declared-but-not-operational, so a GPU
   *async* job is blocked until that infra lands; the CPU async path works today.
6. **Method-contract boundary.** The moment TEASER/FGR/CPD/symmetric-P2Plane/
   non-rigid are implemented, `AGENTS.md` §6 and the method workflow bind them
   (CPU reference first → tests → benchmark manifest → parity → limitations doc).
   Rigid-ICP composition stays in `geometry`; named papers go to `methods/geometry`.
   `GeneralizedICP` needs per-point covariances on both clouds, so it is a whole
   *method*, not a `TransformKind`.
7. **Docs-sync.** Any `.cppm` surface change regenerates
   `docs/api/generated/module_inventory.md`; the Strategy×Backend addition should
   add Registration as a second exemplar in
   [algorithm-variant-dispatch.md](algorithm-variant-dispatch.md); task changes
   regenerate `tasks/SESSION-BRIEF.md`.

## 10. Source anchors

- `Geometry.Registration.cpp:450` — the existing four-stage ICP loop.
- `Geometry.Registration.cppm:73` / `:145` — `RegistrationParams`/`AlignICP` to extend.
- `Geometry.PointCloud.Features.cppm:184` — the coarse-align seam to glue in.
- `Geometry.KMeans.cppm` + [algorithm-variant-dispatch.md](algorithm-variant-dispatch.md) — the Strategy×Backend telemetry to adopt.
- `src/runtime/Runtime.SandboxEditorFacades.cppm` and
  `src/runtime/Runtime.SandboxEditorFacades.cpp` — the current runtime-owned
  algorithm catalog, capability model, and typed command facade.
- `src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp` — the current
  app-owned registration presentation/controller.
- Historical pre-ARCH-006 anchor: the retired
  `src/runtime/Editor/Runtime.SandboxEditorUi.cpp` contained the parallel
  `switch(algorithm)` sites that originally motivated this roadmap.
- `AGENTS.md` §2/§5/§6 — layering, anti-premature-abstraction, method contract.
- [geometry-api-style.md](geometry-api-style.md) — `Expected`/result-record/deterministic-diagnostics policy.
