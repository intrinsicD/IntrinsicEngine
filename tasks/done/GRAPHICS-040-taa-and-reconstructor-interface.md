# GRAPHICS-040 — TAA pass and reconstructor/upscaler interface seam (planning)

## Goal
Lock down a temporal anti-aliasing pass with sub-pixel jitter, motion-vector buffer, history color buffer, and a backend-agnostic `IReconstructor` interface that lets DLSS / FSR / XeSS / MetalFX / neural denoisers (NRD/OIDN/Ray Reconstruction) plug into the same seam without disturbing the post-processing chain or the existing FXAA/SMAA path. Planning only — no jitter/history bodies and no third-party SDK integration here.

## Non-goals
- No vendor-specific SDK integration (DLSS, FSR, XeSS, MetalFX). Each is a future implementation child of this seam, not part of this slice.
- No frame generation (covered by `GRAPHICS-058`).
- No removal of existing FXAA / SMAA passes; they remain alternate AA recipes.
- No motion-vector consumers beyond the AA/reconstructor seam in this slice (visualization overlays, motion blur deferred).
- No CPU-side history reprojection; entirely GPU-resident.

## Context
- Status: done (2026-05-18, branch `claude/graphics-rendering-tasks-dKlmC`).
- Commit reference: pending current change.
- Owner layer: `graphics/renderer` (TAA pass + reconstructor interface + jitter sequence), `graphics/framegraph` (history-buffer lifetime), `graphics/rhi` (storage-image surface).
- Today the post chain runs Histogram → Bloom → ToneMap → FXAA/SMAA. Modern engines treat AA as a *reconstruction* problem: render at lower resolution, jitter sub-pixel, and reconstruct using temporal history. The same pipeline slot accepts ML upscalers as drop-ins.
- The reconstructor seam is the "one interface, many backends" pattern: `IReconstructor::Apply(jittered, depth, motion, history) -> resolved`.
- Cross-links: `GRAPHICS-013A` (postprocess chain), `GRAPHICS-036` (pipelined frames is a prerequisite for clean motion vectors), `GRAPHICS-046` (ray-traced GI denoise plugs into the same seam), `GRAPHICS-058` (frame generation builds on motion vectors).

## Design decisions to record
1. **Jitter sequence.** Lock the sub-pixel jitter pattern (suggested Halton(2,3) length 8 or 16). Decide where the jitter is injected (camera projection matrix override vs. viewport offset). Recorded in `CameraSnapshot`.
2. **Motion-vector buffer.** New frame-graph-managed `R16G16_SFLOAT` (or `R16G16_SNORM`) target produced by the surface pass. Records per-pixel screen-space delta from previous frame. Decide handling of dynamic vs. static geometry.
3. **History-color buffer.** Retained graphics-owned, ping-pong pattern with retire-deadline. Decide format (`R16G16B16A16_SFLOAT` baseline, or `R11G11B10_FLOAT` to halve bandwidth at the cost of HDR clipping).
4. **`IReconstructor` interface.** Locked surface: `Apply(JitteredColor, Depth, MotionVectors, HistoryColor, Output, Hints) -> ReconstructionResult`. Hints carry sharpness, exposure, jitter offset, frame index, target resolution. No vendor types in the public surface.
5. **Reference TAA implementation.** The first concrete `IReconstructor` is an in-engine reference TAA (Karis-style, 5×5 neighborhood clipping with YCoCg variance). Vendor backends are later children.
6. **Recipe selection.** The frame recipe selects `{ NoAA, FXAA, SMAA, TAA, ExternalReconstructor }`. Existing FXAA/SMAA passes remain unchanged. Record the rule.
7. **Resolution policy.** Reconstructor declares input vs. output resolution; the frame graph allocates accordingly. Surface pass renders at input resolution; post-reconstruction passes (tone map, UI) run at output resolution.
8. **Disocclusion handling.** Reference TAA falls back to neighborhood clipping when motion-vector confidence is low. Vendor reconstructors handle internally. Record the rule.
9. **Diagnostics.** `ReconstructorAppliedFrames`, `HistoryDisocclusionPercent`, `JitterOffsetX/Y`. Per-frame recorded into `RenderDiagnostics`.
10. **Layering.** Vendor SDKs (DLSS/FSR/XeSS/MetalFX) live behind `IReconstructor` implementations under separate, optional, build-gated modules. The promoted graphics layer never imports a vendor SDK.
11. **Test split.** `contract;graphics` for jitter sequence determinism, motion-vector shape, history-buffer lifetime, recipe selection; opt-in `gpu;vulkan` smoke for reference TAA correctness.
12. **Test-mode determinism.** A "no-jitter / no-history" mode for golden-image tests. Recorded as a recipe flag.

## Recorded decisions

