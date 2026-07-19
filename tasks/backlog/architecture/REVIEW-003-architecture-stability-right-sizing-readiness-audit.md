---
id: REVIEW-003
theme: F
depends_on:
  - ARCH-014
  - ARCH-015
  - BUG-087
  - BUG-089
  - CORE-006
  - CORE-009
  - GRAPHICS-127
  - HARDEN-086
  - RUNTIME-166
  - RUNTIME-167
  - RUNTIME-168
  - RUNTIME-169
  - RUNTIME-170
  - RUNTIME-171
  - RUNTIME-172
  - RUNTIME-173
  - RUNTIME-174
  - RUNTIME-177
  - RUNTIME-188
  - PROC-027
---
# REVIEW-003 — Architecture stability and right-sizing readiness audit

## Status

- 2026-07-19 dependency amendment: the scene-owner audit split
  `SceneDocumentModule` (`RUNTIME-172`) from `SceneInteractionModule`
  (`RUNTIME-188`). `RUNTIME-172` is retired; `RUNTIME-188` remains open, so
  the audit remains blocked.
- 2026-07-19 dependency update: `GRAPHICS-127` retired at `Operational` on
  native Vulkan and `CPUContracted` on Null/unsupported hosts. Its dead and
  misleading profiler-seam gate is satisfied; the task remains in
  `depends_on` as an auditable static prerequisite.

## Goal

- Establish one commit-anchored, evidence-backed readiness gate proving that
  the current architecture-convergence and right-sizing work is complete before
  deferred research and rendering ideas begin implementation.

## Non-goals

- No engine, method, renderer, build-system, or validator implementation
  changes; this task audits and records evidence only.
- No permanent claim that the architecture can never regress. The result is a
  readiness baseline for one named `main` commit.
- No new architecture framework, milestone service, release-state type, or CI
  gate.
- No weakening, skipping, warning-mode conversion, or allowlisting of an
  existing check to obtain a clean result.
- No absorption of discovered fixes into this task. Each blocking finding gets
  its own scoped task.

## Context

- Owner: architecture/review governance, Theme F.
- `ARCH-014` is the necessary kernel-convergence north star, but it does not by
  itself audit whole-tree complexity, premature abstraction, dead seams, or
  process-tool rent. The other static dependencies are the currently known
  ownership and right-sizing leaves that must retire before this audit runs.
- `RUNTIME-172` and `RUNTIME-188` are separate gates because the audited scene
  ownership split assigns document/history replacement authority and
  frame-driven interaction/readback state to different concrete modules. The
  readiness audit must verify both owners and must not accept a recombined
  `SceneEditingModule` or Engine compatibility facade.
- `BUG-087` is a gate because the documented repository-root task-validator
  invocation currently succeeds after discovering zero tasks; the readiness
  audit must use a fail-closed canonical task validator.
- `BUG-089` is a gate because strict root hygiene currently rejects the tracked
  research-artifact root and named ignored local state; readiness requires the
  policy and executable check to agree without weakening unexpected-root
  detection.
- `GRAPHICS-127` is a gate because the cross-repository audit found an exported
  `IProfiler` seam whose Vulkan implementation is never constructed or driven
  and whose Null implementation labels host-clock data as GPU timing. The
  readiness audit cannot accept that dead/misleading public seam as current
  architecture.
- `HARDEN-086` is a gate because the same audit found two divergent
  runtime-local hierarchy walks that can publish partial results on corrupt
  linked structure. The promoted ECS structure module must own one checked
  query contract before the right-sizing/ownership inventory is accepted.
- Retired `WORKSHOP-009`, `REVIEW-001`, and `REVIEW-002` provide the review
  procedures but cannot act as this future gate: dependencies on tasks in
  `tasks/done/` or `tasks/archive/` are already satisfied.
- This is a one-shot audit, not an umbrella implementation task. If the audit
  discovers a readiness blocker, open a separate task, add its ID to this
  task's `depends_on`, regenerate `tasks/SESSION-BRIEF.md`, and stop. After all
  such dependencies retire, rerun the complete audit against a fresh `main`
  commit rather than reusing earlier partial evidence.
- Remediation tasks added as dependencies must not depend on `REVIEW-003`;
  `validate_tasks.py` checks that IDs resolve but does not currently detect
  dependency cycles.
- Future idea tasks use `REVIEW-003` as their first front-matter dependency so
  the generated session brief reports the intended stabilization block rather
  than presenting those tasks as ready for selection.

## Required changes

- [ ] Confirm every static front-matter dependency is retired on `main` before
      beginning the audit.
- [ ] Record the audited `main` commit SHA, date, host/toolchain identity, and
      clean-worktree state in a dated architecture-readiness report.
- [ ] Run the full architecture-review checklist and record every row as
      `pass`, justified `not-applicable`, or `finding`.
