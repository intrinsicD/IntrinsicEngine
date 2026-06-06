# Clean-workshop review — worked example (template)

This is an **illustrative example** of a clean-workshop review record, written
alongside [`docs/agent/clean-workshop-review.md`](../agent/clean-workshop-review.md)
(installed by `WORKSHOP-009`). It is **not** a review of a real landed change —
it shows the expected shape: a scorecard with `pass | finding | n/a` per row and
a follow-up task ID for every `finding`. Copy this shape into
`docs/reviews/<YYYY-MM-DD>-clean-workshop-review.md` when a change triggers the
gate (see "When this review is required" in the scorecard doc).

## Change under review (example)

- Change: *(example)* "Add a `Pass.SSR` screen-space-reflection pass to the
  default frame recipe and a `ScreenSpaceReflectionSystem` to the renderer."
- Trigger(s): adds a renderer subsystem **and** a frame-graph pass **and** a new
  recipe dependency → gate required.
- Reviewer: *(rotating reviewer)*.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `check_layering.py --root src --strict` clean; the new pass imports only `graphics/rhi` + `core`. |
| 2 | CMake target links match layer policy | pass | No new cross-layer `target_link_libraries` edge. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The pass's `.cppm` exports only graphics/RHI types. |
| 4 | Renderer member/subsystem growth justified by an owning seam | **finding** | `ScreenSpaceReflectionSystem` was added as a bare `m_SsrSystem` field on the renderer rather than registered through a subsystem registry. Follow-up: route it through the registry seam owned by [`WORKSHOP-005`](../../tasks/done/WORKSHOP-005-renderer-subsystem-registry.md). |
| 5 | New passes use typed IDs, not string routing | **finding** | The pass is routed by the string `"Pass.SSR"`; typed pass identity landed in [`WORKSHOP-003`](../../tasks/done/WORKSHOP-003-typed-frame-pass-and-resource-identity.md), and renderer command routing is typed via [`WORKSHOP-004`](../../tasks/done/WORKSHOP-004-typed-command-router.md). Flag this as a regression; do not add more string routes. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | pass | The SSR→composition edge is derived from the `SceneColor`/`SceneDepth` resource reads, not a hand-ordered edge. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | n/a | No task closed at `Scaffolded` by this change. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No allowlist row or temporary marker added. |

## Findings → follow-ups

- Row 4 → `WORKSHOP-005` (renderer subsystem registry): register
  `ScreenSpaceReflectionSystem` through the registry instead of a renderer
  field.
- Row 5 → `WORKSHOP-004` (typed command router): replace the `"Pass.SSR"`
  string route with the typed pass identity introduced by `WORKSHOP-003`.

Both findings are recorded as follow-up task references, not bare TODOs, per the
gate's "Recording findings" rule. Neither blocks the example change by itself;
they keep the decomposition/typing debt visible and owned.
