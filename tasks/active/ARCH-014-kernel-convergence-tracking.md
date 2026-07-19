---
id: ARCH-014
theme: F
depends_on:
  - ARCH-016
  - RUNTIME-187
---
# ARCH-014 — Kernel convergence tracking (umbrella north-star)

## Goal
- Own the runtime kernel/module convergence target so any agent touching the
  engine has one place that says *where we are going* and *how far along we
  are*. This is the umbrella that keeps the
  [kernel target-state doc](../../docs/architecture/kernel-target-state.md)
  scorecard current and stays open until it is all-green.

## Non-goals
- No implementation of the seams or extractions here — child tasks own that
  work. Retired children remain convergence evidence; open children such as
  `RUNTIME-129` own their implementation. This task
  tracks and enforces; it does not do their work.
- No re-litigation of ADR-0024/ADR-0027 decisions (that is an ADR amendment,
  not this task).
- Not retired on a slice: this umbrella closes only when the target-state
  scorecard is fully checked on `main`.

## Context
- Owner/layer: architecture/runtime governance (Theme F).
- Contract: [ADR-0024](../../docs/adr/0024-kernel-module-architecture.md), as
  amended by
  [ADR-0027](../../docs/adr/0027-right-sized-runtime-composition.md). North star:
  [`docs/architecture/kernel-target-state.md`](../../docs/architecture/kernel-target-state.md)
  (living scorecard + knob-decision guide).
- Baseline 2026-07-08 (post-`ARCH-007`): `Runtime.Engine.cppm` has 45
  imports, **27 domain (non-substrate) imports** (measured by the allowlist
  complement in Verification, not a name blocklist), ~13 `Engine::GetX()`
  domain-facade accessors; `OnSimTick`/`OnVariableTick` still present.
  Keep this historical baseline fixed for comparison.
- Fixed legacy-interim reference snapshot 2026-07-13:
  `Runtime.Engine.cppm` had 43 plain imports and 23 domain imports under the
  then-unanchored classifier (which admitted `RenderExtractionService` through
  the `RenderExtraction` prefix). Retired `RUNTIME-178` restored the checked
  exact-v1 snapshot to 42 plain imports, 21 domain imports, 31 distinct public
  getter names, and two re-exports with no temporary debt. ADR-0027 then
  corrected `Runtime.WorldHandle` from domain to kernel substrate, making the
  exact-v2 snapshot 42 / 20 / 2 / 31 without changing the interface.
  Retired `RUNTIME-179`, `RUNTIME-181`, and `RUNTIME-182` reduced the checked
  snapshot to 39 plain imports, 17 domain imports, two re-exports, and 28
  public getter names. Retired `RUNTIME-180` then reduced the current checked
  snapshot to 35 plain imports, 13 domain imports, two re-exports, and 25
  public getter names.
  Retired `HARDEN-085` delivered the authoritative exact-policy ratchet.
  `ARCH-012`
  retired on 2026-07-08 at `Operational`: Sandbox composes
  `Runtime::ClusteringModule`, and `Runtime.Engine.cppm` / `.cpp` contain no
  `KMeans` or `Runtime.ClusteringModule` tokens. The Clustering scorecard row
  is therefore complete. `UI-034` retired on 2026-07-13 at `CPUContracted`;
  the generic editor registry, lazy callback lifecycle, one capture snapshot,
  global visibility command, and property widgets now exist. `ARCH-006`
  retired after moving Sandbox presentation into the app; remaining runtime
  ImGui residue stays visible in the convergence snapshot.
- This task is the umbrella `RORG-031`-style: it references children, keeps
  the scorecard honest, and provides the review guardrail so the kernel does
  not regrow while the migration is in flight.

## Status
- Blocked on the final `RUNTIME-187` convergence leaf; owner: Codex;
  coordination branch:
  `codex/arch-014-kernel-convergence-program`; activated 2026-07-18 after
  `ARCH-015` retirement.
- The 2026-07-18 reconciliation audit measured the exact clean ratchet at
  42 plain imports, 21 then-classified domain imports, 2 re-exports, and 31
  public getter names. Subsequent behavior-backed extractions now measure
  35 / 13 / 2 / 25. The audit also found that the literal scorecard would
  require zero-consumer extension/input frameworks, an unused `InlineModule`,
  and mechanical `IRuntimeModule` wrappers while the right-sizing audit that
  owns that interface is itself blocked on this umbrella.
