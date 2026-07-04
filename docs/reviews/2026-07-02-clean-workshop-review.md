# Clean-workshop review — selected-frame responsiveness slices

## Change under review

- Change: implement the selected-frame responsiveness task batch covering
  [`GRAPHICS-110`](../../tasks/done/GRAPHICS-110-imgui-upload-buffer-in-flight-safety.md),
  [`GRAPHICS-113`](../../tasks/done/GRAPHICS-113-selection-outline-id-work-pruning.md),
  [`GRAPHICS-114`](../../tasks/done/GRAPHICS-114-retained-imgui-overlay-copy-upload-path.md),
  and
  [`RUNTIME-138`](../../tasks/backlog/runtime/RUNTIME-138-nonblocking-selected-entity-editor-cache-pipeline.md).
- Trigger(s): renderer pass/pipeline shape changed, renderer upload helpers
  changed, runtime/editor frame model construction changed, and
  `GRAPHICS-110` retired at `Operational`.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `python3 tools/repo/check_layering.py --root src --strict` clean. |
| 2 | CMake target links match layer policy | pass | No new CMake link edges were added. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | Graphics `.cppm` exports remain graphics/RHI data surfaces; runtime/editor types are not exposed through graphics interfaces. |
| 4 | Renderer member/subsystem growth justified by an owning seam | pass | The new selection outline pipeline descriptor is part of the existing selection pass/pipeline seam; no new renderer subsystem was added. |
| 5 | New passes use typed IDs, not string routing | pass | The outline-only selection path uses existing typed frame-pass/resource identities and renderer command routing; no string-routed pass was introduced. |
| 6 | New frame-recipe dependencies resource-driven or explicitly justified | pass | Outline-only frames declare/write only `EntityId`; `PrimitiveId` and `Picking.Readback` remain resource-driven by pending pick requests. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | `GRAPHICS-110`, `GRAPHICS-113`, and `GRAPHICS-114` retired at `Operational`; open selected-editor cache/job work remains tracked by `RUNTIME-138`. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No layering allowlist row or temporary compatibility exception was added. |

## Findings -> follow-ups

- No findings.
