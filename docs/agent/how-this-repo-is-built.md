# How IntrinsicEngine Is Built

IntrinsicEngine is developed as a sequence of evidence-bearing tasks rather
than from an unstructured issue list. A task states the intended outcome,
dependencies, forbidden scope, tests, documentation obligations, and exact
verification commands before implementation begins. The repository then keeps
the task, the implementing commits, generated state, CI evidence, and a compact
retirement narrative as one auditable chain.

This page is a guided tour for readers outside the project. It describes the
system; it does not add rules. The authoritative operating contract remains
[`AGENTS.md`](../../AGENTS.md), with the detailed task and review procedures in
the [task format](task-format.md) and [review checklist](review-checklist.md).

## The Development Loop

```text
research, defect, or review finding
                |
                v
          backlog task + dependencies
                |
                v
       generated unblocked task view
                |
                v
      active, reviewable implementation
                |
                v
       focused tests + structural gates
                |
                v
       completed task + retirement log
                |
                v
        frozen archive + Git history
```

The loop has four durable states:

| State | Purpose | Evidence to inspect |
| --- | --- | --- |
| [`tasks/backlog/`](../../tasks/backlog/README.md) | Approved or proposed work, grouped by subsystem and convergence theme. Front-matter records stable IDs and dependency edges. | The task's unchecked requirements, tests, acceptance criteria, and `depends_on` list. |
| [`tasks/active/`](../../tasks/active/README.md) | Work spanning multiple slices or sessions, with an explicit owner and next verification step. A single-slice task may go directly from backlog to retirement. | The current task status, slice plan, incremental evidence, branch, and owner. |
| [`tasks/done/`](../../tasks/done/README.md) | Recent completed work with all actionable checkboxes closed, completion metadata, and a retirement narrative. | The completed task plus the append-only [`RETIREMENT-LOG.md`](../../tasks/done/RETIREMENT-LOG.md). |
| [`tasks/archive/`](../../tasks/archive/README.md) | Frozen retired-task history swept out of the small working set. IDs remain authoritative and continue to satisfy dependency edges. | The read-only task record, retirement log, and `git log --follow -- <task>`. |

[`tasks/SESSION-BRIEF.md`](../../tasks/SESSION-BRIEF.md) is generated from task
front-matter. It shows current active work and, for every backlog theme, either
that a task is unblocked or the first unmet task ID. Agents regenerate it when
task state or dependencies change; CI rejects a stale copy. That makes the
picker view a reproducible projection of the task graph, not a hand-maintained
status report.

## Convergence Instead of a Flat Queue

The [backlog convergence map](../../tasks/backlog/README.md) groups tasks by
engine outcome across subsystem directories. A rendering outcome can depend on
runtime wiring, asset ingest, a method contract, and CI evidence without losing
the owning layer of any change. The `theme` and `depends_on` front-matter keep
that cross-domain plan machine-readable, while each task remains a reviewable
unit with one owner.

This separation matters:

- Themes answer "which engine outcome does this advance?"
- Task prefixes and directories answer "which subsystem owns the change?"
- Dependency IDs answer "what evidence must exist first?"
- Acceptance criteria answer "what would prove this slice is finished?"

The generated brief combines those facts so an agent can select a useful,
dependency-ready task without inventing a new roadmap.

## Skills: Progressive Procedure Loading

The repository packages its operating knowledge as twenty-one
[Agent Skills](../../tools/agents/skills/README.md). Only skill metadata is
normally visible to an agent; the full procedure loads when its trigger matches
the work. This preserves a single authoritative contract while avoiding the
cost and conflict of loading every specialist checklist into every session.

The skill inventory has three tiers:

| Tier | Role | Examples |
| --- | --- | --- |
| Source-procedure mirrors | Route agents to canonical repository procedures. Generated `references/` files are synchronized from `docs/agent/`. | Core contract, task workflow, review, methods, benchmarks, documentation sync. |
| Cross-cutting disciplines | Capture recurring repository-specific practices that span several source documents or were learned from repeated defects. | Diagnosis, Vulkan frame triage, GPU smoke authoring, stale-build triage, right-sizing, research ideation. |
| Imported productivity skills | Supply bounded general-purpose interaction patterns while remaining subordinate to the repository contract. | Teaching and design grilling. |

