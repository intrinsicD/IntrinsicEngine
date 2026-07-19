# ADR 0015 — Runtime Reference Scene Bootstrap

- **Status:** Superseded in part by
  [ADR-0027](0027-right-sized-runtime-composition.md) / `RUNTIME-180`
- **Date:** 2026-05-17
- **Owners:** Runtime content helper (`Extrinsic.Runtime.ReferenceScene`), App
  (`Sandbox` bootstrap/teardown and optional camera-seed handoff)
- **Related tasks:** [`tasks/done/GRAPHICS-029`](../../tasks/archive/GRAPHICS-029-runtime-reference-scene-bootstrap.md), [`GRAPHICS-029A`](../../tasks/archive/GRAPHICS-029A-reference-scene-skeleton.md), [`GRAPHICS-029B`](../../tasks/archive/GRAPHICS-029B-triangle-provider-and-camera.md), [`tasks/done/GRAPHICS-080`](../../tasks/archive/GRAPHICS-080-enable-promoted-vulkan-by-default.md), [`RUNTIME-180`](../../tasks/done/RUNTIME-180-extract-camera-module.md)
- **Related docs:** [`docs/architecture/graphics.md`](../architecture/graphics.md), [`src/runtime/README.md`](../../src/runtime/README.md), [`src/app/Sandbox/README.md`](../../src/app/Sandbox/README.md)
- **Supersedes:** none. Extracted from the `## Reference scene bootstrap` section in `docs/architecture/graphics.md` per [`DOCS-001`](../../tasks/archive/DOCS-001-reduce-graphics-architecture-prose.md).
- **Related ADRs:** [ADR-0013](0013-ecs-renderable-residency-bridge.md) records the ECS-renderable residency bridge that the reference scene's entities feed into. [ADR-0014](0014-procedural-source-residency-bridge.md) records the procedural-source residency bridge that early `TriangleProvider` slices exercised. [ADR-0005](0005-vulkan-operational-readiness-gate.md) records the Vulkan operational truth table that governs which backend actually runs when the reference sandbox launches with `Render.EnablePromotedVulkanDevice = true`.
- **Status update:** RUNTIME-097 (2026-06-07) superseded the default
  triangle's procedural-source detail. `RUNTIME-180` (2026-07-19) then
  superseded the provider/registry, Engine-owned lifecycle, and rejection of
  app-owned bootstrap. The content and layering contracts remain in force.

## RUNTIME-180 amendment

ADR-0027 classifies initial reference content as application bootstrap rather
than kernel behavior. `RUNTIME-180` applies that decision:

- `Extrinsic.Runtime.ReferenceScene` exports only
  `ReferenceSceneEntity` / `ReferenceScenePopulation`,
  `BootstrapReferenceScene(selector, scene)`, and
  `TeardownReferenceScene(scene, population) noexcept`.
- The triangle constructor is private. The public provider interface,
  `TriangleProvider`, provider registry, default-registration helpers, camera
  finalization helper, and `ReferenceSceneControl` are retired.
- Generic `Engine` does not interpret `ReferenceSceneConfig`, retain an
  installed flag/population/seed, or expose reference-scene facades.
- Sandbox bootstraps enabled reference content at most once during application
  initialization, retains the exact owning `WorldHandle`, and tears down only
  through that original world when it is still live. A retired original world
  is a safe no-op and a replacement world is never mutated.
- Sandbox optionally hands `population.Camera` to the exact
  app-composed `CameraControllerRegistry`. Reference content remains visible
  and selectable when `CameraModule` is omitted.

The remainder of this ADR records the historical GRAPHICS-029 decision. Where
it assigns lifecycle to Engine, requires providers/registry, or rejects
app-owned bootstrap, this amendment and ADR-0027 take precedence. The
single-entity content shape, CPU-only ECS state, and strict no-RHI/no-renderer
imports remain authoritative.

## Context

The promoted graphics pipeline needs at least one renderable candidate to validate that:

- `Runtime.RenderExtraction` observes a live renderable entity.
- The runtime-owned residency bridge lands the geometry through the selected source lane.
- The default debug-surface material ([GRAPHICS-031](../../tasks/archive/GRAPHICS-031-default-debug-surface-material.md)) composes a frame without crashing on missing geometry.

Without a bootstrap, every sandbox / test that wants a "visible triangle" must either author one ad-hoc or wait for the full ECS scene editor to land. That fragments coverage and makes the GRAPHICS-033D opt-in `gpu;vulkan` visible-triangle smoke depend on whichever app happened to author the test entity.

`GRAPHICS-029` records the planning contract for a **runtime-owned, opt-in** reference scene that produces at least one renderable candidate observable by `Runtime.RenderExtraction`. `GRAPHICS-029A` lands the skeleton + config plumbing; `GRAPHICS-029B` lands the `TriangleProvider` + camera seed + contract test. The reference scene must:

