---
id: GEOM-063
theme: I
depends_on: [GEOM-018, GEOM-019]
maturity_target: CPUContracted
---
# GEOM-063 — Unified parameterization Strategy × Backend surface

## Goal
- Consolidate the existing disk-topology UV solvers (`ComputeLSCM`, `ComputeHarmonic`/Tutte) behind one geometry-owned `Geometry.Parameterization` Strategy × Backend dispatch surface — a single `ParameterizeMesh(mesh, params)` entry with a `Strategy` axis, a `Backend { CPU, GPU }` axis, honest requested-vs-actual backend telemetry, and the shared `ParameterizationDiagnostics` in every result — so every state-of-the-art variant (SCP, ARAP, SLIM, BFF) is choosable through one API and one config/UI/agent surface instead of a growing set of unrelated free functions.

## Non-goals
- No new algorithm — this is a behavior-preserving consolidation. `Tutte`, `Harmonic`, and `Lscm` dispatch to the existing `ComputeHarmonic`/`ComputeLSCM` bodies unchanged; new strategies (`Scp`, `Arap`, `Slim`, `Bff`) are added by `METHOD-021..024` and only reserve their enum slot here (annotated unimplemented, fail-closed).
- No optimization kernels — the shared local/global rotation-fit and injective line-search core is `GEOM-064`.
- No GPU execution — the `Backend::GPU` request is a seam that falls back to `Backend::CPU` with telemetry until `METHOD-026` lands; no RHI import in `src/geometry`.
- No atlas/charting change — `Geometry.UvAtlas` (`FastStaged`/`XAtlas`) is a separate per-chart consumer and keeps its own method selector; it may adopt this surface as a named future consumer but is not migrated here.
- No renderer/runtime/ECS/assets integration (that is `RUNTIME-176`); no Eigen types on the public surface.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only. Extends the existing `Geometry.Parameterization` module (`src/geometry/Geometry.HalfedgeMesh.Parameterization.cppm`, which today exports only `ComputeLSCM`).
- Today parameterization is a set of disjoint entry points with no shared control surface: `Geometry.Parameterization::ComputeLSCM` (free-boundary conformal, CG-solved) and `Geometry.Parameterization.Harmonic::ComputeHarmonic` (`HarmonicWeightType::Cotangent`/`Uniform` fixed-boundary, LDLT-solved, `GEOM-019`). A caller must know which function and which params struct to call per method; there is no single place for config/UI/agents to pick a variant or a backend.
- Dispatch template: `docs/architecture/algorithm-variant-dispatch.md` and the `Geometry.KMeans` exemplar (`Backend { CPU, GPU }`, `RequestedBackend`/`ActualBackend`/`FellBackToCPU`). This task makes `Geometry.Parameterization` the second family to adopt that idiom, exactly as `METHOD-016` established `Geometry.PointCloud.Consolidation` for the LOP family.
- Reuses `Geometry.Parameterization.Diagnostics::EvaluateParameterizationDiagnostics` (conformal/area/symmetric-Dirichlet/stretch/flipped-element/seam metrics, `GEOM-018`) as the one normalized quality record every strategy reports, so variants are comparable on one axis.
- Realizes the "family surface" step of the [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md) that Packs 3–4 (ARAP/SLIM) and the new SOTA packs (SCP/BFF) plug into.

## Control surfaces
- Config/UI/Agent: none new here — this task ships the geometry dispatch surface and its `Strategy`/`Backend` enums. The runtime config-lane, editor panel, and agent apply path that make the choice reachable are owned by `RUNTIME-176` / `UI-036`.

## Backends
- Backend axis: `Backend::CPU` (= `cpu_reference`) now; `Backend::GPU` (= `gpu_vulkan_compute`) is a fall-back-with-telemetry seam here and becomes operational in `METHOD-026`.

