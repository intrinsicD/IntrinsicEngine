# GRAPHICS-050 — Neural texture compression with random-access decode (planning)

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

## Design decisions to record
1. **Format shape.** Per `.ntc` texture: latent grid (low-resolution feature volume) + per-mip MLP weights. Decode at sample = MLP(latent_at_sample, uv). Record the canonical layout and version.
2. **Decoder shape.** Small MLP (suggested 2-3 layers, 16-32 neurons) per material/layout. Decoder weights live in the material-shader specialization, not per-texture.
3. **Random-access correctness.** Decode at any UV produces deterministic output. No spatial dependencies beyond the MLP context window. Record the rule.
4. **Quality target.** Match BC7 PSNR within 1 dB at 4× VRAM savings as the baseline. Record the acceptance metric and test fixture set.
5. **Encoder placement.** Offline encoder under `tools/texture-compress/` (or chosen canonical location). Outputs `.ntc` files. Record the build-time invocation contract.
6. **Fallback to BCn.** Texture assets carry both `.ntc` and `.bc7` (or chosen BCn) representations; runtime selects based on `IDevice` capability flag `NeuralTextureCompressionSupported`. Record the asset shipping contract.
7. **Material integration.** Materials declare per-texture-slot decode kind via `MaterialTypeDesc` flag. Decoder Slang module is selected at compile time. Record the rule.
8. **Vis-buffer integration.** When materialization runs (`GRAPHICS-043`), the decoder is amortized over a tile of pixels with shared latent fetch. Record the optimization opportunity and that the planning slice does not require it.
9. **Diagnostics.** `NtcDecodesPerFrame`, `NtcFallbackToBCnCount`, `NtcAverageDecodeCost`. Counters atomic.
10. **Test split.** `unit` for encoder roundtrip PSNR; `contract;graphics` for material-system selection under null RHI; opt-in `gpu;vulkan` smoke for golden-image correctness on a fixture set.
11. **Layering.** Encoder lives in `tools/`. Decoder Slang modules live in `src/graphics/renderer/`. No vendor SDK.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-050-Impl-A** — `.ntc` shipping format + offline encoder under `tools/` + `unit` PSNR tests.
- **GRAPHICS-050-Impl-B** — Decoder Slang module + per-material specialization (gated by `GRAPHICS-041`).
- **GRAPHICS-050-Impl-C** — Material-system selection + BCn fallback + integration tests.
- **GRAPHICS-050-Impl-D** — Vis-buffer batch-decode optimization (gated by `GRAPHICS-043`; optional).
- **GRAPHICS-050-Impl-E** — Opt-in `gpu;vulkan` smoke on fixture set.

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/architecture/graphics.md` material/texture pipeline section.
- [ ] Update `src/graphics/renderer/README.md` material-system section.
- [ ] Update `src/graphics/assets/README.md` upload-path section.

## Acceptance criteria
- [ ] Eleven decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] BCn shipping path remains the unconditional default.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No removal of BCn / KTX2 paths.
- No vendor SDK imports.
- No CPU-side decode.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
