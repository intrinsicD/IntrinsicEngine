# ADR 0007 — Picking, Selection, and Outline Reporting Seam

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics (SelectionSystem reporting seam), Runtime composition (StableEntityId resolution, outline-mask producer)
- **Related tasks:** [`tasks/done/GRAPHICS-012`](../../tasks/done/GRAPHICS-012-picking-selection-outline.md), [`GRAPHICS-012Q`](../../tasks/done/GRAPHICS-012Q-picking-backend-runtime-clarifications.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`docs/architecture/rendering-three-pass.md`](../architecture/rendering-three-pass.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `Extrinsic.Graphics.SelectionSystem` bullet in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/done/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0006](0006-camera-picking-and-gizmo-runtime-handoff.md) records the producer side (camera controllers, single-shot `PickPixelRequest` scheduling, gizmo handoff); this ADR records the consumer / reporting side (selection ID encoding, drain pattern, runtime ownership of `StableEntityId` resolution and outline-mask production).

## Context

`GRAPHICS-012` established the data-only graphics contracts for the `SelectionSystem` reporting surface, the four-domain selection vocabulary (`Entity`, `Face`, `Edge`, `Point`), and the `Picking.Readback` drain pattern. `GRAPHICS-012Q` then locked four producer-side / consumer-side details that `GRAPHICS-012` deliberately deferred:

1. The shader-side encoding of `EntityId` and `PrimitiveId` writes (so backends pack/unpack identically and the CPU/null contract stays authoritative).
2. The frame-record-time copy + next-`BeginFrame()` drain timing for `Picking.Readback` (so backends do not stall on the graphics queue and CPU/null mirrors the same drain).
3. The runtime/graphics ownership split for `StableEntityId` → live ECS resolution, selection / hover ECS mutation, and the selection-outline input mask that `SelectionOutlinePass` consumes.
4. The transparent / special-material picking eligibility rule that holds until `GRAPHICS-025` lands the hybrid transparent / special-forward lanes.

This ADR captures all four decisions as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical summary of the `SelectionSystem` reporting seam (CPU-visible, reporting-only, drains through the shared `Picking.Readback` pattern, runtime owns `StableEntityId` resolution and the outline mask) and retains a single pointer line to this ADR for the encoding, drain timing, and eligibility details.

## Decision

### 1. Shader-side `EntityId` and `PrimitiveId` encoding

`EntityId` is an `R32_UINT` target that carries the canonical stable extracted entity ID surfaced through `RenderableInstance` — the same `StableEntityId` reported by `PickReadbackResult::StableEntityId`. The cull buckets' `firstInstance` indirection lets every selection ID pass write the authoritative stable entity ID **without** consulting live ECS storage.

- Value `0` is reserved for "no hit" and must never be emitted for a valid renderable.

`PrimitiveId` is an `R32_UINT` target whose high four bits encode the `SelectionPrimitiveDomain` and whose low 28 bits encode the domain-local payload:

| Domain     | Value | Payload source                                                                                    |
|------------|------:|---------------------------------------------------------------------------------------------------|
| `None`     |     0 | reserved (no hit)                                                                                 |
| `Entity`   |     1 | `0` (entity-only hit, no sub-element refinement)                                                  |
| `Face`     |     2 | post-cull authoritative face index from the surface bucket's index range                          |
| `Edge`     |     3 | line bucket's domain-local segment index                                                          |
| `Point`    |     4 | point bucket's domain-local sample index                                                          |

The packing is `(domain << 28) | (payload & 0x0FFFFFFFu)`, mirroring `EncodedSelectionId::DomainShift = 28u` and `DomainMask = 0xFu << DomainShift` from `Graphics.SelectionSystem.cppm`.

Authoritative payload sources per pass:

