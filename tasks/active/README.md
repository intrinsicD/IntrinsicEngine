# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`GEOM-064 — Parameterization optimization kernels seam`](GEOM-064-parameterization-optimization-kernels.md)
  is active on `feature/lop-consolidation-e2e` under `Codex-LocalMerge`. Its
  draft module remains unregistered and untested; the next gate is CMake
  registration plus the focused `ParameterizationOptimize` unit contract.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).
