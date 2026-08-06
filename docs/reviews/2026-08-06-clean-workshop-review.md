# Clean-workshop review — REVIEW-003 architecture readiness

## Change under review

- Change: audit the clean `main` source tree at
  `51e7faddad943ab7727e407d008e474ec076566d` after every static and previously
  discovered REVIEW-003 blocker retired.
- Trigger: whole-tree architecture-convergence and right-sizing closure,
  including runtime composition, renderer ownership, public seams, and task
  maturity.
- Reviewer: Codex, with an independent read-only right-sizing pass over every
  flagged surface.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | The strict layer check scanned 746 files, 7,163 import/include references, and 85 CMake links with zero violations and zero allowlist entries. Runtime remains the composition root. |
| 2 | CMake target links match layer policy | pass | The same CMake-aware strict check found no invalid target edge. REVIEW-003 changes no build file or dependency edge. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | The whole current exported-module inventory and all flagged public seams were inspected. Graphics/RHI/platform surfaces contain no runtime/ECS ownership; runtime consumes lower-layer ports and snapshots. No report-only change adds an API. |
| 4 | Renderer member/subsystem growth is justified by an owning seam | pass | `RenderSubsystemRegistry` owns deterministic partial initialization, rebuild, and reverse shutdown for 17 concrete stages; renderer command routes use `RenderCommandRouter`, and preparation is factored through the existing prep pipeline. The right-sizing deletion test found no new bolted-on framework or single-owner forwarding facade. |
| 5 | New passes use typed IDs, not string routing | pass | Current recipes and command dispatch use `FramePassId`; `RenderCommandRouter.DispatchUsesTypedPassIdNotDebugName` and frame-recipe contract tests pin the rule. REVIEW-003 adds no pass. |
| 6 | New frame-recipe dependencies are resource-driven or explicitly justified | pass | Current recipe compilation derives ordinary ordering from resource reads/writes. Four explicit overlay/finalization contributions are typed data, validated, and anchored deliberately; no imperative hidden pass-order branch was introduced by the audit. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | pass | Strict task policy validated 234 task files and 89 open-task maturity records with no finding. REVIEW-003 closes as a report-only commit-scoped readiness baseline, not a backend capability maturity claim. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | The layer allowlist is empty; promoted source has no unowned TODO/FIXME/HACK marker or migration shim. Technical temporary storage and the permanent `PresentPass` finalization role are not exceptions. |

## Findings → follow-ups

No findings and no follow-up tasks.

## Evidence limits

The fresh default CPU gate passed 4,102/4,102 with the expected headless GLFW
LSan skip. REVIEW-003 does not claim a fresh promoted-Vulkan, GPU, ASan, or
UBSan run; no production source changed in this report-only task.