The authority model depends on the tier. `AGENTS.md` always wins on repository
policy. Mirrored skill content defers to its canonical procedure source;
cross-cutting discipline bodies and explicitly marked skill-canonical sections
are authoritative in place. The [skills index](../../tools/agents/skills/README.md)
records those exceptions in detail. The curated page you are reading is
orientation material, not another procedure mirror.

## Validators: Executable Repository Memory

Text explains intent; validators make selected invariants executable. The
Python and shell tools are divided by ownership rather than hidden behind one
monolithic linter:

| Validator family | What it protects | Entry points |
| --- | --- | --- |
| Task and agent policy | Task shape, unique IDs, dependency resolution, lifecycle links, maturity follow-ups, generated session state, and skill mirrors. | [`tools/agents/`](../../tools/agents/README.md) |
| Documentation | Relative-link integrity and changed-file documentation synchronization. | [`tools/docs/`](../../tools/docs/README.md) |
| Repository structure | Layer imports and CMake edges, root hygiene, test layout, allowlist quality, and module inventory. | [`tools/repo/`](../../tools/repo/README.md) |
| Methods and benchmarks | Method manifests, benchmark manifests, result schemas, and comparable evidence identities. | [Method workflow](method-workflow.md) and [benchmark workflow](benchmark-workflow.md) |
| Workflow regressions | Static fixtures that pin CI routing, prerequisite, timing, cache, and aggregate behavior without waiting for a hosted run to expose drift. | [`tests/regression/tooling/`](../../tests/regression/tooling/) |

These checks do not all answer the same question. A link checker proves that a
reference resolves, not that a rendering backend ran. A CPU contract test
proves backend-neutral behavior, not Vulkan operation. A benchmark result
validator proves schema and identity, not a performance win. The task's
acceptance criteria name the required evidence types, and the review checklist
prevents a narrow check from supporting a broader claim.

## CI: Feedback and Confidence

The checked-in workflows keep fast feedback, structural policy, backend
coverage, and expensive evidence visible as distinct jobs:

| Workflow | Primary role |
| --- | --- |
| [`pr-fast.yml`](../../.github/workflows/pr-fast.yml) | Exact-diff touched-scope feedback: structural-only changes skip C++ setup; source changes use registry-validated unsanitized `ci-fast`; ambiguous changes broaden. The route artifact and C++ timing remain distinct from full confidence gates. |
| [`ci-docs.yml`](../../.github/workflows/ci-docs.yml) | Strict structural, task, documentation, manifest, skill-sync, and workflow-policy checks. |
| [`ci-linux-clang.yml`](../../.github/workflows/ci-linux-clang.yml) | Grouped full CPU correctness for ready candidates and merge groups, reusable isolated ASan/UBSan selection parity, an uncancelled unsanitized `main` push, and mutually exclusive manual evidence modes. |
| [`ci-sanitizers.yml`](../../.github/workflows/ci-sanitizers.yml) | Reusable isolated ASan and UBSan variants invoked by the Linux candidate workflow. |
| [`ci-vulkan.yml`](../../.github/workflows/ci-vulkan.yml) | Promoted Vulkan build and GPU/Vulkan-labeled operational tests for ready candidates and merge groups, with an always-reporting draft-safe result job. |
| [`ci-release.yml`](../../.github/workflows/ci-release.yml) | Path-aware optimized Release benchmark smoke and blocking architecture SLO confidence, with an always-reporting stable candidate context. |
| [`ci-source-coverage.yml`](../../.github/workflows/ci-source-coverage.yml) | Monday 03:00 UTC or manually dispatched unsanitized Clang CPU source-coverage baseline and test-refactor region/branch parity evidence. |
| [`nightly-deep.yml`](../../.github/workflows/nightly-deep.yml) | Daily 05:00 UTC deeper, slower, benchmark, and diagnostic coverage. |

The [benchmark CI policy](../benchmarking/ci-policy.md) owns benchmark-specific
selectors and measurement rules. The linked workflow YAML owns actual workflow
execution, and machine-readable artifacts are the evidence for what ran; this
table is only a navigation layer.

## Worked Example: CI-004

