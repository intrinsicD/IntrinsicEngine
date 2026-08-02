---
id: RUNTIME-138
theme: F
depends_on:
  - ARCH-007
  - ARCH-009
  - RUNTIME-192
  - RUNTIME-194
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex-RuntimeBacklogAudit"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-01T06:50:24Z"
maturity_target: Retired
---
# RUNTIME-138 — Withdraw the broad selected-analysis umbrella

## Status

- Withdrawn and retired on 2026-08-01 at `Retired`; commit reference: this
  task-surgery change plus its generated workflow report.
- The broad async selected-analysis program is not justified by current
  evidence. The bounded UI-030 capture attributes only 0.21% of measured time
  to the editor callback and identifies present/fallback-frame lifecycle as
  the dominant cost. No before/after responsiveness claim is made here.
- Delivered visibility gating, immutable selected-model caching, scan/allocation
  counters, timing diagnostics, canonical property snapshots, and bounded
  `JobService` completion draining remain supported behavior. This retirement
  deletes no implementation and does not weaken their tests.
- The original `claim-grade` profile targeted a possible responsiveness claim.
  Withdrawal makes no research, performance, parity, or capability claim, so
  this administrative evidence review correctly closes at `standard` rather
  than fabricating an experiment for work that was deliberately not pursued.

## Goal

- Remove the speculative all-feature selected-analysis umbrella from the open
  queue, preserve its useful delivered baseline, and require any future async
  derivation to be justified by a measured feature-local bottleneck.

## Non-goals

- No removal or refactor of the selected-model cache, visibility gating,
  diagnostics, canonical property snapshots, or `JobService` behavior.
- No claim that selected-editor work can never become expensive for a specific
  model, property, or method workflow.
- No broad replacement selected-analysis service, scheduler, registry, queue,
  or generation-signature framework.

## Context

- Owner/layer: runtime owns feature operations, copied snapshots, and async
  application; app-owned panels consume those contracts without live runtime
  state.
- The completed RUNTIME-202 facade retirement preserved the existing cache and
  diagnostic behavior while localizing production feature work. It explicitly
  avoided creating a replacement selected-analysis service.
- The delivered metadata/cache path already prevents hidden or steady-state
  consumers from rebuilding the heaviest selected models. The shared
  `JobService` already provides immutable execution, cancellation, stale-result
  rejection, and bounded main-thread publication when a concrete operation
  actually needs asynchronous work.
- A future task is warranted only when a named workflow records full-buffer
  scans or callback cost as material. That task belongs to the feature owner
  and carries its exact generation key, job, diagnostics, and regression
  coverage; it must not reopen this umbrella by default.

## Required changes

- [x] Reconcile the remaining unchecked slices against the post-RUNTIME-202
      production surface and existing shared `JobService` contract.
- [x] Compare the broad responsiveness premise with the bounded UI-030 frame
      pacing evidence and record that no dominant editor-callback bottleneck
      was established.
- [x] Preserve delivered selected-model cache/visibility/diagnostic contracts
      as factual baseline behavior without promising the unfinished umbrella.
- [x] Record the feature-local reintroduction trigger for any future expensive
      selected derivation.
- [x] Right-size the related runtime queue: replace speculative AoS adoption,
      correct consolidation ownership/sequencing, withdraw premature
      orientation presentation, and split helper cleanup by concrete owner.

## Tests

- [x] Run strict task, state-link, documentation-link, and workflow-evidence
      validation for the tracker-only change.
- [x] Confirm no production source, build, shader, or test file changes are
      part of this retirement.

## Review

- Clean-workshop rows 1–2 and 8: pass; strict layering and the empty allowlist
  remain clean.
- Rows 3–6: not applicable; this change adds no C++ module/API, renderer member,
  frame-graph pass, recipe resource, or ordering edge.
- Row 7: pass; both withdrawn plans state `Retired` for planning only and make
  no false `Operational`/parity claim. The remaining open tasks state their
  exact maturity targets and follow-up ownership.

## Docs

- [x] Update runtime, UI, method, architecture, and cross-domain backlog
      indexes so none names RUNTIME-138 as open work.
- [x] Update the factual runtime README to retain delivered behavior while
      replacing umbrella ownership with the feature-local evidence trigger.
- [x] Append the retirement narrative and regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria

- [x] RUNTIME-138 is absent from the open task queue and no dependency waits
      for its broad async pipeline.
- [x] Existing cache, visibility, diagnostics, and bounded work-execution
      behavior remain documented and unmodified.
- [x] Any future async selected derivation requires a named consumer and
      measured bottleneck in a new scoped task.
- [x] No responsiveness, Vulkan, or performance improvement is claimed by this
      administrative retirement.

## Verification

```bash
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/workflow_evidence.py validate --root . --require-complete RUNTIME-138
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Forbidden changes

- Removing delivered cache/diagnostic behavior merely to close the planning
  task.
- Replacing the umbrella with another generic selected-analysis abstraction.
- Claiming responsiveness improvement from task retirement or from the
  historical UI-030 capture.
- Opening a feature-local async path without measured cost and a concrete
  consumer.

## Maturity

- `Retired` applies to the broad planning/ownership umbrella. Delivered
  selected-editor cache and diagnostic slices remain `CPUContracted` factual
  behavior. No `Operational` follow-up is owed for the withdrawn umbrella.
