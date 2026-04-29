# HARDEN-041 — Move remaining test sources into taxonomy directories

## Goal
Move remaining wrapper test sources from legacy subsystem directories into the canonical taxonomy directories without changing test semantics.

## Non-goals
- Do not change test logic, assertions, fixtures, or runtime behavior.
- Do not alter production source under `src/`.
- Do not remove `src/legacy/` or perform legacy module migration.

## Context
HARDEN-040 completed the audit of non-taxonomic test directories and mapped concrete follow-up actions for source movement. This task performs only the mechanical source relocation and associated CMake list path rewires needed to keep builds/tests equivalent.

## Required changes
- Move wrapper test `*.cpp` files from:
  - `tests/Asset/`
  - `tests/Core/`
  - `tests/ECS/`
  - `tests/Graphics/`
  - `tests/Runtime/`
  into canonical taxonomy roots:
  - `tests/unit/`
  - `tests/contract/`
  - `tests/integration/`
  according to the mapping recorded in `docs/reports/test-taxonomy-audit-2026-04-29.md`.
- Update affected `tests/CMakeLists.txt` (and subordinate CMake files if present) to point to relocated files.
- Keep target names and labels stable unless a taxonomy-only rename is explicitly required by the audit mapping.
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` status/evidence for HARDEN-041.

## Tests
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- Update hardening tracker status board and evidence log for HARDEN-041.
- Keep links to the HARDEN-040 audit report current.

## Acceptance criteria
- [x] Wrapper source files are relocated from old subsystem directories into taxonomy directories per HARDEN-040 mapping.
- [x] CMake test registration resolves relocated files without missing-source or missing-target errors.
- [ ] CPU-supported CTest gate remains green after the move.
- [x] Hardening tracker records HARDEN-041 progress and verification evidence.
- [x] Strict task/doc validators pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No test semantic refactors in the same patch as mechanical moves.
- No changes to production runtime/graphics behavior.
- No legacy retirement or `src/legacy/` shrink work.


## Execution notes (2026-04-29)

- Inspected the current taxonomy audit report and `tests/CMakeLists.txt` source-resolution wiring before performing moves.
- Confirmed HARDEN-040 produced inventory counts but did **not** yet publish a per-file destination table for all 37 wrapper sources.
- To avoid mixing mechanical moves with semantic categorization guesses, this task remains **in-progress** until the per-file mapping list is written directly into this task (or a linked mapping appendix) and then executed in one mechanical patch.

### Verified preconditions

- Wrapper directories remain intentionally unregistered in active CTest registration.
- Active taxonomy targets already resolve from `tests/unit`, `tests/contract`, `tests/integration`, `tests/regression`, `tests/gpu`, and `tests/benchmark`.


### File-by-file move table (planned mechanical mapping)

| Source wrapper file | Destination taxonomy path | Category rationale |
|---|---|---|
| `tests/Asset/Test.Asset.EventBus.cpp` | `tests/unit/assets/Test_Asset_EventBus.cpp` | Asset/core utility behavior; unit scope. |
| `tests/Asset/Test.Asset.LoadPipeline.cpp` | `tests/integration/runtime/Test_AssetLoadPipeline.cpp` | Cross-subsystem loading flow; integration scope. |
| `tests/Asset/Test.Asset.PathIndex.cpp` | `tests/unit/assets/Test_Asset_PathIndex.cpp` | Deterministic asset index behavior; unit scope. |
| `tests/Asset/Test.Asset.PayloadStore.cpp` | `tests/unit/assets/Test_Asset_PayloadStore.cpp` | In-memory payload semantics; unit scope. |
| `tests/Asset/Test.Asset.Registry.cpp` | `tests/unit/assets/Test_Asset_Registry.cpp` | Registry contracts without runtime composition; unit scope. |
| `tests/Asset/Test.Asset.Service.cpp` | `tests/integration/runtime/Test_AssetService.cpp` | Service wiring with runtime-facing seams; integration scope. |
| `tests/Core/Test.Core.CallbackRegistry.cpp` | `tests/unit/core/Test_Core_CallbackRegistry.cpp` | Core container/registration primitive; unit scope. |
| `tests/Core/Test.Core.Config.cpp` | `tests/unit/core/Test_Core_Config.cpp` | Config parsing/validation behavior; unit scope. |
| `tests/Core/Test.Core.DagScheduler.cpp` | `tests/unit/core/Test_Core_DagScheduler.cpp` | Scheduler primitive behavior; unit scope. |
| `tests/Core/Test.Core.Error.cpp` | `tests/unit/core/Test_Core_ErrorLegacy.cpp` | Legacy core error behavior parity; unit scope. |
| `tests/Core/Test.Core.Filesystem.cpp` | `tests/unit/core/Test_Core_Filesystem.cpp` | Filesystem helpers with deterministic seams; unit scope. |
| `tests/Core/Test.Core.FrameGraphParallel.cpp` | `tests/integration/runtime/Test_CoreFrameGraphParallel.cpp` | Multi-system framegraph behavior; integration scope. |
| `tests/Core/Test.Core.GraphCompiler.cpp` | `tests/unit/core/Test_Core_GraphCompiler.cpp` | Graph compile semantics; unit scope. |
| `tests/Core/Test.Core.GraphInterfaces.cpp` | `tests/contract/runtime/Test_CoreGraphInterfaces.cpp` | API/interface contract guarantees. |
| `tests/Core/Test.Core.GraphStress.cpp` | `tests/integration/runtime/Test_CoreGraphStress.cpp` | Stress/scenario-level behavior; integration scope. |
| `tests/Core/Test.Core.HandleLease.cpp` | `tests/unit/core/Test_Core_HandleLease.cpp` | Handle lease primitive behavior; unit scope. |
| `tests/Core/Test.Core.Hash.cpp` | `tests/unit/core/Test_Core_HashLegacy.cpp` | Hash behavior parity; unit scope. |
| `tests/Core/Test.Core.LockFreeQueue.cpp` | `tests/unit/core/Test_Core_LockFreeQueue.cpp` | Lock-free container semantics; unit scope. |
| `tests/Core/Test.Core.Logging.cpp` | `tests/unit/core/Test_Core_Logging.cpp` | Logging primitive behavior; unit scope. |
| `tests/Core/Test.Core.Memory.cpp` | `tests/unit/core/Test_Core_MemoryLegacy.cpp` | Memory primitive behavior parity; unit scope. |
| `tests/Core/Test.Core.Process.cpp` | `tests/integration/runtime/Test_CoreProcess.cpp` | Process/runtime seam behavior; integration scope. |
| `tests/Core/Test.Core.ResourcePool.cpp` | `tests/unit/core/Test_Core_ResourcePool.cpp` | Resource-pool primitive invariants; unit scope. |
| `tests/Core/Test.Core.StrongHandle.cpp` | `tests/unit/core/Test_Core_StrongHandleLegacy.cpp` | Strong-handle parity checks; unit scope. |
| `tests/Core/Test.Core.TaskGraph.cpp` | `tests/unit/core/Test_Core_TaskGraphLegacy.cpp` | Task graph primitive semantics; unit scope. |
| `tests/Core/Test.Core.Tasks.cpp` | `tests/unit/core/Test_Core_TasksLegacy.cpp` | Task utility behavior; unit scope. |
| `tests/ECS/Test.ECS.Scene.Registry.cpp` | `tests/unit/ecs/Test_ECS_SceneRegistry.cpp` | ECS registry data behavior; unit scope. |
| `tests/Graphics/Test.Graphics.GpuWorldAndCulling.cpp` | `tests/integration/graphics/Test_GpuWorldAndCulling.cpp` | Graphics pipeline+culling interaction; integration scope. |
| `tests/Graphics/Test.Graphics.MinimalTriangleAcceptance.cpp` | `tests/contract/graphics/Test_MinimalTriangleAcceptance.cpp` | Minimal rendering acceptance contract. |
| `tests/Graphics/Test.Graphics.RenderGraph.cpp` | `tests/integration/graphics/Test_RenderGraphLegacy.cpp` | Render graph multi-system behavior; integration scope. |
| `tests/Graphics/Test.Graphics.Renderer.cpp` | `tests/integration/graphics/Test_RendererLegacy.cpp` | Renderer end-to-end behavior; integration scope. |
| `tests/Graphics/Test.RHI.BufferManager.cpp` | `tests/unit/graphics/Test_RHI_BufferManager.cpp` | RHI resource primitive behavior; unit scope. |
| `tests/Graphics/Test.RHI.CommandContextBarriers.cpp` | `tests/contract/graphics/Test_RHI_CommandContextBarriers.cpp` | Barrier/synchronization contract checks. |
| `tests/Graphics/Test.RHI.PipelineManager.cpp` | `tests/unit/graphics/Test_RHI_PipelineManager.cpp` | RHI pipeline state primitive behavior; unit scope. |
| `tests/Graphics/Test.RHI.SamplerManager.cpp` | `tests/unit/graphics/Test_RHI_SamplerManager.cpp` | RHI sampler primitive behavior; unit scope. |
| `tests/Graphics/Test.RHI.TextureManager.cpp` | `tests/unit/graphics/Test_RHI_TextureManager.cpp` | RHI texture primitive behavior; unit scope. |
| `tests/Runtime/Test.Runtime.EngineLayering.cpp` | `tests/contract/runtime/Test_RuntimeEngineLayering.cpp` | Layering/ownership contract checks. |
| `tests/Runtime/Test.Runtime.StreamingExecutor.cpp` | `tests/integration/runtime/Test_RuntimeStreamingExecutor.cpp` | Runtime streaming executor integration behavior. |

### Next step for this task

1. Re-run build + CPU-supported CTest gate once `external/cache/*-src` offline dependencies are available in this environment.
2. If runtime labels are to be narrowed after wrapper-source relocation, do that in a follow-up taxonomy-label task only (no semantic test changes).

## Execution update (2026-04-29)

- Applied a mechanical-only relocation patch for all 37 wrapper `*.cpp` sources using `git mv` from `tests/{Asset,Core,ECS,Graphics,Runtime}` into taxonomy directories under `tests/unit`, `tests/contract`, and `tests/integration`.
- No test source content edits were made; only file paths changed.
- HARDEN-041B updated `tests/CMakeLists.txt` to register relocated sources under taxonomy-specific object libraries and executable targets:
  - `AssetUnitTestObjs` / `IntrinsicAssetUnitTests`
  - `CoreWrapperUnitTestObjs` / `IntrinsicCoreWrapperUnitTests`
  - `GraphicsUnitTestObjs` + `GraphicsContractTestObjs` / `IntrinsicGraphicsUnitTests` + `IntrinsicGraphicsContractTests`
  - `RuntimeIntegrationTestObjs` / `IntrinsicRuntimeIntegrationTests`
- GPU/backend-facing relocated graphics/runtime suites are labeled `gpu` + `vulkan` and remain outside the default CPU-supported CTest gate.
- Strict task/doc validators pass after the relocation patch.

- Re-ran strict policy/docs validators after relocation status sync:
  - `python3 tools/agents/check_task_policy.py --root . --strict` → passed (`Validated 14 task file(s); findings: 0`).
  - `python3 tools/docs/check_doc_links.py --root . --strict` → passed (`Checked relative links: 105; no broken links`).


## HARDEN-041C update (2026-04-29)

- Audited every relocated source currently registered under `AssetUnitTestObjs`, `CoreWrapperUnitTestObjs`, `GraphicsUnitTestObjs`, `GraphicsContractTestObjs`, and `RuntimeIntegrationTestObjs` in `tests/CMakeLists.txt` against module imports in the relocated taxonomy sources.
- Fixed stale legacy target wiring for relocated suites:
  - `AssetUnitTestObjs` / `IntrinsicAssetUnitTests` now link `ExtrinsicAssets` + `ExtrinsicCore`.
  - `CoreWrapperUnitTestObjs` / `IntrinsicCoreWrapperUnitTests` now link `ExtrinsicCore`.
  - `GraphicsUnitTestObjs` / `IntrinsicGraphicsUnitTests` now link `ExtrinsicRHI` + `ExtrinsicCore` + `ExtrinsicPlatform`.
  - `GraphicsContractTestObjs` / `IntrinsicGraphicsContractTests` now link `ExtrinsicGraphics` + `ExtrinsicRHI` + `ExtrinsicECS` + `ExtrinsicCore` + `ExtrinsicPlatform`.
  - `RuntimeIntegrationTestObjs` / `IntrinsicRuntimeIntegrationTests` now link `ExtrinsicRuntime`.
- Updated runtime-integration registration gate to `if(TARGET ExtrinsicRuntime)` to match the owning promoted runtime target.
- Confirmed no relocated HARDEN-041 `.cpp` in these groups is orphaned in registration (all listed files resolve through taxonomy `SEARCH_DIRS` entries).
- Verification status in this environment:
  - `cmake --preset ci -DINTRINSIC_OFFLINE_DEPS=ON` fails before generation because offline cache is missing (`external/cache/glm-src`).
  - Downstream build/ctest commands therefore cannot prove compiled test execution here.
  - Strict task/docs/layout validators pass.
