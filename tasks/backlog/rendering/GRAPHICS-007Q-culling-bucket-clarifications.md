# GRAPHICS-007Q — Culling bucket clarification follow-ups

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

