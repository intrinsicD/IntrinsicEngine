# ADR 0011 — Debug-View Inspection Table and Visualization Mode Mapping

- **Status:** Accepted
- **Date:** 2026-05-17
- **Owners:** Graphics (`Extrinsic.Graphics.DebugViewSystem`), Runtime/editor (UI-name dictionary, persistence)
- **Related tasks:** [`tasks/done/GRAPHICS-013B`](../../tasks/archive/GRAPHICS-013B-debug-view-and-render-target-inspection.md), [`GRAPHICS-013BQ`](../../tasks/archive/GRAPHICS-013BQ-debug-view-backend-clarifications.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`docs/architecture/rendering-three-pass.md`](../architecture/rendering-three-pass.md), [`src/graphics/renderer/README.md`](../../src/graphics/renderer/README.md)
- **Supersedes:** none. Extracted from the `Extrinsic.Graphics.DebugViewSystem` bullet in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/archive/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0007](0007-picking-selection-and-outline.md) records the `EncodedSelectionId` packing that §1 reuses for the `PrimitiveId` integer-hash visualization. [ADR-0010](0010-postprocess-chain-backend-policy.md) records the one-push-constant-block / one-pass-local-descriptor-set pattern that §2 mirrors.

## Context

`GRAPHICS-013B` established `Extrinsic.Graphics.DebugViewSystem` as the backend-agnostic render-target inspection and resource-selection seam: it builds a deterministic inspection table from `FrameRecipeIntrospection`, classifies resources (texture, depth texture, buffer, backbuffer, alias, unknown), and resolves the requested `DebugViewSettings::RequestedResourceName` with structured fallback diagnostics. The CPU/null contract owns the inspection table, settings shape, and fallback diagnostics.

`GRAPHICS-013BQ` answered four backend-shape questions that `GRAPHICS-013B` deferred:

1. How are per-resource visualization modes derived in the shader without growing `DebugViewSettings` / `DebugViewPushConstants`?
2. Where do `Pass.DebugView` descriptor bindings, per-aspect views, and samplers live?
3. Who owns the human-readable UI-name → `FrameRecipeIntrospection` canonical-name dictionary, and how does it interact with the inspection table?
4. How are buffer-class resources (scene tables, draw-bucket args, histogram, picking readback, transient debug streams) inspected, given that the fullscreen preview path cannot meaningfully display them?

This ADR captures those answers as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical summary of the `Extrinsic.Graphics.DebugViewSystem` seam (deterministic inspection table from `FrameRecipeIntrospection`, classification, requested-name resolution with structured fallback diagnostics) and retains a single pointer line to this ADR for the visualization-mode mapping, descriptor binding, UI-name ownership, and buffer-inspection policy.

## Decision

### 1. Shader visualization mode is derived, not user-selectable

Visualization mode is derived deterministically from the resolved selection's `FrameRecipeResourceKind` + `DebugViewResourceClass`. No user-selectable visualization-mode field is added to `DebugViewSettings`, and `DebugViewPushConstants` keeps its existing four-`uint32` packing: `ResourceKind`, `ResourceClass`, `UsedFallback`, `Reserved`.

The mapping is:

| Resource                                | Visualization                                                                                                                              |
|-----------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------|
| `SceneColorLDR` (`RGBA8_UNORM`)         | Direct LDR color blit                                                                                                                       |
| `SceneColorHDR` (`RGBA16_FLOAT`)        | Backend-local Reinhard tone-mapped color preview (no commitment to the postprocess chain)                                                  |
| `SceneDepth` and other depth-class, including `HZB.Current` | Linearize via existing `CameraUBO` near/far → grayscale ramp                                                                               |
| `SceneNormal` (`RGBA16_FLOAT`)          | World-space normal visualization (`* 0.5 + 0.5`)                                                                                            |
| `EntityId` (`R32_UINT`)                 | Deterministic 32-bit integer hash → color                                                                                                  |
| `PrimitiveId` (`R32_UINT`)              | Decode through `EncodedSelectionId` ([ADR-0007](0007-picking-selection-and-outline.md) §1); high 4 bits (`SelectionPrimitiveDomain`) modulate hue, low 28 bits drive the hash |
| `Albedo` (`RGBA8_UNORM`)                | Direct color                                                                                                                                |
| `Material0` (`RGBA16_FLOAT`, scalar PBR per `surface_gbuffer.frag`: roughness in R, metallic in G, reserved in B/A) | Scalar channel false-color visualization (roughness routed to red, metallic routed to green, blue zeroed) — **not** an integer slot-ID resource and never uses the integer-hash path |
| `ShadowAtlas`                           | Depth-aspect linearization to grayscale at a backend-fixed shadow projection                                                                |
| `DebugViewRGBA`                         | **Non-selectable** as a preview input (see below)                                                                                          |

