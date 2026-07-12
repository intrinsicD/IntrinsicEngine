# Repository Context: IntrinsicEngine

This is a starting map for ideation, not authority. Verify every claim against
the live tree — `AGENTS.md`, `tasks/SESSION-BRIEF.md`, `methods/**/method.yaml`,
`docs/methods/*`, and `docs/architecture/*` — before relying on it.

## Mission

A modular, high-performance, scientifically rigorous **C++23** engine for
graphics, **geometry processing**, and **method-driven research integration**
(`AGENTS.md` §1). It is research-driving software: the point is to implement,
measure, and compare methods, not to ship a product.

## Substrate (what actually exists to build on)

- **Toolchain:** C++23 with C++23 modules, Clang 20+ (`clang-scan-deps`), CMake
  presets (`ci`/`dev`), vcpkg manifest dependencies. GPU path is **Vulkan-only**
  (promoted Vulkan is opt-in; otherwise a **Null device** fallback). There is no
  OpenGL/DirectX path. Default correctness gate is CPU/null.
- **Layer cake (lower never imports higher):** `core` → `geometry` / `assets` /
  `ecs` → `physics` → `graphics/{rhi,assets,vulkan,framegraph,renderer}` →
  `platform` → `runtime` → `app`; plus `methods/`, `benchmarks/`, `tests/`.
- **Geometry substrate:** halfedge mesh, point clouds, graphs; geometry-owned
  IO (OBJ/OFF/PLY/STL/PCD/XYZ/TGF); curvature, parameterization (harmonic),
  subdivision, surface reconstruction (progressive Poisson), intersection
  classification, validation, K-Means clustering.
- **Runtime substrate:** ECS scene/registry; a core task graph and frame graph;
  a render graph driven by `FrameRecipe*`; `IRuntimeModule` command→job→event→
  commit composition (e.g. `ClusteringModule`); a config-control lane
  (`RenderRecipeConfig`).
- **Backend-seam pattern (the key research affordance):** a method exposes a
  CPU **reference** backend as canonical truth and optional optimized/GPU
  backends that report **backend identity + parity delta** against the reference
  (`Geometry.KMeans` with `{CPU, GPU}` tokens and requested-vs-actual fallback
  telemetry is the exemplar). The `RHI::IDevice` + Vulkan compute path lets an
  idea be prototyped on CPU and then GPU-accelerated under a proven parity gate.
- **Measurement substrate:** benchmark manifests with stable `benchmark_id`,
  machine-readable metrics + diagnostics, baseline comparison, and a
  smoke-vs-nightly split (validated by `tools/benchmark/*`). This is the ground
  truth that converts a *candidate*-novel idea into a tested result.
- **Discovery substrate:** a `knowledge-graph` MCP aid over the module DAG and
  the paper→method→code chain.

## High-value research surface (grounded in the open backlog)

**Theme I — Research method implementation** currently holds the open methods:
Closest Point Method and Walk-on-Spheres/Stars PDE solvers, robust mesh boolean,
cross-field/frame-field design, constrained Delaunay tetrahedralization,
progressive-Poisson GPU parity, Coherent Point Drift registration, and
Locally-Optimal-Projection (LOP/WLOP) consolidation; plus the method-readiness
seams: feature-preserving dual contouring, quadric-error simplification,
harmonic/Tutte parameterization, a sparse symmetric generalized eigensolver,
Gaussian-mixture EM, kernel/Nyström/Gaussian-process interpolation, permutohedral
lattice high-dimensional filtering, and grid-downsampling. Treat these as the
*current* frontier, not a ceiling — the ideation job is to find what is missing,
mis-primitived, or transferable, and to propose new candidates and evidence
programs around and beyond them.

Especially fertile given this substrate:
- representations that unify geometry processing with GPU-friendly sparse
  operators and the render/task-graph scheduler;
- methods whose CPU reference is cheap but whose GPU parity is non-obvious (the
  backend-seam is a built-in falsification harness for the speedup claim);
- convergence, robustness, and identifiability of geometry/PDE algorithms on
  degenerate inputs (the engine already values fail-closed diagnostics);
- new diagnostics/measurement protocols, not just algorithms — the benchmark
  machinery rewards a better *measurement* as a first-class contribution.

Cross-domain donor fields that map well onto this substrate: numerical
analysis / PDE theory, optimal transport, computational topology (persistent
homology), spectral graph theory, statistical mechanics, information/coding
theory, and compiler/database scheduling (for data layout and graph execution).

## Constraints (respect these or the idea will not land)

- Preserve the layer cake and deterministic, testable APIs with explicit
  ownership and failure states.
- **CPU reference backend is canonical truth**; optimized/GPU backends must
  report measurable, documented parity deltas — silent divergence is the failure
  mode the method contract exists to prevent.
- Honor the `AGENTS.md` §5 research-engine invariants: **P1** (smallest
  construct; a seam needs a present justification — no speculative abstraction
  without benchmark evidence), **P3** (config-lane controllability, not UI-only),
  **P5** (recipe-driven frames).
- Performance/quality claims require a named baseline; quality/error metrics are
  first-class, never runtime-only.
- Never fabricate prior art, citations, results, or novelty. Label ideas
  *candidate*-novel until the prior-art audit clears them.

## Acceptance workflow (where a selected idea goes)

A chosen candidate becomes a bounded `METHOD-*` / `GEOM-*` task under Theme I
with: a method contract (`method.yaml` + `docs/methods/*`), a CPU reference
backend, analytic + regression correctness tests, a benchmark manifest with a
baseline and the smallest decisive experiment, quality/performance metrics,
backend-parity telemetry, documented numerical limitations, and an explicit
abandonment criterion. Hard-to-reverse architecture decisions get an ADR under
`docs/adr/`.
