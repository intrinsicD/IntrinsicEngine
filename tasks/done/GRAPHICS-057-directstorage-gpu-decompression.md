# GRAPHICS-057 — DirectStorage-analog GPU decompression hookpoint on the transfer queue (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children
  `GRAPHICS-057-Impl-A..E` stay unopened until a consumer (SVT page reads per
  `GRAPHICS-055`, or NTC pages per `GRAPHICS-050`) is scheduled.

## Goal
Lock down the contract for a DirectStorage-analog asset I/O hookpoint that streams compressed asset bytes from disk straight into VRAM and decompresses on the GPU (GDeflate or compatible), routed through the existing hardened transfer queue (`GRAPHICS-018T`/`GRAPHICS-026`), with explicit fallback to CPU decompression + standard upload so the engine ships without DirectStorage support. Planning only — no DirectStorage SDK or `VK_EXT_*` extension enables here.

## Non-goals
- No vendor SDK imports in promoted graphics layers.
- No filesystem virtualization in graphics; runtime owns I/O scheduling.
- No replacement of the existing CPU-decode + upload path; it remains the unconditional baseline.
- No DirectStorage 1.x version targeting; the hookpoint is vendor- and version-agnostic.
- No SVT integration body — `GRAPHICS-055` consumes this hookpoint when it lands.

## Context
- Owner layer: `runtime/` (I/O scheduling + decompression dispatch), `graphics/rhi` (extends `ITransferQueue` capability surface), `graphics/vulkan` (extension wiring; gated on the relevant Vulkan extension landing).
- DirectStorage 1.2 + GDeflate (Microsoft, 2023) and the equivalent Vulkan-side extensions move asset decompression to the GPU and bypass the CPU on the upload critical path. Used in Forspoken, Ratchet & Clank PC, Horizon FW.
- Cross-links: `GRAPHICS-018T` (transfer queue), `GRAPHICS-026` (async streaming), `GRAPHICS-055` (SVT page uploads benefit), `GRAPHICS-050` (NTC pages benefit).

## Recorded decisions
1. **Capability surface.** Extend the transfer queue with an `IGpuDecompressionTransferQueue` capability fetched via `ITransferQueue::QueryInterface<IGpuDecompressionTransferQueue>()`, returning `nullptr` when GPU decompression is unavailable. Rationale: reusing the established `QueryInterface` capability seam (mirroring `IRayTracingDevice`/`IMeshShaderDevice`/`IWorkGraphDevice` from `GRAPHICS-045`/`053`/`054`) keeps GPU decompression purely additive — every existing transfer-queue caller is untouched, and a `nullptr` return is the single, uniform "not available, use the CPU fallback" signal that the routing rule (decision 5) keys off.
2. **Compressed payload formats.** Locked support set as a `CompressedFormat` enum: `GDeflate` (D3D12 DirectStorage GPU path) and `Zstd` (CPU-fallback-friendly), with `None` reserved for uncompressed. Rationale: GDeflate is the de-facto GPU-decompression format with hardware/driver support behind DirectStorage and the Vulkan equivalents, while Zstd is the portable, fast CPU codec that guarantees a working fallback on every host; pinning a small closed enum (rather than an open format registry) keeps the dispatch switch exhaustive and the null-RHI mock complete.
3. **Upload kinds.** Add a `Compressed { Format, CompressedBytes, OutputResource }` upload kind alongside the existing uncompressed `WriteTexture`/`WriteBuffer`. The transfer queue records two routes: a CPU-side fallback (decompress to staging on the CPU, then standard upload) and a GPU-side fast path (upload compressed bytes, decompress in-place/into target via a kernel). Rationale: modeling the compressed upload as one new kind carrying the format + bytes + destination keeps the transfer-queue API uniform (callers submit the same way regardless of route), and recording both routes against one kind is what lets the queue choose by capability at submit time without the caller knowing which path ran.
4. **GPU decompression dispatch.** A compute dispatch on the transfer queue (or graphics queue per capability) runs the decompression kernel to produce uncompressed bytes in target memory. Kernel sourcing rule: a vendor SDK decompressor lives only inside an `IGpuDecompressionTransferQueue` implementation in a **non-promoted** backend module; an in-engine GDeflate decompressor is a Slang compute module under `src/graphics/renderer/` (gated by `GRAPHICS-041`). Rationale: keeping the vendor SDK strictly behind the capability impl in a non-promoted module preserves AGENTS.md §2 (no middleware in promoted layers), while allowing an in-engine Slang GDeflate kernel gives a vendor-independent GPU path; specifying the kernel runs on the transfer queue keeps decompression off the graphics critical path where the hardware supports an async transfer/compute queue.
5. **Streaming integration.** Runtime asset bridges (per `GRAPHICS-015Q`) submit compressed payloads through the *same* `ITransferQueue` path as uncompressed payloads; the queue routes by capability + payload format: GPU fast path when `IGpuDecompressionTransferQueue` is present and the format is GPU-decodable, CPU fallback otherwise. Rationale: routing inside the queue (not at the call site) means the runtime asset bridges from `GRAPHICS-015Q` need no GPU-decompression knowledge — they submit a `Compressed` payload and the queue transparently picks the route — so adding GPU decompression never changes the asset-bridge code or the `AssetService`-free graphics boundary.
6. **Diagnostics.** `GpuDecompressBytesPerFrame`, `CpuDecompressFallbackCount`, and `GpuDecompressKernelDispatchCount` as atomic counters. Rationale: these three signals distinguish the GPU path's throughput (bytes/frame), how often the engine fell back to CPU (fallback count — the signal that capability or format support is missing), and GPU kernel pressure (dispatch count), which together tell whether GPU decompression is actually engaged and carrying load, with no string allocation on the upload path.
7. **Operational-gate addition.** Append "GPU decompression capabilities probed and recorded" to `GRAPHICS-033`'s reason enum as a new optional gate, without rewriting earlier gates; the gate stays `NotRequested` until a consumer opens. Rationale: the operational-gate enum is append-only by contract, so GPU decompression joins as an inert optional gate that preserves every prior gate's meaning and ordering; staying `NotRequested` until a real consumer exists matches the "ships without DirectStorage" posture and keeps the default operational evaluation unchanged.
8. **Failure mode.** A compressed-payload decode failure (corrupt input, unsupported format, kernel error) falls back to CPU decompression and surfaces a structured diagnostic (incrementing `CpuDecompressFallbackCount`); if CPU decode also fails, the upload fails closed with a structured error rather than uploading garbage. Rationale: silently uploading undecoded or partially-decoded bytes would corrupt the resource invisibly, so a two-tier fallback (GPU → CPU → fail-closed) with a structured diagnostic keeps every failure observable and never lets bad bytes reach a sampled resource, matching the fail-closed discipline used across the transfer-queue paths.
9. **Test split.** `unit` for the CPU GDeflate (and Zstd) roundtrip; `contract;rhi` for the `IGpuDecompressionTransferQueue` capability surface, the `Compressed` upload kind, and the routing/fallback decision under null RHI; opt-in `gpu;vulkan` smoke for end-to-end GPU decode on a fixture. Rationale: the CPU codec roundtrip is pure logic best covered by fast `unit` tests, the capability surface and routing/fallback logic are fully checkable under null RHI as `contract;rhi`, and only the real GPU decode needs a device — so the default gate stays green and the device-dependent proof is the single opt-in `gpu;vulkan` fixture.
10. **Layering.** Vendor SDK (DirectStorage) lives behind a non-promoted backend module if integrated, never in `src/graphics/`; the in-engine GDeflate Slang module lives in `src/graphics/renderer/`; `runtime/` owns I/O scheduling and decompression dispatch ordering; no live ECS. Rationale: this split preserves AGENTS.md §2 — promoted graphics never imports a vendor SDK or schedules filesystem I/O, runtime owns composition and I/O scheduling, and the only in-engine GPU code is a Slang kernel in the renderer — so the GPU-decompression contract introduces no new cross-layer edge.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-057-Impl-A** — `IGpuDecompressionTransferQueue` capability surface + null-RHI mock + capability tests.
- **GRAPHICS-057-Impl-B** — CPU fallback (GDeflate / Zstd) + `unit` roundtrip tests.
- **GRAPHICS-057-Impl-C** — In-engine Slang GDeflate kernel (gated by `GRAPHICS-041`).
- **GRAPHICS-057-Impl-D** — Backend wiring + opt-in `gpu;vulkan` smoke (gated by `GRAPHICS-033`).
- **GRAPHICS-057-Impl-E** — Operational-gate extension + diagnostic wiring.

