# ADR 0022: Vertex storage — uniform SoA with per-channel streaming

- **Status:** Accepted
- **Date:** 2026-06-21
- **Owners:** runtime + graphics
- **Related tasks:** RUNTIME-120, RUNTIME-121, RUNTIME-122, RUNTIME-124, RUNTIME-125, RUNTIME-139

## Context

Before RUNTIME-122, the managed GPU geometry path stored vertices interleaved
(AoS): `GpuWorld` kept one contiguous `VertexByteOffset` / `VertexByteCount` /
`VertexStride` block per geometry, and the active GpuScene vertex shaders read
packed vertex structs through the geometry record's single `VertexBufferBDA`.
RUNTIME-122 applied this ADR to the retained GpuWorld path: managed geometry now
stores position, texcoord, normal, and optional packed-color channels as
separate sub-ranges and publishes one BDA per channel. The dormant `surface.vert`
push-constant SoA path remains unused by the default recipe.

Two requirements pushed on this layout:

1. Bind arbitrary geometry properties to vertex channels (normals/colors) across
   meshes, graphs, and point clouds — the latter two currently pack position
   only into 20-byte AoS structs with no normal/color fields.
2. Update a single attribute (e.g. recomputed normals) without re-uploading the
   whole vertex buffer.

With AoS, a single attribute's bytes are scattered one-per-stride, so a channel
cannot be addressed as a contiguous range and partial streaming is impossible.
The device write primitive `RHI::IDevice::WriteBuffer(handle, data, size,
offset)` already supports arbitrary sub-range writes; the managed buffers are
device-local (`HostVisible=false`) and updated via staged copies regardless of
layout.

Three storage strategies were weighed: (A) AoS for static + SoA for dynamic
geometry; (B) both lanes with promote-on-edit conversion; (C) per-attribute
static/dynamic classification.

## Decision

Store managed geometry vertices as **uniform Structure-of-Arrays (SoA)**: one
contiguous sub-range per channel with its own buffer-device-address in the
geometry record. "Dynamic" is modeled as a **per-channel dirty bit** that drives
a partial `WriteBuffer(channelBDA, …, offset)` plus a per-channel upload→read
barrier — not as a separate storage class. A "static" channel is simply one that
never receives a partial write.

This is option (C) realized as uniform SoA + per-channel dirty: one vertex
layout, one shader fetch path, per-attribute streaming available to every
geometry kind.

RUNTIME-125 recorded a CPU smoke probe and a planning-only AoS proposal, but
the probe remained explicit non-adoption evidence and no allocator, shader, or
residency implementation followed. RUNTIME-139 therefore removed that dormant
proposal from the public contract. A future alternate layout requires a new
task backed by claim-eligible GPU profiling and a frozen matched-layout
adoption threshold.

## Consequences

- Positive: per-attribute streaming for all geometry kinds; one shader fetch
  path (no pipeline-variant explosion); graphs/point clouds can carry
  resolver-bound normals/colors by adding channels; channels can be added/removed
  without restriding; fits the existing managed-buffer sub-allocation + deferred-
  free + compaction model.
- Trade-off: static geometry loses some vertex-cache locality versus interleaved
  AoS. With BDA and sequential `gl_VertexIndex` the per-channel loads coalesce,
  and current evidence does not identify vertex fetch as this engine's
  bottleneck. If future profiling proves otherwise, a new evidence-backed task
  may introduce an alternate lane without relying on dormant planning types.
- Follow-up: RUNTIME-122 (SoA storage + shader fetch), RUNTIME-124 (per-channel
  streaming), RUNTIME-125 (non-adoption vertex-fetch probe), RUNTIME-139
  (retirement of speculative storage planning).

## Alternatives Considered

- **Dual AoS/SoA lanes now (A/B):** rejected as the foundation — two vertex
  layouts force two shader fetch paths (pipeline variants across forward /
  deferred / depth / selection / line / point passes) and conversion lifetime
  logic, for a cache-locality benefit that is unmeasured in this engine and that
  the upload mechanism (staged device-local copies) does not change. Reconsider
  only after claim-eligible GPU profiling demonstrates a material bottleneck;
  no public placeholder seam is retained meanwhile.
- **Keep AoS, scatter-write attributes:** rejected — `WriteBuffer` writes one
  contiguous range; strided per-attribute scatter is not supported and would be
  inefficient.
- **Memory-heap static/dynamic split (device-local vs host-visible/ReBAR):**
  orthogonal to layout; a future per-geometry/per-channel placement hint that
  sits on top of SoA, not a reason to fork the layout.

## Validation

- RUNTIME-122: CPU contract tests prove packers publish channel streams and
  `GpuWorld` records channel BDAs; opt-in `gpu;vulkan` smoke proves active
  GpuScene shaders fetch those channels on a Vulkan-capable host.
- RUNTIME-124: per-channel dirty tracking and partial uploads must add their own
  CPU/null contract coverage plus opt-in `gpu;vulkan` proof.
- RUNTIME-139: source and behavior ratchets prove uniform SoA remains the sole
  public/live contract and the non-adoption probe remains reproducible.
