# UI-007 — Sandbox editor drag/drop geometry import

## Status
- Status: done.
- Maturity: `CPUContracted`.
- Completion date: 2026-06-08.
- PR/commit: pending local commit.

## Goal
- Wire OS file drag/drop in the promoted sandbox runtime so loadable mesh, graph, and point-cloud files route through the runtime import facade, materialize as renderable ECS entities, and report status in the sandbox editor UI.

## Non-goals
- No changes to render-graph compilation or `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp`.
- No new file-format decoders beyond the existing promoted mesh, graph, point-cloud, model-scene, and texture importers.
- No asynchronous streaming queue or hot-reload behavior for dropped files.
- No legacy runtime/event-path edits except as reference material.

## Context
- Owner layer: `runtime`, with existing lower-layer dependencies on `platform`, `assets`, `ecs`, `geometry`, and `graphics` component tags allowed by the repository contract.
- Platform already emits `Platform::WindowDropEvent`; runtime currently does not consume it for promoted asset import.
- `Engine::ImportAssetFromPath` already imports model scenes and textures but rejects standalone geometry payloads even though `Runtime.AssetGeometryIO` registers promoted mesh/graph/point-cloud callbacks.
- The sandbox editor file/import panel has a runtime-owned command seam and can surface last import results.
- Dropped files do not carry a UI payload hint, so ambiguous geometry extensions such as PLY must try supported geometry payloads in import-router order before failing closed.

## Required changes
- [x] Register a runtime-owned platform drop listener during `Engine::Initialize()`.
- [x] Extend `Engine::ImportAssetFromPath` to import and materialize standalone mesh, graph, and point-cloud payloads into ECS renderable entities.
- [x] Record the last runtime asset-import event so editor UI can display drop/import status without owning platform events.
- [x] Add sandbox editor UI payload-kind selection for manual imports and display drop/import results consistently.
- [x] Add CPU/null contract tests for geometry import materialization and drop-event routing.

## Tests
- [x] Add/update runtime contract tests covering mesh, graph, and point-cloud import through `Engine::ImportAssetFromPath`.
- [x] Add/update sandbox editor UI tests covering payload hint propagation and last import status display.
- [x] Cover ambiguous PLY point-cloud drop routing without a UI payload hint.
- [x] Run focused runtime/editor tests.
- [x] Run structural checks relevant to task/docs/layering/test layout.

## Docs
- [x] Update runtime/migration documentation to describe promoted drag/drop geometry import support.
- [x] Refresh generated module inventory if public module surfaces change.

## Acceptance criteria
- [x] Dropping a supported mesh, graph, or point-cloud file on the runtime window routes through the same runtime import facade used by the editor panel.
- [x] Standalone geometry imports create ECS entities with geometry sources and render component hints appropriate for the payload domain.
- [x] The sandbox editor UI reports the latest import result, including drag/drop-triggered imports.
- [x] Unsupported or invalid paths fail closed with an error result instead of partial entity creation.
- [x] Existing model-scene and texture import behavior is preserved.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'AssetGeometryIO|SandboxEditorUi' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Adding new layer edges from graphics/platform into runtime, ECS, or live asset services.
- Editing `src/graphics/framegraph/Graphics.RenderGraph.Compiler.cpp` unless a directly failing test proves it is required.

## Maturity
- Target: `CPUContracted` for null/headless verification, with the GLFW OS drop callback path already present in platform and covered by existing platform event semantics.
- `Operational` on an interactive GLFW host is manually exercised by dropping supported files into `ExtrinsicSandbox`; no `Operational` follow-up is owed and no GPU/Vulkan gate is owed by this slice.