## Tests
- [x] Planning slice: validators only.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] The streaming/transfer section of `docs/architecture/graphics.md`, the capability-surface section of `src/graphics/rhi/README.md`, and the operational-gate section of `src/graphics/vulkan/README.md` are deferred to the implementation children (`GRAPHICS-057-Impl-A..E`); per AGENTS.md §9 those docs describe current state, and this planning slice adds no current-state behavior. The recorded decisions above plus the `GRAPHICS-035` roadmap pointer are this slice's docs surface.

## Acceptance criteria
- [x] Ten decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified but not opened.
- [x] CPU-decompress + standard-upload path remains the unconditional baseline.
- [x] No vendor SDK imports in promoted graphics layers.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All ten GPU-decompression decisions are recorded with explicit answers and trade-off rationales: the `QueryInterface`-fetched optional `IGpuDecompressionTransferQueue` returning `nullptr` until available, the closed `CompressedFormat { GDeflate, Zstd, None }` set, the `Compressed { Format, CompressedBytes, OutputResource }` upload kind carrying both CPU-fallback and GPU-fast-path routes, the transfer/graphics-queue compute dispatch with vendor SDK confined to a non-promoted impl and the in-engine GDeflate kernel as a `GRAPHICS-041`-gated Slang module, the queue-side capability+format routing that keeps the `GRAPHICS-015Q` asset bridges decompression-agnostic, the three atomic diagnostics counters, the append-only `GRAPHICS-033` gate staying `NotRequested`, the GPU→CPU→fail-closed failure ladder with structured diagnostics, the unit/`contract;rhi`/opt-in-`gpu;vulkan` test split, and the vendor-SDK-out-of-promoted-layers layering with no live ECS. Implementation children `GRAPHICS-057-Impl-A..E` are identified but not opened; the CPU-decompress + standard-upload path stays the unconditional baseline, no vendor SDK enters promoted graphics, and no DirectStorage/`VK_EXT_*` enables land. Per AGENTS.md §9 the architecture-doc/README updates are deferred to the implementation children so those docs stay current-state.

## Forbidden changes
- No vendor SDK imports in promoted graphics layers.
- No removal of CPU-decompress + standard-upload path.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
