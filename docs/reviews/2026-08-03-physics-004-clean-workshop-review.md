# PHYSICS-004 clean-workshop review

## Change under review

- Change: replace the test-only public runtime physics bridge with an optional
  app-composed `PhysicsModule`, add its generic simulation hook and validated
  config lane, and compose it in Sandbox.
- Trigger: runtime wiring and composition order changed.
- Reviewer: Codex self-review; provisional until the task's independent
  high-risk review is accepted against the final revision.

## Scorecard

| # | Check | Outcome | Notes |
| --- | --- | --- | --- |
| 1 | Promoted layer imports match `/AGENTS.md` §2 | pass | `tools/ci/run_clean_workshop_review.sh . --strict` scanned 7,120 import/include references with no violation. Physics remains `physics -> core, geometry`; ECS/physics composition stays in runtime. |
| 2 | CMake target links match layer policy | pass | No new target-link edge was added; the existing runtime target already owns its ECS/physics dependencies. |
| 3 | No new public API exposes a higher-layer type to a lower layer | pass | `Runtime.PhysicsModule` exports config/snapshot records and the existing runtime lifecycle interface only. It exposes no ECS registry, physics world, or body handle, and creates no unused service publication. |
| 4 | Renderer member/subsystem growth justified by an owning seam | n/a | No renderer subsystem or member changed. |
| 5 | New passes use typed IDs, not string routing | n/a | No render or frame-graph pass changed. |
| 6 | New frame-recipe dependencies resource-driven or justified | n/a | No frame recipe or resource edge changed. |
| 7 | Scaffold/parity tasks have a follow-up maturity gate | n/a | PHYSICS-004 targets a real CPU/Null `Engine::Run()` integration at `Operational`, not a scaffold closure. |
| 8 | Legacy/temporary exceptions have a task ID and expiry | pass | No allowlist entry, compatibility re-export, temporary shim, or unowned marker was added. |

## Findings

No clean-workshop findings. The separate workflow requirement for an accepted
independent high-risk review remains open and is not satisfied by this
self-review.
