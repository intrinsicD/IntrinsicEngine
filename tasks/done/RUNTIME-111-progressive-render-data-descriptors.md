---
id: RUNTIME-111
theme: F
depends_on: [RUNTIME-110]
maturity_target: CPUContracted
---
# RUNTIME-111 — Progressive render-data descriptor contracts

## Completion
- Completed: 2026-06-16. Commit/PR: this retirement commit.
- Maturity: `CPUContracted`.
- Fix summary: added the `Extrinsic.Runtime.ProgressiveRenderData` descriptor
  model for mesh, graph, and point-cloud presentation bindings, generated
  output policy, property-source compatibility diagnostics, and scene
  serialization without raw property pointers, worker state, or GPU handles.
- Evidence: focused runtime contract coverage exercises cross-domain
  descriptors, property picker compatibility/disabled reasons,
  render-lane/component separation, and scene serialization round trips.
- Follow-up boundary: runtime consumption, async work, UI, and backend
  operational proof are owned by `RUNTIME-112` through `RUNTIME-114`,
  `UI-015`, and `GRAPHICS-090`.

## Goal
- Add data-only descriptor contracts for progressive render-data bindings,
  readiness, generated-output policy, and serialization across mesh, graph, and
  point-cloud entities.

## Non-goals
- No async worker execution.
- No Vulkan/RHI resources or graphics-owned handles in ECS/runtime descriptors.
- No UI panels.
- No importer behavior changes.
- No mesh-only shortcut that leaves graph or point-cloud domains unrepresented.

## Context
- Owning subsystem/layer: `runtime` defines persisted binding/state contracts
  over ECS data and `GeometrySources`; lower layers stay reusable and graphics
  consumes only snapshots.
- ADR-0021 accepts shared descriptors plus domain-specific adapters as the
  progressive render-data boundary.
- `RUNTIME-110` retired as the `Scaffolded` planning contract; this task owns
  the first `CPUContracted` gate.
- Existing render-lane components remain intent toggles. This task adds the
  separate data model that maps a lane to a presentation/material key and maps
  slots to uniform defaults, authored textures, generated textures, or property
  bindings.

## Required changes
- [x] Define stable enum/value contracts for geometry domains, lane types,
      property value kinds, presentation kinds, surface/point/line slot
      semantics, readiness states, generated-output provenance, and job-domain
      metadata.
- [x] Define `PropertyBindingDescriptor` records with geometry domain, property
      name, expected value type, source generation/count expectations, and
      diagnostic-ready resolution status without persisting raw property
      pointers.
- [x] Define per-slot binding records that can represent uniform defaults,
      authored texture assets, generated texture assets, property-bake
      requests, and direct property-buffer presentation.
- [x] Define per-entity lane-to-presentation binding records for
      `RenderSurface`, `RenderEdges`, and `RenderPoints` without moving full
      material state into those components.
- [x] Define generated-output policy records with at least `SessionCache`,
      `DeterministicChildAsset`, and `PersistOnSave`, using ADR-0021 defaults
      for generated textures and generated property buffers.
- [x] Add descriptor validation helpers that report compatible, incompatible,
      missing, stale, and unsupported source properties for mesh, graph, and
      point-cloud domains.
- [x] Add serialization/deserialization support for material/presentation
      bindings and generated-output policy while excluding transient job state,
      borrowed property views, and GPU handles.

## Tests
- [x] Add CPU/null descriptor tests for mesh, graph, and point-cloud leaf
      entities proving all accepted domains can be represented.
- [x] Add validation tests for compatible and incompatible property bindings,
      including disabled-with-reason entries for UI property pickers.
- [x] Add serialization round-trip tests proving bindings and generated-output
      policy persist while transient readiness/job state does not.
- [x] Add regression tests proving render-lane components remain primitive
      toggles and presentation records live in the separate binding model.

## Docs
- [x] Update `src/runtime/README.md` with descriptor ownership, persistence,
      and no-raw-pointer/no-GPU-handle rules.
- [x] Link ADR-0021 from the runtime descriptor documentation.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface
      changes.

## Acceptance criteria
- [x] One shared descriptor model represents surface, point, and edge/line
      presentation for mesh, graph, and point-cloud entities.
- [x] Compatible property-source discovery is deterministic and reports
      disabled reasons for incompatible choices.
- [x] Scene serialization can round-trip presentation bindings and
      generated-output policy without serializing transient jobs or raw property
      pointers.
- [x] The default CPU-supported CTest gate verifies the descriptor contract.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Progressive|RenderData|SceneSerialization|RenderLane' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not add graphics, Vulkan, or `AssetService` ownership to ECS components.
- Do not serialize raw property pointers, borrowed property views, or transient
  worker/job state.
- Do not remove or rename `RenderSurface`, `RenderEdges`, or `RenderPoints`.
- Do not implement async scheduling or texture baking in this descriptor task.

## Maturity
- Target: `CPUContracted`.
- This task closes the descriptor and persistence contract. For the descriptor
  task itself, no Operational follow-up is owed; runtime consumption is owned
  by `RUNTIME-113` and `RUNTIME-114`.
