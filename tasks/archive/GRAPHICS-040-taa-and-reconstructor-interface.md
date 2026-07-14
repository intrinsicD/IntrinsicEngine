# GRAPHICS-040 — TAA pass and reconstructor/upscaler interface seam (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

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

## Recorded decisions
1. **Jitter sequence.** Lock a **Halton(2,3) sequence of length 16**, advanced once per rendered frame and wrapped modulo the length. The jitter is injected as a **projection-matrix override** (a sub-pixel translation on the projection's `[2][0]`/`[2][1]` terms scaled by `2/renderWidth`, `2/renderHeight`), **not** a viewport offset, so depth/motion/raster all agree on the same clip-space sample center. The active offset is recorded into `CameraSnapshot::JitterOffset` (NDC units) so motion-vector reconstruction and the reconstructor hints read one authoritative value. Rationale: Halton(2,3)×16 is the canonical low-discrepancy TAA sequence (UE/Frostbite); the projection override keeps the jitter invisible to passes that do not opt in and avoids viewport-rounding artifacts; carrying the offset in the snapshot keeps the runtime→graphics contract the single source of truth.
2. **Motion-vector buffer.** A new frame-graph-managed **`R16G16_SFLOAT`** target produced by the surface/G-buffer pass, storing the per-pixel screen-space delta (current NDC → previous NDC, jitter-removed) in NDC units. Dynamic geometry writes its delta from the previous-frame model-view-projection carried per-instance; static geometry uses the camera-only reprojection of stored depth. `R16G16_SFLOAT` is chosen over `SNORM` so large off-screen motion does not clamp. Rationale: half-float screen-space MVs are the standard TAA input; separating dynamic (per-instance prev-MVP) from static (camera reprojection) avoids ghosting on animated meshes while keeping static draws cheap.
3. **History-color buffer.** Retained graphics-owned **ping-pong pair** with a `framesInFlight` retire deadline, format **`R16G16B16A16_SFLOAT`** (baseline). `R11G11B10_FLOAT` is rejected as the default because the history feeds variance clipping in HDR and the 5-bit-mantissa green/blue channels visibly bias dark-region neighborhood clipping; the half-bandwidth format is recorded as an opt-in quality/perf knob, not the default. Rationale: TAA quality is dominated by history precision; the ping-pong + retire-deadline pattern matches the existing SMAA `AreaTex`/`SearchTex` retained-resource discipline from `GRAPHICS-013AQ`.
4. **`IReconstructor` interface.** Locked CPU-public surface `Apply(JitteredColor, Depth, MotionVectors, HistoryColor, Output, ReconstructionHints) -> ReconstructionResult`. `ReconstructionHints` carries `Sharpness`, `Exposure`, `JitterOffset`, `FrameIndex`, `InputExtent`, `OutputExtent`, and a `Reset` flag (camera cut). `ReconstructionResult` reports `Applied`, `DisocclusionPercent`, and a fail-closed reason. No vendor types, no `Vk*` types, no SDK handles appear in the public surface. Rationale: a single resolution-aware seam lets every upscaler/denoiser be a drop-in; keeping it vendor-free preserves the `graphics → no vendor SDK` layering invariant.
5. **Reference TAA implementation.** The first and only concrete `IReconstructor` in this roadmap is an **in-engine reference TAA**: Karis-style temporal resolve with a **5×5 neighborhood YCoCg variance clip** of the reprojected history, exposure-aware luminance weighting, and a `Reset`-driven history-invalidate. Vendor backends (DLSS/FSR/XeSS/MetalFX) are later `Impl-D` children, one per vendor, opened only when actually integrated. Rationale: an in-engine reference keeps the seam testable and shippable with zero external dependencies and gives vendor backends a parity target.
6. **Recipe selection.** The frame recipe selects exactly one of `{ NoAA, FXAA, SMAA, TAA, ExternalReconstructor }`. `FXAA`/`SMAA` passes are **unchanged**; selecting `TAA`/`ExternalReconstructor` enables jitter + motion-vector production and routes the post chain's resolve slot to the reconstructor. The selector is an explicit recipe-build input (not the retired `RenderConfig::FrameRecipe` field). Rationale: a single enum keeps AA mutually exclusive and lets golden-image tests pin each mode without touching the others.
7. **Resolution policy.** The reconstructor declares `InputExtent` vs `OutputExtent`; the frame graph allocates `SceneColorHDR`/depth/motion at input resolution and the post-reconstruction chain (tone map, debug view, UI/ImGui, present) at output resolution. When `InputExtent == OutputExtent` the reconstructor runs as pure AA. Rationale: making resolution an explicit declared contract lets ML upscalers slot in without the rest of the graph guessing extents; native-res AA is just the 1:1 case.
8. **Disocclusion handling.** The reference TAA falls back to **neighborhood-clamped current-frame color** (no history blend) when motion-vector reprojection confidence is low (depth/MV disagreement beyond a threshold or off-screen history sample); vendor reconstructors handle disocclusion internally and the engine does not second-guess them. The disocclusion fraction is reported through `ReconstructionResult::DisocclusionPercent`. Rationale: clamping to clipped current color is the standard ghost-free fallback; deferring to vendor logic avoids double-correction.
9. **Diagnostics.** Per-frame `ReconstructorAppliedFrames` (monotonic), `HistoryDisocclusionPercent`, and `JitterOffsetX/Y`, recorded into `RenderDiagnostics`. Atomic/scalar writes only; no per-frame strings. Rationale: matches the existing renderer diagnostics discipline and gives the editor an observable AA state without a parallel readback path.
10. **Layering.** Vendor SDKs (DLSS/FSR/XeSS/MetalFX/NRD) live behind `IReconstructor` implementations in **separate, optional, build-gated modules**; the promoted `graphics` layer never imports a vendor SDK and compiles fully without one. The reference TAA lives in `graphics/renderer`. Rationale: preserves AGENTS.md §2 (`graphics/*` depends only on core, asset IDs, `graphics/rhi`, geometry GPU views) and keeps CI dependency-free.
11. **Test split.** `contract;graphics` (null RHI) for jitter-sequence determinism (Halton replay), motion-vector target shape, history ping-pong/retire-deadline lifetime, and recipe selection (each AA mode compiles its expected pass set); opt-in `gpu;vulkan` smoke for reference-TAA resolve correctness. Rationale: every CPU-testable contract is locked under the default gate; only shader-output correctness needs a device.
12. **Test-mode determinism.** A **`NoJitterNoHistory`** recipe flag forces zero jitter and a single-frame passthrough resolve so golden-image tests are deterministic frame-to-frame. Recorded as a recipe-build flag consumed by the jitter and reconstructor stages. Rationale: temporal accumulation is inherently non-deterministic across frames; an explicit golden-image mode keeps reference tests stable without disabling the feature path.

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
- [x] The reconstructor-seam section for `docs/architecture/rendering-three-pass.md` is deferred to the implementation children (`GRAPHICS-040-Impl-A/B/C`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The post-process / AA section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.
- [x] The `docs/migration/nonlegacy-parity-matrix.md` AA row update is deferred to the implementation children for the same reason.

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

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve TAA/reconstructor decisions are recorded with explicit answers and trade-off rationales: the Halton(2,3)×16 projection-override jitter recorded in `CameraSnapshot::JitterOffset`, the `R16G16_SFLOAT` dynamic/static motion-vector buffer, the retained `R16G16B16A16_SFLOAT` ping-pong history with `framesInFlight` retire deadline, the vendor-free `IReconstructor::Apply(...)` surface with resolution-aware hints, the in-engine Karis YCoCg-variance-clip reference TAA, the `{ NoAA, FXAA, SMAA, TAA, ExternalReconstructor }` recipe selector that leaves FXAA/SMAA unchanged, the input-vs-output resolution policy, the neighborhood-clamp disocclusion fallback, the three `RenderDiagnostics` fields, the build-gated vendor-SDK layering rule, the test split, and the `NoJitterNoHistory` golden-image determinism flag. Implementation children `GRAPHICS-040-Impl-A..D` are identified but not opened; no vendor SDK is imported and the FXAA/SMAA recipes are untouched. Per AGENTS.md §9 the architecture-doc/README/parity-matrix updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No vendor SDK imports in promoted graphics modules.
- No removal of FXAA / SMAA passes.
- No coupling of jitter to post-process chain ordering beyond the recorded recipe.
- No mixing of mechanical file moves with semantic refactors.
