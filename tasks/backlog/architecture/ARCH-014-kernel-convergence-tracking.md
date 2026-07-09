---
id: ARCH-014
theme: F
depends_on: []
---
# ARCH-014 — Kernel convergence tracking (umbrella north-star)

## Goal
- Own the runtime kernel/module convergence target so any agent touching the
  engine has one place that says *where we are going* and *how far along we
  are*. This is the umbrella that keeps the
  [kernel target-state doc](../../../docs/architecture/kernel-target-state.md)
  scorecard current and stays open until it is all-green.

## Non-goals
- No implementation of the seams or extractions here — those are the child
  tasks (`ARCH-008`..`ARCH-013`, `RUNTIME-146`..`151`, `ARCH-006`, `UI-034`,
  `RUNTIME-129`, `RUNTIME-137`). This task tracks and enforces; it does not
  do their work.
- No re-litigation of ADR-0024 decisions (that is an ADR amendment, not this
  task).
- Not retired on a slice: this umbrella closes only when the target-state
  scorecard is fully checked on `main`.

## Context
- Owner/layer: architecture/runtime governance (Theme F).
- Contract: [ADR-0024](../../../docs/adr/0024-kernel-module-architecture.md)
  (frozen decisions). North star:
  [`docs/architecture/kernel-target-state.md`](../../../docs/architecture/kernel-target-state.md)
  (living scorecard + knob-decision guide).
- Baseline 2026-07-08 (post-`ARCH-007`): `Runtime.Engine.cppm` has 45
  imports, 17 domain-noun imports, 13 `Engine::GetX()` domain-facade
  accessors; `OnSimTick`/`OnVariableTick` still present. `ARCH-007`
  (CommandBus) is the first seam landed.
- This task is the umbrella `RORG-031`-style: it references children, keeps
  the scorecard honest, and provides the review guardrail so the kernel does
  not regrow while the migration is in flight.

## Required changes
- [ ] After each child seam/extraction merges, update the target-state
      scorecard (flip the invariant boxes that now hold on `main`) and
      refresh the baseline metric numbers.
- [ ] Seed a ratchet guard as a child `HARDEN` task: a
      `tools/repo/check_kernel_convergence.py` (or an extension of an
      existing structural check) that fails when `Runtime.Engine.cppm`
      gains a *new* domain-noun import or a *new* `Engine::GetX()` domain
      accessor — a monotone "no backsliding" gate wired into `pr-fast`.
      Until it exists, the review guardrail below is enforced by reviewers.
- [ ] Keep the child-task inventory in the target-state doc in sync with the
      backlog (add extractions discovered during seam work; remove retired
      rows).
- [ ] Resolve the two open design pressure points before their owning seam
      freezes: GPU-job-participant lifecycle (→ `RUNTIME-137`) and
      world-scoped module state (→ `ARCH-010`); record the decisions in the
      target-state doc.

## Tests
- [ ] `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [ ] `python3 tools/docs/check_doc_links.py --root .` reports no broken
      links.
- [ ] (When the ratchet child lands) the convergence guard runs in `pr-fast`
      and fails a synthetic new-domain-import regression.

## Docs
- [ ] The target-state doc is the deliverable surface; keep its scorecard and
      baseline numbers current (this task's ongoing work).
- [ ] Regenerate `tasks/SESSION-BRIEF.md` when child gating changes.

## Acceptance criteria

This umbrella closes only when ALL of the following hold on `main`:

- [ ] Every "Kernel seams exist" scorecard row is checked.
- [ ] `Runtime.Engine.cppm` import count ≤ 12 substrate modules; domain-noun
      imports = 0; `Engine::GetX()` domain-facade accessors = 0.
- [ ] No `entt::dispatcher::trigger` / direct dispatcher in module code; no
      `Engine&` through any module surface; `OnSimTick`/`OnVariableTick`
      removed.
- [ ] Every "Domains are modules" row is an extracted `Runtime.*Module`.
- [ ] The `InlineModule` research lane ships.
- [ ] The ratchet guard is green in `pr-fast` and prevents new kernel
      backsliding.

## Verification
```bash
# Live scorecard metrics (agents run these to check current state):
grep -cE '^import ' src/runtime/Runtime.Engine.cppm
grep -cE '^import .*(ImGui|Gizmo|Selection|KMeans|ObjectSpaceNormalBake|AssetModel|Camera|SceneSerial|EditorCommand|ReferenceScene|MeshPrimitive|Streaming|DerivedJob)' src/runtime/Runtime.Engine.cppm
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py
```

## Forbidden changes
- Adding an `Engine` method/member for a responsibility the knob-decision
  guide places on a module/command/event/service.
- Marking a scorecard row done without the invariant actually holding on
  `main`.
- Retiring this umbrella before the scorecard is fully green.
