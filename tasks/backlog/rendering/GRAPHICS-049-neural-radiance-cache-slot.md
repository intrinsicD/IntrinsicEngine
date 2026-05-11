# GRAPHICS-049 — Neural radiance cache slot in the GI path (planning)

## Goal
Lock down the contract for an optional Neural Radiance Cache (NRC) slot in the GI path: a small MLP queried by the path tracer / ReSTIR sampler to estimate multi-bounce indirect at primary hit points, trained online from sparse path-traced ground truth, with a clean opt-out and a fallback that keeps GI working without NRC. Planning only — no MLP weights, no training shaders, and no inference shaders here.

## Non-goals
- No general neural network framework integration.
- No vendor SDK (NVIDIA RTX Kit / Falcor NRC) imports in promoted layers.
- No path tracer body — NRC plugs into existing GI consumers from `GRAPHICS-046`.
- No denoiser-style hookpoint here — the seam is the GI sampler, not the post chain.
- No CPU MLP. Inference and online training run on GPU.

## Context
- Owner layer: `graphics/renderer` (NRC pass + MLP eval/train kernels), `graphics/rhi` (no surface change beyond storage-buffer + small descriptor set).
- NRC (Müller et al. 2021, NVIDIA RTX Kit 2024+) reduces variance in real-time path tracing by approximating multi-bounce indirect with a small online-trained MLP. The MLP input is a feature vector derived from hit point geometry; the output is incident radiance. Training samples come from sparse paths traced beyond the cache query.
- Slang's first-class autodiff (gated by `GRAPHICS-041` annotation policy) makes online training natural in-engine.
- Cross-links: `GRAPHICS-041` (Slang autodiff seam), `GRAPHICS-046` (consumer in the GI path), `GRAPHICS-051` (differentiable rendering shares autodiff infrastructure).

## Design decisions to record
1. **MLP shape.** Locked default: 6 fully-connected layers, 64 neurons each, ReLU activations. Input: hit-point position-encoded (frequency encoding, 12 bands) + normal + view direction + roughness + albedo. Output: RGB radiance.
2. **Weights storage.** GPU-resident `R32_SFLOAT` (or FP16) buffer; weights ping-pong updated via training pass. Record the buffer ownership and retire-deadline policy.
3. **Inference pass.** A compute pass invoked from the GI path that, given a batch of query feature vectors, returns radiance estimates. Record the dispatch shape and per-frame query cap.
4. **Training pass.** A compute pass that consumes sparse path-traced ground-truth samples (one path per K queries) and updates weights via SGD/Adam. Record the optimizer choice and learning-rate schedule.
5. **Loss.** Relative L2 loss with luminance weighting. Record the variant.
6. **Cache invalidation.** Hard scene-change events (camera teleport, large light change) trigger a weight reset. Record the heuristic.
7. **Path-tracer integration.** Path tracer queries the NRC at depth K rather than continuing the path. K is recipe-configurable. Record the default.
8. **Opt-out.** `NrcKind { Disabled, Inference, InferenceAndTraining }` per recipe. Default: `Disabled` until ready. Record the rule.
9. **Diagnostics.** `NrcQueriesPerFrame`, `NrcTrainingSamplesPerFrame`, `NrcLossEMA`, `NrcResetCount`. Counters atomic.
10. **Layering.** Inference and training shaders live under `src/graphics/renderer/` as Slang modules. No vendor SDK in promoted layers.
11. **Test split.** `unit` for MLP forward-pass numerics on a CPU reference for tiny inputs; `contract;graphics` for pass wiring + opt-out under null RHI; opt-in `gpu;vulkan` smoke for end-to-end inference correctness on a frozen-weights checkpoint.
12. **Performance bound.** NRC inference must complete within a recipe-defined fraction of the GI budget; exceedance is a diagnostic signal, not a correctness failure.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-049-Impl-A** — MLP weight buffer + inference kernel (frozen weights) + `unit` numerics tests.
- **GRAPHICS-049-Impl-B** — Path-tracer integration seam + recipe opt-in (gated by `GRAPHICS-046`).
- **GRAPHICS-049-Impl-C** — Online training pass + autodiff (gated by `GRAPHICS-041` annotation policy).
- **GRAPHICS-049-Impl-D** — Cache-invalidation heuristic + integration tests.
- **GRAPHICS-049-Impl-E** — Opt-in `gpu;vulkan` smoke on a frozen-weights checkpoint.

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/architecture/rendering-three-pass.md` GI section with the NRC slot.
- [ ] Update `src/graphics/renderer/README.md` GI-pass section.

## Acceptance criteria
- [ ] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] `NrcKind::Disabled` is the unconditional default.
- [ ] No vendor SDK imports.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No vendor SDK imports.
- No CPU-side neural network framework.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
