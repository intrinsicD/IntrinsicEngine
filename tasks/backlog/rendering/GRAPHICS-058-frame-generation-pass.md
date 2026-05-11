# GRAPHICS-058 — Frame generation pass (planning)

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

## Design decisions to record
1. **Input set.** Locked: previous-frame color, current-frame color, motion vectors, depth, camera-params delta, frame-time delta. Record the canonical `FrameGenInputs` struct.
2. **Output kind.** `Interpolated` (between two rendered frames) vs `Extrapolated` (between the latest rendered frame and a predicted future frame). Default: `Interpolated` for visual quality; `Extrapolated` is lower latency. Record the per-recipe selection rule.
3. **Backend interface.** Reuse the `IReconstructor`-style seam: `IFrameGenerator::Generate(FrameGenInputs) -> InterpolatedColor`. Vendor backends are non-promoted impls.
4. **Recipe selection.** `FrameGenKind { Disabled, Interpolated, Extrapolated, ExternalBackend }`. Default: `Disabled` until ready.
5. **Reference implementation.** A minimal in-engine motion-compensated blend interpolator as the first concrete `IFrameGenerator` (correctness baseline, not visual quality target). Vendor backends are later children.
6. **Presentation pacing.** Frame-gen produces `2N` presented frames from `N` rendered frames. The presentation queue paces them with even spacing or vendor-pacing API. Record the rule and the CPU-pacing-policy ownership in `runtime/`.
7. **Latency concerns.** Document the input-to-photon latency increase (one rendered-frame delay for `Interpolated`). Record that latency-mitigation features (Reflex / Anti-Lag analogs) live in vendor backends, not promoted layers.
8. **Disocclusion handling.** Reference interpolator falls back to current-frame color where motion-vector confidence is low. Vendor backends handle internally.
9. **Diagnostics.** `FrameGenAppliedFrames`, `FrameGenDisocclusionFraction`, `FrameGenInterFrameLatencyMs`. Per-frame counters.
10. **Layering.** Vendor SDKs live behind `IFrameGenerator` impls in non-promoted modules. Promoted graphics layers contain the seam + reference impl only.
11. **Test split.** `contract;graphics` for input shape, recipe selection, presentation pacing under null RHI; opt-in `gpu;vulkan` smoke for reference-interpolator visual sanity.
12. **Determinism mode.** A "no frame-gen" recipe flag for golden-image tests.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-058-Impl-A** — `IFrameGenerator` interface + reference motion-blend interpolator + `contract;graphics` tests.
- **GRAPHICS-058-Impl-B** — Recipe selection + presentation pacing wiring with `runtime/` (gated by `GRAPHICS-040`).
- **GRAPHICS-058-Impl-C** — Optional vendor backend hookpoints (one child per vendor; opened only when actually integrated).

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/architecture/rendering-three-pass.md` post-process / present section.
- [ ] Update `src/graphics/renderer/README.md` reconstructor / frame-gen section.

## Acceptance criteria
- [ ] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] Single-frame rendering remains the unconditional default.
- [ ] No vendor SDK imports in promoted graphics layers.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No vendor SDK imports in promoted graphics layers.
- No removal of single-frame rendering.
- No CPU-side interpolation.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
