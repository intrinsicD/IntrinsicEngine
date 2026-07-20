---
id: RUNTIME-175
theme: I
depends_on: [METHOD-016, METHOD-017, METHOD-018, CORE-009, RUNTIME-181]
maturity_target: Operational
---
# RUNTIME-175 — Point-cloud consolidation runtime facade and config lane

## Goal
- Wire the CPU-reference LOP-family consolidation surface into the engine
  end-to-end: a runtime command facade that consolidates a selected point-cloud
  entity and writes the result back through `GeometrySources`, plus a
  serializable config section that makes the strategy and its parameters
  choosable from a config file, an agent/CLI, and the UI as co-equal surfaces.

## Non-goals
- No algorithm code — this task consumes the reference strategies from
  `METHOD-016..018`; it does not implement variants.
- No ImGui panel here — the editor window is `UI-035`; this task ships the
  runtime facade, config section, and result records the panel calls.
- No backend selector, RHI adapter, fallback placeholder, or GPU queue.
  `METHOD-019` may extend the delivered CPU path only for optimized strategies
  that pass its adoption gates; `METHOD-020` owns the later GPU adapter and
  scheduling path.

## Context
- Owner/layer: `src/runtime` owns composition/wiring and the typed section
  codec; `src/app/Sandbox` owns registration and presentation.
  `runtime -> all lower layers`; `app -> runtime` only.
- Template to mirror: `RUNTIME-134` (progressive-Poisson interactive playground) — the first method wired through the full P3 config lane. CORE-009 migrates that value/config-control path to a registered app section; this task must consume the resulting generic section lane rather than reintroducing an engine-owned Sandbox aggregate.
- Config lane (`AGENTS.md` §5 P3): reference model `Extrinsic.Graphics.RenderRecipeConfig` (schema-id + version + diagnostics, side-effect-free preview→validate→apply). CORE-009 owns generic named app-section records/registration in `Core.Config.EngineLoad`; the runtime facade remains the app-composed `Extrinsic.Runtime.EngineConfigControl` service resolved through `Engine::Services()`, whose `RuntimeConfigControlSource { Editor, AgentCli, Programmatic }` tag is the co-equal-surfaces mechanism.
- Editor facade pattern: `ApplySandboxEditor<X>Command` in `Runtime.SandboxEditorFacades.cpp` / `Runtime.SandboxMethodFacade.cpp`, reading geometry via `GS::BuildConstView(raw, entity)` (require `Domain::PointCloud`), writing back via `PopulateFromCloud` + `Dirty::MarkVertexPositionsDirty`, wrapped in `EditorCommandHistory` for undo — mirror `ApplySandboxEditorPointCloudOutlierRemovalCommand`.
- Execution is a direct call to the geometry-owned CPU reference. A backend
  axis is not reserved while there is only one implementation; METHOD-019/020
  must extend this surface only after a concrete backend meets its own gate.
- `ARCH-006` retired the point-cloud presentation into `src/app/Sandbox`; land the runtime seams so the app-owned panel (`UI-035`) consumes them without owning engine state.

## Control surfaces
- Config: registered app section `sandbox.point_cloud_consolidation` (strategy,
  `h`, `mu`, iterations, CLOP component count, EAR edge sensitivity, seed),
  including EAR's authored-or-estimated normal-source policy, round-tripped
  through the generic engine-config section lane and applied as a hot subset.
- UI: Sandbox editor consolidation panel (owned by `UI-035`) drives the same validated apply path.
- Agent/CLI: `config/engine.json` + the `--engine-config <path>` boot flag, and programmatic `EngineConfigControl::LoadAndApplyEngineConfigHotSubsetFile(...)` / `ApplyEngineConfigHotSubset(...)` on the resolved service, tagged `AgentCli`/`Programmatic`.

## Backends
- `cpu_reference` only. The result records the actual implementation identity
  for diagnostics but exposes no requested-backend field or selector.

## Right-sizing
- Add one config value record and one existing-facade command path. Do not add
  a service, backend interface, registry, RHI dependency, or queue for a
  single synchronous CPU implementation.
- The reintroduction trigger is concrete evidence: a strategy passes
  METHOD-019's optimized-CPU gates or METHOD-020's Vulkan parity gate.

## Required changes
- [ ] Add an app-owned `PointCloudConsolidationConfig` value struct and register
      its schema/default/validator through the generic CORE-009 section lane:
      plain `enum`/`std::uint32_t`/`double`/`bool` fields for strategy and the
      shared/per-strategy parameters, including the EAR normal-source policy;
      deterministic defaults. Do not add the type or its fields to
      `Core.Config.Engine`.
- [ ] Add the runtime-owned section codec/validator and app-owned registration;
      let generic `Core.Config.EngineLoad` round-trip the opaque payload and
      generic `EngineConfigControl` report/dispatch the changed stable name.
- [ ] Add `ApplySandboxEditorPointCloudConsolidationCommand` (+ a config-routed
      `...ConfigCommand`) in the existing editor facade: read the selected
      point cloud, call the requested CPU-reference strategy, write positions
      back via `GeometrySources`/`PopulateFromCloud` +
      `MarkVertexPositionsDirty`, and record an undoable
      `EditorCommandHistory` entry.
- [ ] Export a pointer-free
      `SandboxEditorPointCloudConsolidationResult` record with stable
      `cpu_reference` implementation identity, the chosen strategy token, and
      convergence diagnostics (iterations, converged flag, moved distance).
- [ ] Register the runtime seams in the sandbox default policies / module registration so they are reachable from `Engine::Run()`.

## Tests
- [ ] `tests/contract/runtime/Test.PointCloudConsolidationConfig.cpp`
      (`contract;runtime`): the runtime-owned section codec round-trips through
      the generic engine-config lane (parse → serialize → parse is stable) and
      rejects out-of-range values with explicit diagnostics.
- [ ] `tests/contract/runtime/Test.PointCloudConsolidationFacade.cpp`
      (`contract;runtime`, Null device):
      `ApplySandboxEditorPointCloudConsolidationCommand` on a selected point
      cloud runs the selected CPU-reference strategy, updates positions, marks
      vertices dirty, and is undoable.
- [ ] Apply-source parity: applying the same document via `Editor`, `AgentCli`, and `Programmatic` sources produces identical results (co-equal surfaces).
- [ ] Determinism: identical config + input produce identical consolidated output.

## Docs
- [ ] Update `docs/architecture/engine-config.md` and `docs/architecture/runtime-config-control.md` with the new section and hot-apply entry.
- [ ] Add a short runtime-integration note to the three method-package READMEs (`locally_optimal_projection`, `continuous_lop`, `edge_aware_resampling`) pointing at the runtime facade and config path.
- [ ] Refresh `docs/api/generated/module_inventory.md` for the new runtime/app
      module surfaces.

## Acceptance criteria
- [ ] A selected point cloud is consolidated and visibly updated through the
      runtime facade with the CPU reference under the Null/default runtime.
- [ ] The strategy and parameters are choosable from a config file, an
      agent/programmatic apply, and (via `UI-035`) the UI, all through one
      validated apply path with source tagging.
- [ ] No requested-backend token or unavailable implementation is exposed.
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
- No placeholder backend selector, RHI adapter, GPU queue, or fallback
  telemetry before METHOD-019/020 supplies a passing concrete implementation.

## Maturity
- Target: `Operational` — the CPU-reference facade + config lane run in
  `Engine::Run()` under the Null/default runtime with CPU-gate contract tests.
  Optimized/GPU parity and their later control-surface extensions remain
  METHOD-019/020.
