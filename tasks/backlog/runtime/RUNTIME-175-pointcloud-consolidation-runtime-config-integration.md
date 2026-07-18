---
id: RUNTIME-175
theme: I
depends_on: [METHOD-016, CORE-009]
maturity_target: Operational
---
# RUNTIME-175 — Point-cloud consolidation runtime facade, config lane, and backend adapter

## Goal
- Wire the LOP-family consolidation surface into the engine end-to-end: a runtime command facade that consolidates a selected point-cloud entity and writes the result back through `GeometrySources`, a serializable config section that makes the strategy and backend choosable from a config file, an agent/CLI, and the UI as co-equal surfaces, and an RHI-visible backend adapter that falls back honestly when the GPU is unavailable.

## Non-goals
- No algorithm code — this task consumes `Geometry.PointCloud.Consolidation` (`METHOD-016+`); it does not implement variants.
- No ImGui panel here — the editor window is `UI-035`; this task ships the runtime facade, config section, result records, and backend adapter the panel calls.
- No GPU compute kernels — those are `METHOD-020`; the GPU job-queue leg here only schedules that path once it exists.

## Context
- Owner/layer: `src/runtime` owns composition/wiring and the typed section
  codec; `src/app/Sandbox` owns registration and presentation.
  `runtime -> all lower layers`; `app -> runtime` only.
- Template to mirror: `RUNTIME-134` (progressive-Poisson interactive playground) — the first method wired through the full P3 config lane. CORE-009 migrates that value/config-control path to a registered app section; this task must consume the resulting generic section lane rather than reintroducing an engine-owned Sandbox aggregate.
- Config lane (`AGENTS.md` §5 P3): reference model `Extrinsic.Graphics.RenderRecipeConfig` (schema-id + version + diagnostics, side-effect-free preview→validate→apply). CORE-009 owns generic named app-section records/registration in `Core.Config.EngineLoad`; the runtime facade remains `Extrinsic.Runtime.EngineConfigControl` via `Engine::GetConfigControl()`, whose `RuntimeConfigControlSource { Editor, AgentCli, Programmatic }` tag is the co-equal-surfaces mechanism.
- Editor facade pattern: `ApplySandboxEditor<X>Command` in `Runtime.SandboxEditorFacades.cpp` / `Runtime.SandboxMethodFacade.cpp`, reading geometry via `GS::BuildConstView(raw, entity)` (require `Domain::PointCloud`), writing back via `PopulateFromCloud` + `Dirty::MarkVertexPositionsDirty`, wrapped in `EditorCommandHistory` for undo — mirror `ApplySandboxEditorPointCloudOutlierRemovalCommand`.
- Backend adapter pattern: `Runtime.KMeansBackend` (RHI convenience overload,
  `IsOperational()` gate, honest fallback) plus the Sandbox editor's private
  K-Means queue implementation (`JobService` `GpuQueue` registration), attached
  to `Extrinsic.Runtime.SandboxEditorFacades` while its request, submission,
  result, and status DTOs remain on that public facade. The consolidation
  adapter mirrors this ownership shape.
- `ARCH-006` retired the point-cloud presentation into `src/app/Sandbox`; land the runtime seams so the app-owned panel (`UI-035`) consumes them without owning engine state.

## Control surfaces
- Config: registered app section `sandbox.point_cloud_consolidation` (strategy,
  backend, `h`, `mu`, iterations, CLOP component count, EAR edge sensitivity,
  seed), round-tripped through the generic engine-config section lane and
  applied as a hot subset.
- UI: Sandbox editor consolidation panel (owned by `UI-035`) drives the same validated apply path.
- Agent/CLI: `config/engine.json` + the `--engine-config <path>` boot flag, and programmatic `Engine::GetConfigControl().LoadAndApplyEngineConfigHotSubsetFile(...)` / `ApplyEngineConfigHotSubset(...)` tagged `AgentCli`/`Programmatic`.

## Backends
- Backend axis: CPU adapter + honest fallback lands now on `METHOD-016`; the explicit GPU job-queue leg is gated on `METHOD-020` (`gpu_vulkan_compute`).

