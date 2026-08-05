# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-130 — Rejected experiment runs require their historical task seal](BUG-130-rejected-run-historical-task-seal.md)
  (`in-progress`, owner `Codex-RejectedTaskSeal`, branch
  `bug/BUG-130-rejected-run-task-seal`): preserve independently rejected
  clean-source runs without weakening current-task binding for accepted evidence.
- [METHOD-020 — LOP-family GPU (Vulkan compute) backend and parity](METHOD-020-lop-family-gpu-vulkan-compute-backend.md)
  (`in-progress`, owner `Codex-LOPVulkan`, branch `main`): freeze the claim-grade
  protocol and benchmark intent before implementing the private Vulkan path.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).
