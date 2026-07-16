# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-102 — Object-space bake layering test asserts pre-ratchet import placement](BUG-102-object-space-normal-bake-layering-test-import-placement.md)
  (`in-progress`; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`). The next gate is the repaired
  full Vulkan/GPU label selection and retirement synchronization.
- [BUG-103 — Render-graph lifetime test culls its history chain](BUG-103-rendergraph-lifetime-test-culls-history-chain.md)
  (`in-progress`; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`). The next gate is the repaired
  full Vulkan/GPU label selection and retirement synchronization.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).
