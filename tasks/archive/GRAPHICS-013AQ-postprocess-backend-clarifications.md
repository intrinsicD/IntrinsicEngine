# GRAPHICS-013AQ — Postprocess backend clarification follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-012Q` retirement cleared `tasks/active/`.
- Completed: 2026-05-06.
- Branch: `claude/setup-agentic-workflow-3OTyj`.
- Implementation commit: `5e0b97c` (resolve decisions and sync rendering-three-pass / graphics / renderer-README docs).
- Task-state commit: pending retirement commit (this commit moves the file from `tasks/active/` to `tasks/done/`).
- Resolution: decisions recorded below and consequential notes synced into `docs/architecture/rendering-three-pass.md` (postprocess chain contract block), `docs/architecture/graphics.md` (`PostProcessSystem` ownership bullet in the GPU scene ownership block), and `src/graphics/renderer/README.md` (matching ownership-contract bullet next to the existing `PostProcessSystem` line). The rendering backlog README entry for `GRAPHICS-013AQ` is redirected to the `tasks/done/` location by this retirement commit. Verification: `python3 tools/agents/check_task_policy.py --root . --strict` (75 task files validated, 0 findings) and `python3 tools/docs/check_doc_links.py --root .` (187 relative links, no broken links).

## Decisions
- **Bloom backend shape and scratch policy.** Bloom uses a single
  progressive down-/up-sample pyramid built into one half-resolution
  `R16G16B16A16_SFLOAT` mip-chain texture, declared as the
  frame-recipe transient `PostProcess.BloomScratch` resource. The
  pyramid truncates at the smallest mip whose extent is at least
  `8x8`; the maximum mip count is capped at six to bound the budget
  at 1080p+. The downsample stage uses a 13-tap Karis-weighted
  partial-Karis-average filter at the first mip (firefly suppression)
  and a 4x4 box filter for subsequent mips; the upsample stage uses a
  3x3 tent filter with additive blend back into the next-larger mip
  and the final additive blend into `SceneColorHDR` is scaled by
  `PostProcessSettings::BloomIntensity`. Backends do not allocate one
  scratch texture per mip: they allocate the single frame-recipe
  `PostProcess.BloomScratch` texture and create per-mip subviews on
  demand, so the CPU/null contract continues to declare exactly one
  scratch resource. Bloom intermediates are frame-transient and may
  be aliased by the framegraph allocator across frames.
- **Histogram and exposure-adaptation policy.** The histogram stage
  owns a fixed `256`-bin layout (matching
  `PostProcessSettings::HistogramBinCount` default) over a fixed
  log2-luminance range of `[-10, +10]` stops. `PostProcess.Histogram`
  is the frame-transient working buffer (storage buffer of `256`
  `R32_UINT` bins plus a small header for accepted-sample count and
  clamped-min/max diagnostics) and is also the readback source for
  diagnostics. Exposure-adaptation history is **retained**, not
  frame-transient: `PostProcessSystem` owns a small graphics-owned
  storage buffer (sized for `{previous_average_log_lum,
  adaptation_velocity, frame_index}`, a few tens of bytes total)
  allocated once during `PostProcessSystem::Initialize()` through
  `RHI::BufferManager` and freed at `Shutdown()`. The frame recipe
  imports it as a retained resource when histogram/exposure stages
  are enabled. Diagnostics readback uses the same drain pattern as
  `Picking.Readback` (`GRAPHICS-012Q`): the renderer copies the
  histogram bins into a host-visible staging buffer at frame-record
  time and consumes it on the next `BeginFrame()` after the issuing
  frame's fences complete; the CPU/null backend simulates the same
  drain without Vulkan-specific code so the correctness gate stays
  authoritative. Diagnostics surface remains
  `PostProcessDiagnostics`; backends never invent a parallel
  histogram-specific diagnostic struct.
- **Anti-aliasing backend policy.** `FXAA` is a single fullscreen
  draw that samples post-tonemap `SceneColorLDR` through one
  sampled-image binding plus a linear-clamp sampler; it has no
  lookup textures and no intermediate, and `PostProcess.AATemp` is
  unused on the FXAA path. Quality preset is encoded into the
  existing `PostProcessPushConstants::StageKind` packing space (as a
  preset enum index, not as new fields), so the CPU/null
  push-constant contract does not grow. `SMAA` uses the classic
  three-pass form (edge detection -> blending-weight -> neighborhood
  blending). Edge and blend intermediates are frame-transient and
  fold under the existing `PostProcess.AATemp` slot: the framegraph
  exposes them as two named subresources of `AATemp`
  (`AATemp.Edges` `R8G8_UNORM`, `AATemp.Weights` `R8G8B8A8_UNORM`),
  so no new frame-recipe resource declaration is introduced and the
  CPU/null contract continues to gate `AATemp` behind the
  `EnablePostProcess` feature plus a non-`None` AA selection. SMAA
  `AreaTex` (`R8G8_UNORM`, 160x560) and `SearchTex` (`R8_UNORM`,
  256x33) are **retained** graphics-owned static lookup textures
  allocated once during `PostProcessSystem::Initialize()` through
  `RHI::TextureManager`, kept for the lifetime of the system, and
  imported by the SMAA passes as retained resources. SMAA quality
  preset is encoded into `PostProcessPushConstants::StageKind`
  packing in the same way as FXAA. `FXAA` and `SMAA` remain mutually
  exclusive per `PostProcessSettings::AntiAliasing`; backends do not
  schedule both.