- [ ] Run the clean-workshop scorecard, including manual rows 3–6, and record
      every row as `pass`, justified `not-applicable`, or `finding`.
- [ ] Run a fresh whole-tree drift audit and a fresh agent-output audit covering
      the complete interval since their preceding reports.
- [ ] Inventory every then-open Theme F task and every task originating from a
      right-sizing finding; classify each as readiness-blocking or nonblocking
      with a concrete rationale.
- [ ] Inventory exported `I*` interfaces and public `*Service`, `*Bridge`,
      `*Registry`, `*Queue`, `*Binding`, and `*Submission` surfaces. Apply the
      `intrinsicengine-right-sizing` keep-list to each flagged surface and
      record its implementation/consumer count, deletion-test result, and
      verdict.
- [ ] For every blocking finding, open one scoped remediation task, add its ID
      to this task's `depends_on`, regenerate `tasks/SESSION-BRIEF.md`, and stop
      this audit without recording readiness.
- [ ] After every discovered blocker retires, rerun all automated and manual
      checks on a fresh `main` commit and publish the final readiness verdict.

## Tests

- [ ] Configure the `ci` preset, build `IntrinsicTests`, and pass the complete
      default CPU-supported CTest gate on the audited commit.
- [ ] Pass the strict clean-workshop bundle, task/task-state validators,
      docs-sync checks, test-layout check, and root-hygiene check.
- [ ] Regenerate the module inventory to a temporary file and prove it matches
      the committed inventory exactly.
- [ ] Confirm the freshly written standard audit reports satisfy the local
      audit-cadence check; do not wire cadence strictness into PR CI.

## Docs

- [ ] Write `docs/reviews/<YYYY-MM-DD>-clean-workshop-review.md` with automated
      and manual scorecard outcomes and task-linked findings.
- [ ] Write fresh `docs/reports/<YYYY-MM-DD>-agent-output-audit.md` and
      `docs/reports/<YYYY-MM-DD>-drift-audit.md` reports using their canonical
      checklist formats.
- [ ] Write `docs/reports/<YYYY-MM-DD>-architecture-stability-readiness.md`
      summarizing the audited commit, dependency disposition, architecture
      review, right-sizing inventory, verification evidence, and final verdict.
- [ ] Update the architecture backlog index and regenerate
      `tasks/SESSION-BRIEF.md` whenever this task is re-gated or retired.

## Acceptance criteria

- [ ] Every static dependency and every blocker discovered by this audit is in
      `tasks/done/` or `tasks/archive/`.
- [ ] The `ARCH-014` kernel target-state scorecard is fully green on the
      audited `main` commit.
- [ ] Architecture-review, clean-workshop, drift-audit, and agent-output rows
      are all `pass` or justified `not-applicable`; no `finding` lacks a retired
      remediation task.
- [ ] Every right-sizing flag either passes a named keep-list justification or
      cites a retired remediation task; no unresolved premature abstraction,
      single-consumer framework, pure-forwarding facade, fragmented feature,
      or speculative-generalization finding remains.
- [ ] No readiness-blocking dead public seam, untracked compatibility shim,
      temporary migration exception, stale aspirational claim, or unowned
      TODO marker remains in the audited tree.
- [ ] The final readiness report identifies the exact commit and evidence and
      describes the result as a commit-scoped baseline, not a permanent
      guarantee.
- [ ] The default CPU-supported gate and every command in `## Verification`
      pass against the audited commit.
- [ ] Deferred idea tasks remain machine-blocked by `REVIEW-003` until this
      audit retires with the clean verdict above.

## Verification

```bash
git status --short --branch

cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60

tools/ci/run_clean_workshop_review.sh . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root . --strict

python3 tools/repo/generate_module_inventory.py --root src --out /tmp/intrinsic-module-inventory.md
diff -u docs/api/generated/module_inventory.md /tmp/intrinsic-module-inventory.md

# Local readiness evidence only; never promote cadence strictness to a PR gate.
python3 tools/agents/check_audit_cadence.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
```

Manual procedures whose judgments must be recorded in the reports above:

- `docs/agent/architecture-review-checklist.md`
- `docs/agent/clean-workshop-review.md`
- `docs/agent/agent-output-review-checklist.md`
- `docs/agent/drift-audit-checklist.md`
- `tools/agents/skills/intrinsicengine-right-sizing/SKILL.md`

## Forbidden changes

- Implementing or opportunistically fixing any audit finding in this task.
- Closing the audit while a finding, remediation task, or named dependency is
  still open.
- Making a remediation task depend on `REVIEW-003` and thereby creating a
  dependency cycle.
- Replacing manual architecture/right-sizing judgments with validator success
  alone.
- Treating zero interfaces or zero services as a target; justified seams remain
  when they pass the keep-list.
- Mixing this report-only gate with deferred research or rendering feature work.