1. **Jitter sequence.** Halton(2, 3) of length 8 per output-resolution pixel. Rationale: 8 samples is the canonical TAA history depth and matches Karis-style 5×5 clipping; 16 samples buys diminishing accuracy after the history-clip fallback fires. Jitter is injected via a *camera projection matrix override*: runtime extraction computes `jitterOffset = HaltonJitter(frameIndex % 8) / outputResolution`, writes `CameraViewSnapshot::JitterOffsetNdc = jitterOffset`, and the surface pass's shader reads it as `projectionMatrix[2].xy += jitter.xy * 2`. Projection-matrix override (vs. viewport offset) keeps the depth/motion-vector geometry consistent across passes — viewport offset would force every depth-consumer to subtract jitter manually. Rejected: viewport offset (consumer fragility); per-frame random jitter (loses the low-discrepancy benefit and breaks test determinism).
2. **Motion-vector buffer.** New frame-graph-managed `MotionVectors` at format `R16G16_SFLOAT`. Layout: per-pixel screen-space delta `(currentPos - previousPos)` in NDC units, where positive X = right, positive Y = up. Produced by the surface pass via a new fragment-shader output (gated on `MaterialFlags::ProducesMotion`, which is set by default for the GpuWorld surface material). Dynamic geometry: per-instance previous-frame transform is uploaded alongside the current transform in `GpuInstanceTransform`. Static geometry: per-instance previous transform = current transform, so motion = camera-only motion. Rejected: `R16G16_SNORM` (loses dynamic range for large camera moves; signed-float keeps fast-camera-pan accuracy); separate "static" vs "dynamic" motion buffers (doubles bandwidth for no gain since the reconstructor reads one combined buffer).
3. **History-color buffer.** Retained graphics-owned `HistoryColor` with ping-pong (`HistoryColor.Read` / `HistoryColor.Write`); the two handles swap at `EndFrame()` retire time via the existing `framesInFlight` retire-deadline pattern from `GRAPHICS-015Q`. Format: `R16G16B16A16_SFLOAT` baseline. Rationale: `R11G11B10_FLOAT` clips highlights above 64.0 (no alpha channel for blend weight either), and TAA history is precisely where HDR clipping artifacts compound across frames. The bandwidth cost (~16 MB at 1080p × 2 ping-pong = ~32 MB) is acceptable for a reconstruction-quality target. Rejected: `R11G11B10_FLOAT` (HDR clipping); single buffer with in-place update (read-write hazard, kills async-compute affinity for the reconstructor).
4. **`IReconstructor` interface.** Locked C++ surface in `src/graphics/renderer/Graphics.Reconstructor.cppm`:
   ```
   struct ReconstructionHints {
       float Sharpness;            // [0, 1]
       float Exposure;             // scene exposure scalar
       vec2  JitterOffsetNdc;
       uint64_t FrameIndex;
       uvec2 InputResolution;
       uvec2 OutputResolution;
   };
   struct ReconstructionResult {
       bool   Success;
       uint32_t DisocclusionMaskCount;  // pixels classified as disoccluded
       uint64_t LastReasonCode;          // backend-specific, opaque to graphics
   };
   class IReconstructor {
     public:
       virtual ~IReconstructor() = default;
       virtual ReconstructionResult Apply(
           ResourceHandle JitteredColor,
           ResourceHandle Depth,
           ResourceHandle MotionVectors,
           ResourceHandle HistoryRead,
           ResourceHandle HistoryWrite,
           ResourceHandle Output,
           const ReconstructionHints& hints) = 0;
       virtual ReconstructorCapabilities GetCapabilities() const = 0;
   };
   ```
   No `Vk*` types, no vendor SDK types. `ResourceHandle` is the existing `Extrinsic.Graphics.RHI` opaque handle. Hints are read-only POD; the result carries opaque backend reason codes for diagnostics.
