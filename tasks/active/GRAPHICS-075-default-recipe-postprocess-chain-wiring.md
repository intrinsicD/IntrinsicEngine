# GRAPHICS-075 — Default-recipe postprocess chain wiring (Histogram → Bloom → ToneMap → FXAA/SMAA)

## Status

- State: in-progress (Slice E.1 landed; Slice E.2 queued next).
  Maturity reached so far: Slice A closed `Scaffolded → CPUContracted`
  on the ToneMap leaf and the `"PostProcessPass"` umbrella executor
  branch (merged via PR #902); Slice B.1 added the bloom downsample +
  upsample pipeline leases + `RecordPostProcessBloomPass(...)`
  umbrella fan-out and closed `Scaffolded → CPUContracted` on both
  bloom pipelines; Slice B.2 added per-mip iteration + recipe-side
  `BloomScratch.MipLevels` clamping + the multi-mip barrier-sequence
  contract test (merged via PR #904) and closed bloom toward
  `Operational` on the CPU/null gate; Slice C added the FXAA pipeline
  + `RecordPostProcessFXAAPass(...)` umbrella fan-out +
  `PostProcessFXAAPushConstants` 20-byte std430 push block + contract
  tests and closed `Scaffolded → CPUContracted` on the FXAA leaf
  (merged via PR #906). Slice D.1 added the three SMAA pipelines
  (edge / blend / resolve) + their 16-byte std430 push blocks +
  `RecordPostProcessSMAAPass(...)` umbrella fan-out under the single
  `"PostProcessAAPass"` graph pass + contract tests and closed
  `Scaffolded → CPUContracted` on the SMAA pipeline-scaffold leaf
  (merged via PR #907; commits `ca60ac9` + `94c41f0`). Slice D.2a
  landed on branch
  `claude/intrinsicengine-agent-onboarding-32x02`: the AA umbrella
  splits into three ordered graph passes
  (`"PostProcessAA{Edge,Blend,Resolve}Pass"`), the recipe declares
  three matched-format `PostProcess.AATemp.{Edges,Weights,Resolved}`
  transients (`RG8_UNORM` / `RGBA8_UNORM` / backbuffer format), edge /
  blend pipeline descs are fixed at their matched formats,
  `PostProcessSMAAPass` exposes per-stage
  `Execute{Edge,Blend,Resolve}` methods, per-pass renderer helpers
  (`RecordPostProcessAA{Edge,Blend,Resolve}Pass`) route FXAA under the
  resolve pass only and SMAA across all three, and `presentSource`
  flips to `PostProcess.AATemp.Resolved` when
  `FrameRecipeFeatures::EnableAntiAliasing` is set (renderer plumbs
  this from `PostProcessSettings::AntiAliasing != None`). Slice D.2a
  closes `CPUContracted → Operational` on the SMAA pipeline leaf for
  the CPU/null gate; the retained LUT side stays with D.2b. Slice
  D.2b landed on branch
  `claude/intrinsicengine-agent-onboarding-BQgHn`: the new device-aware
  `PostProcessSystem::Initialize(device, textureMgr, bufferMgr)`
  overload allocates + uploads the SMAA `AreaTex` (`RG8_UNORM`,
  160×560) and `SearchTex` (`R8_UNORM`, 66×33) LUT textures via the
  transfer queue (LUT bytes ported byte-for-byte from
  `src/legacy/Graphics/Passes/Graphics.SMAALookupTextures.hpp` into a
  private namespace in `Graphics.PostProcessSystem.cpp` so promoted
  `graphics/renderer` never imports from `src/legacy`) and the
  exposure-adaptation history buffer (new `PostProcessExposureHistory`
  POD, `Storage | TransferDst`). The overload is idempotent and is
  invoked from both the renderer's `Initialize(device)` and its
  `RebuildOperationalResources(device)` so a non-operational-at-init
  device still picks up the allocation when it becomes operational.
  `PostProcessSystem::Shutdown()` releases the three leases before
  clearing the manager pointers (matching the `ShadowSystem` teardown
  ordering contract). New CPU/null contract test
  `PostProcessSMAALookupTexturesSurviveOperationalRebuild` pins
  handle identity + payload sizes across rebuild and asserts
  Shutdown() releases the leases. Slice D.2b closes the retained-
  resource side of the SMAA leaf on the CPU/null gate (the
  descriptor-set wiring of these LUTs into the SMAA blend pass body
  is a separate GPU/Vulkan-gate concern owned by the opt-in
  `gpu;vulkan` smoke).
- Owner/agent: local agent workflow.
- Branch: Slice D.2a on `claude/intrinsicengine-agent-onboarding-32x02`;
  Slice D.2b on `claude/intrinsicengine-agent-onboarding-BQgHn`.
- Activated: 2026-05-21 — first unblocked Theme A default-recipe leaf
  after GRAPHICS-074 retirement.
- Next verification step: open Slice E.2 (host-visible
  `Histogram.Readback` buffer + `BeginFrame()`-side readback drain
  mirroring the `Picking.Readback` pattern +
  `PostProcessSystem::PublishHistogramReadback(...)` + exposure-
  adaptation history buffer consumption) in a follow-up session.
  Slice E.1 (compute pipeline scaffold + `RecordPostProcessHistogramPass`
  + new ordered `"PostProcessHistogramPass"` graph pass + push-constant
  block) has landed on
  `claude/gallant-hypatia-a2puc` and closed
  `Scaffolded → CPUContracted` on the histogram leaf for the CPU/null
  gate.

## Slice plan

The full scope (8 pipelines across 5 stage families + retained
`AreaTex`/`SearchTex` LUTs + exposure-adaptation history buffer + recipe-side
`PostProcess.*` transient resource declarations + executor fan-out +
histogram readback drain + 6 contract tests) does not fit a single
reviewable patch. The plan mirrors the GRAPHICS-070/072/074 slice shape —
each slice preserves the CPU/null gate and only the histogram slice exercises
a readback drain.

- **Slice A (this slice).** ToneMap pipeline + `"PostProcessPass"` umbrella
  executor branch routing through `RecordPostProcessToneMapPass(...)`.
  `NullRenderer` owns `m_PostProcessToneMapPass` +
  `m_PostProcessToneMapPipelineLease` (constructed/reset alongside the
  existing GRAPHICS-070..074 passes); the pipeline is created in
  `InitializeOperationalPassResources(device)` from
  `BuildPostProcessToneMapPipelineDesc()` and republished byte-identical
  across `RebuildOperationalResources()`. `IRenderer` exposes
  `GetPostProcessToneMapPipeline()` / `GetPostProcessToneMapPipelineDesc()`.
  The fullscreen `Bind/Push/Draw(3,1,0,0)` shape matches the existing
  `PostProcessToneMapPass::Execute` body unchanged. The umbrella
  `"PostProcessPass"` keeps its recipe-level shape (Bloom/Histogram/AATemp
  transient writes remain declared); subsequent slices add the bloom /
  histogram / AA pipelines + `RecordPostProcess*Pass(...)` helpers behind
  the same umbrella executor branch (mirroring how GRAPHICS-074's
  `"PickingPass"` fans out to four selection-ID helpers). Defers
  Bloom/Histogram/FXAA/SMAA pipelines, retained SMAA LUT textures +
  exposure-history buffer, recipe-side `PostProcess.AATemp.{Edges,Weights}`
  rename + bloom mip-chain shape, and the histogram readback drain to
  Slices B–E.
- **Slice B.** Bloom downsample + upsample pipelines +
  `RecordPostProcessBloomPass(...)` helper + recipe-side
  `PostProcess.BloomScratch` mip-chain declaration. Pipelines created via
  `BuildPostProcessBloomDownsamplePipelineDesc` /
  `BuildPostProcessBloomUpsamplePipelineDesc`. Bloom-mip
  `ColorAttachment → ShaderRead → ColorAttachment` barrier-sequence
  contract test landed. Slice B splits into:
    - **Slice B.1 (this slice).** Two bloom pipelines (downsample +
      upsample) + leases + accessors + per-shader push-constant blocks
      (`PostProcessBloomDownsamplePushConstants`,
      `PostProcessBloomUpsamplePushConstants`) mirroring
      `post_bloom_downsample.frag` / `post_bloom_upsample.frag` byte-for-
      byte. `PostProcessBloomPass` reshapes to hold both pipelines.
      `RecordPostProcessBloomPass(...)` fans out behind the
      `"PostProcessPass"` umbrella ahead of `RecordPostProcessToneMapPass`.
      Contract test `PostProcessBloomPipelinesSurviveOperationalRebuild`.
      Defers per-mip iteration, `BloomScratch.MipLevels` recipe-side
      change, and the multi-mip barrier-sequence test to Slice B.2.
    - **Slice B.2.** Recipe-side `BloomScratch` `MipLevels = 6` declaration
      + per-mip iteration inside `PostProcessBloomPass::Execute` (down +
      up mip chain with inline `ColorAttachment → ShaderRead →
      ColorAttachment` barriers) + the multi-mip barrier-sequence contract
      test.
- **Slice C (this slice).** FXAA pipeline +
  `RecordPostProcessFXAAPass(...)` helper running in its own ordered
  graph pass (`"PostProcessAAPass"`) declared by the recipe with
  `Read(SceneColorLDR, ShaderRead) + Write(PostProcess.AATemp,
  ColorAttachmentWrite)` so the framegraph compiler emits the
  `SceneColorLDR ColorAttachment → ShaderRead` transition between the
  `PostProcessPass` umbrella render-pass scope (bloom + tonemap) and
  the FXAA umbrella scope. Sharing the `PostProcessPass` umbrella with
  the tonemap leg (Slice C's first attempt) was reverted because it
  aliased the umbrella's own color attachment as a sampled image mid-
  render-pass — Vulkan's classic read-after-write feedback hazard.
  `presentSource` stays on `SceneColorLDR`; flipping present routing
  to consume `PostProcess.AATemp` (or its Slice D
  `AATemp.{Edges,Weights}` siblings) when AA is enabled is Slice D's
  recipe-level change. `NullRenderer` owns `m_PostProcessFXAAPass` +
  `m_PostProcessFXAAPipelineLease` (constructed alongside the existing
  bloom + tonemap passes, reset in `Shutdown()` before
  `m_PostProcessSystem` is reset). The pipeline is created in
  `InitializeOperationalPassResources(device)` from
  `BuildPostProcessFXAAPipelineDesc(m_BackbufferFormat)` (vertex
  `post_fullscreen.vert.spv` + fragment `post_fxaa.frag.spv`, single
  backbuffer-format color target, no depth) and republished byte-
  identical across `RebuildOperationalResources()`. `IRenderer`
  exposes `GetPostProcessFXAAPipeline()` /
  `GetPostProcessFXAAPipelineDesc()`. Pass-local
  `PostProcessFXAAPushConstants` (20 bytes: `vec2 InvResolution + float
  ContrastThreshold + float RelativeThreshold + float SubpixelBlending`)
  mirrors the shader's std430 push block byte-for-byte; the canonical
  20-byte `PostProcessPushConstants` block is intentionally not reused
  even though the wire size matches — under std430 it would alias
  `Exposure`/`Gamma`/`BloomIntensity`/`HistogramBinCount`/`StageKind`
  onto `InvResolution`/`ContrastThreshold`/`RelativeThreshold`/
  `SubpixelBlending` and produce visually-meaningless FXAA output. The
  pass body short-circuits when `IsStageEnabled(FXAA)` is false
  (`AntiAliasing == None` or `SMAA`), but the helper still returns
  `Recorded` under the `"PostProcessAAPass"` accumulator per the same
  "structurally-recorded no-op" taxonomy the bloom helper follows when
  `EnableBloom = false`. Adds `FrameRecipePassKind::PostProcessAA` so
  the introspection table tracks the new pass slot. Defers SMAA
  pipelines + retained LUTs + recipe-side
  `PostProcess.AATemp.{Edges,Weights}` rename + `presentSource = AATemp`
  flip when AA is enabled to Slice D, and Histogram compute + readback
  drain to Slice E.
- **Slice D.** SMAA edge/blend/resolve pipelines + retained `AreaTex`
  (`R8G8_UNORM`, 160×560) + `SearchTex` (`R8_UNORM`, 66×33) LUT textures +
  exposure-adaptation history buffer allocated via a device-aware
  `PostProcessSystem::Initialize(device)` overload + `Shutdown()` releases.
  `AntiAliasing == SMAA` enables the branch; mutually exclusive with FXAA.
  Survive-rebuild test for the retained LUTs. The full scope exceeds a
  single reviewable patch, so Slice D splits the same way Slice B did
  (B.1 = pipeline scaffolding; B.2 = retained / per-mip behaviour):
    - **Slice D.1 (this slice).** Three SMAA pipelines (edge / blend /
      resolve) + per-shader push-constant blocks
      (`PostProcessSMAAEdgePushConstants`,
      `PostProcessSMAABlendPushConstants`,
      `PostProcessSMAAResolvePushConstants`, each 16 bytes mirroring
      `post_smaa_{edge,blend,resolve}.frag` std430 byte-for-byte) +
      leases + `IRenderer` accessors. All three pipelines target the
      *current* `PostProcess.AATemp` recipe attachment (allocated with
      `FrameRecipeSizing::BackbufferFormat`) so the AA umbrella render
      pass stays format-compatible with the bound pipelines on Vulkan;
      Slice D.2's `AATemp.{Edges,Weights}` split is what retargets edge
      to `RG8_UNORM` and blend to `RGBA8_UNORM`. `PostProcessSMAAPass`
      reshapes to hold three pipelines (`SetEdgePipeline` /
      `SetBlendPipeline` / `SetResolvePipeline`) and `Execute(...)`
      records three Bind/Push/Draw triples when `AntiAliasing == SMAA`,
      mirroring the bloom helper's per-stage early-skip on individual
      pipeline `IsValid()` so a partial outage still records the
      surviving stages. `RecordPostProcessSMAAPass(...)` fans out
      behind the existing `"PostProcessAAPass"` umbrella alongside
      FXAA (mutually exclusive per `PostProcessSettings::AntiAliasing`,
      with both pass bodies' `IsStageEnabled` gate enforcing the
      selector so both helpers can run unconditionally and only the
      active stage emits bind/push/draw). Contract tests:
      `PostProcessSMAAPipelinesSurviveOperationalRebuild`,
      `SMAAPushFeedsNonZeroInvResolutionForAllStages`,
      `SMAASkipsWhenAntiAliasingNotSMAA`,
      `SMAARecordsPerStageIndependently`. Defers retained `AreaTex`/
      `SearchTex` allocation, exposure-adaptation history buffer,
      recipe-side `PostProcess.AATemp.{Edges,Weights}` rename (and the
      edge/blend pipeline retargeting to `RG8_UNORM`/`RGBA8_UNORM`), and
      the survive-rebuild contract test for the retained LUTs to
      Slice D.2.
    - **Slice D.2 design note (open before D.2a / D.2b).** The original
      Slice D.2 plan said the edge / blend pipeline builders "flip from
      the Slice D.1 backbuffer-format target to the matching
      split-resource format in the same patch so the AA umbrella render
      pass stays format-compatible across the transition." But the
      current `"PostProcessAAPass"` umbrella declares a single color
      attachment (`builder.Write(postProcessAATemp,
      ColorAttachmentWrite)` in
      `BuildAndCompileDefaultFrameGraph`), and both `RecordPostProcessFXAAPass`
      and `RecordPostProcessSMAAPass` record bind/push/draw on that
      single attachment. Once edge / blend / resolve target three
      different formats (`RG8_UNORM` / `RGBA8_UNORM` / backbuffer), a
      single render-pass scope is no longer format-compatible with all
      three pipelines — the AA umbrella has to be restructured. The
      recommended option (smallest blast radius against the framegraph's
      flat resource model) is to split the AA umbrella into three
      ordered graph passes (`PostProcessAAEdgePass`,
      `PostProcessAABlendPass`, `PostProcessAAResolvePass`), each
      declaring a single `Write` of the matching format. FXAA is
      single-pass and rides on the resolve pass only (edge / blend pass
      bodies short-circuit when `AntiAliasing == FXAA`, same
      "structurally-recorded no-op" taxonomy SMAA already uses for the
      stage-disabled case). SMAA fans out across all three passes. The
      resolve pass writes `PostProcess.AATemp.Resolved` (backbuffer
      format), which `presentSource` flips to when AA is enabled. This
      restructuring is owned by Slice D.2a so D.2b can focus on the
      retained-resource scope without coupling architectural and
      LUT-port work in the same patch.
    - **Slice D.2a.** AA umbrella restructuring + recipe rename +
      edge / blend pipeline format flip + contract tests.
        - Recipe-side: `BuildDefaultFrameRecipe` replaces the
          `PostProcess.AATemp` resource declaration + the single
          `"PostProcessAAPass"` pass with three resources
          (`PostProcess.AATemp.Edges` `RG8_UNORM`,
          `PostProcess.AATemp.Weights` `RGBA8_UNORM`,
          `PostProcess.AATemp.Resolved` backbuffer format) and three
          ordered passes (`"PostProcessAAEdgePass"`,
          `"PostProcessAABlendPass"`, `"PostProcessAAResolvePass"`).
          `FrameRecipeResourceKind::PostProcessAATemp` is replaced by
          `PostProcessAATempEdges` / `PostProcessAATempWeights` /
          `PostProcessAATempResolved`;
          `FrameRecipePassKind::PostProcessAA` is replaced by
          `PostProcessAAEdge` / `PostProcessAABlend` /
          `PostProcessAAResolve`.
          `BuildAndCompileDefaultFrameGraph` creates all three new
          transients (the edge / blend variants at their flipped
          formats, the resolved variant at `sizing.BackbufferFormat`)
          and declares the matching `Read` / `Write` edges per pass.
          `presentSource` flips to `PostProcess.AATemp.Resolved` when
          `AntiAliasing != None` (the recipe-level present routing
          flip the historic Slice D plan called out).
        - Pipeline format flip: `BuildPostProcessSMAAEdgePipelineDesc`
          takes a fixed `RG8_UNORM` color target;
          `BuildPostProcessSMAABlendPipelineDesc` takes a fixed
          `RGBA8_UNORM` color target;
          `BuildPostProcessSMAAResolvePipelineDesc` keeps
          `m_BackbufferFormat`.
          `BuildPostProcessFXAAPipelineDesc` also keeps
          `m_BackbufferFormat` (FXAA rides on the resolve pass).
        - Executor / pass body: the existing
          `RecordPostProcessFXAAPass` / `RecordPostProcessSMAAPass`
          helpers are sliced into per-stage helpers
          (`RecordPostProcessAAEdgePass`,
          `RecordPostProcessAABlendPass`,
          `RecordPostProcessAAResolvePass`) so each ordered graph
          pass's executor branch routes to the correct subset. FXAA
          records under the resolve pass only; SMAA records under all
          three. The `IsStageEnabled` gate stays per pass body.
        - Contract tests update: `Test.RendererFrameLifecycle.cpp`
          recorded-pass counters bump from 8/7/3 to 10/9/5 and
          `kRoutedPasses` adds the three new pass names (replacing
          `"PostProcessAAPass"`). `FrameRecipeContract.DefaultRecipeBuildsCanonicalPassOrder`
          adds the three new pass names between `"PostProcessPass"` and
          `"ImGuiPass"`. The Slice D.1 SMAA contract tests pinning
          `RG8_UNORM` / `RGBA8_UNORM` formats in
          `PostProcessSMAAPipelinesSurviveOperationalRebuild` move from
          aspirational (currently asserting backbuffer format on all
          three) to authoritative. New
          `FrameRecipeSplitsAAUmbrellaPerStage` asserts the three
          ordered passes' `Read` / `Write` declarations and that no
          single graph pass writes two color attachments with
          incompatible formats.
        - Defers retained `AreaTex` / `SearchTex` LUT textures,
          exposure-adaptation history buffer, device-aware
          `PostProcessSystem::Initialize(device)` overload, `Shutdown()`
          release of retained resources, and the survive-rebuild
          contract test to Slice D.2b.
    - **Slice D.2b.** Retained `AreaTex` (`R8G8_UNORM`, 160×560) +
      `SearchTex` (`R8_UNORM`, 66×33) LUT textures + exposure-adaptation
      history buffer allocated via the device-aware
      `PostProcessSystem::Initialize(device)` overload (LUT bytes ported
      from `src/legacy/Graphics/Passes/Graphics.SMAALookupTextures.hpp`,
      uploaded via `IDevice::GetTransferQueue().UploadTexture()`).
      `Shutdown()` releases the retained resources. Survive-rebuild
      contract test `PostProcessSMAALookupTexturesSurviveOperationalRebuild`
      asserts both LUT handles and dimensions survive
      `RebuildGpuResources()` byte-identical. Because the recipe
      rename + edge / blend format flip already landed in Slice D.2a,
      Slice D.2b stays focused on retained-resource ownership and the
      LUT byte port without touching the framegraph or the pass-body
      pipeline-format contracts.

      Note: the historic Slice D description carried "256×33" for
      `SearchTex` in error — the SMAA reference and
      `assets/shaders/post_smaa_blend.frag` both define
      `SMAA_SEARCHTEX_SIZE = vec2(66.0, 33.0)`, and the legacy generator
      in `src/legacy/Graphics/Passes/Graphics.SMAALookupTextures.hpp`
      emits `66×33` bytes. The corrected dimensions are 66×33.
- **Slice E.** Histogram compute pipeline + dispatch + readback drain.
  Splits the same way Slice D did because the dispatch infrastructure
  and the readback drain are independently reviewable, and the dispatch
  has a structural prerequisite (the histogram is a *compute* dispatch
  and so cannot share the existing `"PostProcessPass"` umbrella render-
  pass scope — Vulkan forbids `vkCmdDispatch` inside an active render
  pass; the same constraint Slice D.2a hit in reverse when it had to
  split the AA umbrella to keep matched-format color attachments per
  render pass). The histogram therefore lives in its own ordered graph
  pass `"PostProcessHistogramPass"` declared with
  `Read(SceneColorHDR, ShaderRead) + Write(PostProcess.Histogram,
  BufferUsage::ShaderWrite)` before `"PostProcessPass"`; the umbrella
  pass drops its prior `Write(PostProcess.Histogram, ...)` declaration
  since the histogram is no longer fanned out under it.
    - **Slice E.1 (this slice).** Recipe-side rename — replace the
      `"PostProcess.Histogram"` write inside the `"PostProcessPass"`
      umbrella declaration with a new ordered graph pass
      `"PostProcessHistogramPass"` (ordered after `"PointPass"` and
      before `"PostProcessPass"`) with `Read(SceneColorHDR, ShaderRead)`
      + `Write(PostProcess.Histogram, BufferUsage::ShaderWrite)`. Add
      `FrameRecipePassKind::PostProcessHistogram`. Histogram compute
      pipeline (`post_histogram.comp`), pass-local 16-byte std430
      `PostProcessHistogramPushConstants` block (`uint Width + uint
      Height + float MinLogLum + float RangeLogLum`), and
      `BuildPostProcessHistogramPushConstants(settings, width, height)`
      that defaults the log-luminance range to `[-10, +10]` stops per
      GRAPHICS-013AQ until Slice E.2's exposure adaptation overrides
      the bounds. `NullRenderer` owns `m_PostProcessHistogramPass` +
      `m_PostProcessHistogramPipelineLease` (constructed alongside the
      existing GRAPHICS-070..075 SMAA pass, reset in `Shutdown()`
      before `m_PostProcessSystem` is reset). Pipeline created in
      `InitializeOperationalPassResources(device)` from
      `BuildPostProcessHistogramPipelineDesc()` (compute pipeline,
      `ComputeShaderPath = post_histogram.comp.spv`, `PushConstantSize
      = sizeof(PostProcessHistogramPushConstants)`) and republished
      byte-identical across `RebuildOperationalResources()`. `IRenderer`
      exposes `GetPostProcessHistogramPipeline()` /
      `GetPostProcessHistogramPipelineDesc()`. `RecordPostProcessHistogramPass(...)`
      helper routes the new `"PostProcessHistogramPass"` executor
      branch under the standard `SkippedNonOperational` /
      `SkippedUnavailable` / `Recorded` taxonomy and calls
      `m_PostProcessHistogramPass->SetViewport(width, height)` before
      `Execute(...)` so the dispatch shape (`ceil(W/16) x ceil(H/16) x 1`)
      tracks the backbuffer extent. Pass body short-circuits when
      `IsStageEnabled(Histogram)` is false (i.e. `EnableHistogram =
      false`); the helper still returns `Recorded` per the standing
      "structurally-recorded no-op" taxonomy bloom / FXAA / SMAA use.
      Contract tests:
      `PostProcessHistogramPipelineSurvivesOperationalRebuild` (pin
      the pipeline desc + survive-rebuild),
      `FrameRecipeDeclaresPostProcessHistogramAsOrderedPass` (pin the
      recipe-side split out of `"PostProcessPass"`), and the
      `DefaultRecipeBuildsCanonicalPassOrder` /
      `UsesDeviceFrameLifecycleBackbufferAndCommandContext` (+ rebuild
      / depth-failure / culling-failure variants) updates that move
      `"PostProcessHistogramPass"` into the canonical pass-name list
      and bump the recorded-pass counters from 10/10/9/5 to 11/11/10/6.
      Defers the host-visible `Histogram.Readback` buffer +
      `BeginFrame()`-side readback drain + `PublishHistogramReadback`
      + exposure-adaptation history buffer consumption to Slice E.2.
    - **Slice E.2.** Renderer-owned host-visible `Histogram.Readback`
      buffer (`1024 * frames-in-flight` bytes, `HostVisible | TransferDst`)
      allocated alongside the existing `Picking.Readback` buffer in
      `InitializeOperationalPassResources(device)`; recipe imports it
      via a new `FrameRecipeImports::HistogramReadback` field with
      `BufferState::TransferDst → BufferState::HostReadback`. The
      executor records a `CopyBuffer(PostProcess.Histogram →
      Histogram.Readback @ slot * 1024)` after the histogram dispatch.
      `BeginFrame()` drains completed slots (per-slot `Pending` /
      `IssuedFrame` / `Invalidated` metadata mirroring the
      `Picking.Readback` drain Slice D.3 landed for GRAPHICS-074) and
      forwards the 256-bin payload to
      `PostProcessSystem::PublishHistogramReadback(...)`, which
      updates the retained `PostProcessExposureHistory` storage buffer
      (allocated by Slice D.2b) via a `CopyBuffer(staging →
      ExposureHistory)`. Adds a `HistogramReadbackCopyCount` stat
      counter to `RenderGraphFrameStats` mirroring the existing
      `PickingReadbackCopyCount`. Contract tests:
      `HistogramReadbackBufferSurvivesOperationalRebuild`,
      `HistogramReadbackDrainPublishesEachSlotExactlyOnce`,
      `PublishHistogramReadbackUpdatesExposureHistory`.

## Goal
- Wire the full postprocess chain into the renderer executor under the default recipe per `GRAPHICS-013A`/`013AQ`: pipelines for histogram (compute), bloom downsample/upsample mip chain, tonemap, FXAA, and SMAA edge/blend/resolve created at renderer init; `PostProcessSystem` allocates the retained `AreaTex`/`SearchTex` LUT textures and the exposure-adaptation history buffer; transient `PostProcess.BloomScratch`, `PostProcess.Histogram`, `PostProcess.AATemp` declared by the recipe; executor routes each pass through the recorded `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.

## Non-goals
- No TAA (`GRAPHICS-040` planning).
- No upscaler / `IReconstructor` seam (`GRAPHICS-040`).
- No new postprocess shaders; reuses `assets/shaders/post_*.{frag,comp}` per `GRAPHICS-013A` / `013AQ`.
- No recipe-feature-gate change beyond exposing the existing `PostProcessSettings::AntiAliasing` selector.

## Context
- Status: see canonical [Status](#status) section above (Slices A / B.1 / B.2 / C / D.1 merged; Slice D.2 split into D.2a + D.2b; Slice E queued).
- Owner/layer: `graphics/renderer` (pipelines + executor routes), `PostProcessSystem` (retained resources).
- Planning anchors: `tasks/done/GRAPHICS-013A-postprocess-chain.md`, `tasks/done/GRAPHICS-013AQ-postprocess-backend-clarifications.md`.
- Today: `Pass.PostProcess.{Histogram,Bloom,ToneMap,FXAA,SMAA}.cpp` exist as shells; `PostProcessSystem` exists with settings/diagnostics/push-constant data but no GPU resources allocated; the executor lambda has no postprocess branches.
- Bloom: one frame-transient `PostProcess.BloomScratch` mip-chain texture (≤ 6 mips). Histogram: 256-bin layout over `[-10, +10]` log2 stops; readback via `Picking.Readback` drain pattern (mirror per `GRAPHICS-013AQ`). FXAA/SMAA mutually exclusive per `PostProcessSettings::AntiAliasing`.

## Maturity

- Target: `Operational` on the CPU/null gate for all five stage families
  (the GPU/Vulkan `Operational` claim is gated by the standing opt-in
  `gpu;vulkan` smoke and is intentionally out-of-scope here).
- Slice A closes `Scaffolded → CPUContracted` on the ToneMap leaf and the
  `"PostProcessPass"` umbrella executor branch; full `Operational` is owned
  by Slices B–E.
- Slice B.1 closes `Scaffolded → CPUContracted` on the Bloom downsample +
  upsample pipelines + the `RecordPostProcessBloomPass(...)` umbrella fan-
  out; Slice B.2 lifts the bloom leaf toward `Operational` on the CPU/null
  gate by adding per-mip iteration over the bloom pyramid (capped at
  `kBloomMipChainLevels = 6`, clamped down for small viewports via
  `ComputeBloomMipChainLevels` so Vulkan's `mipLevels <= floor(log2(maxDim)) + 1`
  rule holds) + recipe-side `BloomScratch.MipLevels` storage + renderer-
  side per-frame `PostProcess.BloomScratch` handle + mip-count
  republish. Per-mip subresource barriers (interleaved `ColorAttachment ↔
  ShaderRead` transitions between mip iterations) are *deferred* to a
  follow-up slice because they need both an
  `ICommandContext::TextureBarrier(handle, mipRange, ...)` RHI
  extension and per-mip render-pass restarts; today's inter-pass
  bloom→tonemap transition is owned by the framegraph compiler from
  the recipe-level read/write declarations.
- Slice C closes `Scaffolded → CPUContracted` on the FXAA leaf and the
  `RecordPostProcessFXAAPass(...)` umbrella fan-out: the pipeline lease
  + accessors + executor route + 20-byte `PostProcessFXAAPushConstants`
  push block + the `AntiAliasing == FXAA` selector gate inside the pass
  body land on the CPU/null contract gate. Full `Operational` for the
  FXAA leaf is gated by Slice D's recipe-side `PostProcess.AATemp`
  rename + the opt-in `gpu;vulkan` smoke; the SMAA branch + retained
  `AreaTex`/`SearchTex` LUTs + exposure-adaptation history buffer are
  owned by Slice D, and the Histogram compute pipeline + readback drain
  are owned by Slice E.
- Slice D.1 closes `Scaffolded → CPUContracted` on the SMAA
  pipeline-scaffold leaf and the `RecordPostProcessSMAAPass(...)`
  umbrella fan-out: the three pipeline leases (edge / blend / resolve)
  + accessors + executor route alongside FXAA under
  `"PostProcessAAPass"` + the three 16-byte
  `PostProcessSMAA{Edge,Blend,Resolve}PushConstants` push blocks + the
  `AntiAliasing == SMAA` selector gate inside the pass body land on the
  CPU/null contract gate. Full `Operational` for the SMAA leaf is gated
  by Slice D.2a's AA umbrella restructuring + edge / blend format flip,
  Slice D.2b's retained `AreaTex` / `SearchTex` LUT allocation +
  exposure-adaptation history buffer, and the opt-in `gpu;vulkan` smoke.
- Slice D.2a closes `CPUContracted → Operational` on the SMAA pipeline
  leaf for the CPU/null gate: the AA umbrella splits into three ordered
  graph passes (edge / blend / resolve), the recipe declares three
  matched-format `PostProcess.AATemp.{Edges,Weights,Resolved}` transients,
  edge / blend pipeline descs flip to `RG8_UNORM` / `RGBA8_UNORM`, and
  per-stage `RecordPostProcessAA{Edge,Blend,Resolve}Pass(...)` helpers
  route under the new pass names. Slice D.2a does *not* allocate the
  retained LUT textures or the exposure-adaptation history buffer —
  those stay on `PostProcessSystem` ownership in Slice D.2b — so the
  SMAA fragment shaders still sample placeholder zero LUT bindings on
  the CPU/null gate. Full `Operational` on the GPU/Vulkan gate for the
  SMAA leaf is owned by Slice D.2b + the opt-in `gpu;vulkan` smoke.
- Slice D.2b closes the retained-resource side of the SMAA leaf: the
  device-aware `PostProcessSystem::Initialize(device)` overload
  allocates + uploads the `AreaTex` / `SearchTex` LUT textures and the
  exposure-adaptation history buffer; `Shutdown()` releases them; the
  survive-rebuild contract test asserts both LUT handles + dimensions
  survive `RebuildGpuResources()` byte-identical. Slice D.2b does not
  touch the framegraph or the pass-body pipeline-format contracts —
  those are pinned by Slice D.2a.
- Slice E.1 closes `Scaffolded → CPUContracted` on the Histogram leaf
  for the CPU/null gate: the recipe splits the histogram out of the
  `"PostProcessPass"` umbrella into its own ordered graph pass
  `"PostProcessHistogramPass"` (Vulkan forbids `vkCmdDispatch` inside
  an active render-pass scope, so the compute dispatch cannot share the
  bloom + tonemap render-pass scope); the histogram compute pipeline
  is created in `InitializeOperationalPassResources(device)` from
  `BuildPostProcessHistogramPipelineDesc()` and republished byte-
  identical across `RebuildOperationalResources()`; the per-shader
  16-byte `PostProcessHistogramPushConstants` block (`uint Width +
  uint Height + float MinLogLum + float RangeLogLum`) replaces the
  canonical 20-byte `PostProcessPushConstants` block in the pass body
  per the standing shader-push-constant compatibility policy; the
  `RecordPostProcessHistogramPass(...)` helper routes the new
  executor branch under the standard `SkippedNonOperational` /
  `SkippedUnavailable` / `Recorded` taxonomy. Slice E.1 does *not*
  allocate the host-visible `Histogram.Readback` buffer or wire the
  `BeginFrame()`-side readback drain — those stay on Slice E.2, so
  the histogram dispatch on the CPU/null gate writes the transient
  `PostProcess.Histogram` storage buffer but the result is not
  observed past the frame. Full `Operational` on the GPU/Vulkan gate
  for the Histogram leaf is owned by Slice E.2 + the opt-in
  `gpu;vulkan` smoke.

## Required changes
- [x] **Slice A**: `NullRenderer` owns `m_PostProcessToneMapPass` + `m_PostProcessToneMapPipelineLease`; emplaced alongside the existing GRAPHICS-070..074 passes, reset in `Shutdown()` before `m_PostProcessSystem` is reset. Tonemap pipeline created in `InitializeOperationalPassResources(device)` from `BuildPostProcessToneMapPipelineDesc()` (vertex `post_fullscreen.vert.spv` + fragment `post_tonemap.frag.spv`, single backbuffer-format color target, no depth, `PushConstantSize = sizeof(PostProcessPushConstants)`); republished byte-identical across `RebuildOperationalResources()`. `"PostProcessPass"` executor branch routes through `RecordPostProcessToneMapPass(...)` with the recorded `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy. `IRenderer` exposes `GetPostProcessToneMapPipeline()` / `GetPostProcessToneMapPipelineDesc()`.
- [x] **Slice D.2a**: Recipe-side rename — `BuildDefaultFrameRecipe` replaces `PostProcess.AATemp` with `PostProcess.AATemp.{Edges,Weights,Resolved}` (formats `RG8_UNORM` / `RGBA8_UNORM` / `sizing.BackbufferFormat`); the single `"PostProcessAAPass"` pass declaration is replaced by three ordered passes (`"PostProcessAAEdgePass"`, `"PostProcessAABlendPass"`, `"PostProcessAAResolvePass"`), each declaring a single matched-format `Write`. `FrameRecipeResourceKind::PostProcessAATemp` becomes `PostProcessAATempEdges` / `PostProcessAATempWeights` / `PostProcessAATempResolved`; `FrameRecipePassKind::PostProcessAA` becomes `PostProcessAAEdge` / `PostProcessAABlend` / `PostProcessAAResolve`. `BuildAndCompileDefaultFrameGraph` creates all three transients and declares the matching `Read` / `Write` edges; `presentSource` flips to `PostProcess.AATemp.Resolved` when `FrameRecipeFeatures::EnableAntiAliasing` is set. The renderer plumbs that flag from `SelectedAntiAliasingPipelinesAvailable()`, which is true only when `PostProcessSettings::AntiAliasing != None` *and* the matching mode's pipeline(s) are valid (FXAA → FXAA pipeline; SMAA → all three SMAA pipelines because resolve reads `AATemp.Weights` and blend reads `AATemp.Edges`); otherwise present stays on `SceneColorLDR`. `RecordPostProcessAAResolvePass` mirrors the same per-mode gate so its `RenderCommandPassStatus` reports `SkippedUnavailable` when the selected mode's pipeline is missing instead of falsely recording a no-op against the unwritten resolved attachment.
- [x] **Slice D.2a**: Pipeline format flip — `BuildPostProcessSMAAEdgePipelineDesc` is fixed at `RG8_UNORM` (no longer parameterised); `BuildPostProcessSMAABlendPipelineDesc` is fixed at `RGBA8_UNORM` (no longer parameterised); `BuildPostProcessSMAAResolvePipelineDesc` + `BuildPostProcessFXAAPipelineDesc` keep `m_BackbufferFormat`. The Slice D.1 SMAA pipeline descs are republished with the new fixed formats; existing leases are reset/rebuilt across `RebuildOperationalResources()` byte-identical.
- [x] **Slice D.2a**: Pass-body slicing — `PostProcessSMAAPass::Execute` is split into `ExecuteEdge` / `ExecuteBlend` / `ExecuteResolve` per-stage methods; the existing `RecordPostProcessFXAAPass(...)` / `RecordPostProcessSMAAPass(...)` helpers are replaced by per-stage helpers (`RecordPostProcessAAEdgePass(...)`, `RecordPostProcessAABlendPass(...)`, `RecordPostProcessAAResolvePass(...)`). FXAA records under the resolve pass only; SMAA records under all three. Each per-stage body's `IsStageEnabled` selector stays in place; stage-disabled bodies stay no-op while the helper still reports `Recorded` (same "structurally-recorded no-op" taxonomy bloom + Slice C use).
- [x] **Slice D.2b**: In `PostProcessSystem::Initialize(device, textureMgr, bufferMgr)`: allocates the SMAA `AreaTex` (`R8G8_UNORM`, 160×560 — sized via the exported `kPostProcessSMAAAreaTextureWidth` / `Height` constants) and `SearchTex` (`R8_UNORM`, 66×33 — sized via the exported `kPostProcessSMAASearchTextureWidth` / `Height` constants) LUT textures via `RHI::TextureManager::Create(...)`; uploads their LUT bytes via `IDevice::GetTransferQueue().UploadTexture()` (LUT bytes ported byte-for-byte from `src/legacy/Graphics/Passes/Graphics.SMAALookupTextures.hpp` into a private namespace in `Graphics.PostProcessSystem.cpp` so promoted `graphics/renderer` never imports from `src/legacy`); allocates the exposure-adaptation history buffer (the new exported `PostProcessExposureHistory` POD — `previous_average_log_lum`, `adaptation_velocity`, `frame_index`, plus a 4-byte tail pad — created with `BufferUsage::Storage | TransferDst`, device-local). The overload is idempotent (no-op when the leases are already valid or when `device.IsOperational()` is false), and the renderer also invokes it from `RebuildOperationalResources(device)` so a device that becomes operational only after the initial `Initialize()` still picks up the allocation without a `Shutdown()`+`Initialize()` round-trip.
- [x] **Slice D.2b**: `PostProcessSystem::Shutdown()` frees all retained resources (drops the area / search LUT leases and the exposure-history buffer lease before clearing the manager pointers, matching the `ShadowSystem` teardown ordering contract so the lease destructors call back into a still-live `TextureManager` / `BufferManager`).
- [x] **Slice E.1**: Recipe-side split — `BuildDefaultFrameRecipe` /
  `DescribeDefaultFrameRecipe` add a new ordered graph pass
  `"PostProcessHistogramPass"` (gated on `EnablePostProcess`) declared with
  `Read(SceneColorHDR, ShaderRead)` + `Write(PostProcess.Histogram,
  BufferUsage::ShaderWrite)`, ordered after `"PointPass"` and before
  `"PostProcessPass"`. The histogram write is removed from the
  `"PostProcessPass"` umbrella declaration (the umbrella is a
  render-pass-scope pass that hosts bloom + tonemap fragment work, and
  Vulkan rejects `vkCmdDispatch` inside an active render-pass scope —
  the same render-pass-scope vs compute incompatibility the
  GRAPHICS-074 picking readback hit when it had to copy out of a
  color attachment after the picking render pass ended). New
  `FrameRecipePassKind::PostProcessHistogram` enum value tracks the
  new pass slot in introspection.
- [x] **Slice E.1**: Pass-local push block — new
  `PostProcessHistogramPushConstants` (16 bytes, `uint Width + uint
  Height + float MinLogLum + float RangeLogLum` std430 mirroring
  `post_histogram.comp`'s `layout(push_constant) PushConstants` block
  byte-for-byte) exported from `Pass.PostProcess.Histogram`. The
  canonical 20-byte `PostProcessPushConstants` block is intentionally
  not reused per the standing shader-push-constant compatibility
  policy: pushing it under the histogram shader would alias
  `Exposure` (1.0) onto `Width` (`bit_cast<uint>(1.0f)` = 0x3F800000
  ≈ 1.07e9 pixels wide), `Gamma` (2.2) onto `Height` and
  `BloomIntensity` (0.05) onto `MinLogLum` — producing a degenerate
  out-of-bounds dispatch shape and a meaningless luminance histogram.
  `BuildPostProcessHistogramPushConstants(settings, width, height)`
  derives `Width` / `Height` from the runtime viewport and defaults
  the log-luminance range to `[-10, +10]` stops per `GRAPHICS-013AQ`
  until Slice E.2's exposure adaptation overrides the bounds. The
  histogram pass body's existing canonical-block `PushConstants(&pc,
  sizeof(pc))` call switches to the new typed block.
- [x] **Slice E.1**: `PostProcessHistogramPass::SetViewport(width,
  height)` published by the executor (mirroring the Slice B.2 bloom
  pattern) so the dispatch shape (`ceil(W/16) x ceil(H/16) x 1`)
  tracks the backbuffer extent rather than the stale `(1,1,1)` the
  Slice A stub recorded.
- [x] **Slice E.1**: In `NullRenderer::InitializeOperationalPassResources(device)`,
  create the histogram compute pipeline (`post_histogram.comp`) from
  `BuildPostProcessHistogramPipelineDesc()` (compute pipeline,
  `ComputeShaderPath = post_histogram.comp.spv`, `PushConstantSize =
  sizeof(PostProcessHistogramPushConstants)`). `m_PostProcessHistogramPipelineLease`
  + `m_PostProcessHistogramPass` follow the same reset/republish
  pattern as the tonemap + bloom + FXAA + SMAA leases above.
- [ ] **Slice E.2**: In `NullRenderer::InitializeOperationalPassResources(device)`,
  allocate the host-visible `Histogram.Readback` buffer alongside the
  existing `Picking.Readback` buffer.
- [x] **Slice E.1**: `IRenderer` exposes
  `GetPostProcessHistogramPipeline()` /
  `GetPostProcessHistogramPipelineDesc()`. Executor wires
  `"PostProcessHistogramPass"` to `RecordPostProcessHistogramPass(...)`
  with the standard `SkippedNonOperational` / `SkippedUnavailable` /
  `Recorded` taxonomy; the helper plumbs the backbuffer extent into
  `SetViewport(...)` before invoking `Execute(...)`. Each remaining
  postprocess helper (tonemap, bloom, FXAA, SMAA edge/blend/resolve)
  is unchanged from its prior slice. Per-stage executor branches
  remain in `Graphics.Renderer.cpp`'s per-pass switch.
- [ ] **Slice E.2**: Implement the histogram readback drain on
  `BeginFrame()` after the issuing frame's fences complete; surface
  results through `PostProcessSystem::PublishHistogramReadback(...)`
  (mirror the `Picking.Readback` drain); update the retained
  `PostProcessExposureHistory` storage buffer via a `CopyBuffer(staging →
  ExposureHistory)`.
- [x] **Slice E.1**: `BuildDefaultFrameRecipe` declares the new
  `"PostProcessHistogramPass"` graph pass and routes the
  `PostProcess.Histogram` write through it. The umbrella
  `"PostProcessPass"` no longer declares the histogram write.

## Tests
- [x] **Slice A**: `contract;graphics` test `PostProcessToneMapPipelineSurvivesOperationalRebuild` asserts the tonemap pipeline lease is valid after `Initialize()`, the descriptor's shader paths + single backbuffer-format color target + `Undefined` depth + `PushConstantSize = sizeof(PostProcessPushConstants)` match `BuildPostProcessToneMapPipelineDesc()`, and that the descriptor is byte-identical across `RebuildOperationalResources()`. The existing `RendererFrameLifecycle.DefaultRecipeExecutesEveryDeclaredPass` test moves `"PostProcessPass"` from `kSoftSkippedPasses` to `kRoutedPasses` so the umbrella branch reports `Recorded` on an operational frame.
- [x] **Slice B.1**: `contract;graphics` test `PostProcessBloomPipelinesSurviveOperationalRebuild` asserts both bloom leases are valid after `Initialize()`, the descriptors' shader paths + single `RGBA16_FLOAT` color target + `Undefined` depth + 16-byte `PushConstantSize` match `BuildPostProcessBloomDownsamplePipelineDesc()` / `BuildPostProcessBloomUpsamplePipelineDesc()`, and both descriptors are byte-identical across `RebuildOperationalResources()`. `RendererFrameLifecycle.UsesDeviceFrameLifecycleBackbufferAndCommandContext` (+ rebuild / depth-failure / culling-failure variants) bumps the recorded-pass counter to include the bloom fan-out under the `"PostProcessPass"` umbrella.
- [x] **Slice B.2**: `contract;graphics` tests in `Test.PostProcessChainContract.cpp`:
  - `BloomMipChainPerMipIterationShape` drives `PostProcessBloomPass::Execute(...)` with a published synthetic `PostProcess.BloomScratch` handle + `kBloomMipChainLevels` and walks the recorded event stream to assert `M-1` downsamples + `M-1` upsamples of `Bind/Push/Draw` per step, and that the pass body emits *no* `TextureBarrier(...)` (per the render-pass-scope constraint documented in the pass body).
  - `BloomMipChainClampedToVulkanMipRule` pins `ComputeBloomMipChainLevels` against the boundary sizes that matter (1920x1080 → 6; 16x16 → 5; 8x8 → 4; 1x1 → 1; 0x0 → 1) so the recipe-side `MipLevels` declaration cannot regress to an out-of-range value for tiny viewports.
  - `BloomPassIterationFollowsClampedMipCount` exercises the pass with a 16x16 viewport (5 mips → 4 down + 4 up) and a degenerate single-mip pyramid (no iteration), confirming the pass's iteration count tracks the recipe-allocated mip range rather than the canonical cap.
  - A static_assert pins the canonical depth at compile time; existing `GraphicsPostProcessChainContract` tests updated to match the new per-mip iteration shape.
- [x] **Slice C**: `contract;graphics` test `PostProcessFXAAPipelineSurvivesOperationalRebuild` asserts the FXAA pipeline lease is valid after `Initialize()`, the descriptor's shader paths + single backbuffer-format color target + `Undefined` depth + `PushConstantSize = sizeof(PostProcessFXAAPushConstants)` match `BuildPostProcessFXAAPipelineDesc()`, and that the descriptor is byte-identical across `RebuildOperationalResources()`. `FXAAPushFeedsNonZeroInvResolution` asserts the pass body pushes the 20-byte `PostProcessFXAAPushConstants` block (not the canonical 20-byte `PostProcessPushConstants`) with `InvResolution = 1 / vec2(viewportWidth, viewportHeight)` derived from the camera UBO + FXAA 3.11 quality defaults (`ContrastThreshold = 0.0312`, `RelativeThreshold = 0.063`, `SubpixelBlending = 0.75`). `FXAASkipsWhenAntiAliasingNotFXAA` asserts the pass body emits no bind/push/draw for `AntiAliasing == None` and `AntiAliasing == SMAA`, and records the 3-event bind/push/draw triple for `AntiAliasing == FXAA`. New `FrameRecipeSplitsAAPassFromPostProcessPass` pins the recipe-level split (`PostProcessPass` retains Bloom + ToneMap; `PostProcessAAPass` reads `SceneColorLDR` + writes `PostProcess.AATemp`; AA pass runs strictly after PostProcessPass) so future scope creep cannot collapse the FXAA recording back into the umbrella and reintroduce the read-after-write hazard. `FrameRecipeContract.DefaultRecipeBuildsCanonicalPassOrder` adds `"PostProcessAAPass"` between `"PostProcessPass"` and `"ImGuiPass"`. `RendererFrameLifecycle.UsesDeviceFrameLifecycleBackbufferAndCommandContext` (+ rebuild / depth-failure / culling-failure variants) bumps the recorded-pass counter from 7/6/2 to 8/7/3; the new tick lands under `"PostProcessAAPass"` rather than `"PostProcessPass"`, and `kRoutedPasses` is extended to include the new pass name. The default `AntiAliasing == None` short-circuits the body, so bind/push counts stay at 6.
- [x] **Slice D.1**: `contract;graphics` tests in `Test.RendererFrameLifecycle.cpp` + `Test.PostProcessChainContract.cpp`:
  - `PostProcessSMAAPipelinesSurviveOperationalRebuild` asserts all three SMAA pipeline leases are valid after `Initialize()`, each descriptor's shader paths + color target format (edge → `RG8_UNORM`, blend → `RGBA8_UNORM`, resolve → backbuffer format) + `Undefined` depth + `PushConstantSize = sizeof(PostProcessSMAA{Edge,Blend,Resolve}PushConstants)` match the corresponding `BuildPostProcessSMAA*PipelineDesc()` helpers, and that all three descriptors are byte-identical across `RebuildOperationalResources()`.
  - `SMAAPushFeedsNonZeroInvResolutionForAllStages` drives the pass body with all three pipelines bound + `AntiAliasing == SMAA`, asserts the recorded event stream is 3× Bind/Push/Draw (edge → blend → resolve), and pins each builder's `InvResolution = 1 / vec2(viewportWidth, viewportHeight)` plus SMAA reference defaults (`EdgeThreshold = 0.1`, `MaxSearchSteps = 16`, `MaxSearchStepsDiag = 8`).
  - `SMAASkipsWhenAntiAliasingNotSMAA` asserts the pass body emits no bind/push/draw for `AntiAliasing == None` and `AntiAliasing == FXAA`, and records the 3× Bind/Push/Draw triple for `AntiAliasing == SMAA`. This pins the FXAA/SMAA mutual-exclusion invariant from the SMAA side (mirrors `FXAASkipsWhenAntiAliasingNotFXAA`).
  - `SMAARecordsPerStageIndependently` exercises partial pipeline outage (edge-only / resolve-only), confirming each stage's bind/push/draw is independently gated on its own `IsValid()` (mirrors the bloom helper's per-stage early-skip).
- [x] **Slice D.2a**: `contract;graphics` tests:
  - `PostProcessSMAAPipelinesSurviveOperationalRebuild` now asserts
    edge → `RG8_UNORM`, blend → `RGBA8_UNORM`, resolve → backbuffer
    format authoritatively.
  - `FrameRecipeSplitsAAUmbrellaPerStage` asserts the three ordered
    AA passes' `Read` / `Write` declarations and that each declares a
    single matched-format `Write` (so no single graph pass writes two
    color attachments with incompatible formats).
  - `AntiAliasingFlipsPresentSourceToResolved` asserts that
    `presentSource` flips to `PostProcess.AATemp.Resolved` when
    `FrameRecipeFeatures::EnableAntiAliasing = true` and stays on
    `SceneColorLDR` otherwise (observed via the compiled Present
    pass's `ReadTextures`).
  - `FrameRecipeContract.DefaultRecipeBuildsCanonicalPassOrder`
    replaces `"PostProcessAAPass"` with `"PostProcessAAEdgePass"`,
    `"PostProcessAABlendPass"`, `"PostProcessAAResolvePass"` between
    `"PostProcessPass"` and `"ImGuiPass"`.
  - `RendererFrameLifecycle.UsesDeviceFrameLifecycleBackbufferAndCommandContext`
    (+ rebuild / depth-failure / culling-failure variants) bumps the
    recorded-pass counter from 9/9/8/4 to 10/10/9/5; `kRoutedPasses`
    adds the three new pass names (replacing the single
    `"PostProcessAAPass"`).
  - `GraphicsPostProcessChainContract.PassesRecordOnlyEnabledStages` /
    `SMAAPushFeedsNonZeroInvResolutionForAllStages` /
    `SMAASkipsWhenAntiAliasingNotSMAA` /
    `SMAARecordsPerStageIndependently` drive the SMAA pass through
    its new per-stage Execute methods.
  - `RendererFrameLifecycle.FXAASelectedWithoutPipelineKeepsResolveSkippedAndPresentOnSceneColorLDR`
    / `RendererFrameLifecycle.SMAASelectedWithoutResolvePipelineKeepsResolveSkippedAndPresentOnSceneColorLDR`
    pin the AA-mode-aware resolve gate: with the selected mode's
    pipeline missing the resolve helper must report `SkippedUnavailable`
    so the recipe-build site keeps present on `SceneColorLDR`.
- [x] **Slice D.2b**: `contract;graphics` test `PostProcessSMAALookupTexturesSurviveOperationalRebuild` asserts the retained `AreaTex` / `SearchTex` / exposure-history handles obtained from `renderer->GetPostProcessSystem()` are valid after `Initialize()`, the transfer-queue captured one `UploadTexture(...)` per LUT at the expected byte sizes (160 × 560 × 2 = 179200 for area; 66 × 33 = 2178 for search; mip 0 / array layer 0), and the three handles are byte-identical (StrongHandle `operator==`) across `RebuildOperationalResources()` with zero extra upload records. `Shutdown()` is asserted to release the leases by snapshotting `DestroyTextureCount` / `DestroyBufferCount` from a post-rebuild baseline and checking the post-`Shutdown()` totals increase by ≥ 2 textures and ≥ 1 buffer.
- [x] **Slice E.1**: `contract;graphics` tests:
  - `PostProcessHistogramPipelineSurvivesOperationalRebuild` asserts
    the histogram compute pipeline lease is valid after `Initialize()`,
    the descriptor's `ComputeShaderPath` ends with
    `post_histogram.comp.spv`, `VertexShaderPath` /
    `FragmentShaderPath` are empty (so the backend interprets the
    descriptor as a compute pipeline), `ColorTargetCount == 0`,
    `DepthTargetFormat == Undefined`, and
    `PushConstantSize == sizeof(PostProcessHistogramPushConstants)`;
    descriptor stays byte-identical across
    `RebuildOperationalResources()`.
  - `FrameRecipeDeclaresPostProcessHistogramAsOrderedPass` asserts the
    recipe declares `"PostProcessHistogramPass"` between
    `"PointPass"` and `"PostProcessPass"`, that
    `"PostProcessHistogramPass"` reads `SceneColorHDR` and writes
    `PostProcess.Histogram`, and that `"PostProcessPass"` no longer
    writes `PostProcess.Histogram` (so future scope creep cannot
    collapse the compute dispatch back inside the umbrella render-
    pass scope and re-introduce the dispatch-inside-render-pass
    hazard).
  - `HistogramPushFeedsViewportSizedDispatchShape` asserts that the
    pass body's recorded `Dispatch` call uses
    `gridX = ceil(W/16)` for a `1920x1080` viewport (i.e. `120`) and
    that the pushed 16-byte `PostProcessHistogramPushConstants`
    block carries `Width == 1920`, `Height == 1080`, and the
    canonical `[-10, +10]` log-luminance bounds (`MinLogLum ==
    -10.0`, `RangeLogLum == 1.0 / 20.0`).
  - `DefaultRecipeBuildsCanonicalPassOrder` adds
    `"PostProcessHistogramPass"` between `"PointPass"` and
    `"PostProcessPass"`.
  - `RendererFrameLifecycle.UsesDeviceFrameLifecycleBackbufferAndCommandContext`
    (+ rebuild / depth-failure / culling-failure variants) bumps the
    recorded-pass counters from 10/10/9/5 to 11/11/10/6;
    `kRoutedPasses` adds `"PostProcessHistogramPass"`.
- [ ] **Slice E.2**: `contract;graphics` test that the histogram
  readback drain calls `PostProcessSystem::PublishHistogramReadback`
  after the issuing frame's fence completes; survives rebuild;
  forwards the 256-bin payload byte-identical from the host-visible
  `Histogram.Readback` slot.
- [ ] **Slice E.2**: `contract;graphics` test: `PostProcessDiagnostics`
  reports zero failure counters after a full chain init.

## Docs
- [x] **Slice A**: Update `src/graphics/renderer/README.md` to record the tonemap leg of the postprocess chain as operationally wired (umbrella `"PostProcessPass"` reports `Recorded`); call out Slices B–E as the bloom / FXAA / SMAA / histogram followups.
- [x] **Slice B.1**: Extend `src/graphics/renderer/README.md` to record the bloom downsample + upsample legs as CPU-contract wired behind the umbrella `"PostProcessPass"`; call out Slice B.2 + Slices C/D/E as the remaining followups.
- [x] **Slice B.2**: `src/graphics/renderer/README.md` now records the bloom leg as operationally wired on the CPU/null gate (per-mip iteration over the canonical six-mip pyramid + inline `ColorAttachment ↔ ShaderRead` barriers + renderer-side per-frame `PostProcess.BloomScratch` handle republish); calls out Slices C/D/E as the remaining FXAA/SMAA/Histogram followups.
- [x] **Slice C**: `src/graphics/renderer/README.md` now records the FXAA leg as CPU-contract wired behind a *separate* ordered graph pass (`"PostProcessAAPass"`) declared by the recipe with `Read(SceneColorLDR) + Write(PostProcess.AATemp)` so the framegraph compiler emits the read-after-write barrier the FXAA shader needs; calls out Slices D/E (SMAA shares the AA umbrella; Histogram fans out under `PostProcessPass`) as the remaining followups.
- [x] **Slice D.1**: `src/graphics/renderer/README.md` now records the SMAA pipeline scaffold (three pipelines + per-shader push blocks + `RecordPostProcessSMAAPass(...)` umbrella fan-out under `"PostProcessAAPass"` alongside FXAA, mutually exclusive per `PostProcessSettings::AntiAliasing`) as CPU-contract wired; calls out Slice D.2 (retained `AreaTex` / `SearchTex` LUTs + exposure-adaptation history buffer + recipe-side `PostProcess.AATemp.{Edges,Weights}` rename) and Slice E (Histogram) as the remaining followups.
- [x] **Slice D.2a**: `src/graphics/renderer/README.md` now records the AA umbrella split into three ordered graph passes + edge / blend pipeline format flip + per-stage SMAA Execute methods + per-pass renderer helpers + `presentSource` flip; calls out Slice D.2b (retained LUTs + exposure-adaptation history) and Slice E (Histogram) as the remaining followups.
- [x] **Slice D.2a**: `docs/architecture/rendering-three-pass.md` updates the canonical pipeline-order list (single `PostProcessPass` step + three new `PostProcessAA{Edge,Blend,Resolve}Pass` steps), the frame-recipe transient table (single `PostProcess.AATemp` row replaced by three matched-format rows for `.Edges` / `.Weights` / `.Resolved`), and the SMAA/FXAA backend-follow-ups paragraph.
- [x] **Slice D.2b**: `src/graphics/renderer/README.md` now records the retained SMAA `AreaTex` / `SearchTex` LUT textures + `PostProcessExposureHistory` buffer as allocated via the device-aware `PostProcessSystem::Initialize(device, textureMgr, bufferMgr)` overload (idempotent, invoked from both renderer `Initialize` and `RebuildOperationalResources`, leases dropped in `Shutdown()` matching the `ShadowSystem` teardown ordering); `docs/architecture/rendering-three-pass.md` already factually described the retained-resource ownership at `PostProcessSystem::Initialize()` and `Shutdown()` boundaries, and Slice D.2b makes that text true on the CPU/null gate without further edits.
- [x] **Slice E.1**: `src/graphics/renderer/README.md` records the
  Histogram leg as CPU-contract wired behind its own ordered graph
  pass `"PostProcessHistogramPass"` (compute dispatch outside the
  `"PostProcessPass"` umbrella render-pass scope; reads
  `SceneColorHDR`, writes `PostProcess.Histogram`; canonical
  `[-10, +10]` log-luminance default until Slice E.2's exposure
  adaptation overrides). Calls out Slice E.2 (host-visible
  `Histogram.Readback` buffer + `BeginFrame()`-side drain +
  `PublishHistogramReadback` + exposure-history consumption) as the
  remaining followup.
- [x] **Slice E.1**: `docs/architecture/rendering-three-pass.md`
  pipeline-order list now records `"PostProcessHistogramPass"` as a
  separate step ordered between `"PointPass"` and `"PostProcessPass"`;
  the canonical-pass-list paragraph notes the dispatch-vs-render-pass
  scope reason for the split.
- [ ] **Slice E.2**: Extend `src/graphics/renderer/README.md` and `docs/architecture/rendering-three-pass.md` as the histogram readback drain lands.

## Acceptance criteria
- [x] **Slice A**: ToneMap pipeline records or `SkippedUnavailable` deterministically; `"PostProcessPass"` reports `Recorded` on the operational CPU/null gate.
- [x] **Slice B.1**: Bloom helper records (or `SkippedUnavailable`) deterministically; `"PostProcessPass"` umbrella reports `Recorded` after fan-out on the operational CPU/null gate.
- [x] **Slice B.2**: Bloom pass records the per-mip chain (or `SkippedUnavailable` when pipelines/system are missing) deterministically; the bloom mip-chain barrier-sequence contract test (`BloomMipChainBarrierSequence`) exercises the inline `ColorAttachment ↔ ShaderRead` barriers against a synthetic `PostProcess.BloomScratch` handle.
- [x] **Slice C**: FXAA pass records (or `SkippedUnavailable` when pipeline/system are missing) deterministically; helper runs inside the *separate* `"PostProcessAAPass"` graph pass declared with `Read(SceneColorLDR) + Write(PostProcess.AATemp)` so the framegraph compiler emits the `SceneColorLDR ColorAttachment → ShaderRead` transition between the `PostProcessPass` (bloom + tonemap) and `PostProcessAAPass` (FXAA, SMAA Slice D) render-pass scopes — avoiding the read-after-write feedback hazard that would arise from sharing the umbrella. Default `AntiAliasing == None` short-circuits the pass body to a no-op while the helper still reports `Recorded` under the `"PostProcessAAPass"` accumulator (the same "structurally-recorded no-op" taxonomy the bloom helper follows when `EnableBloom = false`).
- [x] **Slice D.1**: SMAA pass records (or each stage `SkippedUnavailable` when its pipeline is missing) deterministically; `RecordPostProcessSMAAPass(...)` runs inside the `"PostProcessAAPass"` graph pass alongside FXAA (mutually exclusive per `PostProcessSettings::AntiAliasing`, with the pass bodies' `IsStageEnabled` gate enforcing the selector). Default `AntiAliasing == None` short-circuits the SMAA pass body to a no-op while the helper still reports `Recorded` under the `"PostProcessAAPass"` accumulator (the same "structurally-recorded no-op" taxonomy FXAA already follows).
- [x] **Slice D.2a**: SMAA edge / blend / resolve passes each record (or `SkippedUnavailable` when the pipeline is missing) deterministically under their own `"PostProcessAA{Edge,Blend,Resolve}Pass"` ordered graph pass; FXAA records under `"PostProcessAAResolvePass"` only. The recipe declares `PostProcess.AATemp.{Edges,Weights,Resolved}` at `RG8_UNORM` / `RGBA8_UNORM` / backbuffer format; `presentSource` flips to `PostProcess.AATemp.Resolved` when `FrameRecipeFeatures::EnableAntiAliasing` is set so the AA output reaches present.
- [x] **Slice D.2b**: Retained `AreaTex` / `SearchTex` LUT textures + exposure-adaptation history buffer allocated via the device-aware `PostProcessSystem::Initialize(device, textureMgr, bufferMgr)` overload survive `RebuildOperationalResources()` byte-identical; `Shutdown()` releases them. Verified by `PostProcessSMAALookupTexturesSurviveOperationalRebuild` on the CPU/null gate.
- [x] **Slice E.1**: Histogram compute pipeline records (or
  `SkippedUnavailable` when pipeline/system are missing)
  deterministically under its own
  `"PostProcessHistogramPass"` ordered graph pass; the dispatch shape
  tracks the backbuffer extent via `SetViewport(width, height)`; the
  pushed `PostProcessHistogramPushConstants` block carries the
  canonical `[-10, +10]` log-luminance bounds. With
  `EnableHistogram = false` the pass body short-circuits and the
  helper still reports `Recorded` per the "structurally-recorded
  no-op" taxonomy bloom / FXAA / SMAA already follow.
- [ ] **Slice E.2**: Histogram readback drain produces deterministic CPU-visible results.
- [x] **Slice A**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice A).
- [x] **Slice B.1**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice B.1).
- [x] **Slice B.2**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice B.2).
- [x] **Slice C**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice C).
- [x] **Slice D.1**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice D.1).
- [x] **Slice D.2a**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice D.2a).
- [x] **Slice D.2b**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice D.2b — 2081/2081 tests pass; layering, doc-link, and task-policy validators all clean).
- [x] **Slice E.1**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice E.1).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGraphicsContractTests
ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Adding TAA or upscaler logic.
- Adding additional bloom mip variants beyond the recorded six-mip cap.
- Adding new AA techniques.
- Mutating canonical `PostProcessPushConstants` packing.

## Next verification step
- Slice D.1 has landed on `main` via PR #907 (commits `ca60ac9` +
  `94c41f0`) and the CPU/null contract gate stayed green.
- Slice D.2a landed on
  `claude/intrinsicengine-agent-onboarding-32x02`. Verification run
  in-session against a build with `clang-20`/`clang++-20`/
  `clang-scan-deps-20`:
  ```bash
  cmake --preset ci
  cmake --build --preset ci --target IntrinsicGraphicsContractCpuTests
  ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  python3 tools/repo/check_layering.py --root src --strict
  python3 tools/docs/check_doc_links.py --root .
  python3 tools/agents/check_task_policy.py --root . --strict
  ```
  Result: 196/196 contract tests passed (including the updated
  `FrameRecipeContract.DefaultRecipeBuildsCanonicalPassOrder`,
  `GraphicsPostProcessChainContract.FrameRecipeSplitsAAUmbrellaPerStage`,
  `GraphicsPostProcessChainContract.AntiAliasingFlipsPresentSourceToResolved`,
  per-stage SMAA recording tests, and
  `RendererFrameLifecycle.PostProcessSMAAPipelinesSurviveOperationalRebuild`
  with `RG8_UNORM` / `RGBA8_UNORM` edge / blend formats); layering
  + doc-link + task-policy validators all clean.
- Slice D.2b landed on
  `claude/intrinsicengine-agent-onboarding-BQgHn`. Verification run
  in-session against a build with `clang-20`/`clang++-20`/
  `clang-scan-deps-20`:
  ```bash
  cmake --preset ci
  cmake --build --preset ci --target IntrinsicTests
  cmake --build --preset ci --target IntrinsicBenchmarkSmoke
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  python3 tools/repo/check_layering.py --root src --strict
  python3 tools/docs/check_doc_links.py --root .
  python3 tools/agents/check_task_policy.py --root . --strict
  ```
  Result: 2081/2081 default-gate tests passed (including the new
  `RendererFrameLifecycle.PostProcessSMAALookupTexturesSurviveOperationalRebuild`
  contract test asserting handle identity and upload-record stability
  across `RebuildOperationalResources()`); layering + doc-link +
  task-policy validators all clean.
- Slice E.1 lands on `claude/gallant-hypatia-a2puc`. Verification
  expected to run in-session against a build with
  `clang-20`/`clang++-20`/`clang-scan-deps-20`:
  ```bash
  cmake --preset ci
  cmake --build --preset ci --target IntrinsicTests
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  python3 tools/repo/check_layering.py --root src --strict
  python3 tools/docs/check_doc_links.py --root .
  python3 tools/agents/check_task_policy.py --root . --strict
  ```
- After Slice E.1, open Slice E.2 (host-visible `Histogram.Readback`
  buffer + `BeginFrame()`-side readback drain mirroring the
  `Picking.Readback` pattern +
  `PostProcessSystem::PublishHistogramReadback(...)` consuming the
  exposure-adaptation history buffer Slice D.2b allocated).
