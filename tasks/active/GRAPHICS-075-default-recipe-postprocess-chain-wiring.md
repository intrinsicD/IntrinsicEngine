# GRAPHICS-075 — Default-recipe postprocess chain wiring (Histogram → Bloom → ToneMap → FXAA/SMAA)

## Status

- State: in-progress (Slice D.2a landed; Slice D.2b queued next).
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
  the CPU/null gate; the retained LUT side stays with D.2b.
- Owner/agent: local agent workflow.
- Branch: Slice D.2a on `claude/intrinsicengine-agent-onboarding-32x02`;
  Slice D.2b branch TBD.
- Activated: 2026-05-21 — first unblocked Theme A default-recipe leaf
  after GRAPHICS-074 retirement.
- Next verification step: open Slice D.2b (retained `AreaTex` /
  `SearchTex` LUT textures + exposure-adaptation history buffer +
  device-aware `PostProcessSystem::Initialize(device)` overload +
  survive-rebuild contract test) in a follow-up session.

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
- **Slice E.** Histogram compute pipeline + `RecordPostProcessHistogramPass(...)`
  dispatch + `BeginFrame()`-side readback drain mirroring GRAPHICS-074
  Slice D.3's `Picking.Readback` pattern (per-frame slot, `Pending` /
  `IssuedFrame` / `Invalidated` metadata) +
  `PostProcessSystem::PublishHistogramReadback(...)`. Exposure-adaptation
  history buffer consumed.

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