5. **Reference TAA implementation.** First `IReconstructor` is `ReferenceTAAReconstructor` (Karis-style), a pure compute shader pass at `assets/shaders/aa/taa_reference.comp`: 5×5 neighborhood color clipping in YCoCg space using AABB statistics (min, max, variance), motion-vector reprojection of `HistoryRead`, blend-weight = `clamp(1 / (1 + motionMagnitude * 60), 0.05, 0.95)`. No vendor SDK; ships in-engine. The reference TAA is also the determinism baseline for the test-mode no-jitter recipe (Decision 12) — running it with `JitterOffsetNdc = (0, 0)` and `motion = 0` reduces to a pass-through.
6. **Recipe selection.** `FrameRecipe::AAMode` enum: `{ None, FXAA, SMAA, TAA, ExternalReconstructor }`. Default = `FXAA` (current behavior). When `TAA`, the recipe inserts `Pass.Reconstruct.TAA` between `Pass.ToneMap` and the (now-skipped) `Pass.FXAA/SMAA`. When `ExternalReconstructor`, the recipe inserts `Pass.Reconstruct.External` and the renderer expects a runtime-installed `IReconstructor` via `IRenderer::SetReconstructor(std::unique_ptr<IReconstructor>)`; otherwise it falls back to `TAA` and increments `ReconstructorFallbackToTaaCount`. FXAA/SMAA passes remain unchanged and the recipe still routes through them when `AAMode = FXAA/SMAA`.
7. **Resolution policy.** Two resolutions: `InputResolution` (where the surface pass renders) and `OutputResolution` (display). Default: `InputResolution == OutputResolution` (no upscaling). When a reconstructor declares `Capabilities.SupportsUpscaling`, the recipe sets `InputResolution = OutputResolution * 1/Scale` (e.g. 0.5 for 4× area, 0.667 for "Quality" mode). Frame graph passes `Pass.Surface.*`, `Pass.Lighting.*`, `Pass.Cull.*`, `Pass.ToneMap` run at `InputResolution`; `Pass.Reconstruct.*`, `Pass.UI`, `Pass.Present` run at `OutputResolution`. Resource declarations in the frame graph carry which resolution they live in; mismatches fail graph compile with a `ResolutionMismatch` `RenderGraphValidationResult` finding.
8. **Disocclusion handling.** Reference TAA: when motion-vector confidence is low (computed as `previousFrameDepth - reprojectedDepth > epsilon`) the pass falls back to 5×5 neighborhood color clipping (clamps `HistoryRead` to the current-frame neighborhood AABB) and reduces the blend weight toward `1.0` (current-frame-only). `ReconstructionResult::DisocclusionMaskCount` reports the per-frame count of disoccluded pixels. Vendor reconstructors handle disocclusion internally and report through the same field. The clipping in YCoCg space is preferred over RGB to avoid color-cast bleed from large neighborhood AABBs.
9. **Diagnostics.** Atomic counters on `ReconstructorDiagnostics`: `ReconstructorAppliedFrames`, `ReconstructorSkippedFrames` (e.g. when no reconstructor is installed and the recipe falls back), `ReconstructorFallbackToTaaCount`, `HistoryDisocclusionPercent` (last-frame `DisocclusionMaskCount / totalPixels * 100`, recorded as a `float` snapshot, not atomic), `JitterOffsetX` / `JitterOffsetY` (last-frame jitter offset for shader-debug correlation). All `std::atomic<uint64_t>` (and one `std::atomic<float>` for the percent) zeroed on engine `Initialize()`.
10. **Layering.** Vendor SDK adapters live in **separate optional build-gated modules** outside the promoted graphics layer: `src/integrations/dlss/`, `src/integrations/fsr/`, etc., gated by `INTRINSIC_ENABLE_<VENDOR>=ON` CMake options that default to `OFF`. Each adapter implements `IReconstructor`; the integration module links against the vendor SDK; promoted graphics never imports the SDK. Runtime installs the adapter via `IRenderer::SetReconstructor()`. The graphics layer's compile-and-test path requires zero vendor SDKs.
11. **Test split.** `contract;graphics` tests cover (a) Halton(2,3)-length-8 jitter sequence determinism, (b) motion-vector buffer producer/consumer wiring under null RHI, (c) history ping-pong swap + retire-deadline reclamation, (d) recipe selection (`None`/`FXAA`/`SMAA`/`TAA`/`ExternalReconstructor` produce the expected pass list, with fallback when no reconstructor is installed), (e) resolution policy graph-compile rejection of mismatches, (f) reference-TAA disocclusion mask propagation via a CPU mock of `IReconstructor`. Opt-in `gpu;vulkan` smoke (`tests/integration/graphics/Test.ReferenceTAA.cpp`) drives a fixture scene with synthetic camera motion through reference TAA and asserts the output converges to ground truth within tolerance. Excluded from the default CPU gate.
12. **Test-mode determinism.** A recipe flag `FrameRecipe::AAOptions::DisableJitterAndHistory = true` collapses TAA to a pass-through (jitter offset = 0, blend weight = 1, no history read). Used by golden-image regression tests under `tests/regression/graphics/` where deterministic per-frame output matters. The flag is recipe-level (not a global engine flag) so the reference scene + test scenes can opt in independently.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-040-Impl-A** — Camera jitter + motion-vector buffer + null-RHI shape tests.
- **GRAPHICS-040-Impl-B** — `IReconstructor` interface + reference TAA implementation + `contract;graphics` tests.
- **GRAPHICS-040-Impl-C** — Recipe selection + post-chain integration + integration tests.
- **GRAPHICS-040-Impl-D** — Optional vendor backend hookpoints (one child per vendor; opened only when actually integrated).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] `docs/architecture/rendering-three-pass.md` post-process reconstructor-seam row is deferred to Impl-A/B/C landing (planning slice forbids code changes; doc rows describe wired behavior).
- [x] `src/graphics/renderer/README.md` post-process / AA section update is deferred to the same.
- [x] `docs/migration/nonlegacy-parity-matrix.md` AA row update is deferred to the same; the matrix continues to record FXAA/SMAA parity status unchanged in this slice.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] The promoted graphics layer requires no vendor SDK to compile.
- [x] FXAA / SMAA recipes continue to compile and pass without modification.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No vendor SDK imports in promoted graphics modules.
- No removal of FXAA / SMAA passes.
- No coupling of jitter to post-process chain ordering beyond the recorded recipe.
- No mixing of mechanical file moves with semantic refactors.
