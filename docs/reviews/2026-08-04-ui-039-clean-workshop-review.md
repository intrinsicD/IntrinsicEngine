# UI-039 clean-workshop review

## Change under review

- Change: bind the existing LOP-family Sandbox panel to canonical typed
  property references across mesh, graph, and point-cloud element domains,
  and publish canonical mesh vector quantities as float properties.
- Trigger: public runtime/app module surfaces changed. No dependency boundary,
  CMake edge, renderer subsystem, frame-graph pass, recipe edge, or layering
  exception changed; the scorecard was run conservatively for the high-risk
  retirement review.
- Reviewer: `codex-root` self-review; independent fixed-surface acceptance is
  recorded separately under `tasks/evidence/UI-039/`.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | The strict scan covered 749 files, 7,124 import/include references, and 85 CMake links with zero violations or allowlist entries. App continues to import runtime only; runtime and geometry retain their existing ownership. |
| 2 | CMake target links match layer policy | pass | UI-039 adds no CMake link or target edge. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | Runtime exports copied inspector/readiness models and runtime-owned typed property records; geometry exports only geometry-owned float property handles and double-precision calculations. No lower-layer interface names app, ECS ownership, graphics, or live service state. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member or subsystem changed. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass or renderer route changed. |
| 6 | New frame-recipe dependencies are resource-driven or explicitly justified | n/a | No frame recipe or resource dependency changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | n/a | UI-039 retires at `Operational`, not `Scaffolded` or parity maturity. GPU/Vulkan LOP execution remains explicitly owned by `METHOD-020`. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No compatibility shim, temporary marker, or layering allowlist entry was added. |

## Findings and right-sizing

No finding requires a follow-up task. The change reuses the existing
`GeometryPropertyRef`, runtime availability/config/submit path, inspector
property catalog, and one app-owned panel state. The three small public
functions have present production callers at the runtime-to-app boundary; no
converter, universal schema, duplicated per-domain panel, factory, or service
was introduced.