## Required changes
- [x] **Slice A**: `NullRenderer` owns `m_PostProcessToneMapPass` + `m_PostProcessToneMapPipelineLease`; emplaced alongside the existing GRAPHICS-070..074 passes, reset in `Shutdown()` before `m_PostProcessSystem` is reset. Tonemap pipeline created in `InitializeOperationalPassResources(device)` from `BuildPostProcessToneMapPipelineDesc()` (vertex `post_fullscreen.vert.spv` + fragment `post_tonemap.frag.spv`, single backbuffer-format color target, no depth, `PushConstantSize = sizeof(PostProcessPushConstants)`); republished byte-identical across `RebuildOperationalResources()`. `"PostProcessPass"` executor branch routes through `RecordPostProcessToneMapPass(...)` with the recorded `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy. `IRenderer` exposes `GetPostProcessToneMapPipeline()` / `GetPostProcessToneMapPipelineDesc()`.
- [x] **Slice D.2a**: Recipe-side rename — `BuildDefaultFrameRecipe` replaces `PostProcess.AATemp` with `PostProcess.AATemp.{Edges,Weights,Resolved}` (formats `RG8_UNORM` / `RGBA8_UNORM` / `sizing.BackbufferFormat`); the single `"PostProcessAAPass"` pass declaration is replaced by three ordered passes (`"PostProcessAAEdgePass"`, `"PostProcessAABlendPass"`, `"PostProcessAAResolvePass"`), each declaring a single matched-format `Write`. `FrameRecipeResourceKind::PostProcessAATemp` becomes `PostProcessAATempEdges` / `PostProcessAATempWeights` / `PostProcessAATempResolved`; `FrameRecipePassKind::PostProcessAA` becomes `PostProcessAAEdge` / `PostProcessAABlend` / `PostProcessAAResolve`. `BuildAndCompileDefaultFrameGraph` creates all three transients and declares the matching `Read` / `Write` edges; `presentSource` flips to `PostProcess.AATemp.Resolved` when `FrameRecipeFeatures::EnableAntiAliasing` is set (renderer plumbs this from `PostProcessSettings::AntiAliasing != None`).
- [x] **Slice D.2a**: Pipeline format flip — `BuildPostProcessSMAAEdgePipelineDesc` is fixed at `RG8_UNORM` (no longer parameterised); `BuildPostProcessSMAABlendPipelineDesc` is fixed at `RGBA8_UNORM` (no longer parameterised); `BuildPostProcessSMAAResolvePipelineDesc` + `BuildPostProcessFXAAPipelineDesc` keep `m_BackbufferFormat`. The Slice D.1 SMAA pipeline descs are republished with the new fixed formats; existing leases are reset/rebuilt across `RebuildOperationalResources()` byte-identical.
- [x] **Slice D.2a**: Pass-body slicing — `PostProcessSMAAPass::Execute` is split into `ExecuteEdge` / `ExecuteBlend` / `ExecuteResolve` per-stage methods; the existing `RecordPostProcessFXAAPass(...)` / `RecordPostProcessSMAAPass(...)` helpers are replaced by per-stage helpers (`RecordPostProcessAAEdgePass(...)`, `RecordPostProcessAABlendPass(...)`, `RecordPostProcessAAResolvePass(...)`). FXAA records under the resolve pass only; SMAA records under all three. Each per-stage body's `IsStageEnabled` selector stays in place; stage-disabled bodies stay no-op while the helper still reports `Recorded` (same "structurally-recorded no-op" taxonomy bloom + Slice C use).
- [ ] **Slice D.2b**: In `PostProcessSystem::Initialize(device)`: allocate the SMAA `AreaTex` (`R8G8_UNORM`, 160×560) and `SearchTex` (`R8_UNORM`, 66×33) LUT textures via `RHI::TextureManager::Create(...)`; upload their LUT bytes via `IDevice::GetTransferQueue().UploadTexture()` (LUT bytes ported from `src/legacy/Graphics/Passes/Graphics.SMAALookupTextures.hpp`); allocate the exposure-adaptation history buffer (`previous_average_log_lum`, `adaptation_velocity`, `frame_index`).
- [ ] **Slice D.2b**: `PostProcessSystem::Shutdown()` frees all retained resources.
- [ ] In `NullRenderer::InitializeOperationalPassResources(device)`, create:
  - tonemap pipeline (`post_tonemap.frag`), **Slice A** *(done)*
  - bloom downsample + upsample pipelines (`post_bloom_downsample.frag` + `post_bloom_upsample.frag` with a fullscreen vertex), **Slice B.1** *(done)*; per-mip iteration + recipe-side `BloomScratch.MipLevels = ComputeBloomMipChainLevels(width, height)` (clamped against Vulkan's `mipLevels <= floor(log2(maxDim)) + 1` rule) + renderer-side per-frame `PostProcess.BloomScratch` handle + clamped mip-count republish, **Slice B.2** *(done)*. Per-mip subresource barriers between iterations are *deferred*: the pass body emits no `TextureBarrier(...)` (the umbrella render pass is active when `Execute(...)` runs and Vulkan rejects layout transitions inside render-pass scope), and the inter-pass `BloomScratch ColorAttachment → ShaderReadOnly` between bloom and tonemap is owned by the framegraph compiler from the recipe-level read/write declarations
  - FXAA pipeline (`post_fxaa.frag`), **Slice C** *(done)*. `m_PostProcessFXAAPipelineLease` + `m_PostProcessFXAAPass` follow the same reset/republish pattern as the tonemap + bloom leases. The pipeline takes the backbuffer format (same `colorFormat` parameter the tonemap pipeline does) so it stays render-pass-compatible with the tonemap leg's `SceneColorLDR` output. `PostProcessFXAAPushConstants` (20 bytes, std430-matched to `post_fxaa.frag`) replaces the canonical `PostProcessPushConstants` in the pass body
  - SMAA edge/blend/resolve pipelines (`post_smaa_edge.frag`, `post_smaa_blend.frag`, `post_smaa_resolve.frag`), **Slice D.1** *(done)*. `m_PostProcessSMAA{Edge,Blend,Resolve}PipelineLease` + `m_PostProcessSMAAPass` follow the same reset/republish pattern as the FXAA + tonemap + bloom leases. All three pipelines target the current `PostProcess.AATemp` recipe attachment (allocated with `FrameRecipeSizing::BackbufferFormat`) so the AA umbrella render pass stays format-compatible with the pipelines bound inside it; Slice D.2a retargets edge to `RG8_UNORM` and blend to `RGBA8_UNORM` once the recipe declares `PostProcess.AATemp.{Edges,Weights,Resolved}` as three separate transient resources and the AA umbrella splits into three ordered graph passes. Three 16-byte std430 push blocks (`PostProcessSMAAEdgePushConstants`, `PostProcessSMAABlendPushConstants`, `PostProcessSMAAResolvePushConstants`) replace the canonical 20-byte block per the shader-push-constant compatibility policy
  - histogram compute pipeline (`post_histogram.comp`), **Slice E**.
- [ ] Add executor fan-out for the postprocess legs. **Slice A** lands ToneMap inside the `"PostProcessPass"` umbrella; **Slice B.1** adds the bloom helper ahead of tonemap inside the same `"PostProcessPass"` umbrella; **Slice C** *(done)* moves FXAA into its *own* `"PostProcessAAPass"` umbrella branch (a separate ordered graph pass declared by the recipe with `Read(SceneColorLDR) + Write(PostProcess.AATemp)` so the framegraph compiler emits the `SceneColorLDR ColorAttachment → ShaderRead` transition between the two umbrella render-pass scopes; sharing `PostProcessPass` would alias the umbrella's own color attachment as a sampled image mid-render-pass, Vulkan's read-after-write feedback hazard); **Slice D.1** *(done)* fans `RecordPostProcessSMAAPass(...)` out behind the same `"PostProcessAAPass"` branch alongside FXAA (mutually exclusive per `PostProcessSettings::AntiAliasing`, with both pass bodies' `IsStageEnabled` gate enforcing the selector so both helpers can run unconditionally and only the active stage emits bind/push/draw); **Slice D.2a** splits the single `"PostProcessAAPass"` umbrella into three ordered graph passes (`"PostProcessAAEdgePass"`, `"PostProcessAABlendPass"`, `"PostProcessAAResolvePass"`) so edge / blend / resolve pipelines can target format-incompatible color attachments (`RG8_UNORM` / `RGBA8_UNORM` / backbuffer); FXAA records under the resolve pass only, SMAA records under all three; **Slice E** adds the Histogram helper behind the existing `"PostProcessPass"` umbrella (compute pipeline + readback drain). Each helper records under the `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.
- [ ] **Slice E**: Implement the histogram readback drain on `BeginFrame()` after the issuing frame's fences complete; surface results through `PostProcessSystem::PublishHistogramReadback(...)` (mirror the `Picking.Readback` drain).
- [ ] **Slice B**: Confirm `BuildDefaultFrameRecipe` declares `SceneColorHDR`, `SceneColorLDR`, `PostProcess.BloomScratch`, `PostProcess.Histogram`, `PostProcess.AATemp.Edges`, `PostProcess.AATemp.Weights` with the recorded formats. **Slice B.2** *(partial)*: `PostProcess.BloomScratch` now declares `MipLevels = kBloomMipChainLevels` so the per-mip iteration has the subresource storage it needs; the `AATemp.{Edges, Weights}` rename remains a Slice D follow-up.

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
- [ ] **Slice D.2b**: `contract;graphics` test `PostProcessSMAALookupTexturesSurviveOperationalRebuild` for retained `AreaTex`/`SearchTex` LUTs surviving `RebuildGpuResources()` byte-identical.
- [ ] **Slice E**: `contract;graphics` test that the histogram readback drain calls `PostProcessSystem::PublishHistogramReadback` after the issuing frame's fence completes.
- [ ] **Slice E**: `contract;graphics` test: `PostProcessDiagnostics` reports zero failure counters after a full chain init.