1. Stay on the runtime side (apps stay policy-light per the app / runtime boundary).
2. Use a runtime-owned residency bridge rather than authoring asset state.
3. Not attach any GPU-typed value to the ECS entity (preserving the [ADR-0013](0013-ecs-renderable-residency-bridge.md) `ecs → core` invariant).
4. Compose forward-compatibly with the future `Extrinsic.Runtime.CameraControllers` umbrella ([ADR-0006](0006-camera-picking-and-gizmo-runtime-handoff.md) §1).
5. Layer cleanly: no `Extrinsic.Graphics.GpuWorld` / `Renderer` / `GpuAssetCache` / RHI / asset / platform imports.

This ADR captures those answers as the canonical durable home. `docs/architecture/graphics.md` keeps a short canonical pointer to this ADR.

## Decision

### 1. Runtime ownership, opt-in via config

The bootstrap owner is **runtime**:

- A planned module `Extrinsic.Runtime.ReferenceScene` (source `src/runtime/Runtime.ReferenceScene.cppm`) exposes:
  - An `IReferenceSceneProvider` interface.
  - A `ReferenceSceneRegistry` container.
- `Engine::Initialize()` resolves a single `EngineConfig::ReferenceScene::Selector` value (default `ReferenceSceneSelector::Triangle`) and invokes the matching provider **after** the scene registry is constructed but **before** the first frame.
- A `bool m_ReferenceSceneInstalled` guard rejects double-install.
- Teardown runs in `Engine::Shutdown()` before scene tear-down.

App / sandbox stays policy-light per the app / runtime boundary:

- `Sandbox::main.cpp` does **not** flip the flag.
- `CreateReferenceEngineConfig()` flips `EngineConfig::ReferenceScene::Enabled = true`.
- The same helper also sets `Render.EnablePromotedVulkanDevice = true` ([GRAPHICS-080](../../tasks/archive/GRAPHICS-080-enable-promoted-vulkan-by-default.md)) so reference sandbox launches **request** the promoted Vulkan backend; the resolved device remains governed by the [ADR-0005](0005-vulkan-operational-readiness-gate.md) reconciliation truth table.

### 2. `TriangleProvider`: one entity, no GPU-typed ECS state

The first provider, `TriangleProvider`, creates **exactly one entity** through the HARDEN-060 `ECS::Scene::CreateDefault(scene, "ReferenceTriangle")` API.

The entity carries:

- `MetaData{ EntityName = "ReferenceTriangle" }`.
- `Transform::Component{}` (identity).
- `Transform::WorldMatrix{}` (identity).
- `Hierarchy::Component{}` (no parent / children).
- `ECS::Components::StableId` with a durable runtime-authored identity.
- `ECS::Components::Selection::SelectableTag`.
- Exactly one `Graphics::Components::RenderSurface{ Domain = SourceDomain::Vertex }` render hint.
- `Graphics::Components::VisualizationConfig{ Source = UniformColor, Color = white }`.
- Mesh-domain `ECS::Components::GeometrySources` populated from one finite halfedge triangle.

**No GPU-typed value** is attached to the entity:

- No `GpuSceneSlot`.
- No `GpuInstanceHandle`.
- No `RHI::*Handle`.
- No bindless index.
- No material lease.

Residency state stays in the runtime extraction sidecar per [ADR-0013](0013-ecs-renderable-residency-bridge.md) §1.

**No light is created.** The [GRAPHICS-031](../../tasks/archive/GRAPHICS-031-default-debug-surface-material.md) default debug-surface material is unlit by contract, so the first milestone composes through the null renderer without lighting work.

### 3. Camera authorship is forward-compatible

Camera authorship is forward-compatible with the [ADR-0006](0006-camera-picking-and-gizmo-runtime-handoff.md) §1 `Extrinsic.Runtime.CameraControllers` umbrella.

The provider returns an optional `Graphics::CameraViewInput` value:

- Default position: `(0, 0, 3)` looking at the origin.
- 45° vertical FoV.
- Near 0.1, far 100.
- Aspect from `RenderFrameInput::Viewport`.

`Engine::BuildRenderFrameInput()` substitutes that value into `RenderFrameInput::Camera` **while no controller is wired**. When `CameraControllers` opens, the seed value becomes its starting state with **no contract change**.

Determinism for tests is achieved at the content layer through `MetaData::EntityName` and the provider-returned entity span, **not** through `entt::entity` IDs (entt does not promise allocator determinism across registries).

### 4. Strict layering imports

The reference scene module imports **only**:

- ECS scene / component modules.
- `Graphics.Component.RenderGeometry` (CPU-only render-hint value types already imported by `Runtime.RenderExtraction`).
- `Graphics.CameraSnapshots` (for `CameraViewInput`).
- `Core.Config.Engine`.

It does **not** import:

- `Extrinsic.Graphics.GpuWorld`.
- `Extrinsic.Graphics.Renderer`.
- `Extrinsic.Graphics.GpuAssetCache`.
- Any `Extrinsic.RHI.*`.
- `Extrinsic.Asset.*`.
- `Extrinsic.Platform.*`.

So the `AGENTS.md` §2 layering invariants and the [ADR-0013](0013-ecs-renderable-residency-bridge.md) residency-bridge contract both hold.

### 5. Implementation children

