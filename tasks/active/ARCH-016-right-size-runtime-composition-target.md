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
- No engine, runtime, graphics, app, or C++ behavior-test changes. The one
  structural-tool regression expectation follows the factual policy
  reclassification.
- No implementation of domain extraction children.
- No weakening of the domain-free Engine interface, no-domain-facade, no
  `Engine&` module surface, explicit app composition, or state-scope outcomes.
- No decision based on hypothetical future consumers.

## Context
- Owner/layer: architecture/runtime governance. This task may amend ADR-0024
  D1/D2/D3/D9/D10/D11/D12/D13; ADR-0026 supplies the demand-driven cohesion
  rule.
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
- Current exact Engine metrics are 42 plain imports, 20 domain imports,
  2 re-exports, and 31 public getter names with no temporary debt after
  correcting `Runtime.WorldHandle` to kernel substrate.
- `RUNTIME-172` currently prescribes Engine-private `SceneDocument` ownership,
  directly conflicting with the literal ADR-0024/ARCH-014 target.

## Status
- Completed 2026-07-18; owner: Codex; branch:
  `codex/arch-016-runtime-composition-target`; activated after the ARCH-014
  reconciliation merged.
- The focused grilling/right-sizing pass uses the completed live-tree audits:
  one production `IRuntimeModule`; zero extension-registry, priority
  input-filter-chain, and `InlineModule` consumers; six evidence-backed
  responsibility cohorts; and the corrected exact 42/20/2/31 Engine snapshot.
- ADR-0027, the target correction, and the acyclic `RUNTIME-179`..`187`
  implementation graph are accepted. Three independent adversarial reviews
  found no residual scope, mechanism, reference, metric, or dependency-cycle
  issue after corrections.
- Strict task policy/validation/state links, session-brief freshness,
  documentation links/sync, layering/test-layout/root-hygiene/clean-workshop
  structural checks, whitespace checks, the exact live convergence checker,
  and all 19 checker regressions passed. No engine source or behavior surface
  changed.

## Required changes
- [x] Inventory which semantics in `IRuntimeModule`, `EngineSetup`,
      `ServiceRegistry`, and `ModuleSchedule` are load-bearing today and which
      are one-consumer ceremony.
- [x] Run the grilling pass from live evidence: decide whether to retain,
      simplify, or remove each current composition mechanism and record a
      concrete reintroduction trigger for anything removed or deferred.
- [x] Amend ADR-0024 so "domain responsibility belongs outside the kernel" is
      distinct from "must implement the current C++ interface"; use ADR-0026
      cohesion plus present lifecycle/hook/command/service needs to choose a
      concrete composition unit.
- [x] Amend ADR-0026's mechanism-owner note so ADR-0027 makes the prerequisite
      decision and `REVIEW-003` audits the accepted result rather than owning a
      post-ARCH-014 decision.
- [x] Replace D10's zero-consumer extension-registry blocker with the smallest
      closed-core/recipe-data invariant justified today, retaining a named
      trigger for extension registration when a real pass needs it.
- [x] Replace D11's nonexistent priority filter-chain claim with the proven
      single input-capture snapshot contract and name the second-producer
      trigger for a chain.
- [x] Decide whether `InlineModule`/experiment template has a present consumer;
      defer it with a concrete trigger if not.
- [x] Correct the kernel scorecard: classify `WorldHandle` as substrate, define
      domain-facade getter measurement, remove stale completed/open claims, and
      enumerate each real remaining domain responsibility plus state scope.
- [x] Apply the matching exact-policy classifier correction (`v2`,
      2026-07-18) without changing the Engine source, and run the checker plus
      its regression suite so the executable guard and target prose agree.
- [x] Re-scope or supersede `RUNTIME-172` so SceneDocument cannot become
      Engine-private against the accepted ownership target.
- [x] Seed only behavior-carrying implementation children, including one
      explicit app-lifecycle leaf, one mechanism-deletion-test leaf, one
      residual semantic API leaf, and one final mechanical Engine-surface
      ratchet leaf, and update `ARCH-014` dependencies.

## Tests
- [x] Strict documentation links and docs-sync checks pass.
- [x] Strict task policy, task-state links, and session-brief freshness pass.
- [x] The live kernel-convergence checker and its regression suite remain
      green after the factual `WorldHandle` substrate reclassification; no
      Engine-source or temporary-debt snapshot change occurs.

## Docs
- [x] Add accepted ADR-0027 as an explicit amendment/refinement to ADR-0024
      and update ADR-0026's mechanism-owner note.
- [x] Update ADR indexes, runtime architecture, kernel target-state, ARCH-014,
      affected child tasks/indexes, and the generated session brief.
- [x] Do not edit `AGENTS.md`; the repository layer dependency table is
      unchanged.

## Acceptance criteria
- [x] ARCH-014 no longer requires a zero-consumer framework or wrapper count
      merely to turn a scorecard box green.
- [x] The amended target still requires a domain-free Engine public surface,
      no domain facades, no `Engine&` composition leakage, explicit app
      composition, and a world/global state decision for every durable owner.
- [x] The current composition interface/framework has an explicit keep,
      simplify, or remove verdict supported by present consumers and a deletion
      test.
- [x] D10, D11, and InlineModule outcomes name present evidence and concrete
      reintroduction triggers.
- [x] `RUNTIME-172` and every seeded child point in the same ownership
      direction as the amended target.
- [x] No engine source, build, method, benchmark, or C++ behavior-test changes;
      only the convergence-policy fixture expectation changes with the
      classifier.

## Verification
```bash
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tests/regression/tooling/Test.CheckKernelConvergence.py
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
- Editing engine/runtime/app/C++ behavior code in this decision/task-graph
  slice; the structural convergence fixture follows the corrected classifier.
