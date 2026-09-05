# Active Tasks

Place only currently in-progress or blocked tasks in this directory.

Each active task should include:

- Current status (`in-progress` or `blocked`).
- Owner/agent (if known).
- Branch and PR reference (if known).
- Explicit next verification step.

## Currently active

- [`BUG-169` — ASan geometry group exceeds its fixed process
  timeout](BUG-169-asan-geometry-group-timeout.md) — locally complete; the two
  ASan wrappers pass in 55.94 and 27.59 seconds and exact same-binary parity
  covers all 4,262 GoogleTest cases, with hosted ASan confirmation pending.
- [`BUG-168` — runtime source move left compile-hotspot physical identities
  stale](BUG-168-runtime-compile-hotspot-baseline-paths.md) — diagnosed; two
  moved module interfaces retain their pre-move object paths and edge IDs in
  the hosted full-CPU baseline, while all compilation and tests pass.
- [`BUG-165` — dropped-file queue tests race worker
  completion](BUG-165-dropped-geometry-cancellation-test-race.md) — locally
  complete; deterministic pre-decode barriers pass the two-test 200-repeat
  gate and the full CPU-supported suite, with hosted `pr-fast` pending.
- [`BUG-164` — ccache serves stale objects when a macro changes only imported
  module BMIs](BUG-164-ccache-module-bmi-macro-staleness.md) — in progress;
  dependency-local semantic sidecars pass local fixture, core, full-graph, and
  exact graphics evidence; hosted warm-budget evidence remains open.
- [`BUG-167` — LOP benchmark positive runs outside hosted Vulkan
  display](BUG-167-lop-benchmark-hosted-vulkan-display-routing.md) — in
  progress; the five-case operational route is pinned locally and hosted
  `ci-vulkan` execution remains open.

## Records

Retirement records live in the append-only
[`tasks/done/RETIREMENT-LOG.md`](../done/RETIREMENT-LOG.md); this file
describes current state only. The retirement procedure is documented in
[`docs/agent/task-format.md`](../../docs/agent/task-format.md).
