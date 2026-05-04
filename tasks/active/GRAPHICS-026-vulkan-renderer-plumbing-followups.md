# GRAPHICS-026 — Vulkan/renderer plumbing follow-ups

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Branch: `claude/review-graphics-commits-ZUXOW`.
- Activated: 2026-05-04 from review of recent `src/graphics/` commits (`550b0e9` … `c64bba3`).
- Source: `GRAPHICS-018` slice review (renderer frame lifecycle through RHI + promoted Vulkan fail-closed surface).
- Next verification step: run the default CPU correctness gate after each grouped patch lands; confirm no `gpu|vulkan` opt-in tests are required for any item below.

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
  - `tests/support/MockRHI.hpp`
- Each item below cites the originating commit hash from the review.

## Required changes

### 1. Renderer frame lifecycle (origin: 550b0e9, 6ce2cc8, 8f7aaff, c1f130e)
- 1.1 Document the `IRenderer::EndFrame` contract in `Graphics.Renderer.cppm`: clarify that the returned value is the device's post-`EndFrame` global frame counter, not the `frame.FrameIndex` of the just-completed frame (550b0e9).
- 1.2 Stop writing to `RenderGraphFrameStats.Diagnostic` from the `BeginFrame` "device missing" path; introduce a distinct field (e.g. `LifecycleDiagnostic`) or an enum status, since no graph has been compiled at that point (550b0e9).
- 1.3 Replace the magic determinant tolerance in `IsInvertibleFiniteMatrix` with a named constant (e.g. `kCameraInverseDeterminantEpsilon`) (6ce2cc8).
- 1.4 Add a defensive `Core::Log::Warn` when `passNameByIndex` resolution fails for a routed pass name during execute (6ce2cc8).
- 1.5 Resolve the asymmetric pipeline-failure policy: `CullingSystem::Initialize` currently asserts on pipeline-create failure while depth-prepass logs and soft-skips. Pick soft-fail end-to-end (preferred — surface through `RenderGraphFrameStats.CommandPassesSkippedUnavailable`) or hard-fail end-to-end. Update `CullingSystem::Initialize` accordingly and document the chosen policy in `docs/architecture/graphics.md` (6ce2cc8, 8f7aaff).
- 1.6 Make the depth-prepass dependency on `m_CullingInitialized` explicit: rename the guard (e.g. `IsCullingOutputAvailable()`) or add a comment explaining that depth-prepass consumes culling indirect output (8f7aaff).
- 1.7 Stop calling `m_DepthPrepassPass.SetPipeline(...)` both at `Initialize` and at every record. Pick one cache point (8f7aaff).
- 1.8 Export named constants from the renderer/RHI for `kMaxIndirectDrawCount` (currently `100000`) and the cull dispatch group sizing, and reference them from `Test.RendererFrameLifecycle.cpp` instead of hard-coded literals (8f7aaff, 6ce2cc8).
- 1.9 Split `RenderGraphFrameStats` into focused sub-structs (`CompileStats`, `ExecuteStats`, `CommandRecordStats`); keep `Diagnostic` and `DebugDump` at the top level. Adjust call sites and tests (c1f130e).
- 1.10 Promote per-pass command-recording counters to a name-keyed array (or `std::unordered_map<std::string_view, CommandRecordStatus>`) so adding new routed passes does not balloon the stats struct (c1f130e).
- 1.11 Add a documented "device-becomes-operational" reset hook on the renderer: today `m_CullingInitialized` and `MaterialSystem::GpuCapacity` are latched at `Initialize()`. When Vulkan transitions from non-operational to operational, the renderer must re-init culling, re-allocate the material GPU buffer, and reset bindless slot accounting. Either implement the hook now or file a `GRAPHICS-018X-operational-transition` follow-up and link it from `GRAPHICS-018` (6ce2cc8).
- 1.12 In `Graphics.MaterialSystem.cpp`, comment the non-operational shortcut explicitly (`return true` after CPU-side resize) and reference item 1.11 (6ce2cc8).

