# ADR 0010 — Postprocess Chain Backend Policy

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics (`Extrinsic.Graphics.PostProcessSystem`)
- **Related tasks:** [`tasks/done/GRAPHICS-013A`](../../tasks/archive/GRAPHICS-013A-postprocess-chain.md), [`GRAPHICS-013AQ`](../../tasks/archive/GRAPHICS-013AQ-postprocess-backend-clarifications.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`docs/architecture/rendering-three-pass.md`](../architecture/rendering-three-pass.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `Extrinsic.Graphics.PostProcessSystem` bullet in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/archive/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0007](0007-picking-selection-and-outline.md) records the `Picking.Readback` drain pattern that the histogram readback in §2 reuses byte-identically.

## Context

`GRAPHICS-013A` established `Extrinsic.Graphics.PostProcessSystem` as the backend-agnostic HDR-to-LDR chain owner: `PostProcessPushConstants`, `PostProcessSettings`, frame-recipe transient resources (`SceneColorHDR`, `SceneColorLDR`, `PostProcess.BloomScratch`, `PostProcess.Histogram`, `PostProcess.AATemp`), and `PostProcessDiagnostics`.

`GRAPHICS-013AQ` answered four backend-shape questions that `GRAPHICS-013A` deferred:

1. What is the concrete shape of the bloom path (mip count, filter taps, scratch policy)?
2. How are the histogram bin layout, exposure-adaptation history, and diagnostics readback structured?
3. What does FXAA / SMAA backend execution look like, and how do their intermediates fit into the existing `PostProcess.AATemp` slot without adding new frame-recipe resources?
4. Which resources are retained (graphics-owned) vs frame-transient (framegraph-owned), and where do descriptor bindings live?

This ADR captures those answers as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical summary of the `Extrinsic.Graphics.PostProcessSystem` seam (HDR-to-LDR chain owner, frame-recipe transient `SceneColorHDR` / `SceneColorLDR` / `BloomScratch` / `Histogram` / `AATemp`, retained SMAA lookup textures + exposure-adaptation history) and retains a single pointer line to this ADR for the bloom/histogram/AA shape, the retained-vs-transient split, and the descriptor / binding ownership rules.

## Decision

### 1. Bloom backend shape and scratch policy

Bloom uses a **single** progressive down-/up-sample pyramid built into one half-resolution `R16G16B16A16_SFLOAT` mip-chain texture, declared as the frame-recipe transient `PostProcess.BloomScratch` resource.

- The pyramid truncates at the smallest mip whose extent is at least `8x8`.
- The maximum mip count is capped at **six** to bound the budget at 1080p+.

Filter taps:

- **Downsample:** 13-tap Karis-weighted partial-Karis-average filter at the first mip (firefly suppression); 4×4 box filter for subsequent mips.
- **Upsample:** 3×3 tent filter with additive blend back into the next-larger mip.
- **Final composite:** additive blend into `SceneColorHDR` scaled by `PostProcessSettings::BloomIntensity`.

**Backends do not allocate one scratch texture per mip.** They allocate the single frame-recipe `PostProcess.BloomScratch` texture and create per-mip subviews on demand, so the CPU/null contract continues to declare exactly one scratch resource. Bloom intermediates are frame-transient and may be aliased by the framegraph allocator across frames.

### 2. Histogram and exposure-adaptation policy

The histogram stage owns a fixed **256-bin** layout (matching `PostProcessSettings::HistogramBinCount` default) over a fixed log2-luminance range of `[-10, +10]` stops.

- `PostProcess.Histogram` is the frame-transient working buffer: a storage buffer of 256 `R32_UINT` bins plus a small header for accepted-sample count and clamped-min / clamped-max diagnostics. It is also the readback source for diagnostics.
- **Exposure-adaptation history is retained, not frame-transient.** `PostProcessSystem` owns a small graphics-owned storage buffer sized for `{previous_average_log_lum, adaptation_velocity, frame_index}` (a few tens of bytes total), allocated once during `PostProcessSystem::Initialize()` through `RHI::BufferManager` and freed at `Shutdown()`. The frame recipe imports it as a retained resource when histogram / exposure stages are enabled.

Diagnostics readback uses the same drain pattern as `Picking.Readback` ([ADR-0007](0007-picking-selection-and-outline.md) §2):

- The renderer copies the histogram bins into a host-visible staging buffer at frame-record time.
- The renderer consumes the staging buffer on the next `BeginFrame()` after the issuing frame's fences complete.
- The CPU/null backend simulates the same drain **without** Vulkan-specific code, so the correctness gate stays authoritative.

The diagnostics surface remains `PostProcessDiagnostics`. Backends never invent a parallel histogram-specific diagnostic struct.

### 3. Anti-aliasing backend policy

`FXAA` and `SMAA` are mutually exclusive per `PostProcessSettings::AntiAliasing`. Backends do not schedule both.

**FXAA:**

- Single fullscreen draw.
- Samples post-tonemap `SceneColorLDR` through one sampled-image binding plus a linear-clamp sampler.
- **No** lookup textures and **no** intermediate; `PostProcess.AATemp` is unused on the FXAA path.
- Quality preset encoded into the existing `PostProcessPushConstants::StageKind` packing space (as a preset enum index, **not** as new fields), so the CPU/null push-constant contract does not grow.

**SMAA:**

- Classic three-pass form: edge detection → blending-weight → neighborhood blending.
- Edge and blend intermediates are frame-transient and fold under the existing `PostProcess.AATemp` slot. The framegraph exposes them as two named subresources of `AATemp`:
  - `AATemp.Edges` `R8G8_UNORM`.
  - `AATemp.Weights` `R8G8B8A8_UNORM`.
