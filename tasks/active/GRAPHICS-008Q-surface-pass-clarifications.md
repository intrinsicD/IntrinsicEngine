# GRAPHICS-008Q — Surface pass clarification follow-ups

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `GRAPHICS-007Q` retirement cleared `tasks/active/`.
- Branch: `claude/agentic-workflow-session-TaF1r`.
- Next verification step: `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` after recording decisions and mirroring consequential notes into `docs/architecture/rendering-three-pass.md`.

## Decisions
- **Alpha-mask depth/G-buffer bucket policy.** Keep the `SurfaceAlphaMask`
  cull bucket allocated and emitted by `CullingSystem` for forward
  compatibility, but do **not** introduce a dedicated
  `Pass.DepthPrepass.AlphaMask` or `Pass.Deferred.GBuffers.AlphaMask` module
  before material alpha evaluation exists. Until material alpha evaluation
  lands (under `GRAPHICS-006`/material work), `Pass.DepthPrepass`,
  `Pass.Forward.Surface`, and `Pass.Deferred.GBuffers` consume only the
  `SurfaceOpaque` bucket; the `SurfaceAlphaMask` bucket is reserved
  infrastructure consumed by no pass. When alpha-mask material evaluation
  lands, the alpha-mask depth and G-buffer paths reuse the existing pass
  command shapes with an alpha-mask-configured pipeline (alpha-test/`discard`
  shader, depth-write per the policy below) rather than introducing new pass
  modules. Runtime extraction must not flag a renderable with
  `GpuRender_AlphaMask` unless `GpuRender_Surface` is also set; contradictory
  combinations are rejected via `RenderWorld::InvalidSnapshotRecordCount`
  per the `GRAPHICS-007Q` policy.
- **Descriptor-bind seam vs `SceneTableBDA`.** Continue using
  `RHI::GpuScenePushConstants::SceneTableBDA` as the canonical scene-table
  binding seam for the depth, forward surface, and deferred G-buffer passes.
  Do **not** introduce a pass-specific `BindDescriptorSet` seam for these
  passes until either (a) `GpuScenePushConstants` exhausts the 128-byte
  Vulkan guaranteed-minimum push-constant budget (today it packs scene-table
  BDA, frame index, and bucket selector with substantial headroom; the
  budget warning documented in `rendering-three-pass.md` applies to legacy
  `MeshPushConstants`, not this contract), or (b) bindless descriptor
  traffic becomes pass-specific (for example per-pass material samplers or
  per-pass bindless-image arrays). Backends remain free to bind a descriptor
  set that exposes the scene table behind the BDA; the CPU/null command
  contract is unchanged.
- **Renderpass attachment ownership.**
  - `Pass.DepthPrepass`: owns `SceneDepth`. `LoadOp = Clear(1.0)`,
    `StoreOp = Store`, depth-write enabled, depth-test enabled with
    `CompareOp = Less`. No color attachments.
  - `Pass.Forward.Surface` without depth prepass: owns `SceneDepth` and
    `SceneColorHDR`. `SceneDepth` `LoadOp = Clear(1.0)`, `StoreOp = Store`,
    depth-write enabled, `CompareOp = Less`. `SceneColorHDR`
    `LoadOp = Clear`, `StoreOp = Store`.
  - `Pass.Forward.Surface` with depth prepass: depth was written by
    `DepthPrepass`. `SceneDepth` `LoadOp = Load`, `StoreOp = Store`,
    depth-write **disabled**, `CompareOp = Equal` (zero-overdraw early-Z).
    `SceneColorHDR` `LoadOp = Clear`, `StoreOp = Store`.
  - `Pass.Deferred.GBuffers`: owns the MRT set. `SceneNormal`, `Albedo`, and
    `Material0` each `LoadOp = Clear`, `StoreOp = Store`. `SceneDepth`
    follows the same depth-prepass policy as forward: `LoadOp = Load`,
    depth-write disabled, `CompareOp = Equal` when prepass is active;
    otherwise `LoadOp = Clear(1.0)`, depth-write enabled, `CompareOp = Less`.
    The G-buffer MRT set is owned by `Pass.Deferred.GBuffers` and read as
    inputs by `CompositionPass`.
  - Backend implementations may map these to native renderpass or
    dynamic-rendering load/store ops; the CPU/null contract verifies command
    sequencing and resource declarations only and does not encode native
    renderpass enums.
- **Empty/invalid bucket diagnostics.** Empty or invalid culling-bucket
  inputs (`Capacity == 0u`, invalid `IndexedArgsBuffer`/`CountBuffer`
  handles, or `!Indexed` for the indexed depth/surface/G-buffer paths)
  remain deterministic silent no-ops in `Pass.DepthPrepass`,
  `Pass.Forward.Surface`, and `Pass.Deferred.GBuffers`. CPU-side
  `CullingDiagnostics` plus `GpuWorld::Diagnostics` (per `GRAPHICS-007Q`)
  own bucket-population diagnostics; these passes do **not** emit duplicate
  per-pass counters by default. If a class of bucket-validity failure
  becomes visible only at pass dispatch (for example late CPU/GPU mutation
  between cull dispatch and surface dispatch), an opt-in diagnostics
  counter behind a gated flag may be added through a separate follow-up
  implementation task — mirroring the GPU cull-shader diagnostics policy
  from `GRAPHICS-007Q`.

## Goal
- Resolve remaining policy questions discovered while completing the CPU/null depth, forward surface, and G-buffer pass contracts.

## Non-goals
- No implementation changes.
- No Vulkan-only work.

## Context
- Owner: `src/graphics/renderer/Passes`, `src/graphics/renderer/Graphics.FrameRecipe`, and rendering architecture docs.
- Created during `GRAPHICS-008` completion as the backlog location for questions that should not block the CPU-testable pass command contracts.

## Required changes
- Decide whether alpha-mask surfaces need a dedicated depth prepass/G-buffer pass bucket before material alpha evaluation is implemented.
- Decide when to introduce an explicit descriptor-bind command seam versus continuing to use `GpuScenePushConstants::SceneTableBDA` for scene-table access.
- Define renderpass attachment ownership details for backend implementations: depth load/store state when depth prepass is enabled, G-buffer MRT clear/load policy, and forward path depth-write state.
- Decide whether invalid or empty culling buckets should emit per-pass diagnostics or remain silent no-op command skips.

## Tests
- Add/update CPU contract tests only when a policy decision becomes implementation work.

## Docs
- Update `docs/architecture/rendering-three-pass.md` if pass resource ownership or binding policy changes.

## Acceptance criteria
- Open questions above have explicit decisions or child implementation tasks.
- Downstream lighting/shadow work can reference the chosen surface/depth/G-buffer policy without reopening `GRAPHICS-008`.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- No code changes in this clarification task.
- No legacy pass dependency expansion.