- Retired `ARCH-016` accepted ADR-0027, corrected `WorldHandle` to substrate,
  replaced
  mechanism-count outcomes with greppable ownership outcomes, re-scoped the
  contradictory `RUNTIME-172`, and seeded only behavior-carrying children.
  The implementation graph converges through `RUNTIME-179`..`187` plus the
  audited `RUNTIME-188` scene-interaction split;
  application lifecycle removal and `RUNTIME-129` operational bake may proceed
  once their respective owners land. Retired `GRAPHICS-128` closed the bake's
  shared-index-slice prerequisite, leaving `RUNTIME-183` as `RUNTIME-129`'s
  remaining composition blocker. The lifecycle and bake leaves both gate the
  later mechanism deletion audit, semantic auxiliary-surface cleanup, and
  final representation/checker leaf.

## Required changes
- [ ] After each child seam/extraction merges, update the target-state
      scorecard (flip the invariant boxes that now hold on `main`) and
      add or refresh a dated current metric snapshot without rewriting the
      historical 2026-07-08 baseline.
- [x] Seed a ratchet guard as a child `HARDEN` task: a
      `tools/repo/check_kernel_convergence.py` (or an extension of an
      existing structural check) that computes the domain-import count as the
      **allowlist complement** (every `Runtime.Engine.cppm` import that is not
      kernel substrate — never a hardcoded name blocklist, which silently
      undercounts) and fails when it *increases* or a *new* `Engine::GetX()`
      domain accessor appears — a monotone "no backsliding" gate wired into
      `pr-fast`. This checker is the authoritative metric and requires exact
      policy updates on improvements so a stale cap cannot permit regrowth.
- [ ] Keep the child-task inventory in the target-state doc in sync with the
      backlog: add extractions discovered during seam work, retain completed
      rows as checked evidence, and remove retired tasks from open-work
      sequencing text.
- [x] Record the GPU-job-participant lifecycle decision from retired
      `RUNTIME-137`: `JobService` owns the `GpuQueue` participant registry,
      frame-command recording, completion draining, and post-idle shutdown.
- [ ] Require each module extraction to decide whether its durable state is
      world-scoped or global. `ARCH-010` supplied `WorldRegistry`; it did not
      make that policy decision for later domain modules. Track those decisions
      here rather than assigning the open work back to retired `ARCH-010`.
- [x] Run a 2026-07-18 scorecard/right-sizing reconciliation and seed
      `ARCH-016` instead of manufacturing wrappers for zero-consumer or
      one-consumer mechanisms.
- [x] Replace this umbrella's dependency graph and acceptance wording with the
      ADR-0027 evidence-backed child program.

## Tests
- [x] `python3 tools/agents/check_task_policy.py --root . --strict` passes.
- [x] `python3 tools/docs/check_doc_links.py --root .` reports no broken
      links.
- [x] (When the ratchet child lands) the convergence guard runs in `pr-fast`
      and fails a synthetic new-domain-import regression.

## Docs
- [ ] The target-state doc is the deliverable surface; keep its scorecard and
      dated current metric snapshot synchronized while preserving the baseline
      numbers (this task's ongoing work).
- [ ] Regenerate `tasks/SESSION-BRIEF.md` when child gating changes.

## Acceptance criteria

This umbrella closes only when ALL of the following hold on `main`:

- [ ] Every "Kernel seams exist" scorecard row is checked.
- [ ] `Runtime.Engine.cppm` contains only the exact accepted kernel imports,
      with no unused imports, domain imports, domain re-exports, or
      `Engine::GetX()` domain facades.
- [ ] No `entt::dispatcher::trigger` / direct dispatcher in module code; no
      `Engine&` through any module surface; `OnSimTick`/`OnVariableTick`
      removed.
- [ ] Every "Domain responsibilities are app-composed" row is checked with its
      global or world-qualified state scope preserved.
- [ ] The final deletion test removes or narrows any `EngineSetup`,
      `ServiceRegistry`, or `ModuleSchedule` surface without a production
      consumer; deferred D10/D11/D12/world-switch mechanisms remain absent
      until their recorded triggers occur.
- [x] The ratchet guard is green in `pr-fast` and prevents new kernel
      backsliding.

## Verification
```bash
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py
```

## Forbidden changes
- Adding an `Engine` method/member for a responsibility the knob-decision
  guide places on an app-composed owner/command/event/service.
- Marking a scorecard row done without the invariant actually holding on
  `main`.
- Retiring this umbrella before the scorecard is fully green.