## Required changes
- [ ] Add an app-owned `PointCloudConsolidationConfig` value struct and register
      its schema/default/validator through the generic CORE-009 section lane:
      plain `enum`/`std::uint32_t`/`double`/`bool` fields for strategy, backend,
      and the shared/per-strategy parameters; deterministic defaults. Do not
      add the type or its fields to `Core.Config.Engine`.
- [ ] Add the runtime-owned section codec/validator and app-owned registration;
      let generic `Core.Config.EngineLoad` round-trip the opaque payload and
      generic `EngineConfigControl` report/dispatch the changed stable name.
- [ ] Add `Runtime.ConsolidationBackend` (module + impl): a `Consolidate(view, params, RHI::IDevice&)` convenience overload gating GPU on `IsOperational()` and falling back to the `Geometry.PointCloud.Consolidation` CPU reference with `RequestedBackend`/`ActualBackend`/`FellBackToCPU` telemetry.
- [ ] Add `ApplySandboxEditorPointCloudConsolidationCommand` (+ a config-routed `...ConfigCommand`) in the editor facade: read the selected point cloud, run the requested strategy/backend, write the consolidated positions back via `GeometrySources`/`PopulateFromCloud` + `MarkVertexPositionsDirty`, and record an undoable `EditorCommandHistory` entry.
- [ ] Export a `SandboxEditorPointCloudConsolidationResult` record with stable backend ids (`cpu_reference`/`gpu_vulkan_compute`), display names, CPU-fallback reason, the chosen strategy token, and convergence diagnostics (iterations, converged flag, moved distance) — no raw geometry pointers.
- [ ] Add `Runtime.ConsolidationGpuJobQueue` (JobService `GpuQueue` participant) that schedules the `METHOD-020` GPU path inside the frame context and publishes results through the same ECS path; this leg is gated on `METHOD-020` and may land as a second slice.
- [ ] Register the runtime seams in the sandbox default policies / module registration so they are reachable from `Engine::Run()`.

## Tests
- [ ] `tests/contract/runtime/Test.PointCloudConsolidationConfig.cpp`
      (`contract;runtime`): the runtime-owned section codec round-trips through
      the generic engine-config lane (parse → serialize → parse is stable) and
      rejects out-of-range values with explicit diagnostics.
- [ ] `tests/contract/runtime/Test.PointCloudConsolidationFacade.cpp` (`contract;runtime`, Null device): `ApplySandboxEditorPointCloudConsolidationCommand` on a selected point cloud updates positions, marks vertices dirty, and is undoable; a `Backend::GPU` request on the Null device reports `FellBackToCPU` with `ActualBackend == cpu_reference`.
- [ ] Apply-source parity: applying the same document via `Editor`, `AgentCli`, and `Programmatic` sources produces identical results (co-equal surfaces).
- [ ] Determinism: identical config + input produce identical consolidated output.

## Docs
- [ ] Update `docs/architecture/engine-config.md` and `docs/architecture/runtime-config-control.md` with the new section and hot-apply entry.
- [ ] Add a short runtime-integration note to the three method-package READMEs (`locally_optimal_projection`, `continuous_lop`, `edge_aware_resampling`) pointing at the runtime facade and config path.
- [ ] Refresh `docs/api/generated/module_inventory.md` for the new runtime/app
      module surfaces.

## Acceptance criteria
- [ ] A selected point cloud is consolidated and visibly updated through the runtime facade under the Null and default backends.
- [ ] The strategy and backend are choosable from a config file, an agent/programmatic apply, and (via `UI-035`) the UI, all through one validated apply path with source tagging.
- [ ] GPU requests fall back honestly with asserted telemetry; the GPU job-queue leg is present or explicitly gated on `METHOD-020`.
- [ ] Contract tests pass in the default CPU gate; layering holds (`runtime -> lower`, `core -> nothing`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Consolidation|EngineConfig' -LE 'gpu|vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No algorithm/variant code in runtime (consume `Geometry.PointCloud.Consolidation`).
- No UI-only control path — every knob must round-trip through the config lane.
- No live GPU work on the poll thread; readback drains through `AsyncBufferReadback`, not `IDevice::ReadBuffer`.

## Maturity
- Target: `Operational` — the facade + config lane run in `Engine::Run()` on the Null/default backend with CPU-gate contract tests. This slice closes `CPUContracted → Operational` for the CPU/config path; the `gpu_vulkan_compute` job-queue leg is `Operational` owned by `METHOD-020`.
