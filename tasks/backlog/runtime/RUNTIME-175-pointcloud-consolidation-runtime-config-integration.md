---
id: RUNTIME-175
theme: I
depends_on:
  - METHOD-016
  - METHOD-017
  - METHOD-018
  - CORE-009
  - RUNTIME-181
  - RUNTIME-194
  - RUNTIME-201
  - RUNTIME-202
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner:
branch:
worktree:
claimed_at:
maturity_target: Operational
---
# RUNTIME-175 — Point-cloud consolidation runtime operation and config lane

## Goal

- Integrate the CPU-reference LOP-family strategies through one runtime-owned
  consolidation config contract and one explicit typed, asynchronous,
  undoable operation; let the Sandbox app register defaults while UI-035 later
  owns presentation.

## Non-goals

- No algorithm implementation or variant selection logic beyond consuming
  METHOD-016..018.
- No ImGui panel or UI acceptance proof; UI-035 depends on this task and owns
  that downstream presentation/control-surface slice.
- No geometry mutation as a side effect of config preview or apply. Running
  consolidation is a separate explicit operation.
- No backend selector, interface, RHI adapter, fallback placeholder, feature
  service, or private work queue for the single `cpu_reference` implementation.

## Context

- Owner/layer: `runtime` owns the config value, schema identity/version,
  codec/validator, active-config lookup/update helpers, typed operation, and
  result diagnostics. `src/app/Sandbox` owns only registration/default
  aggregation and later presentation. This matches the delivered
  `Runtime.ParameterizationConfig` + `Sandbox.ConfigSections` pattern after
  RUNTIME-202.
- Generic `Core.Config.EngineLoad` stores named opaque section records;
  `EngineConfigControl` owns side-effect-free preview/validation and
  synchronous commit. Neither core nor app owns consolidation semantics.
- Config commit changes only the active validated parameter record. An
  explicit `PointCloudConsolidationRequest` captures a point-cloud snapshot on
  the main thread, runs through the canonical JobService, revalidates current
  identity/generation, and applies through the RUNTIME-201 mutation/history
  transaction.
- METHOD-016..018 collectively supply the promised LOP/WLOP, CLOP, and EAR
  reference family. Keeping all three as dependencies avoids exposing config
  enum values before their implementations exist.
- METHOD-019/020 may extend the same operation only after their evidence gates;
  no requested-backend field is reserved here.

## Control surfaces

- Config: runtime-owned section `sandbox.point_cloud_consolidation`, including
  strategy and shared/per-strategy parameters, round-tripped through the
  generic engine-config section lane.
- Agent/CLI: boot-file and `EngineConfigControl` hot-subset preview/apply use
  the same runtime validator and source tagging. Applying config does not run
  the operation.
- UI: UI-035 reads/edits the same runtime-owned config and submits the same
  explicit operation after this task retires; it owns visible controls and UI
  parity proof.

## Backends

- `cpu_reference` only. Results report the actual implementation identity but
  requests expose no backend selector.

## Right-sizing

- Add one plain config record plus codec/free functions and extend the existing
  geometry-processing command surface with one request/result operation.
- Reuse `JobService`, geometry-source writeback, and the common mutation
  transaction. Do not add a service, backend seam, registry, executor, or
  Sandbox compatibility facade.

## Required changes

- [ ] Add runtime-owned `PointCloudConsolidationConfig` and stable section
      name/schema/version constants with deterministic defaults for strategy,
      `h`, `mu`, iterations, CLOP component count, EAR edge sensitivity,
      normal-source policy, and seed where the method contracts require them.
- [ ] Add runtime-owned serialize/validate/get/set helpers and a registration
      factory over the generic CORE-009 section record. Keep parsing and
      validation side-effect-free and return canonical payload plus explicit
      diagnostics.
- [ ] Register the runtime factory/default through app-owned
      `Sandbox.ConfigSections`; do not duplicate the config type or validator
      in app.
- [ ] Add `PointCloudConsolidationRequest` / result/status records and one
      typed geometry-processing operation. Capture immutable input on the main
      thread, run the selected reference strategy through JobService, and
      publish pending/ready/failure/stale state without blocking editor code.
- [ ] Revalidate selected entity, world/document, geometry source, and relevant
      generations before apply; write current positions through the canonical
      geometry-source mutation path and one undoable RUNTIME-201 transaction.
- [ ] Keep config commit and operation submission separate: preview/apply must
      never mutate geometry, and running the operation must be an explicit
      request using a validated config snapshot.
- [ ] Export pointer-free result diagnostics containing actual
      `cpu_reference` identity, strategy, iteration/convergence state, moved
      distance, and actionable failure/stale reasons supplied by the methods.

## Tests

- [ ] Runtime config contracts prove canonical parse/serialize/parse
      round-trip, deterministic defaults, schema/version rejection, parameter
      bounds, and identical Editor/AgentCli/Programmatic config commits.
- [ ] A config preview and commit prove zero geometry, history, selection, or
      job side effects.
- [ ] Null/headless operation contracts run every delivered reference
      strategy, update the selected point cloud, mark the correct dirty
      domains, publish diagnostics, and create exactly one undoable history
      entry.
- [ ] Stale world/entity/source/config-generation completions fail closed with
      no geometry/history mutation; repeated identical input is deterministic.
- [ ] App composition tests prove Sandbox registers the runtime-owned section
      factory without defining a second config vocabulary.
- [ ] Default CPU-supported correctness gate passes.

## Docs

- [ ] Update engine-config/runtime-control docs with the runtime-owned section,
      non-destructive config apply, and explicit operation boundary.
- [ ] Update runtime/app READMEs with semantic ownership and app registration.
- [ ] Add integration notes to the METHOD-016..018 package READMEs after the
      runtime path exists; point UI presentation to UI-035.
- [ ] Regenerate the module inventory for any new public module surface.

## Acceptance criteria

- [ ] Under the Null/default runtime, an explicit typed request executes each
      delivered CPU-reference strategy and applies an undoable selected-cloud
      update through current-generation checks.
- [ ] Config files and AgentCli/Programmatic hot apply round-trip and commit the
      same runtime-owned validated config without executing consolidation.
- [ ] App owns registration/default aggregation only; runtime owns config and
      operation semantics; core remains generic.
- [ ] No UI implementation, requested-backend token, unavailable variant,
      Sandbox facade, or duplicate work lifecycle lands.
- [ ] UI-035 remains the explicit follow-up for visible UI parity and does not
      block this runtime/config task through a circular acceptance criterion.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloudConsolidation|EngineConfig|GeometryProcessingOperations' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Algorithm or variant code in runtime.
- App-owned consolidation config types, codecs, validation, operation
  semantics, or live engine state.
- Geometry mutation during config preview/apply.
- UI-only tuning, placeholder backend selection, a new method service, or a
  feature-specific executor alongside JobService.
- `ApplySandboxEditor*` compatibility commands or a replacement facade.

## Maturity

- Target: `Operational` for the explicit CPU-reference operation and config
  lane under the Null/default runtime. UI-035 owns the later visible UI
  control-surface proof; optimized/GPU parity remains METHOD-019/020.
