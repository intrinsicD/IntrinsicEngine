# ADR 0022: Vertex storage — uniform SoA with per-channel streaming

- **Status:** Accepted
- **Date:** 2026-06-21
- **Owners:** runtime + graphics
- **Related tasks:** RUNTIME-120, RUNTIME-121, RUNTIME-122, RUNTIME-124, RUNTIME-125

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

An AoS "fast lane" for proven-static, vertex-fetch-bound geometry (options A/B)
is **deferred** to a profile-gated optimization task (`RUNTIME-125`); it is not
on the critical path.

## Consequences

- Positive: per-attribute streaming for all geometry kinds; one shader fetch
  path (no pipeline-variant explosion); graphs/point clouds can carry
  resolver-bound normals/colors by adding channels; channels can be added/removed
  without restriding; fits the existing managed-buffer sub-allocation + deferred-
  free + compaction model.
- Trade-off: static geometry loses some vertex-cache locality versus interleaved
  AoS. With BDA and sequential `gl_VertexIndex` the per-channel loads coalesce,
  and vertex fetch is rarely this engine's bottleneck; if profiling proves
  otherwise, `RUNTIME-125` adds the opt-in AoS fast lane.
- Follow-up: RUNTIME-122 (SoA storage + shader fetch), RUNTIME-124 (per-channel
  streaming), RUNTIME-125 (deferred AoS fast lane, profile-gated).

## Alternatives Considered

- **Dual AoS/SoA lanes now (A/B):** rejected as the foundation — two vertex
  layouts force two shader fetch paths (pipeline variants across forward /
  deferred / depth / selection / line / point passes) and conversion lifetime
  logic, for a cache-locality benefit that is unmeasured in this engine and that
  the upload mechanism (staged device-local copies) does not change. Retained as
  a deferred, profile-gated optimization (RUNTIME-125).
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