- **Descriptor and binding ownership.** `SceneColorHDR` and
  `SceneColorLDR` are frame-recipe transient resources owned by the
  framegraph; postprocess passes obtain their RHI texture handles
  through the frame-recipe resource map, never through static
  descriptor sets. `PostProcess.BloomScratch`, `PostProcess.Histogram`,
  and `PostProcess.AATemp` are frame-transient working resources
  owned by `PostProcessPass`, declared by the frame recipe, and
  aliasable across frames by the transient allocator. The retained
  resources owned by `PostProcessSystem` are exactly the SMAA
  `AreaTex`/`SearchTex` lookup textures and the exposure-adaptation
  history buffer; they are allocated through
  `RHI::TextureManager`/`RHI::BufferManager` at `Initialize()`,
  released at `Shutdown()`, and imported into the frame graph by the
  passes that need them when their corresponding settings are
  enabled. Each promoted postprocess pass uses one push-constant
  block (`PostProcessPushConstants`) and one pass-local descriptor
  set listing only the sampled-image inputs and (for compute stages)
  storage-buffer outputs it uses; concrete `VkDescriptorSetLayout`
  bindings remain backend-local under `src/graphics/vulkan` and do
  not leak through RHI or renderer code. Backends never bypass
  `PostProcessSystem` to allocate retained postprocess state, and
  the CPU/null backend validates ownership through the same seam
  without Vulkan-specific code so the default CPU correctness gate
  remains authoritative.

## Resolution
- Decisions recorded above and consequential notes synced into
  `docs/architecture/rendering-three-pass.md` (postprocess chain
  contract block), `docs/architecture/graphics.md` (GPU scene
  ownership `PostProcessSystem` bullet), and
  `src/graphics/renderer/README.md` (matching ownership-contract
  bullet next to the existing `PostProcessSystem` line). The
  rendering backlog README entry for `GRAPHICS-013AQ` is redirected
  to the `tasks/done/` location by the retirement commit.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify concrete backend/shader details that remain after the CPU/null `GRAPHICS-013A` postprocess chain contracts.

## Non-goals
- No C++ behavior changes.
- No debug-view, ImGui, or present/finalization policy work.
- No Vulkan-only implementation in this docs-only clarification task.

## Context
- `GRAPHICS-013A` established `PostProcessSystem` settings, deterministic stage ordering, diagnostics, push-constant packet data, explicit `SceneColorHDR` to `SceneColorLDR` frame-recipe resources, and CPU/mock command contracts for `Histogram`, `Bloom`, `ToneMap`, `FXAA`, and `SMAA` pass shims.
- Remaining questions affect concrete shader kernels, descriptor binding, temporal/exposure history, and backend resource strategies and should not be mixed with CPU/null contract work.

## Required changes
- [x] Clarify bloom implementation policy: downsample/upsample pyramid shape, scratch texture count, and how `PostProcess.BloomScratch` maps to concrete backend resources.
- [x] Clarify histogram/exposure policy: bin count, luminance range, adaptation history ownership, readback/diagnostics format, and whether history is frame-transient or retained.
- [x] Clarify anti-aliasing backend policy: `FXAA` and `SMAA` shader inputs, lookup textures, edge/blend intermediate ownership, and quality presets.
- [x] Clarify descriptor/binding ownership for `SceneColorHDR`, `SceneColorLDR`, postprocess intermediates, and any retained LUT/history resources.

## Tests
- [x] Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- [x] Update `docs/architecture/rendering-three-pass.md`, renderer docs, and backend notes with selected postprocess backend policies.

## Acceptance criteria
- [x] Vulkan/backend work can implement real postprocess effects without changing the CPU/null graphics contracts from `GRAPHICS-013A`.
- [x] Frame-transient versus retained postprocess resources are documented explicitly.
- [x] Debug-view, ImGui, and present policy remain out of scope.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Expanding into debug-view or ImGui/present ownership.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

