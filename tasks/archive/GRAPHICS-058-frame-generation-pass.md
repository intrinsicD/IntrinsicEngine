# GRAPHICS-058 — Frame generation pass (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children
  `GRAPHICS-058-Impl-A..C` stay unopened until the reference frame-generation
  path is scheduled; vendor-backend children open only when actually integrated.

## Goal
Lock down the contract for an optional frame-generation pass that synthesizes interpolated (or extrapolated) intermediate frames between two rendered frames using motion vectors, depth, and history color, exposed through the `IReconstructor`-style seam from `GRAPHICS-040` so vendor backends (DLSS Frame Generation, FSR 3 Frame Generation, XeSS 2 Frame Generation, MetalFX) plug in without disturbing the rendering path. Planning only — no interpolator bodies and no vendor SDK integration here.

## Non-goals
- No vendor SDK imports in promoted graphics layers.
- No removal of single-frame rendering paths; frame generation is opt-in per recipe.
- No reflex/anti-lag latency-reduction body — vendor-specific concerns are recorded as integration considerations only.
- No CPU-side frame interpolation.
- No support for frame generation without motion vectors; depends on `GRAPHICS-040`.

## Context
- Owner layer: `graphics/renderer` (frame-gen pass + presentation handoff), `graphics/rhi` (existing storage-image surface), `runtime/` (presentation pacing if needed).
- DLSS 3+ Frame Generation, FSR 3 Frame Generation, XeSS 2 Frame Generation, and MetalFX Frame Interpolation all share the same input set: two consecutive rendered frames + motion vectors + depth + camera params. The plug-in point is identical to the upscaler seam from `GRAPHICS-040`.
- Frame generation is the most latency-sensitive feature in this roadmap; it interacts with presentation pacing and input-to-photon latency. Record those concerns explicitly.
- Cross-links: `GRAPHICS-040` (motion vectors + reconstructor seam prerequisite), `GRAPHICS-036` (pipelined frames; frame-gen interacts with presentation timing), `GRAPHICS-013C` (present pass; frame-gen output replaces or augments the standard presented frame).