## Docs
- [x] **Slice A**: Update `src/graphics/renderer/README.md` to record the tonemap leg of the postprocess chain as operationally wired (umbrella `"PostProcessPass"` reports `Recorded`); call out Slices B–E as the bloom / FXAA / SMAA / histogram followups.
- [x] **Slice B.1**: Extend `src/graphics/renderer/README.md` to record the bloom downsample + upsample legs as CPU-contract wired behind the umbrella `"PostProcessPass"`; call out Slice B.2 + Slices C/D/E as the remaining followups.
- [x] **Slice B.2**: `src/graphics/renderer/README.md` now records the bloom leg as operationally wired on the CPU/null gate (per-mip iteration over the canonical six-mip pyramid + inline `ColorAttachment ↔ ShaderRead` barriers + renderer-side per-frame `PostProcess.BloomScratch` handle republish); calls out Slices C/D/E as the remaining FXAA/SMAA/Histogram followups.
- [x] **Slice C**: `src/graphics/renderer/README.md` now records the FXAA leg as CPU-contract wired behind a *separate* ordered graph pass (`"PostProcessAAPass"`) declared by the recipe with `Read(SceneColorLDR) + Write(PostProcess.AATemp)` so the framegraph compiler emits the read-after-write barrier the FXAA shader needs; calls out Slices D/E (SMAA shares the AA umbrella; Histogram fans out under `PostProcessPass`) as the remaining followups.
- [x] **Slice D.1**: `src/graphics/renderer/README.md` now records the SMAA pipeline scaffold (three pipelines + per-shader push blocks + `RecordPostProcessSMAAPass(...)` umbrella fan-out under `"PostProcessAAPass"` alongside FXAA, mutually exclusive per `PostProcessSettings::AntiAliasing`) as CPU-contract wired; calls out Slice D.2 (retained `AreaTex` / `SearchTex` LUTs + exposure-adaptation history buffer + recipe-side `PostProcess.AATemp.{Edges,Weights}` rename) and Slice E (Histogram) as the remaining followups.
- [x] **Slice D.2a**: `src/graphics/renderer/README.md` now records the AA umbrella split into three ordered graph passes + edge / blend pipeline format flip + per-stage SMAA Execute methods + per-pass renderer helpers + `presentSource` flip; calls out Slice D.2b (retained LUTs + exposure-adaptation history) and Slice E (Histogram) as the remaining followups.
- [x] **Slice D.2a**: `docs/architecture/rendering-three-pass.md` updates the canonical pipeline-order list (single `PostProcessPass` step + three new `PostProcessAA{Edge,Blend,Resolve}Pass` steps), the frame-recipe transient table (single `PostProcess.AATemp` row replaced by three matched-format rows for `.Edges` / `.Weights` / `.Resolved`), and the SMAA/FXAA backend-follow-ups paragraph.
- [ ] **Slice D.2b / E**: Extend `src/graphics/renderer/README.md` and `docs/architecture/rendering-three-pass.md` as the remaining slices land.

