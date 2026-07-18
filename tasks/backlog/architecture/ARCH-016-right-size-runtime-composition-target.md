---
id: ARCH-016
theme: F
depends_on:
  - ARCH-015
---
# ARCH-016 — Right-size the ADR-0024 runtime-composition target

## Goal
- Amend ADR-0024 and the kernel target-state so runtime composition is judged
  by observable ownership and coupling outcomes rather than by creating a
  literal `IRuntimeModule`, extension registry, filter chain, or inline builder
  for every scorecard noun.

## Non-goals
- No engine, runtime, graphics, app, or test code changes.
- No implementation of domain extraction children.
- No weakening of the domain-free Engine interface, no-domain-facade, no
  `Engine&` module surface, explicit app composition, or state-scope outcomes.
- No decision based on hypothetical future consumers.

## Context
- Owner/layer: architecture/runtime governance. This task may amend ADR-0024
  D1/D9/D10/D11/D12/D13; ADR-0026 supplies the demand-driven cohesion rule.
- The tree has one production `IRuntimeModule` (`ClusteringModule`), 990 lines
  around one K-Means solve. `ServiceRegistry` and `ModuleSchedule` therefore
  still have one production module consumer.
- The D10 extension-pass registry, D11 priority input-capture filter chain, and
  D12 `InlineModule` have zero production consumers. The checked input-capture
  row actually describes a single proven end-of-editor-frame snapshot, not a
  priority filter chain.
- `REVIEW-003` is supposed to right-size every exported interface/framework,
  but depends on `ARCH-014`; `ARCH-014` currently requires literal module and
  zero-consumer framework outcomes first. ADR-0026 explicitly does not ratify
  `IRuntimeModule`. The current task graph is therefore circular in decision
  order.
- Current exact Engine metrics are 42 plain imports, 21 domain imports,
  2 re-exports, and 31 public getter names with no temporary debt.
- `RUNTIME-172` currently prescribes Engine-private `SceneDocument` ownership,
  directly conflicting with the literal ADR-0024/ARCH-014 target.

## Status
- Backlog; unblocked after `ARCH-015` retirement. Next gate: run a focused
  grilling/right-sizing pass over the live composition mechanisms and write
  ADR-0027.

## Required changes
- [ ] Inventory which semantics in `IRuntimeModule`, `EngineSetup`,
      `ServiceRegistry`, and `ModuleSchedule` are load-bearing today and which
      are one-consumer ceremony.
- [ ] Run the grilling pass from live evidence: decide whether to retain,
      simplify, or remove each current composition mechanism and record a
      concrete reintroduction trigger for anything removed or deferred.
- [ ] Amend ADR-0024 so "domain responsibility belongs outside the kernel" is
      distinct from "must implement the current C++ interface"; use ADR-0026
      cohesion plus present lifecycle/hook/command/service needs to choose a
      concrete composition unit.
- [ ] Amend ADR-0026's mechanism-owner note so ADR-0027 makes the prerequisite
      decision and `REVIEW-003` audits the accepted result rather than owning a
      post-ARCH-014 decision.
- [ ] Replace D10's zero-consumer extension-registry blocker with the smallest
      closed-core/recipe-data invariant justified today, retaining a named
      trigger for extension registration when a real pass needs it.
- [ ] Replace D11's nonexistent priority filter-chain claim with the proven
      single input-capture snapshot contract and name the second-producer
      trigger for a chain.
- [ ] Decide whether `InlineModule`/experiment template has a present consumer;
      defer it with a concrete trigger if not.
- [ ] Correct the kernel scorecard: classify `WorldHandle` as substrate, define
      domain-facade getter measurement, remove stale completed/open claims, and
      enumerate each real remaining domain responsibility plus state scope.
- [ ] Re-scope or supersede `RUNTIME-172` so SceneDocument cannot become
      Engine-private against the accepted ownership target.
- [ ] Seed only behavior-carrying implementation children, including one final
      exact Engine-surface ratchet leaf, and update `ARCH-014` dependencies.

## Tests
- [ ] Strict documentation links and docs-sync checks pass.
- [ ] Strict task policy, task-state links, and session-brief freshness pass.
- [ ] The live kernel-convergence checker remains exact and green; no policy
      snapshot changes occur in this docs/task-only slice.

## Docs
- [ ] Add accepted ADR-0027 as an explicit amendment/refinement to ADR-0024
      and update ADR-0026's mechanism-owner note.
- [ ] Update ADR indexes, runtime architecture, kernel target-state, ARCH-014,
      affected child tasks/indexes, and the generated session brief.
- [ ] Do not edit `AGENTS.md`; the repository layer dependency table is
      unchanged.

## Acceptance criteria
- [ ] ARCH-014 no longer requires a zero-consumer framework or wrapper count
      merely to turn a scorecard box green.
- [ ] The amended target still requires a domain-free Engine public surface,
      no domain facades, no `Engine&` composition leakage, explicit app
      composition, and a world/global state decision for every durable owner.
- [ ] The current composition interface/framework has an explicit keep,
      simplify, or remove verdict supported by present consumers and a deletion
      test.
- [ ] D10, D11, and InlineModule outcomes name present evidence and concrete
      reintroduction triggers.
- [ ] `RUNTIME-172` and every seeded child point in the same ownership
      direction as the amended target.
- [ ] No source, build, method, benchmark, or test code changes.

## Verification
```bash
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode \
  --base-ref origin/main --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Forbidden changes
- Satisfying the target by adding an unused registry, interface, module
  wrapper, or compatibility facade.
- Moving a domain responsibility behind Engine-private glue.
- Predetermining a module-family taxonomy or sharing records without two
  production callers.
- Editing code in this decision/task-graph slice.