- **`GRAPHICS-029-Impl-A`** — skeleton + config plumbing. Retired as [`GRAPHICS-029A`](../../tasks/archive/GRAPHICS-029A-reference-scene-skeleton.md).
- **`GRAPHICS-029-Impl-B`** — `TriangleProvider` + camera seed + contract test. Retired as [`GRAPHICS-029B`](../../tasks/archive/GRAPHICS-029B-triangle-provider-and-camera.md).
- **`GRAPHICS-029-Impl-C`** (optional) — additional providers gated on `GRAPHICS-030` / `GRAPHICS-034`. Identified but not opened.

## Consequences

Positive:

- The reference scene is the canonical single seed for "visible triangle" tests, so the [GRAPHICS-033D](../../tasks/archive/GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md) opt-in `gpu;vulkan` smoke does not need to author its own entity.
- The opt-in `EngineConfig::ReferenceScene::Enabled` flag means production apps that don't want the entity don't pay for it.
- The provider returns a `CameraViewInput` seed that the future `CameraControllers` umbrella can adopt as starting state without contract change.
- No GPU-typed ECS state is attached to the reference entity, so the residency-bridge invariant from [ADR-0013](0013-ecs-renderable-residency-bridge.md) holds in the simplest possible scene.
- Strict imports keep the reference-scene module from accidentally pulling in `Renderer` / `GpuWorld` / RHI, which would invert the runtime → graphics direction.

Trade-offs and risks:

- The reference scene flips `Render.EnablePromotedVulkanDevice = true` in the sandbox config helper. On hosts without Vulkan, this is harmless (the [ADR-0005](0005-vulkan-operational-readiness-gate.md) truth table falls back to Null with the `VulkanRequestedButNotOperational` breadcrumb), but it is loud — a developer reading the sandbox config might think the sandbox *requires* Vulkan.
- Determinism is at the content layer (`MetaData::EntityName`), not the `entt::entity` ID layer. Tests that compare entity IDs across runs will be flaky; tests must compare names or the provider-returned entity span.
- The single-entity / no-light shape is intentional but constrained. A provider that wants to author lighting, multiple entities, or asset-backed geometry must wait for `GRAPHICS-029-Impl-C` (gated on `GRAPHICS-030` / `GRAPHICS-034`).
- The double-install guard rejects re-initialization; an editor that wants to reset the reference scene mid-run must call `Engine::Shutdown()` + `Engine::Initialize()` rather than touching `m_ReferenceSceneInstalled`.
- The `CreateReferenceEngineConfig()` helper bundles three orthogonal flips (reference scene enabled, promoted Vulkan requested, possibly more in future). Sandbox / test code that wants one but not the others must build the config explicitly rather than calling the helper.

Follow-up tasks required: none from this ADR. `GRAPHICS-029-Impl-C` (additional providers) lands when `GRAPHICS-030` / `GRAPHICS-034` are ready.

## Alternatives Considered

- **App-owned reference scene flip.** Historically rejected per §1.
  Superseded by ADR-0027/`RUNTIME-180`: the config value remains in the shared
  boot schema, but the app that chooses reference content owns its bootstrap
  and original-world teardown.
- **Asset-backed reference geometry.** Rejected per §2: would couple the reference scene to `GpuAssetCache` lifetime and asset-event plumbing, neither of which is needed to produce a visible triangle. Procedural geometry through [ADR-0014](0014-procedural-source-residency-bridge.md) is simpler and exercises the same residency bridge skeleton.
- **GPU-typed components on the reference entity.** Rejected per §2: violates the [ADR-0013](0013-ecs-renderable-residency-bridge.md) `ecs → core` invariant; residency state stays in the runtime sidecar.
- **Direct camera-controller wiring at bootstrap.** Rejected per §3: the `CameraControllers` umbrella does not exist yet; the provider seeds a `CameraViewInput` value that the future umbrella can adopt without contract change.
- **`entt::entity`-based determinism.** Rejected per §3: entt does not promise allocator determinism across registries; tests must compare `MetaData::EntityName` or the provider-returned entity span.
- **Importing `Renderer` / `GpuWorld` from the reference-scene module.** Rejected per §4: inverts the runtime → graphics direction and would let the reference scene reach into renderer state.

## Validation

- [`tasks/done/GRAPHICS-029`](../../tasks/archive/GRAPHICS-029-runtime-reference-scene-bootstrap.md) records the parent planning contract captured in §§1–4.
- [`tasks/done/GRAPHICS-029A`](../../tasks/archive/GRAPHICS-029A-reference-scene-skeleton.md) records the skeleton + config plumbing.
- [`tasks/done/GRAPHICS-029B`](../../tasks/archive/GRAPHICS-029B-triangle-provider-and-camera.md) records the `TriangleProvider` + camera seed + contract test.
- [`tasks/done/GRAPHICS-080`](../../tasks/archive/GRAPHICS-080-enable-promoted-vulkan-by-default.md) records the `Render.EnablePromotedVulkanDevice = true` flip referenced by §1; the resolved backend is governed by [ADR-0005](0005-vulkan-operational-readiness-gate.md).
- The default CPU correctness gate (`ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`) exercises `TriangleProvider` and the runtime extraction observation of the reference entity without requiring a Vulkan device.