## Recorded decisions
1. **Input set.** Locked `FrameGenInputs` struct: `PrevColor`, `CurrColor`, `MotionVectors`, `Depth`, `CameraParamsDelta` (prev→curr view/proj delta), and `FrameTimeDelta`. Rationale: this is the exact input set DLSS-FG/FSR3-FG/XeSS2-FG/MetalFX all consume, so locking it as the canonical struct means every vendor backend and the in-engine reference share one input contract; carrying the camera-params delta and frame-time delta (not just the two color buffers) is what lets an interpolator reproject correctly and place the synthesized frame at the right temporal midpoint.
2. **Output kind.** `FrameGenOutputKind { Interpolated, Extrapolated }`. Default `Interpolated` (synthesizes a frame *between* the two rendered frames — higher visual quality, adds one rendered-frame of latency); `Extrapolated` (predicts a frame *after* the latest rendered frame — lower latency, more artifacts) is opt-in per recipe. Rationale: interpolation is the quality-first default that every shipping FG implementation uses, while extrapolation trades quality for latency on input-sensitive content; making the choice a per-recipe selection (decision 4) lets the application pick per workload without two separate pass implementations, and recording the latency cost of each up front ties directly to decision 7.
3. **Backend interface.** Reuse the `IReconstructor`-style seam from `GRAPHICS-040`: `IFrameGenerator::Generate(FrameGenInputs) -> InterpolatedColor`. Vendor backends are non-promoted implementations behind this interface. Rationale: frame generation shares the upscaler seam's exact plug-in shape (two frames + motion + depth + camera in, color out), so reusing the established `IReconstructor` pattern avoids a second vendor-plug-in mechanism and guarantees a vendor FG backend slots in the same way an upscaler does — with the SDK confined to a non-promoted impl per the layering rule.
4. **Recipe selection.** `FrameGenKind { Disabled, Interpolated, Extrapolated, ExternalBackend }`, default `Disabled` until the feature is ready. `Interpolated`/`Extrapolated` select the in-engine reference; `ExternalBackend` routes to a registered vendor `IFrameGenerator`. Rationale: a four-state recipe enum defaulting to `Disabled` keeps single-frame rendering the untouched default (acceptance criterion) and makes turning FG on an explicit per-recipe decision; separating the two reference modes from `ExternalBackend` lets the engine ship a correctness baseline without any vendor SDK while still exposing the vendor route.
5. **Reference implementation.** A minimal in-engine motion-compensated blend interpolator is the first concrete `IFrameGenerator` — a correctness/ship-without-vendor baseline, explicitly **not** a visual-quality target. Vendor backends are later `Impl-C` children. Rationale: an in-engine reference guarantees the FG seam is exercisable and testable on every host without a vendor SDK (mirroring the method-workflow "reference backend first" discipline), and labeling it a correctness baseline rather than a quality target sets the right expectation — vendor backends own visual quality, the reference owns "the seam works and produces a plausible frame".
6. **Presentation pacing.** Frame generation produces `2N` presented frames from `N` rendered frames; the presentation queue paces them with even spacing (CPU pacing policy owned by `runtime/`), with a vendor-pacing-API hook reserved for `ExternalBackend`. Rationale: unpaced FG frames cause visible judder (the synthesized frame must be presented at the temporal midpoint, not back-to-back with the real frame), so even spacing is the minimum-correct pacing; placing the CPU pacing policy in `runtime/` keeps presentation timing in the composition layer where it belongs (graphics produces the frame, runtime decides when it shows) and preserves the AGENTS.md §2 boundary.
7. **Latency concerns.** Documented explicitly: `Interpolated` adds one rendered-frame of input-to-photon latency (the current frame must be held until the interpolated frame is presented); latency-mitigation features (Reflex / Anti-Lag analogs) live in vendor backends, not promoted layers, and are out of scope here. Rationale: frame generation's defining trade-off is added latency, so recording the exact cost (one rendered frame for interpolation) and where mitigation lives prevents a future reader from assuming FG is free; keeping Reflex/Anti-Lag analogs in vendor backends matches the layering rule and the non-goal that forbids a latency-reduction body in promoted layers.
8. **Disocclusion handling.** The reference interpolator falls back to current-frame color wherever motion-vector confidence is low (disoccluded/newly-revealed regions); vendor backends handle disocclusion internally. Rationale: disocclusion is the primary FG artifact source, and a "fall back to the real current frame where motion is unreliable" rule is the simplest correctness-preserving behavior for the reference (no invented geometry, just the nearest real pixels), while leaving sophisticated inpainting to vendor backends keeps the reference minimal and predictable.
9. **Diagnostics.** `FrameGenAppliedFrames`, `FrameGenDisocclusionFraction`, and `FrameGenInterFrameLatencyMs` as per-frame counters. Rationale: these three surface whether FG is actually engaged (applied frames), the dominant artifact driver (disocclusion fraction — high values mean visible quality loss), and the latency cost the feature is paying (inter-frame latency ms), which together let a tuner judge whether FG is a net win on a given workload without any string output.
10. **Layering.** Vendor SDKs live behind `IFrameGenerator` implementations in non-promoted modules; promoted graphics layers contain the seam + the in-engine reference impl only; `runtime/` owns presentation pacing; no live ECS. Rationale: this split preserves AGENTS.md §2 — no vendor SDK in promoted graphics, the reference impl is the only in-engine FG body, and presentation pacing stays in the runtime composition layer — so the frame-generation contract introduces no new cross-layer edge.
11. **Test split.** `contract;graphics` for the `FrameGenInputs` shape, recipe selection, and presentation-pacing math under null RHI; opt-in `gpu;vulkan` smoke for reference-interpolator visual sanity. Rationale: the input contract, the recipe enum routing, and the 2N-from-N pacing schedule are all pure logic checkable under null RHI as `contract;graphics`, so the planning contract is fully testable on the default gate; only the actual interpolated-image visual sanity needs a device, so it is the single opt-in `gpu;vulkan` fixture and the default gate stays green.
12. **Determinism mode.** A "no frame-gen" recipe flag (equivalently `FrameGenKind::Disabled`) is the canonical golden-image test configuration, so deterministic golden-image tests never run through the interpolator. Rationale: synthesized frames are inherently non-deterministic across drivers/backends and would make golden-image comparison flaky, so pinning golden-image tests to the FG-disabled path keeps the deterministic test corpus stable while FG correctness is proven separately by its own `contract`/opt-in-`gpu` tests.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-058-Impl-A** — `IFrameGenerator` interface + reference motion-blend interpolator + `contract;graphics` tests.
- **GRAPHICS-058-Impl-B** — Recipe selection + presentation pacing wiring with `runtime/` (gated by `GRAPHICS-040`).
- **GRAPHICS-058-Impl-C** — Optional vendor backend hookpoints (one child per vendor; opened only when actually integrated).

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The post-process / present section of `docs/architecture/rendering-three-pass.md` and the reconstructor / frame-gen section of `src/graphics/renderer/README.md` are deferred to the implementation children (`GRAPHICS-058-Impl-A..B`); per AGENTS.md §9 those docs describe current state, and this planning slice adds no current-state behavior. The recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this slice's docs surface.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Single-frame rendering remains the unconditional default.
- [x] No vendor SDK imports in promoted graphics layers.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve frame-generation decisions are recorded with explicit answers and trade-off rationales: the locked `FrameGenInputs` struct (prev/curr color, motion vectors, depth, camera-params delta, frame-time delta), the `Interpolated` (default) vs `Extrapolated` output kinds with their latency trade-off, the reused `IReconstructor`-style `IFrameGenerator::Generate` seam, the `FrameGenKind { Disabled, Interpolated, Extrapolated, ExternalBackend }` recipe enum defaulting to `Disabled`, the in-engine motion-compensated-blend reference as a correctness (not quality) baseline, the 2N-from-N even-spacing presentation pacing owned by `runtime/`, the explicit one-rendered-frame interpolation latency with vendor-only mitigation, the current-frame disocclusion fallback, the three per-frame diagnostics counters, the vendor-SDK-out-of-promoted-layers layering with no live ECS, the `contract;graphics` + opt-in `gpu;vulkan` test split, and the FG-disabled golden-image determinism mode. Implementation children `GRAPHICS-058-Impl-A..C` are identified but not opened (vendor children open only when actually integrated); single-frame rendering stays the unconditional default, no vendor SDK enters promoted graphics, and no interpolator bodies land. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No vendor SDK imports in promoted graphics layers.
- No removal of single-frame rendering.
- No CPU-side interpolation.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
