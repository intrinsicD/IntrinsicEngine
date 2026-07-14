# GRAPHICS-026 — Vulkan/renderer plumbing follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Branch: `claude/review-graphics-commits-ZUXOW`.
- Commit / PR: pending local agent workflow handoff.
- Activated: 2026-05-04 from review of recent `src/graphics/` commits (`550b0e9` … `c64bba3`).
- Source: `GRAPHICS-018` slice review (renderer frame lifecycle through RHI + promoted Vulkan fail-closed surface).
- Progress 2026-05-04: completed item 2.1 plus item 5.1 by replacing Vulkan implementation-file `stderr` diagnostics with `Core::Log::*`, routing `VK_CHECK_*` diagnostics through a logger bridge, and documenting the logging convention.
- Progress 2026-05-04 slice 2: completed renderer lifecycle/statistics items 1.1–1.10 and 1.12; completed Vulkan documentation/guard items 2.2–2.8 and 2.10–2.14 plus fallback bindless diagnostics for 2.16; reduced `Test.RendererRhiBoundary.cpp` to symbol/linkage guards while adding conditional constructor-only Vulkan fail-closed behavioral coverage; added CPU/mock command-pass unavailable tests for 3.2–3.3; completed docs/task hygiene items 4.1–4.4; created follow-ups `GRAPHICS-018R`, `GRAPHICS-018S`, and `GRAPHICS-018T` for remaining operational-transition/sampler/upload work.
- Completed: 2026-05-04.
- Verification note 2026-05-04: default `ci` preset still names unavailable local `clang-20`; final default CPU evidence used the same `ci` preset with explicit `/usr/bin/clang` and `/usr/bin/clang++` (Clang 22, C++23-capable) plus `INTRINSIC_HEADLESS_NO_GLFW=ON`. Constructor-only Vulkan fail-closed coverage was separately configured in `build/ci-vulkan-contract` with `EXTRINSIC_BACKEND=Vulkan` and explicit ImGuizmo source overrides; it builds `ExtrinsicBackendsVulkan` and runs without GPU/swapchain bring-up.

## Goal
- Address the plumbing/quality issues identified by the review of the recent `src/graphics/` commits without expanding the slice scope of `GRAPHICS-018`.
- Remove latent contradictions (asymmetric failure policy, dead state, undocumented invariants), tighten test methodology (replace grep-on-source with behavioral coverage where the seam allows), and unify diagnostics/logging conventions.

## Non-goals
- No new renderer features.
- No Vulkan instance/swapchain bring-up (remains owned by `GRAPHICS-018`).
- No CPU correctness behavior change for backend-independent code paths.
- No mandatory `gpu|vulkan` tests in the default CPU gate.

## Context
- Owner files:
  - `src/graphics/renderer/Graphics.Renderer.cpp` / `.cppm`
  - `src/graphics/renderer/Graphics.MaterialSystem.cpp`
  - `src/graphics/renderer/Graphics.CullingSystem.cpp`
  - `src/graphics/vulkan/Backends.Vulkan.Device.cpp` / `.cppm`
  - `src/graphics/vulkan/Backends.Vulkan.Memory.cppm`
  - `src/graphics/vulkan/README.md`
  - `docs/architecture/graphics.md`
  - `tasks/active/GRAPHICS-018-vulkan-renderer-integration.md`
  - `tasks/backlog/rendering/GRAPHICS-018Q-vulkan-integration-clarifications.md`
  - `tests/contract/graphics/Test.RendererFrameLifecycle.cpp`
  - `tests/contract/graphics/Test.RendererRhiBoundary.cpp`
  - `tests/contract/graphics/Test.VulkanFailClosedContract.cpp`
  - `tests/support/MockRHI.hpp`
- Each item below cites the originating commit hash from the review.

## Required changes