## Required changes
- [ ] Add a `ParameterizationStrategy` enum (`Tutte`, `Harmonic`, `Lscm`, plus reserved `Scp`, `Arap`, `Slim`, `Bff`) and a `Backend { CPU, GPU }` enum to the `Geometry.Parameterization` module surface.
- [ ] Add `ParameterizeParams` (selected strategy, `Backend Compute`, boundary policy passthrough reused from `HarmonicParams`, pinned vertices/UVs, deterministic seed/iteration budget for the iterative strategies) and `ParameterizeResult` (per-vertex `std::vector<glm::vec2> UVs`, embedded `ParameterizationDiagnostics`, `RequestedBackend`/`ActualBackend`/`FellBackToCPU`, a `ParameterizationStatus`, and iteration/convergence fields for iterative strategies).
- [ ] Add `[[nodiscard]] std::optional<ParameterizeResult> ParameterizeMesh(const HalfedgeMesh::Mesh&, const ParameterizeParams&)` that exhaustively dispatches on `Strategy`: `Tutte`/`Harmonic` → `ComputeHarmonic` (mapping the two through `HarmonicWeightType`), `Lscm` → `ComputeLSCM`, and each reserved strategy → an explicit `NotImplemented` status (no crash, no arbitrary projection) until its `METHOD-*` lands.
- [ ] Populate `ParameterizationDiagnostics` on every returned result via `EvaluateParameterizationDiagnostics`, even for the wrapped legacy strategies, so all variants report one comparable quality record.
- [ ] Set backend telemetry on every result (`ActualBackend == CPU`; `FellBackToCPU == (RequestedBackend != CPU)`), mirroring the `Geometry.KMeans` CPU entry point.
- [ ] Keep `ComputeLSCM` and `ComputeHarmonic` reachable and behavior-identical (they remain the strategy bodies); add a `ToString(ParameterizationStrategy)`/`ToString(ParameterizationStatus)`.
- [ ] No new module-library target: register any new `.cpp` implementation unit in the existing `IntrinsicGeometry` lists in `src/geometry/CMakeLists.txt` (alphabetical placement).

## Tests
- [ ] `tests/unit/geometry/Test.ParameterizationDispatch.cpp` (`unit;geometry`).
- [ ] Dispatch parity: `ParameterizeMesh` with `Tutte`/`Harmonic`/`Lscm` returns UVs bitwise-identical to the corresponding direct `ComputeHarmonic`/`ComputeLSCM` call on square-fan and circle-boundary disk fixtures.
- [ ] Diagnostics present: every successful result carries a populated `ParameterizationDiagnostics` with `Status == Success` on a valid disk fixture.
- [ ] Backend telemetry: a `Backend::GPU` request returns `ActualBackend == CPU`, `FellBackToCPU == true`; a `Backend::CPU` request reports no fallback.
- [ ] Reserved strategies fail closed: `Scp`/`Arap`/`Slim`/`Bff` return `ParameterizationStatus::NotImplemented` with no UVs and no NaN/Inf until their method task lands.
- [ ] Determinism: identical `(mesh, params)` produce bitwise-identical UVs across two runs.

## Docs
- [ ] Document the unified surface, the `Strategy`/`Backend` axes, and the "reserved strategy fails closed" rule in the `Geometry.Parameterization` interface docs and `docs/architecture/geometry.md`.
- [ ] Add a "Parameterization family surface" note to `docs/architecture/parameterization-mapping-roadmap.md` naming `ParameterizeMesh` as the dispatch entry the SOTA packs extend.
- [ ] Regenerate `docs/api/generated/module_inventory.md`.

## Acceptance criteria
- [ ] `Tutte`/`Harmonic`/`Lscm` are choosable on one `ParameterizationStrategy` axis with identical output to the legacy entry points.
- [ ] Every result carries normalized `GEOM-018` diagnostics and honest backend telemetry.
- [ ] Reserved SOTA strategies fail closed (no crash, no arbitrary UVs).
- [ ] Public surface exposes only `std`/`glm`/geometry-owned records (no Eigen, no RHI); layering holds (`geometry -> core`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization|Harmonic|Lscm' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No new algorithm or numeric behavior change to the wrapped strategies.
- No RHI/Eigen types on the public geometry surface; no runtime/graphics/assets imports.
- No arbitrary projection fallback for unsupported topology or unimplemented strategies — report structured status.
- No migration of `Geometry.UvAtlas` onto this surface in this task (named future consumer only).

## Maturity
- Target: `CPUContracted` — the CPU dispatch surface is verified by the default gate. `Operational` engine wiring is owned by `RUNTIME-176`; the `Backend::GPU` path becomes operational in `METHOD-026`.
