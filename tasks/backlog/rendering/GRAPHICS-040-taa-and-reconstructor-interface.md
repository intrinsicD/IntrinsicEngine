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

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-040-Impl-A** — Camera jitter + motion-vector buffer + null-RHI shape tests.
- **GRAPHICS-040-Impl-B** — `IReconstructor` interface + reference TAA implementation + `contract;graphics` tests.
- **GRAPHICS-040-Impl-C** — Recipe selection + post-chain integration + integration tests.
- **GRAPHICS-040-Impl-D** — Optional vendor backend hookpoints (one child per vendor; opened only when actually integrated).

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/rendering-three-pass.md` post-process section with the reconstructor seam.
- Update `src/graphics/renderer/README.md` post-process / AA section.
- Update `docs/migration/nonlegacy-parity-matrix.md` AA row.

## Acceptance criteria
- Twelve decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- The promoted graphics layer requires no vendor SDK to compile.
- FXAA / SMAA recipes continue to compile and pass without modification.

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
