# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [BUG-151 — Work graph cannot advance a declared multi-slice task](BUG-151-work-graph-multi-slice-cycle.md)
  — High-risk workflow repair for one auditable next-slice cycle with fresh
  bounded attempts and an exact clean baseline; no engine or method change.
- [METHOD-038 — Feature-aligned, remeshing-stable curvature segmentation](METHOD-038-feature-aligned-remeshing-stable-curvature-segmentation.md)
  — Slice A intake and baseline profiling; the bounded 10k fixture cohort now
  covers cold/reusable curvature and one exact planar continuous-boundary
  comparison. The production selector remains unchanged while the broader
  analytic screening corpus and candidate split stay open.

## History

Retirement narratives live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).