### 1. Renderer frame lifecycle (origin: 550b0e9, 6ce2cc8, 8f7aaff, c1f130e)
- [x] 1.1 Document the `IRenderer::EndFrame` contract in `Graphics.Renderer.cppm`: clarify that the returned value is the device's post-`EndFrame` global frame counter, not the `frame.FrameIndex` of the just-completed frame (550b0e9).
- [x] 1.2 Stop writing to `RenderGraphFrameStats.Diagnostic` from the `BeginFrame` "device missing" path; introduce a distinct field (e.g. `LifecycleDiagnostic`) or an enum status, since no graph has been compiled at that point (550b0e9).
- [x] 1.3 Replace the magic determinant tolerance in `IsInvertibleFiniteMatrix` with a named constant (e.g. `kCameraInverseDeterminantEpsilon`) (6ce2cc8).
- [x] 1.4 Add a defensive `Core::Log::Warn` when `passNameByIndex` resolution fails for a routed pass name during execute (6ce2cc8).
- [x] 1.5 Resolve the asymmetric pipeline-failure policy: `CullingSystem::Initialize` currently asserts on pipeline-create failure while depth-prepass logs and soft-skips. Pick soft-fail end-to-end (preferred — surface through `RenderGraphFrameStats.CommandPassesSkippedUnavailable`) or hard-fail end-to-end. Update `CullingSystem::Initialize` accordingly and document the chosen policy in `docs/architecture/graphics.md` (6ce2cc8, 8f7aaff).
- [x] 1.6 Make the depth-prepass dependency on `m_CullingInitialized` explicit: rename the guard (e.g. `IsCullingOutputAvailable()`) or add a comment explaining that depth-prepass consumes culling indirect output (8f7aaff).
- [x] 1.7 Stop calling `m_DepthPrepassPass.SetPipeline(...)` both at `Initialize` and at every record. Pick one cache point (8f7aaff).
- [x] 1.8 Export named constants from the renderer/RHI for `kMaxIndirectDrawCount` (currently `100000`) and the cull dispatch group sizing, and reference them from `Test.RendererFrameLifecycle.cpp` instead of hard-coded literals (8f7aaff, 6ce2cc8).
- [x] 1.9 Split `RenderGraphFrameStats` into focused sub-structs (`CompileStats`, `ExecuteStats`, `CommandRecordStats`); keep `Diagnostic` and `DebugDump` at the top level. Adjust call sites and tests (c1f130e).
- [x] 1.10 Promote per-pass command-recording counters to a name-keyed array (or `std::unordered_map<std::string_view, CommandRecordStatus>`) so adding new routed passes does not balloon the stats struct (c1f130e).
- [x] 1.11 Follow-up filed: `GRAPHICS-018R-operational-transition` blocks any `GRAPHICS-018` slice that marks Vulkan operational; timeline is before `VulkanDevice::IsOperational()` can become true. It owns the documented renderer reset hook for culling, material GPU buffer, and bindless accounting (6ce2cc8).
- [x] 1.12 Done 2026-05-04: `Graphics.MaterialSystem.cpp` comments the non-operational CPU-mirror shortcut and references `GRAPHICS-018R` (6ce2cc8).

