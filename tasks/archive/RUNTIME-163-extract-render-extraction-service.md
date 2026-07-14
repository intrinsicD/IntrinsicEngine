---
id: RUNTIME-163
theme: F
depends_on: [RUNTIME-162]
maturity_target: Operational
completed: 2026-07-09
---
# RUNTIME-163 — Extract render extraction service out of Engine

## Status
- Retired on 2026-07-09 at maturity `Operational`.
- `Extrinsic.Runtime.RenderExtractionService` owns the live
  `RenderExtractionCache`, configured `RenderWorldPool`, last extraction stats,
  and frame-index counter.
- `Runtime.Engine` keeps frame phase ordering and public compatibility facades
  while delegating cache/pool/stats/frame-index ownership through the service.
- Verification passed: focused render-extraction / render-world pool / runtime
  sandbox CPU coverage, `RuntimeEngineLayering` integration coverage, strict
  task/docs/layering/test-layout checks, `IntrinsicTests`, and the default
  CPU-supported CTest gate at 3646/3646.
- Warning-mode root/task-state findings remain pre-existing and unchanged:
  retired `ARCH-007`..`ARCH-013` index links, root `ara/`, and root
  `imgui.ini`.
- PR/commit: pending.

## Goal
- Move render-extraction owner state out of `Runtime.Engine.cppm` /
  `Runtime.Engine.cpp` into a focused runtime service: the
  `RenderExtractionCache`, render-world pool, last extraction stats, and frame
  index. Keep `Engine` as the concrete frame-order owner and preserve the
  existing public compatibility accessors.

## Non-goals
- Changing `RenderExtractionCache::ExtractAndSubmit(...)`, geometry residency,
  material binding, visualization adapter, spatial debug, render-world pool, or
  renderer contract semantics.
- Moving the full `Runtime.Engine:FrameLoop` partition or changing the render
  frame contract hook interfaces.
- Changing asset import, scene document, object-space normal bake, model-scene
  handoff, mesh primitive-view, or visualization adapter callers beyond routing
  their existing render-extraction dependency through the service.
- Completing any GPU/Vulkan or render-cache performance work.

## Context
- Owner: `runtime`; this is composition glue around the promoted
  `Extrinsic.Runtime.RenderExtraction` and `Extrinsic.Runtime.RenderWorldPool`
  modules.
- After `RUNTIME-162`, `Runtime.Engine.cppm` still imports render extraction and
  render-world pool modules directly, stores `RenderExtractionCache`,
  `RenderWorldPool`, `RuntimeRenderExtractionStats`, and the frame-index counter,
  and exposes render-extraction helper state through Engine accessors.
- `Runtime.Engine.cpp` still shuts down the extraction cache directly, passes the
  raw cache/pool through render and maintenance hooks, mirrors the last stats,
  releases pooled slots, and forwards material/visualization binding facades
  directly into `RenderExtractionCache`.
- This follows the `RUNTIME-146` through `RUNTIME-162` decomposition pattern:
  `Engine` remains the frame skeleton and subsystem wiring root, while
  subsystem-local ownership and compatibility facades move behind
  runtime-owned modules.

## Required changes
- [x] Add `Extrinsic.Runtime.RenderExtractionService` under `src/runtime/`
  owning `RenderExtractionCache`, optional `RenderWorldPool`, last
  `RuntimeRenderExtractionStats`, and the monotonic frame index.
- [x] Move render-world-pool configuration, current/consumed frame-index access,
  last-stats publication/readback, pooled-front release, cache shutdown, material
  texture binding readback, visualization adapter binding facade, and mesh
  primitive-view cache invalidation helpers behind the service.
- [x] Update `Runtime.Engine.cppm` to store the service instead of the raw
  render-extraction cache/pool/stats/frame-index members and to stop directly
  importing `Extrinsic.Runtime.RenderExtraction` and
  `Extrinsic.Runtime.RenderWorldPool`.
- [x] Update `Runtime.Engine.cpp` so initialization, shutdown, render contract
  hook construction, maintenance hooks, dependent subsystem wiring, and public
  facade methods delegate through the service while preserving frame phase
  ordering.
- [x] Add the new module to `src/runtime/CMakeLists.txt`.

## Tests
- [x] Add runtime source-contract coverage proving render-extraction cache,
  render-world pool, last extraction stats, frame-index ownership, and direct
  material/visualization binding forwarding no longer live in
  `Runtime.Engine.cppm` / `Runtime.Engine.cpp`.
- [x] Preserve render extraction, render-world pool, runtime sandbox, and Engine
  layering coverage.
- [x] Run the default CPU-supported correctness gate before retirement.

## Docs
- [x] Update `src/runtime/README.md` to document
  `Extrinsic.Runtime.RenderExtractionService` and revise the Engine/render
  extraction current-state wording.
- [x] Update `tasks/backlog/runtime/README.md` with the factual decomposition
  state.
- [x] Update `tasks/backlog/README.md` if the Theme F Engine-decomposition
  summary changes.
- [x] Regenerate `docs/api/generated/module_inventory.md`.
- [x] Regenerate `tasks/SESSION-BRIEF.md` after opening.

## Acceptance criteria
- [x] `Runtime.Engine.cppm` no longer imports or stores `RenderExtractionCache`,
  `RenderWorldPool`, `RuntimeRenderExtractionStats`, or the render-extraction
  frame-index counter directly.
- [x] `Runtime.Engine.cpp` no longer calls `RenderExtractionCache::Shutdown(...)`,
  `GetMaterialTextureAssetBindings(...)`,
  `SetVisualizationAdapterBinding(...)`, `ClearVisualizationAdapterBinding(...)`,
  `GetVisualizationAdapterBinding(...)`, or
  `GetVisualizationAdapterBindingRevision()` directly.
- [x] Existing behavior remains unchanged: extraction still writes/publishes the
  same render-world slots, stats still mirror pool diagnostics, maintenance still
  ticks all residency retire windows after asset/cache ticks, and public Engine
  render-extraction facades still work.
- [x] Strict task, docs, layering, and test-layout checks pass, aside from
  pre-existing warning-mode root/task-state findings if unchanged by this
  slice.

## Verification
```bash
python3 tools/agents/generate_session_brief.py
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeRenderExtraction|RenderWorldPool|RuntimeEngineLayering|RuntimeSandboxAcceptance' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
cmake --build --preset ci --target IntrinsicRuntimeIntegrationTests
ctest --test-dir build/ci --output-on-failure -R 'RuntimeEngineLayering|RuntimeSandboxAcceptance|RenderWorldPool' --timeout 180
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_state_links.py --root .
python3 tools/repo/check_root_hygiene.py --root .
git diff --check
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Changing render extraction cache algorithms, stats accounting, residency
  semantics, material/visualization binding contents, or render-world pool slot
  lifecycle.
- Changing renderer contract ordering, `IRenderFrameHooks`, or
  `IAssetFrameHooks`.
- Moving asset import or scene document ownership into this service.
- Reverting unrelated dirty worktree changes.

## Maturity
- Target: `Operational` for this cleanup slice.
- This slice closes at `Operational` when live Engine frame processing delegates
  render-extraction owner state and compatibility facades to the new runtime
  service and focused render-extraction/layering coverage plus the default CPU
  gate pass.
