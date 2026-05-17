# ADR 0013 — ECS Renderable Residency Bridge

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Runtime extraction (bridge owner, sidecar / cache), Graphics (data-only `GpuSceneSlot` value type, `GpuWorld` upload seam)
- **Related tasks:** [`tasks/done/GRAPHICS-028`](../../tasks/done/GRAPHICS-028-ecs-renderable-residency-bridge.md), [`GRAPHICS-023A`](../../tasks/done/GRAPHICS-023A-gpu-scene-slot-asset-generation-tracking.md), [`GRAPHICS-023B`](../../tasks/done/GRAPHICS-023B-gpu-scene-slot-asset-rebind-decision.md), [`GRAPHICS-023C`](../../tasks/done/GRAPHICS-023C-runtime-asset-generation-observation.md), [`GRAPHICS-023D`](../../tasks/done/GRAPHICS-023D-runtime-asset-generation-rebind-acknowledgment.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `## ECS renderable residency bridge` section in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/done/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0014](0014-procedural-source-residency-bridge.md) records the procedural-geometry first slice of this bridge captured by `GRAPHICS-030`; this ADR records the parent contract.

## Context

The promoted graphics layer must not import live ECS storage (`AGENTS.md` §2: "`graphics/* -> no live ECS knowledge`"). At the same time, runtime composition must be able to:

- Read live ECS to find renderable entities.
- Track per-entity GPU residency state (instance handles, scene slots, material instances, asset bindings, last-seen asset generations).
- Decide when a previously bound asset's generation has changed and the renderable needs a rebind.
- Submit per-frame transform / material / visualization records and clear consumed dirty tags.

`GRAPHICS-028` records the parent planning contract for that bridge. `GRAPHICS-023A` / `023B` / `023C` / `023D` record an explicit four-step observation / acknowledgment loop for asset rebinds that keeps graphics out of ECS:

- **`023A`** introduces the `GpuSceneSlot::SourceAsset` / `LastSeenAssetGeneration` fields as a data-only value type owned by the runtime sidecar.
- **`023B`** adds `GpuSceneSlot::EvaluateSourceAssetRebind(observed)` — a pure value-type method that compares the stored binding against an observed `(AssetId, generation)` without importing `Graphics.GpuAssetCache` or querying live asset state.
- **`023C`** wires runtime observation: render extraction may read `AssetInstance::Source`, query a supplied `Graphics.GpuAssetCache`, and report whether a future rebind is required, **without** advancing `LastSeenAssetGeneration`.
- **`023D`** closes the loop with `Runtime::AcknowledgeRenderableAssetRebind(slot, observation)` — an explicit caller-driven helper that advances `LastSeenAssetGeneration` only when the asset identity matches; no auto-acknowledge, no upload, no file-watching, no shader recompile, no texture residency reload.

This ADR captures the parent contract as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical pointer to this ADR and to the planning task; the detailed static-vs-dynamic stream policy, dirty-tag CPU-only invariant, hierarchy / primitive policy, and the four-step observation / acknowledgment loop live here.

## Decision

### 1. Bridge owner

The ECS-renderable-residency bridge owner is **runtime**.

- `Extrinsic.Runtime.RenderExtraction` is allowed to query live ECS, read CPU-only `AssetInstance::Source`, `GeometrySources::*`, hierarchy, transform, and dirty-tag components, and maintain an entity-keyed residency sidecar / cache.
- That cache may store graphics-owned value types — `Graphics::Components::GpuSceneSlot`, `GpuInstanceHandle`, material instances, and asset-binding metadata (`GpuSceneSlot::SourceAsset` and `GpuSceneSlot::LastSeenAssetGeneration`) — but those values remain **outside** canonical ECS components and **outside** the live `entt::registry` as GPU-typed ECS state.

Graphics render passes receive only renderer-submitted snapshots / views and must not query ECS or runtime sidecar storage directly.

### 2. Four-step asset-rebind observation / acknowledgment loop

This is the seam that lets the runtime sidecar detect a rebind without graphics importing live ECS or asset state, and without runtime silently acknowledging stale bindings.