### 2. Vulkan device surface (origin: c613b1c, 7336e67, 7e09ac5, 5c84d9a, 7d056cf, 295363e, 1a5c2be)
- 2.1 Replace every `std::fprintf(stderr, ...)` in `Backends.Vulkan.Device.cpp` with the project logger (`Core::Log::Warn`/`Core::Log::Error`). Audit: `Initialize`, `CreateSampler`, `CreateTexture`, `WriteTexture` (c613b1c, 5c84d9a, 7d056cf, 1a5c2be).
- 2.2 Document the `BeginFrame`/`EndFrame` rotation invariant in `Backends.Vulkan.Device.cpp` (single-acquire pairing; `m_FrameSlot` is rotated in `EndFrame` based on the slot handed out in `BeginFrame`) (c613b1c).
- 2.3 Either consume `m_NeedsResize` (in a future swapchain-recreate path) or remove the writes that set it from `Resize` and `SetPresentMode` until the consumer exists; today the writes are dead state (c613b1c).
- 2.4 Replace removed helper declarations (`CreateInstance`, `CreateSurface`, `PickPhysicalDevice`, `CreateLogicalDevice`, `CreateVma`, `CreateSwapchain`, `RegisterSwapchainImages`, `CreatePerFrameResources`, `DestroyPerFrameResources`, `CreateGlobalPipelineLayout`, `CreateDefaultSampler`, `FindMemoryType`, `FindDepthFormat`, `SupportsFormat`) with a `// TODO(GRAPHICS-018):` comment block in `Backends.Vulkan.Device.cppm` listing the missing bring-up surface. Without breadcrumbs, future authors lose the contract (7e09ac5).
- 2.5 In `DeferDelete`, replace the immediate-execution fallback for non-operational devices with an early `return`. A non-operational device cannot own real GPU resources and the immediate-fallback branch is unreachable in practice; if it ever is reached, immediate execution is wrong (7e09ac5).
- 2.6 Add a header comment to `BeginOneShot`/`EndOneShot` that this submits to the graphics queue with a `vkQueueWaitIdle`, stalling the queue. Mark as "init-time only — runtime uploads must use `ITransferQueue`" (7e09ac5, 1a5c2be).
- 2.7 Add a documented invariant to `Shutdown` describing the relationship between the deferred-deletion queue and the resource-pool drain: "any handle still present in `m_Buffers/m_Images/m_Samplers/m_Pipelines` has not been queued for destruction." Add a debug-only assertion that the pool entries do not also appear in the deletion queue (295363e).
- 2.8 In `CreateSampler`, comment the intentional `m_SamplerAnisotropySupported = false` default and reference the device-feature negotiation slice that will set it (5c84d9a).
- 2.9 In `CreateSampler`, honor `RHI::SamplerDesc::BorderColor` if it exists; if not, file a `GRAPHICS-018X-sampler-border-color` follow-up. Today the border color is hard-coded `VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK` (5c84d9a).
- 2.10 Document the 2D-array vs 3D vs cube interpretation of `RHI::TextureDesc::DepthOrArrayLayers` and the unused `Tex2D` array path inside `CreateTexture` (7d056cf).
- 2.11 In `WriteTexture`, assert at entry that the destination usage includes a sampled bit (`(usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0`) since the function transitions to `SHADER_READ_ONLY_OPTIMAL`; otherwise log and skip (1a5c2be).
- 2.12 In `WriteTexture`, tighten the size check from `dataSizeBytes < requiredBytes` to `dataSizeBytes == requiredBytes`, or document why slack is permitted (1a5c2be).
- 2.13 In `WriteTexture`, do not update `image->CurrentLayout` to `SHADER_READ_ONLY_OPTIMAL` if `EndOneShot` reports a partial failure. Either thread an error return through `EndOneShot` or only set `CurrentLayout` after a verified submit (1a5c2be).
- 2.14 Extend `FormatBlockByteSize` to handle `D24_UNORM_S8_UINT` / `D32_SFLOAT_S8_UINT` correctly (depth-stencil aspect upload), or explicitly reject depth-stencil uploads with a logged diagnostic instead of returning 0 silently (1a5c2be).
- 2.15 File a follow-up for batched multi-mip/multi-layer texture uploads (`GRAPHICS-018X-texture-upload-batching`) and reference it from the `GRAPHICS-018` slice line (1a5c2be).
- 2.16 Move `FallbackBindlessHeap` and `FallbackTransferQueue` so they are at minimum reachable for testing (e.g. file-private namespace + factory used by both `VulkanDevice` and a unit test). Add a debug log + atomic counter on `FallbackBindlessHeap::AllocateTextureSlot` so silent-invalid-index returns leave a breadcrumb (7336e67).

