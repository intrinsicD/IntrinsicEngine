# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GRAPHICS-076`](GRAPHICS-076-default-recipe-debug-view-and-present-wiring.md) —
  Default-recipe `Pass.DebugView` and canonical `Pass.Present` wiring.
  Status: blocked on Vulkan-capable host (Slices A–C landed via PRs
  #921 + #922 (Slice A follow-up), #923, and #924; Slice D remains and
  is a `gpu;vulkan` smoke that cannot run without a working Vulkan
  driver). Owner: unassigned; next pick-up by any agent on a
  Vulkan-capable host. Headers last refreshed on
  `claude/intrinsicengine-agent-onboarding-vGJrv`. Promoted from
  `tasks/backlog/rendering/` on 2026-05-23 with a four-slice plan
  covering canonical present wiring (A, landed via PR #921 + Slice A
  follow-up PR #922), canonical debug-view wiring (B, landed via
  PR #923), the non-present-write `Backbuffer` negative test (C,
  landed via PR #924), and the scaffold-retirement-obligation
  `gpu;vulkan` default-recipe smoke (D, blocked on Vulkan-capable
  host) that unblocks `GRAPHICS-081`. CPU-only environments should
  pick the next earliest unblocked Theme A leaf from
  [`tasks/backlog/README.md`](../backlog/README.md) rather than
  scaffolding Slice D without the ability to verify it.

Previously-active
[`GEOM-015`](../done/GEOM-015-gjk-termination-diagnostics.md) — GJK
termination diagnostics and scale-aware tolerance policy retired to
`tasks/done/` on 2026-05-22 after all four slices landed (PRs #915,
#917, #919). The next slice was picked per the priority rules
in [`docs/agent/prompt/prompt.md`](../../docs/agent/prompt/prompt.md):
no reproducible bugs are open, so the earliest unblocked Theme A leaf
in [`tasks/backlog/README.md`](../backlog/README.md) won —
`GRAPHICS-076`, gated only by the retired `GRAPHICS-075`.