1. **`GpuSceneSlot` carries the binding (GRAPHICS-023A).** The sidecar's `GpuSceneSlot` holds `SourceAsset: Assets::AssetId` and `LastSeenAssetGeneration: uint64`. These fields are CPU-only data; graphics never reads them through render-pass code.
2. **`EvaluateSourceAssetRebind(observed)` decides (GRAPHICS-023B).** A pure value-type method on `GpuSceneSlot` compares the stored `(SourceAsset, LastSeenAssetGeneration)` against an observed `(AssetId, generation)` supplied by the caller. It does **not** import `Graphics.GpuAssetCache` and does **not** query live asset state. The result is a typed enum (`NoChange` / `RebindRequired` / `IdentityMismatch`).
3. **Runtime observes (GRAPHICS-023C).** `RenderExtractionCache` may read `AssetInstance::Source`, query a supplied `Graphics.GpuAssetCache` for the current generation, call `EvaluateSourceAssetRebind(observed)`, and report whether a future rebind is required. It does **not** mark newer generations as last-seen.
4. **Runtime acknowledges explicitly (GRAPHICS-023D).** `Runtime::AcknowledgeRenderableAssetRebind(slot, observation)` advances `GpuSceneSlot::LastSeenAssetGeneration` to the observed generation **only** when the asset identity matches. It does **not** auto-acknowledge inside `RenderExtractionCache::ExtractAndSubmit`, perform uploads, watch files, recompile shaders, or reload texture residency. The caller (a later upload / rebind slice) is responsible for performing the binding work before acknowledging.

This split keeps acknowledgment a deliberate act: a runtime that observes a newer generation but cannot rebind this frame leaves the slot un-acknowledged so the observation re-fires next frame.

### 3. Static-vs-dynamic geometry residency

Static-vs-dynamic geometry residency is decided **per stream**:

- **Immutable, shared asset geometry** flows from the CPU asset identifier on `AssetInstance::Source` through runtime normalization to `Assets::AssetId`, then through `Graphics.GpuAssetCache` and `GpuWorld::UploadGeometry()`.
- **Dynamic per-entity streams** are owned by runtime residency policy and may use `GpuSceneSlot` named buffers or a future `GpuWorld` successor for per-entity updates.
- **Per-instance state** — transforms, render flags, bounds, material slots — continues to flow through runtime-submitted transform / material records and `GpuWorld` instance SSBO updates.

### 4. Dirty tags are CPU-only semantic markers

Dirty tags in ECS remain CPU-only semantic markers. Editing systems may mark:

- `DirtyTransform`.
- `DirtyVertexPositions`.
- `DirtyVertexAttributes`.
- Topology tags.
- The `GpuDirty` escape hatch.

But they **must not** encode:

- Renderer buffer names.
- `RHI::BufferHandle` values.
- Bindless indices.
- `GpuSceneSlot` references.

The runtime bridge consumes tags in dependency order, maps CPU property / channel changes to packed upload streams or named dynamic buffers, and clears **only** the tags it has consumed.

Mesh, graph, point-cloud, and primitive domains share a uniform `GpuWorld::GeometryUploadDesc` target so `GpuWorld` remains domain-agnostic.

### 5. Hierarchy and primitive policy

Hierarchy and primitive policy are also **runtime-owned**:

- Only renderable leaves with geometry sources or asset sources materialize `GpuInstanceHandle` entries.
- Root / interior hierarchy nodes propagate transforms, visibility, and editor policy to leaves **before** extraction.
- Authored primitive entities default to regular `GpuWorld` instances that reference shared unit geometry so they participate in culling, sorting, and picking.
- High-volume transient debug primitives may instead be collected into per-frame instanced debug batches following the existing transient debug packet pattern, **without** consuming retained `GpuWorld` instance slots.

### 6. Implementation status

The full bridge remains a follow-up to the completed `GRAPHICS-028` planning task. The current promoted runtime extraction cache already owns the partial sidecar for:

- Render hints.
- Instance allocation / free.
- Transform / light / visualization submission.
- Consumed `DirtyTransform` clearing.

The asset-rebind observation / acknowledgment loop from §2 is complete and CPU-tested. The remaining static-vs-dynamic stream extraction, hierarchy traversal, and primitive collection paths land under their own follow-up task IDs.

## Consequences

Positive:

- Graphics stays clean of live ECS imports; the `graphics/* -> no live ECS knowledge` invariant from `AGENTS.md` §2 holds.
- The four-step observation / acknowledgment loop is the only path through which the sidecar's last-seen asset generation can advance, so a runtime that observes a newer generation but cannot rebind this frame deterministically re-fires next frame.
- `GpuSceneSlot::EvaluateSourceAssetRebind(...)` is a pure value-type method; tests can exercise it without booting an asset cache or a renderer.
- Per-stream static / dynamic residency lets immutable shared geometry share `GpuAssetCache` leases while dynamic per-entity streams keep per-entity ownership.
- Dirty tags stay CPU-only; an editor that wants to mark a property dirty cannot accidentally pin a `GpuSceneSlot` reference into ECS storage.

Trade-offs and risks:

- The four-step loop is deliberately not auto-acknowledged. A caller that forgets to call `Runtime::AcknowledgeRenderableAssetRebind(...)` after a successful upload will see the observation re-fire every frame. This is the intended safety property but means upload/rebind callers must wire the acknowledgment explicitly.
- The full bridge is not implemented; only the partial sidecar (render hints, instance alloc/free, transform/light/visualization submission, dirty-transform clearing) and the asset-rebind loop are live. Future follow-ups must respect the static-vs-dynamic split, dirty-tag CPU-only invariant, and hierarchy / primitive policy.
- `GpuWorld::GeometryUploadDesc` is a single upload-descriptor shape across mesh / graph / point-cloud / primitive domains. Adding a domain whose upload genuinely needs more state would force a contract change here; the current single-shape rule is intentional to keep `GpuWorld` domain-agnostic.
- High-volume transient debug primitives use per-frame instanced debug batches rather than retained `GpuWorld` slots. A naïve implementation that "helpfully" promotes them to retained instances would silently regress instance-slot pressure; reviewers must check that authored primitives use retained slots and transient debug uses the packet path.

Follow-up tasks required: the remaining static / dynamic stream extraction, hierarchy traversal, and primitive collection slices land under their own follow-up task IDs once promoted from the parent `GRAPHICS-028` planning record.

## Alternatives Considered

- **Graphics queries live ECS.** Rejected per §1: violates `AGENTS.md` §2 and would couple graphics to ECS storage lifetimes.
- **Storing GPU-typed values (`GpuSceneSlot`, `GpuInstanceHandle`, …) as canonical ECS components.** Rejected per §1: would create graphics-typed live ECS state and force ECS lifetimes onto GPU resources.
- **Auto-acknowledge `LastSeenAssetGeneration` inside `RenderExtractionCache::ExtractAndSubmit`.** Rejected per §2: would advance the last-seen generation even when the upload had not actually happened, so a failed rebind would look successful next frame.
- **Encoding renderer buffer names or bindless indices in dirty tags.** Rejected per §4: dirty tags must stay CPU-only semantic markers so editors and runtime systems can mark intent without pinning renderer state into ECS.
- **Materializing `GpuInstanceHandle` entries for non-renderable hierarchy roots.** Rejected per §5: wastes retained instance slots on nodes that have no geometry; only leaves with geometry / asset sources earn a handle.
- **Retained `GpuWorld` slots for high-volume transient debug primitives.** Rejected per §5: would saturate instance-slot pressure with per-frame churn; the transient debug packet path already handles that traffic correctly.

## Validation

- [`tasks/done/GRAPHICS-028`](../../tasks/done/GRAPHICS-028-ecs-renderable-residency-bridge.md) records the parent planning contract captured in §§1, 3–6.
- [`tasks/done/GRAPHICS-023A`](../../tasks/done/GRAPHICS-023A-gpu-scene-slot-asset-generation-tracking.md), [`023B`](../../tasks/done/GRAPHICS-023B-gpu-scene-slot-asset-rebind-decision.md), [`023C`](../../tasks/done/GRAPHICS-023C-runtime-asset-generation-observation.md), and [`023D`](../../tasks/done/GRAPHICS-023D-runtime-asset-generation-rebind-acknowledgment.md) record the four-step asset-rebind observation / acknowledgment loop captured in §2.
- `src/graphics/renderer/README.md` carries the matching CPU-only `GpuSceneSlot` ownership note alongside the runtime extraction documentation.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises `GpuSceneSlot::EvaluateSourceAssetRebind(...)` and `Runtime::AcknowledgeRenderableAssetRebind(...)` without requiring an asset cache or a Vulkan device.
