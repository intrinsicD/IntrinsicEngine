# Clean-workshop review — RUNTIME-134 Slice A

## Change under review

- Change: Add the RUNTIME-134 CPU reference progressive-Poisson point-cloud
  playground command and Sandbox processing UI, with a private runtime link to
  the METHOD-012 reference target.
- Trigger(s): new CMake link edge into runtime composition and runtime/UI command
  wiring.
- Reviewer: Codex.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` ran `check_layering.py --root src --strict`; no violations. |
| 2 | CMake target links match layer policy | pass | Runtime links `IntrinsicProgressivePoissonReference` privately; the exported runtime module surface does not expose method types. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | `Runtime.SandboxEditorUi.cppm` exports runtime DTOs only; method headers stay in the implementation unit. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | n/a | No renderer member, subsystem, or pass added. |
| 5 | New passes use typed IDs, not string routing | n/a | No frame-graph pass added. |
| 6 | New frame-recipe dependencies are resource-driven or explicitly justified | n/a | No frame-recipe dependency added. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | `RUNTIME-134` stays active; Slice A records remaining config-facade, debounced rerun, mesh preprocessing, and final interactive work. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No allowlist row or temporary exception added. |

## Findings -> follow-ups

- None.
