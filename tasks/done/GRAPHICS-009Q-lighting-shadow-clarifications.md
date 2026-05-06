# GRAPHICS-009Q — Lighting and shadow clarification follow-ups

## Status
- State: done.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-008Q` retirement cleared `tasks/active/`.
- Completed: 2026-05-06.
- Branch: `claude/agentic-workflow-session-ft1YJ`.
- Implementation commit: pending local agent workflow handoff.
- Task-state commit: pending local agent workflow handoff.
- Resolution: decisions recorded below and consequential notes synced into the lighting/shadow/deferred composition contract section of `docs/architecture/rendering-three-pass.md`. Verification: `python3 tools/agents/check_task_policy.py --root . --strict` (75 task files validated, 0 findings) and `python3 tools/docs/check_doc_links.py --root .` (185 relative links, no broken links).

## Decisions
- **Shadow atlas sizing in the frame recipe.** Keep the current
  `BuildDefaultRenderGraph` allocation viewport-sized (`width × height`,
  `D32_FLOAT`) as the CPU/null-testable default until backend integration
  lands; do **not** widen `FrameRecipeFeatures` with shadow-sizing extents.
  When backend wiring needs the typed atlas extent, plumb it through a new
  `FrameRecipeShadowSizing { CascadeCount, AtlasResolution, AtlasWidth,
  AtlasHeight }` value object derived from `ShadowSystem::BuildAtlasDesc()`
  and supplied alongside `FrameRecipeSizing`/`FrameRecipeFeatures`/
  `FrameRecipeImports`. The typed sizing must remain optional on
  `BuildDefaultFrameRecipe` so the existing CPU/null harnesses that pass only
  features/sizing/imports continue to compile. The sizing transition is
  tracked as an implementation slice for the Vulkan shadow pass under the
  existing `GRAPHICS-018` Vulkan integration backlog scope and must not be
  smuggled into this clarification task.
- **`ShadowAtlas` sampler binding seam.** The promoted graphics layer
  declares the shadow atlas as a frame-graph depth resource named
  `"ShadowAtlas"` (`D32_SFLOAT`); concrete backends bind it through the
  global descriptor set already used by `CameraUBO` (`set 0`) at the global
  shadow sampler binding (`binding 1`) as `sampler2DShadow` with
  `VK_COMPARE_OP_LESS_OR_EQUAL` for hardware-accelerated PCF. Both the
  forward `Pass.Forward.Surface` and the deferred `Pass.Deferred.Lighting`
  command contracts read the atlas through this seam; neither pass receives
  a per-pass sampler descriptor or comparison-mode override. The CPU/null
  contract continues to validate `CameraUBO` cascade/atlas packing without
  binding the texture. Moving the binding to a different global slot or
  introducing per-pass shadow sampler state is out of scope for backend
  integration unless a future task explicitly redesigns the global
  descriptor layout.
- **Cascade view-projection ownership and missing-caster diagnostics.**
  Texel-snapped cascade view-projection matrices and split distances are
  computed by the **runtime/shadow extraction** seam (the runtime layer
  reads camera params + light dir + scene bounds from ECS, fits tight
  cascades, applies texel snapping, and submits the result via
  `ShadowSystem::SetCascadeData(ShadowCascadeData{...})`). The graphics
  `ShadowSystem` accepts the immutable cascade payload, clamps cascade count
  to `RHI::kMaxShadowCascades`, packs it into `CameraUBO`
  (`ShadowCascadeMatrices`, `ShadowCascadeSplitsAndCount`,
  `ShadowBiasAndFilter`, `ShadowAtlasSizeAndFlags`), and reports
  unsupported/disabled diagnostics. Graphics never reads live ECS shadow
  state. Missing or empty shadow-caster sets are **not** a graphics-layer
  diagnostic: when no shadow caster bucket is produced, `ShadowPass`
  records its silent no-op via the existing
  `CullingDiagnostics`/`GpuWorld::Diagnostics` surfaces (per
  `GRAPHICS-007Q`/`GRAPHICS-008Q`). Caster-set authoring failures
  (missing-light, unconfigured directional, broken dirty-flag flow) belong
  to runtime extraction's diagnostics surface and are not duplicated
  inside graphics. Reimplementing the legacy
  `Graphics::ComputeCascade*` helpers in the runtime extraction layer is a
  follow-up implementation slice owned by `GRAPHICS-017` (camera/view/
  shadow extraction); the legacy helpers under `src/legacy/Graphics/`
  remain behavioral reference only.
- **Deferred lighting push constants.** Keep
  `DeferredLightingPushConstants` scene-table-only
  (`SceneTableBDA` + alignment padding, total ≤ 128 bytes) for the current
  CPU/null command contract. Do **not** add a typed debug/lighting-mode
  field to the deferred lighting push constant in this task. Shader-side
  debug/lighting visualization modes are owned by `GRAPHICS-013B`'s
  debug-view backend clarifications (`GRAPHICS-013BQ`) and consume
  `DebugViewPushConstants` rather than mutating the deferred-lighting
  packet. If a future lighting-mode toggle (for example a deferred PBR vs.
  flat-debug split) is required, it lands either as a typed field on
  `LightEnvironmentPacket`/`CameraUBO` (so forward and deferred share the
  state) or as an explicit follow-up task that justifies the budget cost on
  the deferred lighting push constant; no preemptive field is reserved.

## Resolution
- Decisions recorded above and consequential notes synced into the
  "Lighting, shadow, and deferred composition contract" section of
  `docs/architecture/rendering-three-pass.md`.
- Verification: `python3 tools/agents/check_task_policy.py --root . --strict`
  and `python3 tools/docs/check_doc_links.py --root .`.

## Goal
- Clarify remaining non-code decisions from `GRAPHICS-009` before Vulkan/backend-specific lighting and shadow work depends on them.

## Non-goals
- No C++ behavior changes.
- No Vulkan-only implementation.
- No new light types, clustered lighting, IBL, or area lights.

## Context
- Owner: graphics renderer architecture docs and future backend integration notes.
- `GRAPHICS-009` established CPU/null-testable contracts for light packet diagnostics, shadow params/cascade metadata, shadow-pass command gating, and fullscreen deferred lighting command recording.
- The remaining questions are documentation/decision records so later backend work can wire resources without changing ownership boundaries.

## Required changes
- Decide whether frame-recipe shadow atlas sizing remains viewport-sized until backend integration or receives a typed shadow-sizing input separate from `FrameRecipeFeatures`.
- Clarify the exact backend binding seam for `ShadowAtlas` sampler state and comparison mode.
- Clarify runtime/shadow extraction ownership for texel-snapped cascade view-projection calculation and missing shadow-caster diagnostics.
- Clarify whether deferred lighting push constants remain scene-table-only or gain a typed debug/lighting-mode field in a later task.

## Tests
- Documentation/checker only; no C++ tests required unless docs tooling changes.

## Docs
- Update `docs/architecture/rendering-three-pass.md` and any backend integration doc that references shadow atlas sizing or binding.

## Acceptance criteria
- Follow-up decisions are captured without changing implementation behavior.
- Later Vulkan/backend tasks can consume the documented shadow/lighting seams without live ECS access or graphics-layer ownership violations.

## Verification
```bash
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Mixing docs-only clarification with semantic C++ behavior edits.
- Reintroducing live ECS light or shadow-caster access into graphics.
- Making GPU/Vulkan tests part of the default CPU-supported gate.

