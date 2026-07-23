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
  work. Retired children, including `RUNTIME-129`, remain convergence
  evidence; open children own their implementation. This task
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
  public getter names. Retired `RUNTIME-172` removed document/history ownership
  and facades and reduced the checked snapshot to 33 plain imports, 11 domain
  imports, two re-exports, and 22 public getter names. Retired `RUNTIME-188`
  removes interaction ownership/facades and the obsolete mesh-view
  compatibility surface, reducing the checked snapshot to 26 plain imports,
  4 domain imports, two re-exports, and 15 public getter names.
  `RUNTIME-183` removes the remaining asset/import/cache/bake ownership and
  facade surface, reducing the checked snapshot to 22 plain imports, zero
  domain imports, two re-exports, and 10 public getter names.
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
- Retired on 2026-07-23 at the architecture-governance `Retired` endpoint on
  `codex/arch-014-kernel-convergence-program`. Every ADR-0027 child is retired,
  the living target-state scorecard is all-green, and the exact Engine guard
  is closed at `12/0/0/5` with no temporary debt.
- The 2026-07-18 reconciliation audit measured the exact clean ratchet at
  42 plain imports, 21 then-classified domain imports, 2 re-exports, and 31
  public getter names. Subsequent behavior-backed extractions now measure
  22 / 0 / 2 / 10. The audit also found that the literal scorecard would
  require zero-consumer extension/input frameworks, an unused `InlineModule`,
  and mechanical `IRuntimeModule` wrappers while the right-sizing audit that
  owns that interface is itself blocked on this umbrella.
- Retired `ARCH-016` accepted ADR-0027, corrected `WorldHandle` to substrate,
  replaced
  mechanism-count outcomes with greppable ownership outcomes, re-scoped the
  contradictory `RUNTIME-172`, and seeded only behavior-carrying children.
  The implementation graph converges through `RUNTIME-179`..`187` plus the
  retired `RUNTIME-188` scene-interaction split;
  application lifecycle removal may now proceed inside its accepted owner.
  Retired `GRAPHICS-128` closed the bake's shared-index-slice prerequisite,
  retired `RUNTIME-183` supplied the accepted private AssetWorkflow composition
  owner, and retired `RUNTIME-129` now supplies the operational bake evidence.
  `RUNTIME-184` removed the application callback, `RUNTIME-185` deleted
  unproven composition machinery, `RUNTIME-186` settled the remaining
  semantic Engine API, and `RUNTIME-187` completed opaque state plus the exact
  declaration-backed checker. The complete CPU selector passed 4,269/4,269,
  fresh ASan and UBSan selectors each passed 2,923/2,923, and the promoted
  Vulkan intersection passed 48/48 including shutdown LeakSanitizer. The
  [clean-workshop review](../../docs/reviews/2026-07-23-arch-014-clean-workshop-review.md)
  records no findings.
- Commit: pending this retirement checkpoint.

## Required changes
- [x] After each child seam/extraction merges, update the target-state
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
- [x] Keep the child-task inventory in the target-state doc in sync with the
      backlog: add extractions discovered during seam work, retain completed
      rows as checked evidence, and remove retired tasks from open-work
      sequencing text.
- [x] Record the GPU-job-participant lifecycle decision from retired
      `RUNTIME-137`: `JobService` owns the `GpuQueue` participant registry,
      frame-command recording, completion draining, and post-idle shutdown.
- [x] Require each module extraction to decide whether its durable state is
      world-scoped or global. `ARCH-010` supplied `WorldRegistry`; it did not
      make that policy decision for later domain modules. Track those decisions
      here rather than assigning the open work back to retired `ARCH-010`.
      Retired `RUNTIME-172` records a global document-module object whose complete
      durable state is bound to one validated active world and reset, never
      cached, across world changes.
      `RUNTIME-183` records a global asset-workflow module with persistent
      import/bake objects, per-boot asset/cache/handoffs, and exact borrowed
      `{WorldHandle, Registry*, binding epoch}` scene state that resets rather
      than resurrects across replacement, switch, retirement, and reinitialize.
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
- [x] The target-state doc is the deliverable surface; keep its scorecard and
      dated current metric snapshot synchronized while preserving the baseline
      numbers (this task's ongoing work).
- [x] Regenerate `tasks/SESSION-BRIEF.md` when child gating changes.

## Acceptance criteria

This umbrella closes only when ALL of the following hold on `main`:

- [x] Every "Kernel seams exist" scorecard row is checked.
- [x] `Runtime.Engine.cppm` contains only the exact accepted kernel imports,
      with no unused imports, domain imports, domain re-exports, or
      `Engine::GetX()` domain facades.
- [x] No `entt::dispatcher::trigger` / direct dispatcher in module code; no
      `Engine&` through any module surface; `OnSimTick`/`OnVariableTick`
      removed.
- [x] Every "Domain responsibilities are app-composed" row is checked with its
      global or world-qualified state scope preserved.
- [x] The final deletion test removes or narrows any `EngineSetup`,
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