### 2. Vulkan device surface (origin: c613b1c, 7336e67, 7e09ac5, 5c84d9a, 7d056cf, 295363e, 1a5c2be)
- [x] 2.1 Done 2026-05-04: replaced `stderr` diagnostics in `Backends.Vulkan.Device.cpp` with the project logger (`Core::Log::Warn`/`Core::Log::Error`). Audit: `Initialize`, `WriteBuffer`, `CreateTexture`, `WriteTexture`, `CreateSampler` (c613b1c, 5c84d9a, 7d056cf, 1a5c2be).
- [x] 2.2 Document the `BeginFrame`/`EndFrame` rotation invariant in `Backends.Vulkan.Device.cpp` (single-acquire pairing; `m_FrameSlot` is rotated in `EndFrame` based on the slot handed out in `BeginFrame`) (c613b1c).
- [x] 2.3 Either consume `m_NeedsResize` (in a future swapchain-recreate path) or remove the writes that set it from `Resize` and `SetPresentMode` until the consumer exists; today the writes are dead state (c613b1c).
- [x] 2.4 Replace removed helper declarations (`CreateInstance`, `CreateSurface`, `PickPhysicalDevice`, `CreateLogicalDevice`, `CreateVma`, `CreateSwapchain`, `RegisterSwapchainImages`, `CreatePerFrameResources`, `DestroyPerFrameResources`, `CreateGlobalPipelineLayout`, `CreateDefaultSampler`, `FindMemoryType`, `FindDepthFormat`, `SupportsFormat`) with a `// TODO(GRAPHICS-018):` comment block in `Backends.Vulkan.Device.cppm` listing the missing bring-up surface. Without breadcrumbs, future authors lose the contract (7e09ac5).
- [x] 2.5 In `DeferDelete`, replace the immediate-execution fallback for non-operational devices with an early `return`. A non-operational device cannot own real GPU resources and the immediate-fallback branch is unreachable in practice; if it ever is reached, immediate execution is wrong (7e09ac5).
- [x] 2.6 Add a header comment to `BeginOneShot`/`EndOneShot` that this submits to the graphics queue with a `vkQueueWaitIdle`, stalling the queue. Mark as "init-time only — runtime uploads must use `ITransferQueue`" (7e09ac5, 1a5c2be).
- [x] 2.7 Add a documented invariant to `Shutdown` describing the relationship between the deferred-deletion queue and the resource-pool drain: "any handle still present in `m_Buffers/m_Images/m_Samplers/m_Pipelines` has not been queued for destruction." Add a debug-only assertion that the pool entries do not also appear in the deletion queue (295363e).
- [x] 2.8 In `CreateSampler`, comment the intentional `m_SamplerAnisotropySupported = false` default and reference the device-feature negotiation slice that will set it (5c84d9a).
- [x] 2.9 Done via `tasks/archive/GRAPHICS-018S-sampler-border-color.md`: `RHI::SamplerDesc::BorderColor` now exists with an opaque-black default, and `CreateSampler` maps it through the Vulkan backend before any non-black border colors are relied on by renderer/material behavior.
- [x] 2.10 Document the 2D-array vs 3D vs cube interpretation of `RHI::TextureDesc::DepthOrArrayLayers` and the unused `Tex2D` array path inside `CreateTexture` (7d056cf).
- [x] 2.11 In `WriteTexture`, assert at entry that the destination usage includes a sampled bit (`(usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0`) since the function transitions to `SHADER_READ_ONLY_OPTIMAL`; otherwise log and skip (1a5c2be).
- [x] 2.12 In `WriteTexture`, tighten the size check from `dataSizeBytes < requiredBytes` to `dataSizeBytes == requiredBytes`, or document why slack is permitted (1a5c2be).
- [x] 2.13 In `WriteTexture`, do not update `image->CurrentLayout` to `SHADER_READ_ONLY_OPTIMAL` if `EndOneShot` reports a partial failure. Either thread an error return through `EndOneShot` or only set `CurrentLayout` after a verified submit (1a5c2be).
- [x] 2.14 Extend `FormatBlockByteSize` to handle `D24_UNORM_S8_UINT` / `D32_SFLOAT_S8_UINT` correctly (depth-stencil aspect upload), or explicitly reject depth-stencil uploads with a logged diagnostic instead of returning 0 silently (1a5c2be).
- [x] 2.15 Follow-up filed: `GRAPHICS-018T-texture-upload-batching`, referenced from `GRAPHICS-018` and `GRAPHICS-018Q`; timeline is before multi-mip/layer Vulkan texture smoke tests (1a5c2be).
- [x] 2.16 Done 2026-05-04: fallback services are reachable through `CreateVulkanDevice()` in configurations that build `ExtrinsicBackendsVulkan`; `Test.VulkanFailClosedContract.cpp` exercises constructor-only fail-closed behavior without GPU/swapchain creation, and fallback bindless allocation now emits a log breadcrumb plus `GetFallbackBindlessAllocationAttemptCount()` (7336e67).

