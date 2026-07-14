---
id: RUNTIME-115
theme: F
depends_on: [RUNTIME-102, RUNTIME-109, RUNTIME-112, RUNTIME-113]
maturity_target: CPUContracted
---
# RUNTIME-115 — Selected mesh bake command surface

## Completion
- Retired on 2026-06-17 at maturity `CPUContracted`.
- Owner/agent: Codex.
- Branch/PR: local `main`; PR not opened.
- Summary: runtime now owns `Extrinsic.Runtime.SelectedMeshTextureBake`,
  which validates selected-mesh bake requests, builds the generic mesh
  attribute bake request, supports synchronous and derived-job execution,
  reloads generated texture payloads through `AssetService`, and optionally
  binds generated outputs through command-history-owned presentation slots.
- Evidence: focused runtime/UI coverage passed in the combined
  `SandboxEditor|EditorCommandHistory|Uv|TextureBake|SelectedMeshTextureBake|MeshAttributeTextureBake|DerivedJob|Progressive|PropertyCatalog`
  CTest run.

## Slice plan
- **Slice A. Command contract module.** Add a runtime module for selected-mesh bake request/result/status data, validation, generated asset path construction, and mapping from progressive slots to `MeshAttributeTextureBakeRequest`.
- **Slice B. Synchronous CPU command path.** Execute validated vertex/face bakes through the existing CPU baker, load/reload generated `AssetTexture2DPayload` records through `AssetService`, and update progressive bindings only when explicitly requested.
- **Slice C. Derived-job observability.** Submit an observable CPU derived job when a registry is supplied, preserve previous outputs while pending/failed, discard stale apply results, and expose deterministic diagnostics.
- **Slice D. Tests/docs/retirement.** Add runtime contract tests for valid bakes, invalid requests, command-history dirtying, generated asset reuse, and binding mutation; update docs and retire at `CPUContracted`.

## Goal
- Add a runtime-owned selected-mesh texture-bake command surface that editor UI can call to bake compatible mesh properties into generated texture payloads, update presentation/material bindings, and report progress/diagnostics without UI owning bake, asset, worker, or graphics state.

## Non-goals
- No UI widgets or ImGui rendering in this task.
- No new bake algorithm beyond the existing `Extrinsic.Runtime.MeshAttributeTextureBake` contract.
- No graph or point-cloud texture baking.
- No Vulkan/RHI allocation or direct graphics resource mutation.
- No implicit UV generation inside the baker; UV resolution remains upstream.
- No persistence of transient worker/job state.

## Context
- Owning subsystem/layer: `runtime` owns command/history integration, selected-entity lookup, derived-job scheduling/apply, generated texture payload lifecycle requests, and progressive presentation descriptor mutation. Assets/geometry provide CPU data; graphics owns GPU residency.
- `RUNTIME-109` exposes the generic CPU mesh attribute bake request/result seam for vertex and face domains.
- `RUNTIME-112` exposes derived-job snapshots and progress/diagnostics.
- `RUNTIME-113` exposes progressive presentation descriptors and property binding resolution.
- `RUNTIME-102` owns editor command history and dirty-state routing for user-visible mutations.
- `UI-014` needs this command surface before texture-bake controls can be more than a disabled/data-only panel.

## Required changes
- [x] Add a runtime command-facing selected-mesh bake request model that names source entity, source domain, source property, encoder, output size, range/colormap policy, target semantic, generated key policy, and binding target.
- [x] Validate selected-entity liveness, mesh availability, source property support, property count, resolved UV availability, encoder compatibility, output resolution, and target binding compatibility before scheduling work.
- [x] Route user-visible bake requests through `EditorCommandHistory` and document which state changes are undoable versus asynchronous derived outputs.
- [x] Schedule bake work through the runtime derived-job path when work is non-trivial; keep a deterministic synchronous/test hook if existing runtime tests require it.
- [x] Apply bake completions on the main thread only, discard stale results after entity/source/binding changes, and preserve previous valid generated outputs until replacements are ready.
- [x] Register or reload generated `AssetTexture2DPayload` data through existing runtime asset/generated-texture seams without importing live `AssetService` ownership into UI.
- [x] Update progressive presentation/material binding descriptors only after a successful bake or an explicit user request to bind the generated output.
- [x] Expose command result and job diagnostics for invalid UVs, unsupported property domain/type, count mismatch, non-finite values, invalid range/resolution, zero coverage, stale completion, and upload deferral.

## Tests
- [x] Add runtime contract tests proving valid vertex and face bake commands construct the expected `MeshAttributeTextureBakeRequest`.
- [x] Add command-history/dirty-state tests proving user-visible bake requests route through runtime editor command ownership.
- [x] Add invalid-request tests for non-mesh selection, missing property, unsupported domain, unsupported type, count mismatch, missing/invalid UVs, invalid resolution, and incompatible target slot.
- [x] Add derived-job tests for queued/running/complete/failed/stale bake states and diagnostic propagation.
- [x] Add generated texture key/reload tests proving repeated user bakes replace or reload the intended generated payload instead of minting unbounded duplicate assets.
- [x] Add binding mutation tests proving generated outputs are bound only when requested and previous valid outputs remain visible until replacements are ready.

## Docs
- [x] Update `src/runtime/README.md` with the selected-mesh bake command ownership, generated texture lifecycle, and async apply rules.
- [x] Update `src/runtime/Editor/README.md` if the editor command surface is documented there.
- [x] Update `tasks/active/UI-014-uv-backend-and-texture-bake-controls.md` if command availability changes the UI gate.
- [x] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

## Acceptance criteria
- [x] Editor/UI callers have one runtime-owned command surface for selected-mesh property texture bakes.
- [x] The command surface rejects unsupported requests before work is scheduled and reports deterministic diagnostics.
- [x] Successful bakes produce or reload generated texture payloads and optionally update presentation/material bindings without UI owning assets or graphics resources.
- [x] Bake progress and failures are visible through derived-job/runtime snapshots.
- [x] The default CPU-supported CTest gate verifies the command surface, diagnostics, and generated-output lifecycle.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests IntrinsicAssetUnitTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SelectedMeshBake|MeshAttributeTextureBake|DerivedJob|Progressive|EditorCommandHistory' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not run texture baking from UI rendering code.
- Do not allocate or mutate Vulkan/RHI resources in runtime.
- Do not make graph or point-cloud texture baking depend on mesh UV assumptions.
- Do not auto-generate UVs inside the selected-mesh bake command.
- Do not bypass `EditorCommandHistory` for user-visible binding or bake-request mutations.

## Maturity
- Target: `CPUContracted`.
- This runtime task closes the selected-mesh bake command contract; no `Operational` follow-up is owed.
