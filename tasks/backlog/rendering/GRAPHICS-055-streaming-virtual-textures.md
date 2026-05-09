# GRAPHICS-055 — Streaming Virtual Textures (planning)

## Goal
Lock down the contract for a Streaming Virtual Texture (SVT) system: a virtual texture address space backed by demand-paged 128² physical pages, a feedback pass that records per-pixel page-id requests, a runtime page request resolver that uploads required pages from a transcodable shipping format (KTX2/Basis Universal UASTC), and a sampler that resolves virtual UVs through a frame-graph-owned page table. Planning only — no encoder, no upload pipeline changes here.

## Non-goals
- No virtualized geometry / Nanite-style cluster DAG (`GRAPHICS-056`).
- No VSM page allocation (covered by `GRAPHICS-047`); SVT and VSM are independent address spaces.
- No replacement of the existing direct-residency texture path; SVT is opt-in per material.
- No CPU-side decode of virtual texture pages; transcoding to GPU-format BCn happens through `Graphics.GpuAssetCache` upload.
- No DirectStorage analog here (covered by `GRAPHICS-057`); SVT depends on it but does not open it.

## Context
- Owner layer: `graphics/renderer` (feedback pass + SVT sampler module), `graphics/framegraph` (page-table resource lifetime), `graphics/assets` (page upload), `assets` (virtual texture asset format), `runtime/` (page request resolver + I/O scheduling).
- Streaming Virtual Texturing is the standard pattern in UE5 / Unity HDRP for handling terabyte texture sets on a finite VRAM budget. The 128² page size and KTX2/Basis Universal UASTC shipping format are widely adopted.
- Cross-links: `GRAPHICS-018T` (transfer queue feeds SVT page uploads), `GRAPHICS-042` (PBR consumer), `GRAPHICS-043` (vis-buffer materialization is the natural batched feedback site), `GRAPHICS-050` (NTC pages share the same paging mechanism), `GRAPHICS-057` (DirectStorage analog accelerates page reads).

## Design decisions to record
1. **Address space.** Locked virtual texture address space size (suggested 16K×16K virtual texels per virtual texture); page size 128×128. Record the rule and the per-asset metadata.
2. **Page table.** R32_UINT per virtual page: high bits = atlas page index, low bits = mip level + flags. Frame-graph-owned. Record the encoding.
3. **Physical atlas.** A 2D texture array (or single large 2D) holding resident pages. Decide between sparse-residency (`VK_KHR_*sparse*`) and a manually-managed atlas with eviction. Default: manually-managed atlas.
4. **Feedback pass.** A compute pass (or material-shader side channel) writes per-pixel page-id requests to a feedback buffer. The runtime drains this buffer each frame to determine pages to upload. Record the feedback buffer shape and drain cadence.
5. **Page request resolver.** A runtime-side service consumes feedback, deduplicates, prioritizes by mip level, and dispatches uploads through `Graphics.GpuAssetCache` + the existing transfer queue. Record the runtime module placement.
6. **Eviction policy.** LRU per page with a "do not evict if requested in last K frames" guard. Record the K policy.
7. **Shipping format.** KTX2 + Basis Universal UASTC tiled per page. Decode at upload to GPU-native BCn (or NTC, gated by `GRAPHICS-050`). Record the asset shipping spec.
8. **Sampler integration.** Materials sample SVT via a Slang sampler module that performs the virtual-to-physical UV translation. Record the rule that direct-residency textures continue to work unchanged.
9. **Missing-page fallback.** Sampling a non-resident page returns a fallback (low-mip representative or magenta-checker per `GRAPHICS-015Q`) and emits a feedback request. Record the rule.
10. **Diagnostics.** `SvtPagesResidentCount`, `SvtPagesEvictedCount`, `SvtPageMissCount`, `SvtFeedbackQueueDepth`. Counters atomic.
11. **Test split.** `contract;graphics` for page-table encoding/decoding, feedback-pass shape, sampler module under null RHI; `contract;runtime` for resolver dedup/prioritization; opt-in `gpu;vulkan` smoke for end-to-end SVT golden image.
12. **Layering.** No live ECS. `assets/` owns shipping format; `runtime/` owns resolver; `graphics/` owns sampler + atlas.

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-055-Impl-A** — Page-table + physical atlas resources + frame-graph lifetime + null-RHI shape tests.
- **GRAPHICS-055-Impl-B** — Feedback pass + drain cadence + runtime resolver `contract;runtime` tests.
- **GRAPHICS-055-Impl-C** — Shipping format ingest in `assets/` + `Graphics.GpuAssetCache` upload integration.
- **GRAPHICS-055-Impl-D** — Slang sampler module + material-system opt-in (gated by `GRAPHICS-041`).
- **GRAPHICS-055-Impl-E** — Opt-in `gpu;vulkan` end-to-end smoke.

## Tests
- Planning slice: validators only.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` texture-streaming section.
- Update `src/graphics/renderer/README.md` material/sampler section.
- Update `src/graphics/assets/README.md` upload-path section.

## Acceptance criteria
- Twelve decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified but not opened.
- Direct-residency texture path remains the unconditional default.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No removal of direct-residency texture path.
- No CPU-side page decode in graphics.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