### 3. Test methodology (origin: c613b1c, 7336e67, 7e09ac5, 5c84d9a, 7d056cf, 295363e, 1a5c2be)
- [x] 3.1 Done 2026-05-04: `Test.RendererRhiBoundary.cpp` now keeps only minimal symbol/linkage source guards; behavioral Vulkan fail-closed assertions live in `Test.VulkanFailClosedContract.cpp` when the Vulkan backend target is configured. Targets covered:
  - [x] Instantiate a `VulkanDevice` (constructor only — no `Initialize`), assert `IsOperational() == false`.
  - [x] With device non-operational, call `CreateBuffer({.SizeBytes = 64})`, `CreateTexture({.Width=1, .Height=1, ...})`, `CreateSampler({})`, `CreatePipeline({})` and assert each returns an empty handle.
  - [x] Call `GetBindlessHeap().AllocateTextureSlot({}, {})`, assert `kInvalidBindlessIndex`.
  - [x] Call `GetTransferQueue().UploadBuffer({}, nullptr, 0, 0)`, assert returned token is invalid and `IsComplete(token) == true`.
  - [x] Existing grep tests are reduced to one symbol-name assertion per promoted method rather than full-signature or implementation-body matches.
- [x] 3.2 Done 2026-05-04: `RendererFrameLifecycle.DepthPrepassPipelineFailureSkipsUnavailableCommandPass` covers unavailable depth-prepass accounting with name-keyed pass status.
- [x] 3.3 Done 2026-05-04: `RendererFrameLifecycle.CullingPipelineFailureSkipsRoutedCommandPassesUnavailable` covers unavailable culling accounting after item 1.5 settled on soft-fail policy.

### 4. Documentation and task hygiene (origin: docs commits c64bba3, 802f94c, 715fe6e, fd4a9cb, d068146, 252bd4e, 4aec951)
- [x] 4.1 Done 2026-05-04: `GRAPHICS-018` current slice is a bulleted status list.
- [x] 4.2 Done 2026-05-04: renderer/RHI behavior is canonical in `docs/architecture/graphics.md`; `src/graphics/vulkan/README.md` links there and keeps backend-specific details.
- [x] 4.3 Done 2026-05-04: `GRAPHICS-018Q` now separates "Blocks next operational Vulkan slice" from "Nonblocking clarifications and follow-ups".
- [x] 4.4 Done 2026-05-04: fail-closed shims are linked to `GRAPHICS-018R`, `GRAPHICS-018S`, and `GRAPHICS-018T` with owner context and timelines.

## Follow-up task IDs
- `GRAPHICS-018R-operational-transition`: blocking; owner context `runtime` + `graphics/renderer` + `graphics/vulkan`; timeline before any `GRAPHICS-018` slice marks Vulkan operational; removes/reconciles fallback bindless/transfer and non-operational renderer state shims.
- `GRAPHICS-018S-sampler-border-color`: completed in `tasks/done/`; owner context `graphics/rhi` + `graphics/vulkan`; landed before renderer/material behavior relies on non-black sampler border colors.
- `GRAPHICS-018T-texture-upload-batching`: nonblocking; owner context `graphics/vulkan` + `graphics/assets`; timeline before multi-mip/layer/cube Vulkan texture upload smoke tests.

### 5. Project conventions
- 5.1 Done 2026-05-04: `docs/architecture/graphics.md` and `src/graphics/vulkan/README.md` state that Vulkan implementation files must use `Core::Log::*`, not direct `stderr` writes. Reference from item 2.1; this slice also updated the direct `StagingBelt` diagnostics and routed `Vulkan.hpp` `VK_CHECK_*` macros through `Backends.Vulkan.DiagnosticsLogging.cpp`.

## Tests
- [x] All items above must keep `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` green.
- [x] Behavioral tests added under §3 must be labeled `contract;graphics` so they run in the default gate.
- [x] Any new opt-in Vulkan smoke test must be labeled `gpu;vulkan` per `AGENTS.md §7`.

