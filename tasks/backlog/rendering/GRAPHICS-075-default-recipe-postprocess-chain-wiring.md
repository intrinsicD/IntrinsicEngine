# GRAPHICS-075 — Default-recipe postprocess chain wiring (Histogram → Bloom → ToneMap → FXAA/SMAA)

## Goal
- Wire the full postprocess chain into the renderer executor under the default recipe per `GRAPHICS-013A`/`013AQ`: pipelines for histogram (compute), bloom downsample/upsample mip chain, tonemap, FXAA, and SMAA edge/blend/resolve created at renderer init; `PostProcessSystem` allocates the retained `AreaTex`/`SearchTex` LUT textures and the exposure-adaptation history buffer; transient `PostProcess.BloomScratch`, `PostProcess.Histogram`, `PostProcess.AATemp` declared by the recipe; executor routes each pass through the recorded `SkippedNonOperational` / `SkippedUnavailable` / `Recorded` taxonomy.

## Non-goals
- No TAA (`GRAPHICS-040` planning).
- No upscaler / `IReconstructor` seam (`GRAPHICS-040`).
- No new postprocess shaders; reuses `assets/shaders/post_*.{frag,comp}` per `GRAPHICS-013A` / `013AQ`.
- No recipe-feature-gate change beyond exposing the existing `PostProcessSettings::AntiAliasing` selector.

## Context
- Status: not started.
- Owner/layer: `graphics/renderer` (pipelines + executor routes), `PostProcessSystem` (retained resources).
- Planning anchors: `tasks/done/GRAPHICS-013A-postprocess-chain.md`, `tasks/done/GRAPHICS-013AQ-postprocess-backend-clarifications.md`.
- Today: `Pass.PostProcess.{Histogram,Bloom,ToneMap,FXAA,SMAA}.cpp` exist as shells; `PostProcessSystem` exists with settings/diagnostics/push-constant data but no GPU resources allocated; the executor lambda has no postprocess branches.
- Bloom: one frame-transient `PostProcess.BloomScratch` mip-chain texture (≤ 6 mips). Histogram: 256-bin layout over `[-10, +10]` log2 stops; readback via `Picking.Readback` drain pattern (mirror per `GRAPHICS-013AQ`). FXAA/SMAA mutually exclusive per `PostProcessSettings::AntiAliasing`.

## Required changes
- [ ] In `PostProcessSystem::Initialize(device)`: allocate the SMAA `AreaTex` (`R8G8_UNORM`, 160×560) and `SearchTex` (`R8_UNORM`, 256×33) LUT textures via `RHI::TextureManager::Create(...)`; upload their LUT bytes via `IDevice::GetTransferQueue().UploadTexture()`; allocate the exposure-adaptation history buffer (`previous_average_log_lum`, `adaptation_velocity`, `frame_index`).
- [ ] `PostProcessSystem::Shutdown()` frees all retained resources.
- [ ] In `NullRenderer::InitializeOperationalPassResources(device)`, create:
  - histogram compute pipeline (`post_histogram.comp`),
  - bloom downsample + upsample pipelines (`post_bloom_downsample.frag` + `post_bloom_upsample.frag` with a fullscreen vertex),
  - tonemap pipeline (`post_tonemap.frag`),
  - FXAA pipeline (`post_fxaa.frag`),
  - SMAA edge/blend/resolve pipelines (`post_smaa_edge.frag`, `post_smaa_blend.frag`, `post_smaa_resolve.frag`).
- [ ] Add executor branches `"Pass.PostProcess.Histogram"`, `"…Bloom"`, `"…ToneMap"`, `"…FXAA"`, `"…SMAA"` routing through corresponding `RecordPostProcess*Pass(...)` helpers with the recorded taxonomy. AA selection per `PostProcessSettings::AntiAliasing` (FXAA xor SMAA).
- [ ] Implement the histogram readback drain on `BeginFrame()` after the issuing frame's fences complete; surface results through `PostProcessSystem::PublishHistogramReadback(...)` (mirror the `Picking.Readback` drain).
- [ ] Confirm `BuildDefaultFrameRecipe` declares `SceneColorHDR`, `SceneColorLDR`, `PostProcess.BloomScratch`, `PostProcess.Histogram`, `PostProcess.AATemp.Edges`, `PostProcess.AATemp.Weights` with the recorded formats.

## Tests
- [ ] `contract;graphics` test: pipeline + retained-resource creation succeeds at renderer init; `PostProcessDiagnostics` reports zero failure counters.
- [ ] `contract;graphics` test: with the chain enabled, executor records each pass in order and reports `Recorded`.
- [ ] `contract;graphics` test: barrier sequence between bloom mips and tonemap is correct (`SceneColorHDR` ColorAttachment → ShaderRead → ColorAttachment for the next mip).
- [ ] `contract;graphics` test: histogram readback drain calls `PostProcessSystem::PublishHistogramReadback` after the issuing frame's fence completes.
- [ ] `contract;graphics` test: `AreaTex`/`SearchTex` survive `RebuildGpuResources()` byte-identical.
- [ ] `contract;graphics` test: `PostProcessSettings::AntiAliasing` toggle correctly selects FXAA vs SMAA exclusively.

## Docs
- [ ] Update `src/graphics/renderer/README.md` to record postprocess as operationally wired.
- [ ] Update `docs/architecture/rendering-three-pass.md` if pipeline-order step numbers shift.

## Acceptance criteria
- [ ] Each postprocess pass records or `SkippedUnavailable` deterministically.
- [ ] Histogram readback drain produces deterministic CPU-visible results.
- [ ] No regression in CPU/null tests for non-postprocess passes.

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
- Allocate retained resources, create the pipelines, wire the executor routes + drain, exercise the contract tests above.
