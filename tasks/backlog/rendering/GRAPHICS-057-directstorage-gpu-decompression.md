# GRAPHICS-057 — DirectStorage-analog GPU decompression hookpoint on the transfer queue (planning)

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

## Design decisions to record
1. **Capability surface.** Extend `ITransferQueue` with `IGpuDecompressionTransferQueue` capability fetched via `QueryInterface`. Returns `nullptr` when GPU decompression is unavailable.
2. **Compressed payload formats.** Locked support set: GDeflate (D3D12 DirectStorage), Zstd (CPU fallback). Record the format-id enum.
3. **Upload kinds.** `Compressed { Format, CompressedBytes, OutputResource }`. The transfer queue records a CPU-side fallback (decompress to staging then standard `WriteTexture`/`WriteBuffer`) and a GPU-side fast path. Record the dispatch rule.
4. **GPU decompression dispatch.** A compute pass on the transfer queue (or graphics queue per capability) runs the appropriate decompression kernel (vendor or in-engine) to produce uncompressed bytes in target memory. Record the kernel sourcing rule (vendor SDK in `IGpuDecompressionTransferQueue` impl outside promoted graphics layers, or in-engine GDeflate Slang implementation).
5. **Streaming integration.** Runtime asset bridges (per `GRAPHICS-015Q`) submit compressed payloads through the same path as uncompressed payloads; the queue routes based on capability + payload format. Record the routing rule.
6. **Diagnostics.** `GpuDecompressBytesPerFrame`, `CpuDecompressFallbackCount`, `GpuDecompressKernelDispatchCount`. Counters atomic.
7. **Operational-gate addition.** Append "GPU decompression capabilities probed and recorded" as a gate in `GRAPHICS-033`'s reason enum without rewriting earlier gates.
8. **Failure mode.** Compressed-payload decode failure (corrupt input, format unsupported) falls back to CPU decompression and surfaces a structured diagnostic. Record the rule.
9. **Test split.** `unit` for CPU GDeflate roundtrip; `contract;rhi` for `IGpuDecompressionTransferQueue` capability surface under null RHI; opt-in `gpu;vulkan` smoke for end-to-end GPU decode on a fixture.
10. **Layering.** Vendor SDK (DirectStorage) lives behind a non-promoted backend module if integrated; not in `src/graphics/`. In-engine GDeflate Slang module lives in `src/graphics/renderer/`.

## Required changes
- [ ] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [ ] Cross-link upstream and downstream tasks enumerated in Context.
- [ ] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-057-Impl-A** — `IGpuDecompressionTransferQueue` capability surface + null-RHI mock + capability tests.
- **GRAPHICS-057-Impl-B** — CPU fallback (GDeflate / Zstd) + `unit` roundtrip tests.
- **GRAPHICS-057-Impl-C** — In-engine Slang GDeflate kernel (gated by `GRAPHICS-041`).
- **GRAPHICS-057-Impl-D** — Backend wiring + opt-in `gpu;vulkan` smoke (gated by `GRAPHICS-033`).
- **GRAPHICS-057-Impl-E** — Operational-gate extension + diagnostic wiring.

## Tests
- [ ] Planning slice: validators only.
- [ ] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [ ] Update `docs/architecture/graphics.md` streaming/transfer section.
- [ ] Update `src/graphics/rhi/README.md` capability surface.
- [ ] Update `src/graphics/vulkan/README.md` operational-gate section.

## Acceptance criteria
- [ ] Ten decisions are recorded with explicit answers and trade-off rationales.
- [ ] Implementation child slices are identified but not opened.
- [ ] CPU-decompress + standard-upload path remains the unconditional baseline.
- [ ] No vendor SDK imports in promoted graphics layers.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No vendor SDK imports in promoted graphics layers.
- No removal of CPU-decompress + standard-upload path.
- No live ECS access.
- No mixing of mechanical file moves with semantic refactors.