### 3. Test methodology (origin: c613b1c, 7336e67, 7e09ac5, 5c84d9a, 7d056cf, 295363e, 1a5c2be)
- 3.1 Replace grep-on-source contract assertions in `Test.RendererRhiBoundary.cpp` with behavioral assertions where possible. Targets:
  - Instantiate a `VulkanDevice` (constructor only — no `Initialize`), assert `IsOperational() == false`.
  - With device non-operational, call `CreateBuffer({.SizeBytes = 64})`, `CreateTexture({.Width=1, .Height=1, ...})`, `CreateSampler({})`, `CreatePipeline({})` and assert each returns an empty handle.
  - Call `GetBindlessHeap().AllocateTextureSlot({}, {})`, assert `kInvalidBindlessIndex`.
  - Call `GetTransferQueue().UploadBuffer({}, nullptr, 0, 0)`, assert returned token is invalid and `IsComplete(token) == true`.
  - Keep the existing grep tests only for the "every promoted method is *defined*" link-time gate, and reduce them to one symbol-name assertion per method instead of full signature match.
- 3.2 Add a CPU-side renderer test exercising the `CommandPassesSkippedUnavailable` accounting: configure a `MockDevice` with `Operational = true` but a `MockPipelineManager` that fails `Create` for the depth-prepass path; assert `stats.CommandPassesSkippedUnavailable >= 1` and `stats.DepthPrepassCommandsRecorded == 0`.
- 3.3 Once item 1.5 settles failure policy for culling, add the corresponding `Skipped*` test for cull pipeline failure.

### 4. Documentation and task hygiene (origin: docs commits c64bba3, 802f94c, 715fe6e, fd4a9cb, d068146, 252bd4e, 4aec951)
- 4.1 Reformat the `GRAPHICS-018` "Current slice" line into a bulleted list (it is currently a 5-line run-on sentence) and trim duplicated phrasing.
- 4.2 Pick a single canonical home for the Vulkan promoted-surface story. Recommendation: keep architecture-level prose in `docs/architecture/graphics.md` and have `src/graphics/vulkan/README.md` link to it instead of duplicating. Remove duplicated paragraphs from one of the two.
- 4.3 In `tasks/backlog/rendering/GRAPHICS-018Q-vulkan-integration-clarifications.md`, split entries into two sections: "Blocks next slice" (shader packaging, dynamic-rendering attachment ownership) and "Nonblocking". The current "nonblocking" label is technically true but obscures that several entries gate the next bring-up slice.
- 4.4 For each fail-closed shim listed in `GRAPHICS-018` (fallback bindless heap, fallback transfer queue, empty-handle Create*), record an explicit removal task ID and timeline as required by the review checklist (`docs/agent/review-checklist.md` "Temporary shims") and `AGENTS.md §13`.

### 5. Project conventions
- 5.1 Add a one-line entry to `docs/architecture/graphics.md` (or the closest project-wide convention doc) stating that Vulkan TUs must use `Core::Log::*`, not `std::fprintf(stderr, ...)`. Reference from item 2.1.

## Tests
- All items above must keep `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` green.
- Behavioral tests added under §3 must be labeled `contract;graphics` so they run in the default gate.
- Any new opt-in Vulkan smoke test must be labeled `gpu;vulkan` per `AGENTS.md §7`.

## Docs
- Each grouped patch must update the corresponding doc section: `docs/architecture/graphics.md` for renderer-side changes, `src/graphics/vulkan/README.md` for backend-side changes, and the `GRAPHICS-018` slice line if behavior visible at the architectural level shifts.
- This task file itself counts as the planning artifact; no separate plan document.

## Acceptance criteria
- All items in §1–§5 are either implemented or have a referenced follow-up task ID with a timeline.
- The asymmetric pipeline-failure policy (item 1.5) is resolved one way or the other; no codebase still mixes assert and soft-fail for missing routed-pass pipelines.
- The latent operational-transition gap (item 1.11) is either fixed or has a tracked follow-up that blocks the next `GRAPHICS-018` slice.
- `Test.RendererRhiBoundary.cpp` no longer asserts on full method signatures (item 3.1); behavioral coverage is added for the fail-closed Vulkan paths.
- `RenderGraphFrameStats` is split (item 1.9) and per-pass counters are name-keyed (item 1.10).
- All `std::fprintf(stderr, ...)` in `src/graphics/vulkan/` are replaced with `Core::Log::*`.
- Default CPU correctness gate remains green.
- No new dependency edges; layering invariants from `AGENTS.md §2` preserved.

## Verification
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