- No new frame-recipe resource declaration is introduced and the CPU/null contract continues to gate `AATemp` behind the `EnablePostProcess` feature plus a non-`None` AA selection.
- SMAA `AreaTex` (`R8G8_UNORM`, 160×560) and `SearchTex` (`R8_UNORM`, 256×33) are **retained** graphics-owned static lookup textures allocated once during `PostProcessSystem::Initialize()` through `RHI::TextureManager`, kept for the lifetime of the system, and imported by the SMAA passes as retained resources.
- SMAA quality preset is encoded into `PostProcessPushConstants::StageKind` packing the same way as FXAA.

### 4. Descriptor and binding ownership

**Frame-recipe transient resources owned by the framegraph:**

- `SceneColorHDR`, `SceneColorLDR` (pipeline inputs/outputs).
- `PostProcess.BloomScratch`, `PostProcess.Histogram`, `PostProcess.AATemp` (working resources).

Postprocess passes obtain their RHI texture handles through the frame-recipe resource map, **never** through static descriptor sets. These working resources are aliasable across frames by the transient allocator.

**Retained resources owned by `PostProcessSystem`:**

- SMAA `AreaTex` lookup texture.
- SMAA `SearchTex` lookup texture.
- Exposure-adaptation history buffer.

They are allocated through `RHI::TextureManager` / `RHI::BufferManager` at `Initialize()`, released at `Shutdown()`, and imported into the frame graph by the passes that need them when their corresponding settings are enabled.

**Per-pass binding shape:**

- Each promoted postprocess pass uses **one** push-constant block (`PostProcessPushConstants`) and **one** pass-local descriptor set listing only the sampled-image inputs and (for compute stages) storage-buffer outputs it uses.
- Concrete `VkDescriptorSetLayout` bindings remain backend-local under `src/graphics/vulkan` and do not leak through RHI or renderer code.

**Backends never bypass `PostProcessSystem` to allocate retained postprocess state.** The CPU/null backend validates ownership through the same seam without Vulkan-specific code so the default CPU correctness gate remains authoritative.

## Consequences

Positive:

- The bloom budget is bounded (one scratch texture, ≤ 6 mips) and identical across backends.
- The 256-bin histogram + retained exposure-adaptation buffer means tone mapping converges deterministically across runs without per-backend variation in bin layout or smoothing.
- FXAA / SMAA fit into the existing single `PostProcess.AATemp` slot via named subresources; no new frame-recipe resource declaration is added.
- The retained vs frame-transient split is small and explicit: only SMAA lookups and the exposure-history buffer are retained by `PostProcessSystem`; everything else lives on the framegraph.
- Histogram readback reuses the `Picking.Readback` drain pattern, so the CPU/null contract validates the same diagnostics path as any GPU backend.

Trade-offs and risks:

- The 6-mip / `8x8` floor is a hard cap chosen for 1080p+; super-resolution scenes (8K+) will under-blur the largest-radius bloom relative to what they could afford. Lifting the cap requires re-evaluating budget — this ADR deliberately freezes the cap so backends do not silently tune it.
- The fixed `[-10, +10]` log2-luminance range trades range for bin density. Scenes whose physical luminance falls outside that range will clamp; the `PostProcessDiagnostics` clamped-min / clamped-max counters surface this but the range is not user-adjustable to keep the histogram comparable across runs.
- Encoding FXAA / SMAA quality presets into `PostProcessPushConstants::StageKind` packing means the packing space is shared across stages. Adding a future stage with many quality presets must re-check the packing budget before growing the enum range.
- Backend implementers must remember that `AATemp` is the only AA frame-transient slot. A naïve port that allocates separate edge / weight textures would silently regress alias-coverage; the named-subresource rule is the seam reviewers must enforce.

Follow-up tasks required: none from this ADR.

## Alternatives Considered

- **One scratch texture per bloom mip.** Rejected per §1: forces backends to allocate up to six separate textures and would diverge from the single frame-recipe `PostProcess.BloomScratch` declared by the CPU/null contract.
- **User-adjustable histogram range or bin count.** Rejected per §2: makes diagnostics non-comparable across runs and across backends; the fixed range / bin count is the comparability seam.
- **New frame-recipe resources for SMAA edge / weight intermediates.** Rejected per §3: would expand the frame-recipe surface and break the CPU/null contract's existing `AATemp` gate. Named subresources of `AATemp` fit without surface growth.
- **Per-stage descriptor sets stitched at runtime.** Rejected per §4: defeats the one-pass / one-descriptor-set rule and would expose backend descriptor layouts through renderer code.
- **Backend-private postprocess state outside `PostProcessSystem`.** Rejected per §4: would diverge from the CPU/null ownership seam and let backends drift on retained-resource lifetime.
- **Histogram readback through a backend-specific staging path.** Rejected per §2: the `Picking.Readback` drain is the established pattern for host-visible readback that the CPU/null backend can simulate without Vulkan code.

## Validation

- [`tasks/done/GRAPHICS-013A`](../../tasks/archive/GRAPHICS-013A-postprocess-chain.md) records the underlying `Extrinsic.Graphics.PostProcessSystem` contract, frame-recipe resource declarations, `PostProcessSettings` / `PostProcessPushConstants` shape, and `PostProcessDiagnostics` field set.
- [`tasks/done/GRAPHICS-013AQ`](../../tasks/archive/GRAPHICS-013AQ-postprocess-backend-clarifications.md) records the four clarification decisions captured in §§1–4.
- `docs/architecture/rendering-three-pass.md` carries the matching postprocess chain contract block authored by `GRAPHICS-013AQ`.
- `src/graphics/renderer/README.md` carries the matching ownership-contract bullet next to the existing `PostProcessSystem` line.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises the retained-vs-transient ownership, the histogram drain pattern, and the AATemp subresource gate without a Vulkan device.
