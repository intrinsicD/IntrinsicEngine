# GRAPHICS-050 — Neural texture compression with random-access decode (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until the Slang pipeline (`GRAPHICS-041`) and PBR materials (`GRAPHICS-042`) consume the decoder.

## Goal
Lock down the contract for a Neural Texture Compression (NTC) format that pairs each compressed texture with a tiny per-material MLP decoder, decoded inline within the material shading kernel via a Slang module, with random-access correctness, BC-format-or-better quality at multi-x VRAM savings, and a clean fallback to BCn so the engine ships without NTC support. Planning only — no encoder, no decoder kernels here.

## Non-goals
- No replacement of BCn / KTX2 / Basis paths (`GRAPHICS-055` covers SVT and KTX2 shipping format).
- No vendor SDK imports.
- No video / animated texture compression in this task.
- No CPU-side decode; inline GPU decode only.
- No mip-chain reconstruction beyond what the encoder produces.

## Context
- Owner layer: `assets/` (encoder + .ntc shipping format), `graphics/assets` (GPU upload), `graphics/renderer` (decoder Slang module consumed by material kernels).
- NVIDIA Random-Access Neural Texture Compression (Vaidyanathan et al. 2023; GPUOpen Neural BC Compression 2024) achieves ~7× VRAM reduction at BCn quality with random-access decode. The decoder is a small per-material MLP evaluated at sample time. Quality and decode cost are tunable.
- Slang's generics make per-material decoder specialization a natural fit: each material type can declare an `NtcDecoder<Layout>` instance.
- Cross-links: `GRAPHICS-041` (Slang module pipeline), `GRAPHICS-042` (PBR materials are the primary consumer), `GRAPHICS-043` (vis-buffer materialization is a natural batch-decode site), `GRAPHICS-055` (SVT page-table interaction).

