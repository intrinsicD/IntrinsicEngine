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
  Status: in-progress (Slice A landing). Owner: Claude on
  `claude/intrinsicengine-agent-onboarding-GdEzP`. Promoted from
  `tasks/backlog/rendering/` on 2026-05-23 with a four-slice plan covering
  canonical present wiring (A), canonical debug-view wiring (B), the
  non-present-write `Backbuffer` negative test (C), and the
  scaffold-retirement-obligation `gpu;vulkan` default-recipe smoke (D)
  that unblocks `GRAPHICS-081`.

Previously-active
[`GEOM-015`](../done/GEOM-015-gjk-termination-diagnostics.md) — GJK
termination diagnostics and scale-aware tolerance policy retired to
`tasks/done/` on 2026-05-22 after all four slices landed (PRs #915,
#917, #919). The next slice was picked per the priority rules
in [`docs/agent/prompt/prompt.md`](../../docs/agent/prompt/prompt.md):
no reproducible bugs are open, so the earliest unblocked Theme A leaf
in [`tasks/backlog/README.md`](../backlog/README.md) won —
`GRAPHICS-076`, gated only by the retired `GRAPHICS-075`.