`DebugViewRGBA` is the `Pass.DebugView` color attachment. `DebugViewSystem::BuildInspectionTable()` already gates `Previewable` on `resource.Kind != FrameRecipeResourceKind::DebugViewRGBA`, so the visualization-mode mapping has **no** `DebugViewRGBA` entry and backends must not invent a `DebugViewRGBA` blit / self-preview mode that would bypass that aliasing gate.

Adding a separate material-slot-ID render target later is a distinct follow-up that would introduce its own `R32_UINT` resource and integer-hash mapping. Future user-selectable overrides (manual mode, channel masks, false-color ramps) are explicit follow-ups and must extend `DebugViewSettings` / `DebugViewPushConstants` through their own task with budget justification.

### 2. Descriptor binding ownership

`Pass.DebugView` owns **one** pass-local descriptor set with exactly two bindings:

- `b0` — sampled image view of the resolved selection's texture / depth resource.
- `b1` — single linear-clamp sampler.

This mirrors the postprocess pattern of one push-constant block plus one pass-local descriptor set ([ADR-0010](0010-postprocess-chain-backend-policy.md) §4).

Backend-local responsibilities under `src/graphics/vulkan` (never leak through RHI or renderer module surfaces):

- `VkDescriptorSetLayout` definitions.
- Per-aspect view creation:
  - Color view for `RGBA8_UNORM` / `RGBA16_FLOAT` resources.
  - Depth-aspect-only view for depth-class resources.
  - Integer-typed view for `R32_UINT` selection-ID resources (`EntityId` / `PrimitiveId`).
- Sampler creation.

`DebugViewRGBA` is the pass color attachment, owned by the framegraph through the frame-recipe resource map, and is **not** part of the descriptor set. Aliasing protection relies on the existing exclusion of `DebugViewRGBA` from the previewable inspection set, so no separate descriptor-side aliasing gate is added.

**No retained graphics-owned debug-view textures or buffers exist.** Backends never bypass `DebugViewSystem` to allocate debug-view state, and the CPU/null backend exercises the same seam without Vulkan-specific code so the default CPU correctness gate stays authoritative.

### 3. UI-name dictionary ownership

Runtime / editor code owns the dictionary that maps human-readable UI strings (for example "Scene Color (HDR)" or "Picking IDs") to canonical `FrameRecipeIntrospection::Resources[i].Name` keys (for example `"SceneColorHDR"`, `"EntityId"`).

The runtime:

1. Builds this dictionary from `DebugViewSystem::BuildInspectionTable(frameRecipe)` rows. The `Name` / `Kind` / `ResourceClass` / `Enabled` / `Previewable` columns drive grouping, disabled-state UX, and previewable filtering.
2. Writes the canonical name into `DebugViewSettings::RequestedResourceName` via `DebugViewSystem::SetSettings(...)`.

Graphics never receives display strings, never imports ImGui or platform / window state, and never owns the dictionary.

The default `RequestedResourceName = "FrameRecipe.PresentSource"` remains the graphics-side fallback when runtime has not yet selected anything.

Editor persistence of the active selection across sessions is runtime-owned and out of scope here; the matching runtime adapter shape lands as part of the `GRAPHICS-014Q` visualization-runtime work tracked by [ADR-0009](0009-visualization-packets-and-overlay-upload.md).

### 4. Buffer textual / statistical inspection is deferred

Buffer resources (GPU scene tables, draw-bucket args / counts, `PostProcess.Histogram`, `Picking.Readback`, transient debug streams) remain listed in `BuildInspectionTable()` with `DebugViewResourceClass::Buffer` but stay deliberately **non-previewable** in `Pass.DebugView`.

Textual and statistical inspection of buffers is **deferred** to a future runtime / editor visualization surface tracked under `GRAPHICS-014Q` — **not** added to the fullscreen preview path. That future surface consumes existing per-owner diagnostics rather than introducing a parallel buffer-readback API on `DebugViewSystem`:

- `PostProcessDiagnostics` for `PostProcess.Histogram`.
- `SelectionSystem` plus `Picking.Readback` drains for picking.
- `GpuWorld::Diagnostics` for scene-table buffers.
- `SpatialDebugVisualizerDiagnostics` for transient debug streams.