## Acceptance criteria
- [x] **Slice A**: ToneMap pipeline records or `SkippedUnavailable` deterministically; `"PostProcessPass"` reports `Recorded` on the operational CPU/null gate.
- [x] **Slice B.1**: Bloom helper records (or `SkippedUnavailable`) deterministically; `"PostProcessPass"` umbrella reports `Recorded` after fan-out on the operational CPU/null gate.
- [x] **Slice B.2**: Bloom pass records the per-mip chain (or `SkippedUnavailable` when pipelines/system are missing) deterministically; the bloom mip-chain barrier-sequence contract test (`BloomMipChainBarrierSequence`) exercises the inline `ColorAttachment ↔ ShaderRead` barriers against a synthetic `PostProcess.BloomScratch` handle.
- [x] **Slice C**: FXAA pass records (or `SkippedUnavailable` when pipeline/system are missing) deterministically; helper runs inside the *separate* `"PostProcessAAPass"` graph pass declared with `Read(SceneColorLDR) + Write(PostProcess.AATemp)` so the framegraph compiler emits the `SceneColorLDR ColorAttachment → ShaderRead` transition between the `PostProcessPass` (bloom + tonemap) and `PostProcessAAPass` (FXAA, SMAA Slice D) render-pass scopes — avoiding the read-after-write feedback hazard that would arise from sharing the umbrella. Default `AntiAliasing == None` short-circuits the pass body to a no-op while the helper still reports `Recorded` under the `"PostProcessAAPass"` accumulator (the same "structurally-recorded no-op" taxonomy the bloom helper follows when `EnableBloom = false`).
- [x] **Slice D.1**: SMAA pass records (or each stage `SkippedUnavailable` when its pipeline is missing) deterministically; `RecordPostProcessSMAAPass(...)` runs inside the `"PostProcessAAPass"` graph pass alongside FXAA (mutually exclusive per `PostProcessSettings::AntiAliasing`, with the pass bodies' `IsStageEnabled` gate enforcing the selector). Default `AntiAliasing == None` short-circuits the SMAA pass body to a no-op while the helper still reports `Recorded` under the `"PostProcessAAPass"` accumulator (the same "structurally-recorded no-op" taxonomy FXAA already follows).
- [x] **Slice D.2a**: SMAA edge / blend / resolve passes each record (or `SkippedUnavailable` when the pipeline is missing) deterministically under their own `"PostProcessAA{Edge,Blend,Resolve}Pass"` ordered graph pass; FXAA records under `"PostProcessAAResolvePass"` only. The recipe declares `PostProcess.AATemp.{Edges,Weights,Resolved}` at `RG8_UNORM` / `RGBA8_UNORM` / backbuffer format; `presentSource` flips to `PostProcess.AATemp.Resolved` when `FrameRecipeFeatures::EnableAntiAliasing` is set so the AA output reaches present.
- [ ] **Slice D.2b**: Retained `AreaTex` / `SearchTex` LUT textures + exposure-adaptation history buffer allocated via the device-aware `PostProcessSystem::Initialize(device)` overload survive `RebuildGpuResources()` byte-identical; `Shutdown()` releases them.
- [ ] **Slice E**: Each remaining postprocess pass records or `SkippedUnavailable` deterministically.
- [ ] **Slice E**: Histogram readback drain produces deterministic CPU-visible results.
- [x] **Slice A**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice A).
- [x] **Slice B.1**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice B.1).
- [x] **Slice B.2**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice B.2).
- [x] **Slice C**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice C).
- [x] **Slice D.1**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice D.1).
- [x] **Slice D.2a**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice D.2a).

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
- After Slice D.2a, open Slice D.2b (retained `AreaTex` /
  `SearchTex` LUT textures + exposure-adaptation history buffer
  allocated via the device-aware
  `PostProcessSystem::Initialize(device)` overload + survive-rebuild
  contract test for the retained LUTs).