- **`EntityIdPass`** writes the stable entity ID to `EntityId` and writes `EncodeSelectionId(SelectionPrimitiveDomain::Entity, 0u)` into `PrimitiveId`. A hit with no sub-element refinement still carries the entity domain bits.
- **`FaceIdPass`** writes `EncodeSelectionId(Face, faceIndex)` where `faceIndex` is the post-cull authoritative face index from the surface bucket's index range exposed by `RenderableInstance` (the same face IDs that drive CPU refinement). When an authoritative topology face ID is not available (helper lines or non-mesh surface sources), the rendered primitive index is written and CPU refinement remains the compatibility fallback per the surface/triangle policy in `rendering-three-pass.md`.
- **`EdgeIdPass`** writes `EncodeSelectionId(Edge, edgeIndex)` where `edgeIndex` is the line bucket's domain-local segment index from the same authoritative source the GPU draw uses; helper lines on pure-surface entities continue to fall back to CPU face-anchored refinement.
- **`PointIdPass`** writes `EncodeSelectionId(Point, pointIndex)` where `pointIndex` is the point bucket's domain-local sample index.

Backends never invert this packing themselves. Shaders pack `(domain << 28) | (payload & 0x0FFFFFFFu)`, and the CPU/null contract validates the same packing through `EncodeSelectionId`, so the `EncodedSelectionId` helper is the **single seam** for pack/unpack.

### 2. Backend `Picking.Readback` → `PublishPickResult` / `PublishNoHit` drain

The `Picking.Readback` resource is a graphics-owned, host-visible buffer copied from the requested pixel(s) of the `EntityId` and `PrimitiveId` targets at frame-record time. It is **not** a synchronous stall.

- The renderer copies requested pixels into `Picking.Readback` at frame-record time.
- The renderer drains the readback on the next `BeginFrame()` after the issuing frame's fences complete.
- Valid samples invoke `SelectionSystem::PublishPickResult(...)`, decoding the `R32_UINT` words through `EncodedSelectionId` and populating `PickReadbackResult::StableEntityId` from the matching `EntityId` sample.
- When the requested pixel reads `EntityId == 0`, when the original request was invalidated (resize, frame drop), or when the readback fails deterministically, the renderer calls `SelectionSystem::PublishNoHit()` instead.

The CPU/null backend simulates the same drain through the same seam without any Vulkan-specific code path, keeping the CPU/null correctness gate authoritative.

Backends never invoke `RequestPick` or `ConsumePick` themselves: pending-pick consumption stays inside the renderer's frame-record path so `SelectionSystemDiagnostics` counters (`PickRequestCount` / `PickConsumeCount` / `PickHitCount` / `PickNoHitCount`) remain comparable across backends.

### 3. Runtime ownership of `StableEntityId` and outline-mask resolution

`src/runtime` is the sole owner of:

- `StableEntityId` → live ECS entity resolution.
- Any selection / hover ECS mutation.
- The selection-outline input mask.

Runtime consumes `SelectionSystem::GetLastPickResult()` / `GetLastPointIdResult()` (or the equivalent extraction-side seam), resolves the stable ID through its sidecar maps, applies editor selection policy (single-select vs additive, hover vs commit, point picking vs entity picking), and surfaces the resulting selected / hovered stable IDs back through the runtime extraction batch as part of `SelectionSnapshot`. `SelectionOutlinePass` consumes that snapshot's stable IDs as the outline-mask producer.

Graphics never reads or mutates ECS state and never imports editor selection policy. This preserves the `AGENTS.md` graphics layer rules (`graphics/* -> no live ECS knowledge`) and the existing `Graphics.SelectionSystem` reporting-only contract.

### 4. Transparent / special-material picking eligibility

Until the implementation children identified by [`GRAPHICS-025`](../../tasks/done/GRAPHICS-025-hybrid-transparent-special-material-path.md) add transparent / special-material lanes, picking eligibility is restricted to the eight-bucket cull contract documented in `rendering-three-pass.md`:

- `SelectionSurface`, `SelectionLines`, and `SelectionPoints` mirror the opaque `SurfaceOpaque` / `Lines` / `Points` lanes for `Selectable` renderables only.
- Transparent and special-forward renderables are **not** eligible for ID-pass writes.
- Runtime extraction must surface them through CPU pick fallback (matching the existing CPU compatibility fallback policy for missing primitive hints) if editor policy requires picking transparent surfaces.

When those implementation children introduce transparent / special-material lanes, eligibility extends through new selectable sub-buckets **without** changing:

- The `EncodedSelectionId` domain / payload packing.
- The four-bucket selection vocabulary.

## Consequences

Positive:

- One pack / unpack seam (`EncodedSelectionId`) means the CPU/null contract is the same authority as any GPU backend; backends cannot drift on packing.
- Picking never stalls the graphics queue — readback is host-visible buffer copy + next-frame drain — so backends remain free to schedule presentation independently of pick-readback availability.
- Runtime owns `StableEntityId` → ECS resolution and the outline mask, preserving the `graphics/* -> no live ECS knowledge` invariant; the same code paths work for editor picking and runtime gameplay picking.
- The transparent picking gate is explicit and time-bounded by `GRAPHICS-025`; the encoding and vocabulary do not change when that lane lands.

Trade-offs and risks:

- The packed `(domain, payload)` shape uses only 28 payload bits per primitive. For meshes with > 2^28 primitives the encoding will saturate; the high-bit policy is locked because changing it would break wire parity between CPU/null and backend implementations. Future expansion would require a second `PrimitiveId` target rather than re-bit-allocating, which is a breaking change.
- The CPU-fallback path for transparent / special-material picking is an editor-policy escape hatch, not a graphics commitment. Runtimes that need transparent picking before `GRAPHICS-025` lands must implement the fallback in extraction; this ADR does not give them a graphics shortcut.
- Backends are forbidden from calling `RequestPick` / `ConsumePick` themselves. A backend that "helpfully" consumed a pick from a debug path would silently desynchronize `SelectionSystemDiagnostics` counters. The rule is explicit so reviewers can reject such code.

Follow-up tasks required: none from this ADR. `GRAPHICS-025` already tracks the transparent / special-forward lane extension and will land independently.

## Alternatives Considered

- **Per-backend `PrimitiveId` packing.** Rejected per §1: forces every consumer (CPU/null, Vulkan, future backends) to demultiplex against backend identity; the `EncodedSelectionId` helper is the single seam.
- **Synchronous `Picking.Readback` stall on the graphics queue.** Rejected per §2: stalls the frame and couples presentation to pick latency; the host-visible buffer + next-frame drain decouples them.
- **Graphics owns `StableEntityId` → live ECS resolution.** Rejected per §3: would require graphics to import live ECS storage, violating `AGENTS.md` §2.
- **Selection-outline mask produced by graphics from raw `EntityId` reads.** Rejected per §3: the outline mask is editor policy (which entities are visually highlighted) and must remain runtime-owned; graphics consumes the resolved `SelectionSnapshot.StableIds` and renders the mask without owning the policy.
- **Transparent picking through ID-pass writes before `GRAPHICS-025`.** Rejected per §4: would require sorting-aware ID writes in the forward transparent lane, which the eight-bucket cull contract does not yet provide; CPU pick fallback is the documented escape until `GRAPHICS-025` lands.
- **Two-target `PrimitiveId` to grow payload to 60 bits.** Rejected as out of scope: would change the wire contract and require all backends + the CPU/null reference to re-pack. Recorded here only so future readers know the single-target shape is intentional and time-bounded by `GRAPHICS-025`-class lane growth, not by primitive-count growth.

## Validation

- [`tasks/done/GRAPHICS-012`](../../tasks/done/GRAPHICS-012-picking-selection-outline.md) records the underlying `SelectionSystem` reporting surface, the four-domain selection vocabulary, and the `Picking.Readback` resource shape.
- [`tasks/done/GRAPHICS-012Q`](../../tasks/done/GRAPHICS-012Q-picking-backend-runtime-clarifications.md) records the four clarification decisions captured in §§1–4.
- `docs/architecture/rendering-three-pass.md` carries the matching `PrimitiveId` target row, the picking and sub-element selection contract, and the picking notes block authored by `GRAPHICS-012Q`.
- `src/graphics/renderer/README.md` carries the matching ownership-contract bullet next to the `DebugViewSystem` and `ImGuiOverlaySystem` entries.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises the CPU/null drain through the same `SelectionSystem::PublishPickResult` / `PublishNoHit` seam, so any divergence in packing, drain timing, or counter behavior surfaces without needing a Vulkan device.
