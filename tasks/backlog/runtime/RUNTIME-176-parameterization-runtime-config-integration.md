---
id: RUNTIME-176
theme: I
depends_on: [GEOM-063]
maturity_target: Operational
---
# RUNTIME-176 — Parameterization runtime facade, config lane, backend adapter, and UV view model

## Goal
- Wire the parameterization surface into the engine end-to-end: a runtime command facade that parameterizes a selected mesh entity and writes the UVs back as `v:texcoord` through `GeometrySources`, a serializable config section that makes the strategy and backend choosable from a config file, an agent/CLI, and the UI as co-equal surfaces, an RHI-visible backend adapter that falls back honestly, and a runtime-built **UV view model** (per-vertex UVs + face connectivity + chart/seam + per-face distortion) that `UI-036` draws in the resizable UV split view without owning geometry.

## Non-goals
- No algorithm code — this task consumes `Geometry.Parameterization` (`GEOM-063+`); it does not implement strategies.
- No ImGui panel or 2D drawing here — the editor window and the UV-layout rendering are `UI-036`; this task ships the facade, config section, result record, backend adapter, and the UI-consumable UV view model the panel reads.
- No GPU compute kernels — those are `METHOD-026`; the GPU job-queue leg here only schedules that path once it exists.
- No new second viewport/camera or renderer change — the UV view is delivered by `UI-036` from this task's view model (see `ADR-0025`); a GPU-rendered UV target is the optional `GRAPHICS-122` upgrade.

## Context
- Owner/layer: `src/runtime` (composition/wiring) plus a config value struct in `src/core` (`Core.Config.Engine`). `runtime -> all lower layers`; `core -> nothing`.
- Template to mirror: `RUNTIME-134` (progressive-Poisson interactive playground) and `RUNTIME-175` (point-cloud consolidation) — the config-lane exemplars. Both added a `SandboxConfig` value struct, an `EngineConfigControl` hot-subset apply, a runtime command DTO, ECS-property writeback, and a `CpuReference`/`VulkanCompute` backend toggle.
- Config lane (`AGENTS.md` §5 P3): the engine tree is `EngineConfig { …, SandboxConfig }` in `Core.Config.Engine.cppm`; the file lane is `Core.Config.EngineLoad` (schema `intrinsic.core.engine-config`); the runtime facade is `Extrinsic.Runtime.EngineConfigControl` via `Engine::GetConfigControl()`, whose `RuntimeConfigControlSource { Editor, AgentCli, Programmatic }` tag is the co-equal-surfaces mechanism.
- Editor facade pattern: `ApplySandboxEditor<X>Command` in `Runtime.SandboxEditorUi.cpp` / `Runtime.SandboxMethodFacade.cpp`, reading geometry via `GS::BuildConstView(raw, entity)` (require `Domain::Mesh`) and writing UVs back through the promoted `v:texcoord` path (`Runtime.MeshGeometryPacker` packs `{position.xyz, U, V}`; RUNTIME-108/GRAPHICS-088 already upload resolved UVs) plus the texcoord dirty tag, wrapped in `EditorCommandHistory` for undo — mirror the UV-atlas regeneration command from retired `UI-014`.
- Backend adapter pattern: `Runtime.KMeansBackend` (RHI convenience overload, `IsOperational()` gate, honest fallback) + `Runtime.KMeansGpuJobQueue` (JobService `GpuQueue` registration). The parameterization adapter mirrors these.
- UV view model precedent: `SandboxEditorUvDiagnosticsModel` (`Runtime.SandboxEditorUi.cppm`) is already a runtime-built, pointer-free UI model for UV state (atlas dims, chart count, provenance). This task adds a richer `SandboxEditorParameterizationViewModel` that additionally carries the per-vertex UVs, face connectivity, chart/seam records, and per-face distortion from `ParameterizationDiagnostics` so the panel can draw the 2D layout.
- Coordinate with `ARCH-006` Slice 4 (moving mesh processing surfaces out of runtime into `src/app`): land the runtime seams so the app panel (`UI-036`) consumes them without owning engine state.

## Control surfaces
- Config: `EngineConfig.sandbox.parameterization` (strategy, backend, boundary policy, per-strategy params — ARAP/SLIM iterations+tolerance, BFF `BoundaryTarget` mode + targets, SCP options, pinned vertices, seed), round-tripped by `Core.Config.EngineLoad` and applied as a hot subset.
- UI: Sandbox parameterization editor panel + UV split view (owned by `UI-036`) drives the same validated apply path.
- Agent/CLI: `config/engine.json` + the `--engine-config <path>` boot flag, and programmatic `Engine::GetConfigControl().LoadAndApplyEngineConfigHotSubsetFile(...)` / `ApplyEngineConfigHotSubset(...)` tagged `AgentCli`/`Programmatic`.

## Backends
- Backend axis: CPU adapter + honest fallback lands now on `GEOM-063`; the explicit GPU job-queue leg is gated on `METHOD-026` (`gpu_vulkan_compute`) and may land as a second slice.

