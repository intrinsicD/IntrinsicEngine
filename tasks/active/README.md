# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GEOM-015`](GEOM-015-gjk-termination-diagnostics.md) — GJK termination
  diagnostics and scale-aware tolerance policy. Promoted from
  `tasks/backlog/geometry/` on 2026-05-22 as the deferred successor to
  GEOM-007 Slice 3.3.d (GEOM-007 retired the same day). Status: in-progress
  (Slice 1). Slice 1 adds `RobustPredicates::ApproxZeroSq` / `ApproxZeroLen`
  helpers with `SignedResult`-shaped diagnostics plus unit tests, no
  callsite migration. Slices 2 (Support / SDFContact magnitude-guard
  migration), 3 (GJK normalized-workspace policy decision + architecture
  doc), and 4 (GJK termination-reason diagnostics surface) are queued. Next
  verification step:
  `ctest --test-dir build/ci --output-on-failure -R 'RobustPredicates' --timeout 60`
  plus `python3 tools/repo/check_layering.py --root src --strict` and
  `python3 tools/agents/check_task_policy.py --root . --strict` after Slice 1
  lands.
