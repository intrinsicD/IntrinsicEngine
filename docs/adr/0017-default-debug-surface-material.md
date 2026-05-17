# ADR 0017 — Default Debug Surface Material (Slot 0)

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics (`Extrinsic.Graphics.MaterialSystem` slot 0 registration, default debug surface shader pair, forward graphics pipeline)
- **Related tasks:** [`tasks/done/GRAPHICS-031`](../../tasks/done/GRAPHICS-031-default-debug-surface-material.md) (parent planning), [`GRAPHICS-031A`](../../tasks/done/GRAPHICS-031A-default-debug-surface-shaders-and-pipeline.md) (shader pair + pipeline implementation)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `## Material registry and slot contract` GRAPHICS-031 paragraph (lines 727–746) in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/active/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0018](0018-missing-material-fallback-substitution.md) records the substitution policy that uses this material as the fallback. [ADR-0014](0014-procedural-source-residency-bridge.md) records the `Triangle` packer whose vertex format this material consumes. [ADR-0015](0015-reference-scene-bootstrap.md) records the `TriangleProvider` reference entity that depends on this material to compose a frame.

## Context

`Extrinsic.Graphics.MaterialSystem` owns promoted material-slot allocation. Slot `0` (`kDefaultMaterialSlotIndex`) is the immutable fallback / default material slot: every stale or invalid material handle resolves to it. The `GRAPHICS-031` planning slice decided **what slot 0 actually contains** — the parent decision that the substitution policy in [ADR-0018](0018-missing-material-fallback-substitution.md) depends on.

Three forces shape the decision:

1. Slot 0 must produce a deterministic non-black, non-magenta, immediately-visible surface so missing or fallback materials are obvious to a developer without being confused with the GRAPHICS-015 magenta texture fallback ([ADR-0016](0016-texture-residency-and-asset-cache-policy.md) §3).
2. Slot 0 must not introduce new descriptor sets, new cull buckets, new passes, or new frame-recipe resources — it has to thread through the existing `SurfaceOpaque` cull bucket and `MaterialBuffer` SSBO so it is a backend-neutral addition.
3. The vertex format must match the `Triangle` packer planned by GRAPHICS-030-Impl-A (retired as [GRAPHICS-030A](../../tasks/done/GRAPHICS-030A-procedural-geometry-descriptor-cache.md)) so the [ADR-0015](0015-reference-scene-bootstrap.md) reference triangle composes through slot 0 by default.

This ADR captures the slot-0 material definition (registration name, type ID, flags, base-color factor, pre-population timing, shader pair, vertex format, pipeline state, cull-bucket reuse) plus the debug-variant naming family. The substitution / diagnostics half lives in [ADR-0018](0018-missing-material-fallback-substitution.md) as a separately traceable decision.

`docs/architecture/graphics.md` keeps the short canonical bullets (slot 0 is the immutable fallback; stale handles resolve to it; debug-variant naming family) and retains a single pointer line to this ADR for the slot-0 registration + shader pair + pipeline details.

## Decision

### 1. Slot 0 registration

Slot 0 is registered as `"Material.DefaultDebugSurface"` with:

- `MaterialTypeID = kMaterialTypeID_DefaultDebugSurface = 2u`.
- `MaterialFlags::Unlit`.
- Deterministic non-black `BaseColorFactor = { 0.55, 0.20, 0.85, 1.0 }` (a visible purple).

The slot is pre-populated by `MaterialSystem::Initialize()` and **republished byte-identical** by `MaterialSystem::RebuildGpuResources()` so the slot-0 contents survive operational transitions ([ADR-0005](0005-vulkan-operational-readiness-gate.md), [ADR-0004](0004-vulkan-backend-bringup-and-fallback.md) `RebuildOperationalResources` seam) without re-authoring.

### 2. Shader pair and bindings

The shader pair lives at:

- `assets/shaders/forward/default_debug_surface.vert`.
- `assets/shaders/forward/default_debug_surface.frag`.

The shaders consume:

- The canonical `GpuScenePushConstants` scene-table BDA.
- The existing `MaterialBuffer` SSBO at `set = 3, binding = 0`.

**No per-material descriptor set is added.** Slot 0 reuses the canonical material binding surface.

### 3. Vertex format

The vertex format is:

- Position: `vec3`.
- Optional packed RGBA8 vertex color: `uint32`.

This matches the `Triangle` packer from [ADR-0014](0014-procedural-source-residency-bridge.md) so the [ADR-0015](0015-reference-scene-bootstrap.md) reference triangle composes through slot 0 by default.

### 4. Forward graphics pipeline state

The forward graphics pipeline is created **once** at renderer init with:

- `CullMode = Back`.
- `DepthCompareOp = Less` (or `Equal` when running after `Pass.DepthPrepass`).
- `BlendEnabled = false`.
- `PolygonMode = Fill`.
- `PrimitiveTopology = TriangleList`.
- `MSAA samples = 1`.
- Dynamic state: `{ Viewport, Scissor }`.

### 5. Cull-bucket reuse

The default debug surface lane consumes the existing **`SurfaceOpaque` cull bucket**.

- **No new bucket.**
- **No new pass.**
- **No new descriptor set.**

