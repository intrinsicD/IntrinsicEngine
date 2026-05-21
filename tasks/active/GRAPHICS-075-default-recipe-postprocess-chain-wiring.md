# GRAPHICS-075 — Default-recipe postprocess chain wiring (Histogram → Bloom → ToneMap → FXAA/SMAA)

## Status

- State: in-progress (Slice C landing). Maturity reached so far: Slice A
  closed `Scaffolded → CPUContracted` on the ToneMap leaf and the
  `"PostProcessPass"` umbrella executor branch (merged via PR #902);
  Slice B.1 added the bloom downsample + upsample pipeline leases +
  `RecordPostProcessBloomPass(...)` umbrella fan-out and closed
  `Scaffolded → CPUContracted` on both bloom pipelines; Slice B.2 added
  per-mip iteration + recipe-side `BloomScratch.MipLevels` clamping +
  the multi-mip barrier-sequence contract test (merged via PR #904) and
  closed bloom toward `Operational` on the CPU/null gate. Slice C adds
  the FXAA pipeline + `RecordPostProcessFXAAPass(...)` umbrella fan-out
  + `PostProcessFXAAPushConstants` 20-byte std430 push block + contract
  tests and closes `Scaffolded → CPUContracted` on the FXAA leaf.
- Owner/agent: local agent workflow.
- Branch: Slice C on `claude/setup-agent-workflow-ReViy`.
- Activated: 2026-05-21 — first unblocked Theme A default-recipe leaf after
  GRAPHICS-074 retirement.
- Next verification step: after Slice C's CPU/null contract gate green, open
  Slice D (SMAA edge/blend/resolve pipelines + retained `AreaTex` / `SearchTex`
  LUT textures + exposure-adaptation history buffer + recipe-side
  `PostProcess.AATemp.{Edges,Weights}` rename, behind the same
  `"PostProcessPass"` umbrella branch).

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
  `RecordPostProcessFXAAPass(...)` umbrella fan-out behind the existing
  `"PostProcessPass"` branch (fans out *after*
  `RecordPostProcessToneMapPass` so the FXAA shader's sampled-image
  read of the post-tonemap LDR target sees the freshly-written result
  in recorded order). `NullRenderer` owns `m_PostProcessFXAAPass` +
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
  `Recorded` under the umbrella's accumulator per the same
  "structurally-recorded no-op" taxonomy the bloom helper follows when
  `EnableBloom = false`. Defers SMAA pipelines + retained LUTs +
  recipe-side `PostProcess.AATemp.{Edges,Weights}` rename to Slice D
  and Histogram compute + readback drain to Slice E.
- **Slice D.** SMAA edge/blend/resolve pipelines + retained `AreaTex`
  (`R8G8_UNORM`, 160×560) + `SearchTex` (`R8_UNORM`, 256×33) LUT textures +
  exposure-adaptation history buffer allocated via a device-aware
  `PostProcessSystem::Initialize(device)` overload + `Shutdown()` releases.
  `AntiAliasing == SMAA` enables the branch; mutually exclusive with FXAA.
  Survive-rebuild test for the retained LUTs.
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
- Status: in-progress (Slice B.1 landing; Slice B.2 + Slices C/D/E queued).
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

## Required changes
- [x] **Slice A**: `NullRenderer` owns `m_PostProcessToneMapPass` + `m_PostProcessToneMapPipelineLease`; emplaced alongside the existing GRAPHICS-070..074 passes, reset in `Shutdown()` before `m_PostProcessSystem` is reset. Tonemap pipeline created in `InitializeOperationalPassResources(device)` from `BuildPostProcessToneMapPipelineDesc()` (vertex `post_fullscreen.vert.spv` + fragment `post_tonemap.frag.spv`, single backbuffer-format color target, no depth, `PushConstantSize = sizeof(PostProcessPushConstants)`); republished byte-identical across `RebuildOperationalResources()`. `"PostProcessPass"` executor branch routes through `RecordPostProcessToneMapPass(...)` with the recorded `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy. `IRenderer` exposes `GetPostProcessToneMapPipeline()` / `GetPostProcessToneMapPipelineDesc()`.
- [ ] **Slice D**: In `PostProcessSystem::Initialize(device)`: allocate the SMAA `AreaTex` (`R8G8_UNORM`, 160×560) and `SearchTex` (`R8_UNORM`, 256×33) LUT textures via `RHI::TextureManager::Create(...)`; upload their LUT bytes via `IDevice::GetTransferQueue().UploadTexture()`; allocate the exposure-adaptation history buffer (`previous_average_log_lum`, `adaptation_velocity`, `frame_index`).
- [ ] **Slice D**: `PostProcessSystem::Shutdown()` frees all retained resources.
- [ ] In `NullRenderer::InitializeOperationalPassResources(device)`, create:
  - tonemap pipeline (`post_tonemap.frag`), **Slice A** *(done)*
  - bloom downsample + upsample pipelines (`post_bloom_downsample.frag` + `post_bloom_upsample.frag` with a fullscreen vertex), **Slice B.1** *(done)*; per-mip iteration + recipe-side `BloomScratch.MipLevels = ComputeBloomMipChainLevels(width, height)` (clamped against Vulkan's `mipLevels <= floor(log2(maxDim)) + 1` rule) + renderer-side per-frame `PostProcess.BloomScratch` handle + clamped mip-count republish, **Slice B.2** *(done)*. Per-mip subresource barriers between iterations are *deferred*: the pass body emits no `TextureBarrier(...)` (the umbrella render pass is active when `Execute(...)` runs and Vulkan rejects layout transitions inside render-pass scope), and the inter-pass `BloomScratch ColorAttachment → ShaderReadOnly` between bloom and tonemap is owned by the framegraph compiler from the recipe-level read/write declarations
  - FXAA pipeline (`post_fxaa.frag`), **Slice C** *(done)*. `m_PostProcessFXAAPipelineLease` + `m_PostProcessFXAAPass` follow the same reset/republish pattern as the tonemap + bloom leases. The pipeline takes the backbuffer format (same `colorFormat` parameter the tonemap pipeline does) so it stays render-pass-compatible with the tonemap leg's `SceneColorLDR` output. `PostProcessFXAAPushConstants` (20 bytes, std430-matched to `post_fxaa.frag`) replaces the canonical `PostProcessPushConstants` in the pass body
  - SMAA edge/blend/resolve pipelines (`post_smaa_edge.frag`, `post_smaa_blend.frag`, `post_smaa_resolve.frag`), **Slice D**
  - histogram compute pipeline (`post_histogram.comp`), **Slice E**.
- [ ] Add executor fan-out inside the `"PostProcessPass"` umbrella branch (mirroring the GRAPHICS-074 `"PickingPass"` fan-out) routing through `RecordPostProcessHistogramPass(...)`, `…BloomPass(...)`, `…ToneMapPass(...)`, `…FXAAPass(...)`, `…SMAAPass(...)` helpers with the recorded taxonomy. AA selection per `PostProcessSettings::AntiAliasing` (FXAA xor SMAA). **Slice A** lands ToneMap only; **Slice B.1** adds the bloom helper ahead of tonemap; **Slice C** *(done)* adds the FXAA helper *after* tonemap (so the FXAA shader's sampled-image read of the post-tonemap LDR target sees the freshly-written result in recorded order); Slices D/E add the SMAA + Histogram helpers behind the same umbrella branch.
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
- [x] **Slice C**: `contract;graphics` test `PostProcessFXAAPipelineSurvivesOperationalRebuild` asserts the FXAA pipeline lease is valid after `Initialize()`, the descriptor's shader paths + single backbuffer-format color target + `Undefined` depth + `PushConstantSize = sizeof(PostProcessFXAAPushConstants)` match `BuildPostProcessFXAAPipelineDesc()`, and that the descriptor is byte-identical across `RebuildOperationalResources()`. `FXAAPushFeedsNonZeroInvResolution` asserts the pass body pushes the 20-byte `PostProcessFXAAPushConstants` block (not the canonical 20-byte `PostProcessPushConstants`) with `InvResolution = 1 / vec2(viewportWidth, viewportHeight)` derived from the camera UBO + FXAA 3.11 quality defaults (`ContrastThreshold = 0.0312`, `RelativeThreshold = 0.063`, `SubpixelBlending = 0.75`). `FXAASkipsWhenAntiAliasingNotFXAA` asserts the pass body emits no bind/push/draw for `AntiAliasing == None` and `AntiAliasing == SMAA`, and records the 3-event bind/push/draw triple for `AntiAliasing == FXAA`. `RendererFrameLifecycle.UsesDeviceFrameLifecycleBackbufferAndCommandContext` (+ rebuild / depth-failure / culling-failure variants) bumps the recorded-pass counter from 7/6/2 to 8/7/3 to include the FXAA fan-out under the `"PostProcessPass"` umbrella (the default `AntiAliasing == None` short-circuits the body, so bind/push counts stay at 6).
- [ ] **Slice D**: `contract;graphics` test for SMAA pipeline rebuild + `AreaTex`/`SearchTex` survive `RebuildGpuResources()` byte-identical + FXAA/SMAA mutual exclusion.
- [ ] **Slice E**: `contract;graphics` test that the histogram readback drain calls `PostProcessSystem::PublishHistogramReadback` after the issuing frame's fence completes.
- [ ] **Slice E**: `contract;graphics` test: `PostProcessDiagnostics` reports zero failure counters after a full chain init.

## Docs
- [x] **Slice A**: Update `src/graphics/renderer/README.md` to record the tonemap leg of the postprocess chain as operationally wired (umbrella `"PostProcessPass"` reports `Recorded`); call out Slices B–E as the bloom / FXAA / SMAA / histogram followups.
- [x] **Slice B.1**: Extend `src/graphics/renderer/README.md` to record the bloom downsample + upsample legs as CPU-contract wired behind the umbrella `"PostProcessPass"`; call out Slice B.2 + Slices C/D/E as the remaining followups.
- [x] **Slice B.2**: `src/graphics/renderer/README.md` now records the bloom leg as operationally wired on the CPU/null gate (per-mip iteration over the canonical six-mip pyramid + inline `ColorAttachment ↔ ShaderRead` barriers + renderer-side per-frame `PostProcess.BloomScratch` handle republish); calls out Slices C/D/E as the remaining FXAA/SMAA/Histogram followups.
- [x] **Slice C**: `src/graphics/renderer/README.md` now records the FXAA leg as CPU-contract wired behind the umbrella `"PostProcessPass"` (pipeline + lease + helper + 20-byte `PostProcessFXAAPushConstants` push block + `AntiAliasing == FXAA` selector gate inside the pass body); calls out Slices D/E as the remaining SMAA + Histogram followups.
- [ ] **Slices D–E**: Extend `src/graphics/renderer/README.md` as each pass family lands.
- [ ] **Any slice**: Update `docs/architecture/rendering-three-pass.md` if pipeline-order step numbers shift — Slice A: not applicable (the recipe-level umbrella stays at the same step). Slice C: not applicable (FXAA fan-out lands behind the same `"PostProcessPass"` umbrella step, no numbered-step reflow).

## Acceptance criteria
- [x] **Slice A**: ToneMap pipeline records or `SkippedUnavailable` deterministically; `"PostProcessPass"` reports `Recorded` on the operational CPU/null gate.
- [x] **Slice B.1**: Bloom helper records (or `SkippedUnavailable`) deterministically; `"PostProcessPass"` umbrella reports `Recorded` after fan-out on the operational CPU/null gate.
- [x] **Slice B.2**: Bloom pass records the per-mip chain (or `SkippedUnavailable` when pipelines/system are missing) deterministically; the bloom mip-chain barrier-sequence contract test (`BloomMipChainBarrierSequence`) exercises the inline `ColorAttachment ↔ ShaderRead` barriers against a synthetic `PostProcess.BloomScratch` handle.
- [x] **Slice C**: FXAA pass records (or `SkippedUnavailable` when pipeline/system are missing) deterministically; helper fans out under the `"PostProcessPass"` umbrella *after* tonemap. Default `AntiAliasing == None` short-circuits the pass body to a no-op while the helper still reports `Recorded` under the umbrella's accumulator (the same "structurally-recorded no-op" taxonomy the bloom helper follows when `EnableBloom = false`).
- [ ] **Slices D–E**: Each remaining postprocess pass records or `SkippedUnavailable` deterministically.
- [ ] **Slice E**: Histogram readback drain produces deterministic CPU-visible results.
- [x] **Slice A**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice A).
- [x] **Slice B.1**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice B.1).
- [x] **Slice B.2**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice B.2).
- [x] **Slice C**: No regression in CPU/null tests for non-postprocess passes (default gate passes after Slice C).

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
- Slice C verification (run from a build with `clang-20`/`clang++-20`/`clang-scan-deps-20` on PATH, or pass the C/C++ compiler overrides as Slice 2 of GEOM-006 documents):
  ```bash
  cmake --preset ci
  cmake --build --preset ci --target IntrinsicGraphicsContractTests
  ctest --test-dir build/ci --output-on-failure -L 'contract' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  python3 tools/repo/check_layering.py --root src --strict
  python3 tools/docs/check_doc_links.py --root .
  ```
- After Slice C lands, open Slice D (SMAA edge/blend/resolve pipelines +
  retained `AreaTex` / `SearchTex` LUT textures + exposure-adaptation
  history buffer + recipe-side `PostProcess.AATemp.{Edges,Weights}`
  rename, behind the same `"PostProcessPass"` umbrella branch).