`DebugViewSystem` does not grow a buffer-inspection method or buffer-readback path under this clarification, and the CPU/null contract from `GRAPHICS-013B` is unchanged. If runtime work later surfaces a graphics-side buffer-inspection seam, it is promoted via a new follow-up task rather than re-opening `GRAPHICS-013B`.

## Consequences

Positive:

- The push-constant packing stays at four `uint32`s; no per-resource visualization-mode field grows. Adding modes is a deliberate budget decision.
- One pass-local descriptor set with exactly two bindings keeps `Pass.DebugView` symmetrical with the postprocess passes; backend reviewers have one shape to check.
- Graphics never imports ImGui / platform / window state or human-readable strings; the UI-name dictionary is editor / runtime policy.
- Buffer inspection routes through existing per-owner diagnostics rather than a parallel `DebugViewSystem` API, so each buffer kind has exactly one diagnostics owner.
- `DebugViewRGBA` self-sampling is prevented by the inspection-table gate; backends cannot bypass that gate by inventing a self-preview blit.

Trade-offs and risks:

- Quality of preview is hard-coded per resource. A reader who wants a different visualization (e.g., custom false-color ramp for `SceneNormal`) cannot toggle it from settings — they must extend `DebugViewSettings` / `DebugViewPushConstants` through their own task with budget justification. This is deliberate friction.
- Buffer inspection is deferred indefinitely (tracked under `GRAPHICS-014Q`). Until that lands, buffer-class resources show up in the inspection table but cannot be previewed; reviewers must not "fix" this by quietly adding a buffer-readback path on `DebugViewSystem`.
- The `Material0` channel mapping is tied to `surface_gbuffer.frag`'s scalar PBR layout. If that shader is reorganized, the visualization mapping must move with it; this ADR records the current binding so future readers do not interpret `Material0` as an integer slot-ID hash by mistake.
- The runtime-owned UI-name dictionary must stay in sync with `FrameRecipeIntrospection` rows. Drift produces "ghost" entries in the editor that point at resource names the recipe no longer publishes; the runtime side is responsible for refreshing on recipe rebuild.

Follow-up tasks required: none from this ADR. Future user-selectable visualization-mode overrides, the buffer-inspection surface, and any material-slot-ID render target are explicit follow-ups under their own task IDs.

## Alternatives Considered

- **User-selectable visualization-mode field on `DebugViewSettings`.** Rejected per §1: would grow the push-constant packing, force per-resource compatibility tables, and let users select nonsense pairings (e.g., normal visualization on a depth resource). The deterministic mapping from `(Kind, Class)` is the canonical seam.
- **Self-preview blit for `DebugViewRGBA`.** Rejected per §§1, 2: bypasses the inspection-table aliasing gate and could create read-during-write on the pass color attachment. The aliasing gate is the single seam.
- **Backend-owned debug-view textures (retained shadow / preview buffer outside the framegraph).** Rejected per §2: would duplicate frame-recipe state and let backends drift on debug-view lifetime; everything stays frame-transient or framegraph-managed.
- **Graphics-owned UI-name dictionary or display-string mapping.** Rejected per §3: pulls ImGui / platform / window state into graphics. Runtime / editor own the dictionary and write canonical names into `DebugViewSettings::RequestedResourceName`.
- **Buffer-readback API on `DebugViewSystem`.** Rejected per §4: each buffer kind already has a diagnostics owner; a parallel readback API would have to demultiplex by buffer kind and would duplicate state the per-owner diagnostics already publish.

## Validation

- [`tasks/done/GRAPHICS-013B`](../../tasks/archive/GRAPHICS-013B-debug-view-and-render-target-inspection.md) records the underlying `Extrinsic.Graphics.DebugViewSystem` contract, the inspection table shape, the requested-name resolution, and the structured fallback diagnostics.
- [`tasks/done/GRAPHICS-013BQ`](../../tasks/archive/GRAPHICS-013BQ-debug-view-backend-clarifications.md) records the four clarification decisions captured in §§1–4.
- `docs/architecture/rendering-three-pass.md` carries the matching debug-view contract block authored by `GRAPHICS-013BQ`.
- `src/graphics/renderer/README.md` carries the matching ownership-contract bullet next to the existing `DebugViewSystem` line.
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises the inspection table, the visualization-mode mapping table, the `DebugViewRGBA` previewable exclusion, and the four-`uint32` push-constant packing without a Vulkan device.