## Recorded decisions
1. **Format shape.** Per `.ntc` texture: a low-resolution latent feature grid + per-mip MLP weights, decoded at sample as `MLP(latent_at_sample, uv)`, with a recorded canonical layout and version. Rationale: separating a coarse latent grid from a small per-mip MLP is what the NTC reference uses to get random access — the latent grid captures spatial structure cheaply and the MLP reconstructs detail, and pinning version + layout keeps the format forward-compatible.
2. **Decoder shape.** A small MLP (2–3 layers, 16–32 neurons) per material/layout, with decoder weights living in the material-shader specialization rather than per-texture. Rationale: a tiny MLP keeps per-sample decode affordable inline in the shading kernel, and binding the decoder to the material layout (not each texture) lets one specialized decoder serve every texture of that material type, avoiding per-texture weight churn.
3. **Random-access correctness.** Decode at any UV produces deterministic output with no spatial dependencies beyond the MLP context window. Rationale: random access is the property that makes NTC usable inline in shading (vs. block-streaming codecs) — guaranteeing no cross-sample dependency means the decoder behaves like a sampler and is safe under arbitrary access patterns including anisotropic filtering.
4. **Quality target.** Match BC7 PSNR within 1 dB at 4× VRAM savings as the baseline acceptance metric, validated against a recorded fixture set. Rationale: a concrete PSNR-vs-BC7 acceptance metric makes "BC-or-better quality" testable rather than aspirational, and 4× is a conservative savings floor (the reference reaches ~7×) that leaves quality headroom.
5. **Encoder placement.** An offline encoder under `tools/texture-compress/` outputs `.ntc` files, invoked through a recorded build-time contract. Rationale: NTC encoding is an expensive offline optimization that belongs in `tools/` (not the runtime or `assets/` load path), and a build-time invocation contract keeps generation reproducible and out of the hot path.
6. **Fallback to BCn.** Texture assets carry both `.ntc` and a BCn (e.g. `.bc7`) representation; runtime selects based on an `IDevice` capability flag `NeuralTextureCompressionSupported`, with the dual-representation shipping contract recorded. Rationale: shipping both guarantees the engine runs on hardware without efficient NTC decode, and a capability-flag selection keeps the choice a device decision rather than a build-time lock-in — BCn stays the unconditional default.
7. **Material integration.** Materials declare per-texture-slot decode kind via a `MaterialTypeDesc` flag, and the decoder Slang module is selected at compile time. Rationale: a per-slot flag lets a material mix NTC and BCn textures, and compile-time decoder selection (via Slang generics) avoids a runtime branch in the inner sampling loop, keeping decode cost predictable.
8. **Vis-buffer integration.** When materialization runs (`GRAPHICS-043`), the decoder is amortized over a tile of pixels with a shared latent fetch; the planning slice records this optimization opportunity but does not require it. Rationale: vis-buffer materialization processes pixels in material-coherent tiles, so a shared latent fetch per tile is a natural decode-cost amortization — recording it as optional keeps the baseline decode correct without coupling NTC delivery to the vis-buffer schedule.
9. **Diagnostics.** `NtcDecodesPerFrame`, `NtcFallbackToBCnCount`, and `NtcAverageDecodeCost` are atomic counters. Rationale: decode count surfaces workload, the fallback counter makes "NTC silently unused on this device" observable, and average decode cost surfaces the per-sample budget — all without strings.
10. **Test split.** `unit` for encoder roundtrip PSNR; `contract;graphics` for material-system selection under null RHI; opt-in `gpu;vulkan` smoke for golden-image correctness on a fixture set. Rationale: roundtrip PSNR is a pure CPU-checkable quality metric, material selection is device-independent, and only the on-device decoded image needs a GPU — keeping the default gate green.
11. **Layering.** The encoder lives in `tools/`, decoder Slang modules live in `src/graphics/renderer/`, and no vendor SDK is imported. Rationale: preserves AGENTS.md §2 — offline tooling in `tools/`, runtime decode engine-owned through the Slang pipeline, vendor middleware excluded by contract.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-050-Impl-A** — `.ntc` shipping format + offline encoder under `tools/` + `unit` PSNR tests.
- **GRAPHICS-050-Impl-B** — Decoder Slang module + per-material specialization (gated by `GRAPHICS-041`).
- **GRAPHICS-050-Impl-C** — Material-system selection + BCn fallback + integration tests.
- **GRAPHICS-050-Impl-D** — Vis-buffer batch-decode optimization (gated by `GRAPHICS-043`; optional).
- **GRAPHICS-050-Impl-E** — Opt-in `gpu;vulkan` smoke on fixture set.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The material/texture pipeline section of `docs/architecture/graphics.md` is deferred to the implementation children (`GRAPHICS-050-Impl-B/C`); the recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this planning slice's docs surface, landing in the architecture doc when the feature is current-state per AGENTS.md §9.
- [x] The material-system section of `src/graphics/renderer/README.md` is deferred to the same implementation children for the same reason.
- [x] The upload-path section of `src/graphics/assets/README.md` is deferred to the same implementation children for the same reason.

## Acceptance criteria
- [x] Eleven decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] BCn shipping path remains the unconditional default.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All eleven NTC decisions are recorded with explicit answers and trade-off rationales: the latent-grid + per-mip-MLP versioned `.ntc` format, the per-material-layout small-MLP decoder, the random-access no-cross-sample-dependency rule, the BC7-PSNR-within-1dB-at-4× acceptance metric, the offline `tools/texture-compress/` encoder contract, the dual `.ntc`+BCn shipping with `NeuralTextureCompressionSupported` capability selection, the `MaterialTypeDesc`-flag compile-time decoder selection, the optional vis-buffer tile-amortized decode, the three atomic NTC counters, the roundtrip-PSNR / null-RHI-contract / opt-in-`gpu;vulkan` test split, and the tools-encoder / renderer-decoder / no-vendor-SDK layering audit. Implementation children `GRAPHICS-050-Impl-A..E` are identified but not opened; the BCn shipping path stays the unconditional default and no encoder or decoder kernels land. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No removal of BCn / KTX2 paths.
- No vendor SDK imports.
- No CPU-side decode.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
