# GRAPHICS-055 — Streaming Virtual Textures (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children
  `GRAPHICS-055-Impl-A..E` stay unopened until promoted upstream gates
  (`GRAPHICS-041` Slang, `GRAPHICS-057` GPU decompression) are ready to consume.

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

## Recorded decisions
1. **Address space.** Locked virtual texture address space size at 16K×16K virtual texels per virtual texture, page size 128×128, giving a 128×128 page grid per virtual texture. Per-asset metadata records the actual populated extent, mip count, and BCn target format so a smaller texture occupies only the pages it needs. Rationale: 16K² matches the UE5/HDRP convention and keeps the page-table addressable in an R32_UINT entry (128×128 = 16384 pages fits comfortably with mip+flag bits to spare); fixing the page size at 128² matches the broadly-adopted hardware-friendly tile size and lets the physical atlas, feedback buffer, and shipping-format tiling all share one constant.
2. **Page table.** R32_UINT per virtual page: high 16 bits = physical atlas page index, low bits = mip level (4 bits) + residency/flags (rest). Frame-graph-owned, rebuilt/patched per frame by the resolver. Rationale: a single 32-bit indirection per page is the minimum-bandwidth encoding that still carries the atlas slot, the mip level (so trilinear sampling can blend across resident mips), and a resident/non-resident flag; making it frame-graph-owned keeps its lifetime under the same compiler-managed barrier discipline as other transient targets rather than a hand-managed retained resource.
3. **Physical atlas.** A manually-managed 2D texture array holding resident 128² pages with engine-owned LRU eviction; sparse-residency (`VK_KHR_*sparse*`) is explicitly rejected for this slice. Rationale: a manually-managed atlas is portable across every backend and host (sparse residency is an optional, unevenly-supported capability), keeps eviction policy in engine code where it is testable under null RHI, and avoids coupling the canonical path to a hardware feature that would force a second fallback atlas anyway.
4. **Feedback pass.** A compute pass writes per-pixel desired page-id (virtual page + mip) into a screen-resolution (or tile-downsampled) feedback buffer; the runtime drains it once per frame at the start of the next frame, mirroring the `Picking.Readback` drain cadence. Feedback buffer shape: one R32_UINT per feedback texel encoding `(virtualTextureId, pageX, pageY, mip)`. Rationale: reusing the established readback-drain cadence keeps SVT on the same one-frame-latency, no-stall path already proven for picking; a downsampled feedback target bounds the drain cost while still naming every page a visible surface needs, and packing the request into one R32_UINT keeps the drain a flat dedup over a uint array.
5. **Page request resolver.** A runtime-side service (`Extrinsic.Runtime.VirtualTextureResolver`, mirroring the `AssetBridges`/`SpatialDebugAdapters` runtime-producer umbrella pattern) consumes the feedback buffer, deduplicates requests, prioritizes by coarsest-mip-first, and dispatches page uploads through `Graphics.GpuAssetCache` + the existing transfer queue. Rationale: placing the resolver in `runtime/` keeps graphics free of I/O scheduling and `AssetService` knowledge (AGENTS.md §2), reuses the existing GpuAssetCache/transfer-queue upload path rather than inventing a parallel one, and coarsest-mip-first prioritization guarantees a usable (if blurry) page lands before its sharper children so the surface is never left at the magenta fallback longer than necessary.
6. **Eviction policy.** LRU per page with a "do not evict if requested within the last K = 4 frames" guard (K = frames-in-flight + 1). Rationale: LRU is the standard, cheap-to-maintain residency policy; the K-frame guard prevents thrashing a page that is still being referenced by in-flight frames or oscillating visibility, and tying K to frames-in-flight + 1 reuses the same retire-window reasoning already used by `GpuAssetCache::Tick(currentFrame, framesInFlight)` so a page cannot be evicted while a recorded-but-not-retired frame still samples it.
7. **Shipping format.** KTX2 + Basis Universal UASTC, tiled per 128² page, transcoded at upload time to GPU-native BCn (or NTC when `GRAPHICS-050` is active). Per-page tiling with a small border is recorded in the asset metadata. Rationale: UASTC-in-KTX2 is the de-facto portable transcodable supercompression format (one shipping artifact transcodes to BC7/BC5/ASTC per device), per-page tiling lets the resolver read exactly the bytes for one page instead of the whole texture, and deferring the actual decode to the existing GpuAssetCache upload keeps CPU-side decode out of graphics per the non-goals.
8. **Sampler integration.** Materials sample SVT through a Slang sampler module (gated by `GRAPHICS-041`) that translates virtual UV → page-table lookup → physical-atlas UV with manual trilinear across resident mips. Direct-residency textures continue to sample through the existing path unchanged; SVT is selected per material-texture slot. Rationale: isolating the virtual→physical translation in a reusable Slang module keeps it out of every material shader body and lets it evolve independently; keeping direct-residency sampling on its current path guarantees the non-virtualized default is untouched and that opting a slot into SVT is a local material decision, not a global pipeline change.
9. **Missing-page fallback.** Sampling a non-resident page returns the coarsest resident ancestor mip if one exists, otherwise the `GRAPHICS-015Q` magenta-checker fallback, and always emits a feedback request for the desired page. Rationale: falling back up the mip chain keeps a surface visually coherent (blurry, not magenta) while its page streams in, reusing the existing magenta fallback only as the last resort preserves a single visible-failure convention across the texture system, and emitting the feedback request from the miss path is what closes the demand-paging loop without a separate "request" API.
10. **Diagnostics.** `SvtPagesResidentCount`, `SvtPagesEvictedCount`, `SvtPageMissCount`, `SvtFeedbackQueueDepth` as atomic counters. Rationale: these four signals cover the residency working set (resident), churn (evicted), demand pressure not yet satisfied (miss), and backlog (queue depth) — the minimum set to tell "warming up" from "thrashing" from "over budget" — and atomic counters keep them lock-free on the per-frame drain path with no string allocation.
11. **Test split.** `contract;graphics` for page-table encode/decode and the Slang sampler translation + feedback-pass shape under null RHI; `contract;runtime` for resolver dedup/prioritization/eviction; opt-in `gpu;vulkan` smoke for an end-to-end SVT golden image. Rationale: the encoding, sampler math, and resolver policy are all pure logic checkable under null RHI / CPU, so the planning contract is fully testable on the default gate; the end-to-end visual proof is the only part that needs a real device and is therefore the only opt-in `gpu;vulkan` fixture, keeping the default gate green.
12. **Layering.** No live ECS. `assets/` owns the KTX2/UASTC shipping format; `runtime/` owns the resolver + I/O scheduling; `graphics/` owns the sampler module, feedback pass, and physical atlas. Rationale: this split preserves AGENTS.md §2 — graphics never imports `AssetService` or schedules I/O, runtime owns composition and upload scheduling, and the shipping-format decode stays in the CPU-only asset layer — so no new cross-layer edge is introduced by the SVT contract.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-055-Impl-A** — Page-table + physical atlas resources + frame-graph lifetime + null-RHI shape tests.
- **GRAPHICS-055-Impl-B** — Feedback pass + drain cadence + runtime resolver `contract;runtime` tests.
- **GRAPHICS-055-Impl-C** — Shipping format ingest in `assets/` + `Graphics.GpuAssetCache` upload integration.
- **GRAPHICS-055-Impl-D** — Slang sampler module + material-system opt-in (gated by `GRAPHICS-041`).
- **GRAPHICS-055-Impl-E** — Opt-in `gpu;vulkan` end-to-end smoke.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The texture-streaming section of `docs/architecture/graphics.md`, the material/sampler section of `src/graphics/renderer/README.md`, and the upload-path section of `src/graphics/assets/README.md` are deferred to the implementation children (`GRAPHICS-055-Impl-A..D`); per AGENTS.md §9 those docs describe current state, and this planning slice adds no current-state behavior. The recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this slice's docs surface.

## Acceptance criteria
- [x] Twelve decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] Direct-residency texture path remains the unconditional default.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All twelve SVT decisions are recorded with explicit answers and trade-off rationales: the 16K² virtual address space with 128² pages, the R32_UINT page-table encoding, the manually-managed (non-sparse) physical atlas with LRU eviction, the compute feedback pass draining on the `Picking.Readback` cadence, the runtime `VirtualTextureResolver` consuming feedback with coarsest-mip-first prioritization, the LRU + K=frames-in-flight+1 eviction guard, the KTX2/Basis-UASTC per-page shipping format transcoded at upload, the `GRAPHICS-041`-gated Slang sampler with direct-residency left unchanged, the mip-ancestor-then-magenta missing-page fallback that re-emits a feedback request, the four atomic diagnostics counters, the null-RHI-contract + opt-in `gpu;vulkan` test split, and the assets/runtime/graphics layering split with no live ECS. Implementation children `GRAPHICS-055-Impl-A..E` are identified but not opened; the direct-residency texture path stays the unconditional default and no encoder/upload-pipeline/Vulkan changes land. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No removal of direct-residency texture path.
- No CPU-side page decode in graphics.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