## Required changes
- [ ] Add a `ParameterizationConfig` value struct nested in `SandboxConfig` (`Core.Config.Engine.cppm`): plain `enum`/`std::uint32_t`/`double`/`bool` fields for strategy, backend, boundary policy, and per-strategy params; deterministic defaults.
- [ ] Extend `Core.Config.EngineLoad.cpp` parser/serializer and the schema table (and `docs/architecture/engine-config.md`) to round-trip the section with diagnostics; add it to the `EngineConfigControl` hot-apply subset.
- [ ] Add `Runtime.ParameterizationBackend` (module + impl): a `Parameterize(view, params, RHI::IDevice&)` convenience overload gating GPU on `IsOperational()` and falling back to the `Geometry.Parameterization` CPU path with `RequestedBackend`/`ActualBackend`/`FellBackToCPU` telemetry.
- [ ] Add `ApplySandboxEditorParameterizationCommand` (+ a config-routed `...ConfigCommand`) in the editor facade: read the selected mesh, run the requested strategy/backend, write UVs back as `v:texcoord` via `GeometrySources` + the texcoord dirty tag, and record an undoable `EditorCommandHistory` entry.
- [ ] Export a `SandboxEditorParameterizationResult` record with stable backend ids (`cpu_reference`/`cpu_optimized`/`gpu_vulkan_compute`), display names, CPU-fallback reason, chosen strategy token, and the `ParameterizationDiagnostics` summary — no raw geometry pointers.
- [ ] Export a `SandboxEditorParameterizationViewModel` (per-vertex UVs, face index triples, chart/seam records, per-face distortion scalar, UV bounds) built from the selected mesh + last result, for `UI-036` to draw the 2D layout; pointer-free and copyable.
- [ ] Add `Runtime.ParameterizationGpuJobQueue` (JobService `GpuQueue` participant) that schedules the `METHOD-026` GPU path inside the frame context and publishes UVs through the same `v:texcoord` path; gated on `METHOD-026`, may land as a second slice.
- [ ] Register the runtime seams in the sandbox default policies / module registration so they are reachable from `Engine::Run()`.

## Tests
- [ ] `tests/contract/runtime/Test.ParameterizationConfig.cpp` (`contract;runtime`): the config section round-trips through `Core.Config.EngineLoad` (parse → serialize → parse is stable) and rejects out-of-range values with explicit diagnostics.
- [ ] `tests/contract/runtime/Test.ParameterizationFacade.cpp` (`contract;runtime`, Null device): `ApplySandboxEditorParameterizationCommand` on a selected disk mesh writes finite `v:texcoord`, marks texcoords dirty, and is undoable; a `Backend::GPU` request on the Null device reports `FellBackToCPU` with `ActualBackend == cpu_reference`.
- [ ] View-model build: given a parameterized mesh, `SandboxEditorParameterizationViewModel` has UV count == vertex count, face triples matching the mesh, finite UV bounds, and per-face distortion populated — with no raw pointers.
- [ ] Apply-source parity: applying the same document via `Editor`, `AgentCli`, and `Programmatic` sources produces identical UVs (co-equal surfaces).
- [ ] Determinism: identical config + input produce identical UVs.

## Docs
- [ ] Update `docs/architecture/engine-config.md` and `docs/architecture/runtime-config-control.md` with the new section and hot-apply entry.
- [ ] Add a short runtime-integration note to the parameterization method-package READMEs pointing at the runtime facade and config path.
- [ ] Refresh `docs/api/generated/module_inventory.md` for the new runtime/core modules.

## Acceptance criteria
- [ ] A selected mesh is parameterized and its UVs are visibly updated (checker/texcoord) through the runtime facade under the Null and default backends.
- [ ] The strategy and backend are choosable from a config file, an agent/programmatic apply, and (via `UI-036`) the UI, all through one validated apply path with source tagging.
- [ ] The UV view model is produced for `UI-036` and is pointer-free; GPU requests fall back honestly with asserted telemetry; the GPU job-queue leg is present or explicitly gated on `METHOD-026`.
- [ ] Contract tests pass in the default CPU gate; layering holds (`runtime -> lower`, `core -> nothing`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization|EngineConfig' -LE 'gpu|vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No algorithm/strategy code in runtime (consume `Geometry.Parameterization`).
- No UI-only control path — every knob must round-trip through the config lane.
- No live GPU work on the poll thread; readback drains through `AsyncBufferReadback`, not `IDevice::ReadBuffer`.
- No second camera/viewport or renderer contract change in this task (see `ADR-0025`; the optional GPU-rendered UV target is `GRAPHICS-122`).

## Maturity
- Target: `Operational` — the facade + config lane run in `Engine::Run()` on the Null/default backend with CPU-gate contract tests. This slice closes `CPUContracted → Operational` for the CPU/config path; the `gpu_vulkan_compute` job-queue leg is `Operational` owned by `METHOD-026`.
