# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`ARCH-006`](ARCH-006-sandbox-editor-content-out-of-runtime.md) — move
  Sandbox editor presentation from runtime to app through the retired UI-034
  contribution seam (`in progress`, owner: Codex, branch:
  `codex/ui-034-editor-window-contribution`).
- [`BUG-064`](BUG-064-ci-vulkan-framepacing-headless-display.md) — provision
  an isolated Xvfb + lavapipe environment for the strict hosted frame-pacing
  capture (`in progress`, owner: Codex, branch:
  `codex/bug-064-software-vulkan`).

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).
