# RUNTIME-098 — Promoted scene serialization and editor command seam

## Status
- Status: done.
- Owner/agent: Codex.
- Branch: `main`.
- Completion date: 2026-06-07.
- PR/commit: this retirement commit.
- Maturity reached: `CPUContracted`.
- Summary: Runtime now owns backend-neutral JSON scene save/load for current
  sandbox-authored ECS data, `Engine` exposes file-path facades with selection
  and lookup cleanup on load, and `SandboxEditorUi` exposes an ImGui
  `File / Scene` command window routed through runtime-owned command surfaces.

## Goal
- Add a promoted runtime scene save/load seam that persists current ECS-authored sandbox scene data and exposes it through the default Sandbox editor UI command surface.

## Non-goals
- Reanimating the legacy `Runtime.SceneSerializer` or `Runtime.EditorUI` modules.
- Serializing GPU-only handles, renderer leases, descriptor slots, or cache state.
- Re-importing arbitrary legacy asset source references on load; promoted model/texture import remains owned by `Engine::ImportAssetFromPath` and asset handoff tasks.
- Persisting every possible ECS component; this slice covers current sandbox-authoring data required for mesh, graph, point-cloud, transform, hierarchy, selection eligibility, stable identity, and render hints.
- Full dirty-tracker UX, file dialogs, undo/redo integration, or overwrite prompts.
- Retiring legacy serializer files; this slice is a promoted parity step, not a deletion task.

## Context
- Owner/layer: `runtime`, which may compose ECS, assets, graphics component authoring hints, and core IO while lower layers remain unaware of runtime/editor state.
- Legacy scene persistence records entity names, transforms, hierarchy, asset source paths, and display settings. The promoted scene path now authors sandbox geometry directly in ECS `GeometrySources`, so parity must target those current components instead of legacy asset/source component names.
- The existing `SandboxEditorUi` already uses runtime-owned command surfaces for asset import, camera/render controls, primitive views, and visualization settings. Scene save/load should follow that model and keep UI as a command/event producer.

## Required changes
- [x] Add a promoted `Extrinsic.Runtime.SceneSerialization` module with backend-neutral JSON serialization/deserialization over `ECS::Scene::Registry` and `Core::IO::IIOBackend`.
- [x] Persist deterministic entity records for metadata names, durable stable ids, local transforms, hierarchy parent links, selectable tags, render geometry hints, and mesh/graph/point-cloud `GeometrySources` property data.
- [x] Add `Engine::SaveSceneToPath` and `Engine::LoadSceneFromPath` facades that use `FileIOBackend`, reset stale selection/refinement state on load, and rebuild runtime lookup state.
- [x] Add a Sandbox editor scene-file command surface, model, pure command helpers, and ImGui `File / Scene` window for save/load path entry.
- [x] Keep `SandboxEditorUi` runtime/editor-owned; do not import legacy `Runtime.EditorUI`.

## Tests
- [x] Add contract coverage for scene document round-trip through memory IO, including mesh topology, graph/point-cloud properties, transforms, stable ids, hierarchy, selectable tags, and render hints.
- [x] Add contract coverage for malformed/unsupported scene documents failing closed with explicit errors.
- [x] Add editor command-surface coverage for save/load routing, unavailable commands, and invalid paths.

## Docs
- [x] Update runtime docs to describe the promoted scene serialization seam and editor command routing.
- [x] Update migration parity docs to record promoted scene persistence coverage and remaining non-goals.
- [x] Update runtime backlog/index records as needed.
- [x] Regenerate `docs/api/generated/module_inventory.md` because a public runtime module surface is added.

## Acceptance criteria
- [x] Scene save/load works without legacy modules and without graphics/Vulkan availability.
- [x] The default Sandbox editor has a dedicated scene file command window wired to runtime-owned save/load facades.
- [x] Serializer load replaces scene contents deterministically and does not leave stale runtime selection state.
- [x] Focused runtime contract tests pass under the CPU-supported gate.
- [x] Layering, task policy, docs links, and test layout checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicRuntimeGraphicsCpuTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeSceneSerialization|SandboxEditorUi|^RuntimeSandboxAcceptance\.' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding cross-layer imports from ECS, graphics, assets, or platform back into runtime/editor.
- Persisting renderer/RHI/backend-private state.
- Modifying or deleting legacy serializer modules in this slice.

## Maturity
- Target: `CPUContracted`.
- This slice closes the promoted scene persistence seam under CPU/null tests; no `Operational` follow-up is owed because the serializer is backend-neutral. File-dialog UX, dirty prompts, and full legacy deletion are separate future tasks if needed.