`CI-004`, "Build only the test executables selected by each gate", is a compact
example of the full lifecycle. Its current immutable record is
[`tasks/archive/CI-004-label-derived-test-build-aggregates.md`](../../tasks/archive/CI-004-label-derived-test-build-aggregates.md).

| Date | Lifecycle event | Durable evidence |
| --- | --- | --- |
| 2026-07-09 | Seeded in the process backlog with the measured `CI-003` baseline as a dependency. | Commit [`a0a23f3d`](https://github.com/intrinsicD/IntrinsicEngine/commit/a0a23f3d616d48f931dc3cf462c8cc1b4faf8670) created the task, updated the convergence map, and regenerated the session brief. |
| 2026-07-09 | Promoted after its dependency was ready; the task gained a three-slice plan and the active indexes changed. | Commit [`d4820936`](https://github.com/intrinsicD/IntrinsicEngine/commit/d48209368477bc66869ece228d289a6c8626c3c3) recorded promotion and the structural verification used for the state change. |
| 2026-07-10 | Slice A centralized test-target labels and generated exact aggregate targets and inventories. | Commit [`e741293d`](https://github.com/intrinsicD/IntrinsicEngine/commit/e741293dfd7a2d52ae3f93573183a7ef89891974) changed the CMake registry and added [`Test.TestBuildAggregates.py`](../../tests/regression/tooling/Test.TestBuildAggregates.py). |
| 2026-07-10 | Slice B routed PR-fast and Vulkan workflows through those aggregates and made missing selected binaries fail before CTest. | Commit [`bc5c7cea`](https://github.com/intrinsicD/IntrinsicEngine/commit/bc5c7cea0ebf66e4951537adad66ea3c7b3ae367) changed workflow, prerequisite, regression, and documentation surfaces together. |
| 2026-07-10 | Slice C recorded local selector, case-count, and deterministic build-edge evidence, while leaving hosted wall-time evidence as an explicit retirement blocker. | Commit [`99c579e0`](https://github.com/intrinsicD/IntrinsicEngine/commit/99c579e003d4c637ed015e6a86ba3a085c671c56) extended the task's evidence record without presenting local edges as hosted performance. |
| 2026-07-10 | Retired at `Operational` after hosted PR-fast and Vulkan artifacts completed the remaining acceptance evidence. | Commit [`5400fdcb`](https://github.com/intrinsicD/IntrinsicEngine/commit/5400fdcb7ac2902c77d1b466c27bb8996c31c2bf) recorded the hosted comparison, closed every actionable checkbox, moved the task to `done`, appended the [retirement narrative](../../tasks/done/RETIREMENT-LOG.md), regenerated task state, and unblocked its dependents. |
| 2026-07-14 | Swept with the retired corpus into frozen history; the task ID remained valid for dependency resolution. | Commit [`865a61bc`](https://github.com/intrinsicD/IntrinsicEngine/commit/865a61bc3ac88b648bb9bc9e54df259ffcf99204) created the archive policy and rewrote inbound links without changing the completed claim. |

The final record deliberately distinguishes structural and performance
evidence. Gate-specific targets reduced the PR-fast command closure by 10.3%,
but the available hosted PR-fast samples did not show a wall-time improvement,
so the task makes no speedup claim. The Vulkan samples showed both a 32.6%
smaller command closure and lower build times, and the main `gpu;vulkan`
selector passed. The isolated frame-pacing capture still failed and remained
owned by `BUG-064`; `CI-004` did not claim otherwise. That boundary is visible
in the archived task, the retirement log, and the cited commits.

## How to Audit Any Retired Claim

Start from the task ID, not from a summary sentence:

1. Find the task through the category index, retirement log, or
   `rg '<TASK-ID>' tasks`.
2. Read its acceptance criteria and recorded verification. Every actionable
   checkbox in a completed task must be closed.
3. Follow its implementation and evidence commit references with
   `git show <commit>`.
4. Inspect the named regression, workflow artifact, benchmark result, or
   backend-labeled run. Match the evidence scope to the claim.
5. Read the retirement narrative for the concise outcome and explicit
   deferrals. Follow-ups receive new task IDs rather than edits to archived
   history.

That chain is the portfolio artifact: not merely that code exists, but that the
repository records why it was selected, what was forbidden, how it was tested,
what the evidence supports, and what remains open.