## Docs
- [x] Each grouped patch must update the corresponding doc section: `docs/architecture/graphics.md` for renderer-side changes, `src/graphics/vulkan/README.md` for backend-side changes, and the `GRAPHICS-018` slice line if behavior visible at the architectural level shifts.
- [x] This task file itself counts as the planning artifact; no separate plan document.

## Acceptance criteria
- [x] All items in §1–§5 are either implemented or have a referenced follow-up task ID with a timeline.
- [x] The asymmetric pipeline-failure policy (item 1.5) is resolved one way or the other; no codebase still mixes assert and soft-fail for missing routed-pass pipelines.
- [x] The latent operational-transition gap (item 1.11) is either fixed or has a tracked follow-up that blocks the next `GRAPHICS-018` slice.
- [x] `Test.RendererRhiBoundary.cpp` no longer asserts on full method signatures (item 3.1); behavioral coverage is added for the fail-closed Vulkan paths.
- [x] `RenderGraphFrameStats` is split (item 1.9) and per-pass counters are name-keyed (item 1.10).
- [x] All `std::fprintf(stderr, ...)` in `src/graphics/vulkan/` are replaced with `Core::Log::*`.
- [x] Default CPU correctness gate remains green.
- [x] No new dependency edges; layering invariants from `AGENTS.md §2` preserved.

## Verification
Final evidence (2026-05-04):

```bash
# Default preset attempt documents the local toolchain issue.
cmake --preset ci
# Failed locally because clang-20/clang++-20 are not on PATH.

# Valid default CPU gate in the ci build tree with available C++23 Clang.
cmake --preset ci \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DINTRINSIC_HEADLESS_NO_GLFW=ON
cmake --build --preset ci --target IntrinsicTests -j2
ctest --test-dir build/ci --output-on-failure -R '^Renderer(RhiBoundary|FrameLifecycle)\.' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
# Result: 1559/1559 default CPU tests passed; 2 SLO tests skipped by test body.

# Constructor-only Vulkan fail-closed seam; no GPU/swapchain bring-up.
cmake -S . -B build/ci-vulkan-contract -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DINTRINSIC_BUILD_SANDBOX=OFF \
  -DINTRINSIC_BUILD_TESTS=ON \
  -DINTRINSIC_ENABLE_CUDA=OFF \
  -DINTRINSIC_ENABLE_SANITIZERS=ON \
  -DEXTRINSIC_BACKEND=Vulkan \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DINTRINSIC_OFFLINE_DEPS=ON \
  -DINTRINSIC_UPDATE_DEPS=OFF \
  -Dimguizmo_SOURCE_DIR=/home/alex/Documents/IntrinsicEngine/external/cache/imguizmo-src/src \
  -DFETCHCONTENT_SOURCE_DIR_IMGUIZMO=/home/alex/Documents/IntrinsicEngine/external/cache/imguizmo-src/src
cmake --build build/ci-vulkan-contract --target IntrinsicGraphicsVulkanContractTests ExtrinsicBackendsVulkan -j2
ctest --test-dir build/ci-vulkan-contract --output-on-failure -R '^VulkanFailClosedContract\.' --timeout 60
# Result: 1/1 Vulkan fail-closed contract test passed.

python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
# Result: no layering violations, 0 task findings, no broken relative links.
```

```bash
# Default CPU correctness gate.
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

# Focused contract tests for the touched seams.
ctest --test-dir build/ci --output-on-failure -R '^Renderer(RhiBoundary|FrameLifecycle)\.' --timeout 60

# Layering and task structural checks.
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .

# Optional Vulkan backend build sanity (only on a confirmed-current C++23 build tree).
set -o pipefail
cmake --build build/dev-clang-ninja --target ExtrinsicBackendsVulkan -j2 2>&1 \
  | tee /tmp/intrinsic-vulkan-backend-build.log | tail -n 120
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Introducing unrelated feature work.
- Making Vulkan mandatory for normal CI or CPU correctness gates.
- Expanding the active scope of `GRAPHICS-018`; this task is strictly plumbing follow-up.
- Adding new RHI surface methods without a corresponding doc and task entry.
