# GRAPHICS-049 — Neural radiance cache slot in the GI path (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until the GI path (`GRAPHICS-046`) and Slang autodiff (`GRAPHICS-041`) ship implementation slices.

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

## Recorded decisions
1. **MLP shape.** Locked default: 6 fully-connected layers, 64 neurons each, ReLU activations; input is hit-point position frequency-encoded (12 bands) + normal + view direction + roughness + albedo, output is RGB radiance. Rationale: a 6×64 MLP is the NRC reference scale — large enough to capture multi-bounce indirect, small enough to infer per-pixel in budget — and frequency-encoding the position is what lets a small MLP represent high-frequency spatial radiance variation.
2. **Weights storage.** A GPU-resident `R32_SFLOAT` (or FP16) buffer, ping-pong updated by the training pass, graphics-owned under the retire-deadline policy. Rationale: ping-pong weights let inference read a stable snapshot while training writes the next, avoiding a read/write hazard mid-frame, and graphics ownership keeps the GPU-resource lifetime inside the layer that dispatches the kernels.
3. **Inference pass.** A compute pass invoked from the GI path that takes a batch of query feature vectors and returns radiance estimates, with a recorded dispatch shape and a per-frame query cap. Rationale: batching queries amortizes weight-buffer loads across the workgroup, and a per-frame cap bounds NRC cost so it cannot starve the rest of the GI budget under a query spike.
4. **Training pass.** A compute pass that consumes sparse path-traced ground-truth samples (one path per K queries) and updates weights via Adam, with a recorded learning-rate schedule. Rationale: Adam is the NRC-reference optimizer (robust to the noisy online gradients), and training from one path per K queries keeps the ground-truth ray budget a small fraction of inference, which is the whole point of caching.
5. **Loss.** Relative L2 loss with luminance weighting. Rationale: relative L2 is the NRC-reference loss — it is scale-invariant across the wide HDR radiance range so bright regions do not dominate the gradient, and luminance weighting matches perceptual importance.
6. **Cache invalidation.** Hard scene-change events (camera teleport, large light change) trigger a weight reset, keyed on a recorded heuristic (camera delta or aggregate light-power delta exceeding a threshold). Rationale: the online cache assumes temporal coherence, so a discontinuous scene change makes the learned radiance stale; an explicit reset converges faster than letting the optimizer chase the discontinuity and avoids ghosting.
7. **Path-tracer integration.** The path tracer queries the NRC at depth K rather than continuing the path, with K recipe-configurable (default 1, i.e. query after the first bounce). Rationale: terminating paths into the cache at a shallow depth is the variance/cost win; making K recipe-configurable lets a recipe trade bias (shallower K) against cost (deeper K) per quality target.
8. **Opt-out.** `NrcKind { Disabled, Inference, InferenceAndTraining }` per recipe, defaulting to `Disabled` until ready. Rationale: a three-state enum cleanly separates "off", "use frozen weights", and "train online", and defaulting to `Disabled` guarantees the GI path renders identically without NRC so no scene regresses before the slot is integration-tested.
9. **Diagnostics.** `NrcQueriesPerFrame`, `NrcTrainingSamplesPerFrame`, `NrcLossEMA`, and `NrcResetCount` are atomic counters. Rationale: query/training counts surface workload, the loss EMA surfaces convergence/divergence, and the reset count surfaces invalidation thrash — the signals needed to tune K and the reset heuristic, without strings.
10. **Layering.** Inference and training shaders live under `src/graphics/renderer/` as Slang modules; no vendor SDK in promoted layers. Rationale: keeps the NRC implementation engine-owned and backend-portable through the Slang pipeline (AGENTS.md §2), with vendor middleware excluded by contract.
11. **Test split.** `unit` for MLP forward-pass numerics against a CPU reference on tiny inputs; `contract;graphics` for pass wiring + opt-out under null RHI; opt-in `gpu;vulkan` smoke for end-to-end inference correctness on a frozen-weights checkpoint. Rationale: the forward pass is a deterministic function checkable on the CPU, wiring/opt-out is device-independent, and only the on-device inference result needs a GPU — keeping the default gate green.
12. **Performance bound.** NRC inference must complete within a recipe-defined fraction of the GI budget; exceedance is a diagnostic signal, not a correctness failure. Rationale: NRC is a variance/cost optimization, so a budget overrun should be observable and tunable rather than failing the frame — treating it as a counter signal preserves render correctness while flagging mis-tuned configs.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-049-Impl-A** — MLP weight buffer + inference kernel (frozen weights) + `unit` numerics tests.
- **GRAPHICS-049-Impl-B** — Path-tracer integration seam + recipe opt-in (gated by `GRAPHICS-046`).
- **GRAPHICS-049-Impl-C** — Online training pass + autodiff (gated by `GRAPHICS-041` annotation policy).
- **GRAPHICS-049-Impl-D** — Cache-invalidation heuristic + integration tests.
- **GRAPHICS-049-Impl-E** — Opt-in `gpu;vulkan` smoke on a frozen-weights checkpoint.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The GI-slot section of `docs/architecture/rendering-three-pass.md` is deferred to the implementation children (`GRAPHICS-049-Impl-A/B/C`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The GI-pass section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] `NrcKind::Disabled` is the unconditional default.
- [x] No vendor SDK imports.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve NRC decisions are recorded with explicit answers and trade-off rationales: the 6×64 ReLU MLP with frequency-encoded geometry inputs, the ping-pong graphics-owned weight buffer, the batched per-frame-capped inference pass, the Adam training pass fed by one path per K queries, the luminance-weighted relative-L2 loss, the scene-change weight-reset heuristic, the recipe-configurable depth-K path-tracer query, the `NrcKind` three-state opt-out defaulting to `Disabled`, the four atomic NRC counters, the Slang-module / no-vendor-SDK layering, the unit-numerics / null-RHI-contract / opt-in-`gpu;vulkan` test split, and the budget-fraction performance bound treated as a diagnostic. Implementation children `GRAPHICS-049-Impl-A..E` are identified but not opened; `NrcKind::Disabled` stays the unconditional default and no MLP weights, training, or inference shaders land. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No vendor SDK imports.
- No CPU-side neural network framework.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
