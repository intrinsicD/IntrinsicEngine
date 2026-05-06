# GRAPHICS-007Q — Culling bucket clarification follow-ups

## Status
- State: in-progress.
- Owner/agent: local agent workflow.
- Activated: 2026-05-06 after `tasks/active/` cleared following `CI-001` and `GRAPHICS-024` retirement.
- Branch: `claude/agentic-workflow-session-7ytd0`.
- Next verification step: run `python3 tools/agents/check_task_policy.py --root . --strict` and `python3 tools/docs/check_doc_links.py --root .` after the consequential `docs/architecture/rendering-three-pass.md` notes are merged, then retire to `tasks/done/`.

## Decisions
- **Selection bucket primitive-domain split.** Keep the existing three selection
  buckets (`SelectionSurface`, `SelectionLines`, `SelectionPoints`) split by
  primitive domain only. Do **not** introduce alpha-mask- or depth-only-specific
  selection buckets. Material-driven alpha-mask discard and depth-only behavior
  during selection ID writes belong to the bound pipeline/shader at each
  selection ID pass, not to a separate bucket. Bucket cardinality and
  `RHI::GpuCullBucketTable` remain at the eight outputs documented in
  `GRAPHICS-007`. Transparent or special-material selection paths are deferred
  to `GRAPHICS-025`.
- **`GpuInstanceStatic::VisibilityMask` and `Layer` policy.**
  - `VisibilityMask` is the per-view inclusion bitmask: a renderable
    participates in a view/pass when
    `(instance.VisibilityMask & view.VisibilityMask) != 0`. The default
    instance value `0xFFFF'FFFFu` participates in every view. Runtime camera/
    view extraction (`GRAPHICS-017`) is responsible for translating ECS
    view-layer policy into the per-view mask supplied to culling and pass
    consumers; graphics never reads ECS layer state directly.
  - `Layer` is an opaque 32-bit grouping tag for higher-level runtime/editor
    use (for example world vs UI vs gizmo lanes). It is **not** a culling
    visibility filter. The cull shader does not branch on `Layer`; passes or
    recipes that need to act on it (overlay scheduling, editor toggles)
    consume it explicitly. The default value `0` means "world / unspecified".
  - Implementing the per-view `VisibilityMask` AND inside `instance_cull.comp`
    is a follow-up implementation slice tracked under `GRAPHICS-017` (camera/
    view extraction); current shader behavior is preserved by the default
    `0xFFFF'FFFFu` mask.
- **Culling diagnostics ownership.** Keep CPU-side `GpuWorld::Diagnostics` as
  the single authoritative diagnostic surface for invalid geometry slots,
  out-of-range slots, zero-sized geometry ranges, non-positive world-sphere
  radii, and stale/freed handles. CPU generation checks already see all of
  these before dispatch. Do **not** mirror these counters through a GPU
  diagnostics counter buffer in `instance_cull.comp` by default; the cull
  shader continues to silently skip the documented invalid-record cases. If
  a future class of invalid records becomes visible only at GPU dispatch
  (for example late SSBO mutation between CPU sync and dispatch), an opt-in
  GPU counter can be introduced behind a diagnostics gate via a separate
  follow-up implementation task.
- **Unsupported bucket combinations.** Treat contradictory render-flag
  combinations (for example `Transparent | Selectable` before a transparent
  selection lane exists, or `Surface | Line | Point` on the same instance) as
  CPU-side configuration errors at the runtime extraction seam. Runtime
  extraction must reject and count these through `InvalidSnapshotRecordCount`
  on `RenderWorld`. The cull shader continues to follow the documented
  bucket-emission rules without ad-hoc bucket merging or splitting. Future
  transparent/special-material selection lanes are owned by `GRAPHICS-025`,
  which decides whether to add new selection buckets or reuse existing ones
  with material-driven shading.

## Goal
- Resolve remaining product/architecture questions for culling bucket policy before downstream selection, diagnostics, and layer-mask consumers depend on the current contracts.

## Non-goals
- No implementation changes.
- No changes to the `GRAPHICS-007` bucket schema unless a follow-up task explicitly requires them.

## Context
- Owner: `src/graphics/renderer`, `src/graphics/rhi`, and rendering architecture docs.
- Created during `GRAPHICS-007` completion as the backlog location for upcoming clarification questions.

## Required changes
- Decide whether selection buckets should remain split by primitive domain (`SelectionSurface`, `SelectionLines`, `SelectionPoints`) or be further specialized for alpha-mask/depth-only picking behavior.
- Define the authoritative layer-mask policy for `GpuInstanceStatic::VisibilityMask` and `Layer` once camera/view-layer extraction lands.
- Decide whether culling diagnostics for invalid geometry slots/ranges should stay in CPU `GpuWorld` diagnostics, be mirrored through a GPU diagnostics counter buffer, or both.
- Define escalation behavior for unsupported bucket combinations, such as a future transparent selectable surface that needs both alpha and selection-specific material evaluation.

## Tests
- Add/update contract tests only after policy decisions become implementation work.

## Docs
- Update `docs/architecture/rendering-three-pass.md` if any bucket or diagnostics policy changes.

## Acceptance criteria
- Open questions above have explicit decisions or child implementation tasks.
- Downstream tasks (`GRAPHICS-008`, `GRAPHICS-012`) can reference the chosen policy without re-opening culling ownership.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- No code changes in this clarification task.
- No legacy graphics dependency expansion.