### 6. Debug-variant naming family

Follow-up debug-material variants attach as additional `MaterialTypeDesc` registrations and additional well-known slot constants under the naming family:

- `Material.DefaultDebug<Variant>`.
- `kDefaultDebug<Variant>MaterialSlotIndex`.

Anticipated variants: `Wireframe`, `Line`, `Point`, `Normals`, `UVs`, `Depth`, `InstanceId`. They share the same descriptor layout family.

These variants are identified but **not opened** by `GRAPHICS-031`. Each variant lands under its own follow-up task ID when needed.

## Consequences

Positive:

- Slot 0 produces a deterministic visible purple surface, immediately distinguishable from the magenta-and-black texture fallback in [ADR-0016](0016-texture-residency-and-asset-cache-policy.md).
- The slot 0 contents are pre-populated by `Initialize()` and republished byte-identically by `RebuildGpuResources()`, so the slot survives operational transitions without re-authoring.
- The shader pair reuses the canonical `MaterialBuffer` SSBO binding (`set = 3, binding = 0`) and the `GpuScenePushConstants` scene-table BDA — no per-material descriptor set is added.
- The vertex format aligns with the `Triangle` packer from [ADR-0014](0014-procedural-source-residency-bridge.md), so the [ADR-0015](0015-reference-scene-bootstrap.md) reference triangle composes end-to-end through slot 0.
- No new cull bucket, no new pass, no new descriptor set, no new frame-recipe resource is introduced. Backends pick up slot 0 with zero structural changes.
- The debug-variant naming family is explicit, so future `Wireframe` / `Normals` / `UVs` debug materials land as additive registrations rather than parallel material systems.

Trade-offs and risks:

- The hard-coded `BaseColorFactor = { 0.55, 0.20, 0.85, 1.0 }` is a deliberate "this is the default debug surface" signal. Shipping builds that want a different fallback color must replace it through a follow-up task; reviewers must reject ad-hoc edits that "tone it down" because the visibility signal is the safety property.
- The cull-bucket reuse means slot 0 inherits any future change to `SurfaceOpaque` semantics. Reviewers must check that bucket changes (e.g., transparent / special-forward growth from `GRAPHICS-025`) preserve slot-0 visibility through the opaque lane.
- The shader pair lives at fixed paths under `assets/shaders/forward/`. A reorganization of the shader asset tree must update both paths and the loader; this ADR records the current paths as the canonical seam.
- `MaterialTypeID = 2u` is a magic constant. The header where it lives is the single seam — reviewers must reject any code that re-derives the ID from a string hash or a different scheme.
- The debug-variant naming family is reserved; a future debug material that uses a different naming scheme (`Material.WireframeOverlay` instead of `Material.DefaultDebugWireframe`) would silently fork the family and confuse consumers. Reviewers must enforce the `Material.DefaultDebug<Variant>` shape.

Follow-up tasks required: none from this ADR. The debug-variant family registrations (`Material.DefaultDebugWireframe`, …) and any future material-slot-ID render target land under their own task IDs.

## Alternatives Considered

- **Black or transparent slot-0 surface.** Rejected per §1: invisible fallbacks hide problems instead of surfacing them; the visible purple is the safety property.
- **Magenta slot-0 to match the texture fallback.** Rejected per §1: would confuse the "missing material" and "missing texture" signals; the texture fallback is magenta-and-black checkerboard, this material is solid purple.
- **Per-material descriptor set for slot 0.** Rejected per §2: would add a binding surface that every backend must thread; reusing `MaterialBuffer` at `set = 3, binding = 0` keeps the slot-0 shader pair backend-neutral.
- **New `Material.DefaultDebugSurface` cull bucket or pass.** Rejected per §5: would force frame-recipe additions and break the "no structural change" property; slot 0 routes through `SurfaceOpaque`.
- **Slot-0 contents authored at first use rather than at `Initialize()`.** Rejected per §1: would produce a one-frame "no fallback" window and would race operational transitions; pre-population + byte-identical republish is the deterministic shape.
- **Parallel naming families for debug variants (`Material.WireframeOverlay`, `Material.NormalsPreview`).** Rejected per §6: forks the family. The `Material.DefaultDebug<Variant>` shape keeps consumers' grouping rules stable.

## Validation

- [`tasks/done/GRAPHICS-031`](../../tasks/done/GRAPHICS-031-default-debug-surface-material.md) records the parent planning contract captured in §§1–6.
- [`tasks/done/GRAPHICS-031A`](../../tasks/done/GRAPHICS-031A-default-debug-surface-shaders-and-pipeline.md) records the shader pair, vertex format, descriptor / push-constant reuse, pipeline state, and cull-bucket reuse implementation captured in §§2–5.
- [ADR-0018](0018-missing-material-fallback-substitution.md) records the substitution / diagnostics half (when and why slot 0 replaces a snapshot record's resolved slot, and which counters increment).
- `src/graphics/renderer/README.md` carries the matching `Graphics.MaterialSystem` ownership-contract bullet documenting the slot-0 contents.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises the slot-0 registration, the `kMaterialLayoutVersion == 1` SSBO layout consumption, and the `RebuildGpuResources()` byte-identical republish without requiring a Vulkan device.
