---
id: BUG-066
theme: G
depends_on:
  - ARCH-011
maturity_target: CPUContracted
---
# BUG-066 — RuntimeModule system order follows module registration order

## Status
- Completed 2026-07-09 at `CPUContracted` on
  `copilot/bug-066-runtime-module-ordering`; commit/PR: current change.
- Focused module ordering coverage passed 7/7 and the default CPU-supported
  gate passed 3635/3635. Strict layering, test-layout, task-policy,
  task-state-link, docs-link, and docs-sync checks passed.
- Clean-workshop review: automated rows passed; manual row 3 passed (the
  runtime API exports only allowed core/ECS types), row 4 was N/A, row 5 passed
  (typed `StringID` signals; `PassName` is identity/diagnostics, not routing),
  and row 6 was N/A. No findings or follow-up tasks.
- `Operational` remains owned by `ARCH-012`.

## Goal
- Restore the ARCH-011 contract so module-registered simulation systems have a
  deterministic schedule independent of `AddModule` order, with causal order
  expressed through explicit named system signals.

## Non-goals
- No change to the core `FrameGraph` sequential RAW/WAR/WAW hazard model.
- No ClusteringModule extraction; `ARCH-012` remains the `Operational` owner.
- No general frame-hook ordering redesign.

## Context
- Owner/layer: Copilot, `runtime`; branch
  `copilot/bug-066-runtime-module-ordering`.
- PR #1013 introduced the `Runtime.Module` seam and merged while
  `RuntimeModuleContract.SimSystemScheduleIsIndependentOfRegistrationOrder`
  and `...ModuleSimSystemExecutesWithDeclaredDependencyOrder` failed in
  `pr-fast`, `ci-linux-clang`, ASan, and UBSan.
- Focused local reproduction on 2026-07-09 confirms that reversing
  `AddModule` order produces `{reader, writer}` and lets the reader observe
  `WriterRan == false`.
- Root cause: `Core::FrameGraph` intentionally orients resource hazards from
  earlier passes to later passes; ARCH-011 incorrectly assumed a `Read` and a
  `Write` declaration alone identifies producer direction across arbitrary
  module registration order.

## Required changes
- [x] Extend `SimSystemDesc` with explicit named wait/signal metadata for
      cross-system causal order.
- [x] Make `ModuleRegistrationSink::ApplySimSystems` add systems in a stable,
      dependency-respecting order before the core `FrameGraph` compiles
      sequential resource hazards.
- [x] Keep resource declarations on the existing shared
      `FrameGraphBuilder`; do not alter core DAG hazard semantics.
- [x] Correct the ARCH-011 task record and architecture docs to describe the
      repaired contract factually.

## Tests
- [x] Preserve the reversed-registration regression and declare the intended
      writer-to-reader signal dependency.
- [x] Prove the compiled schedule is identical under both module-add orders.
- [x] Prove execution observes writer-before-reader under reversed module-add
      order.
- [x] Run the focused contract tests and the default CPU-supported gate.

## Docs
- [x] Update `docs/architecture/runtime.md` and the feature-module playbook
      with the explicit named-signal ordering rule.
- [x] Regenerate `docs/api/generated/module_inventory.md`,
      `tasks/SESSION-BRIEF.md`, and relevant task indexes.

## Acceptance criteria
- [x] The two failing PR #1013 RuntimeModule contract tests pass locally.
- [x] Reversing module registration cannot reverse a uniquely named
      module-system schedule.
- [x] Explicit system signals determine causal order without changing
      `Core::FrameGraph` RAW/WAR/WAW behavior.
- [x] Default CPU gate and strict structural checks pass.
- [x] `Operational` remains owned by `ARCH-012`.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeContractTests
ctest --test-dir build/ci --output-on-failure -R RuntimeModuleContract --timeout 60
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --strict
```

## Forbidden changes
- Do not canonicalize core FrameGraph passes globally.
- Do not make pass names imply producer/consumer direction.
- Do not weaken, remove, or quarantine the failing contract tests.
- Do not claim `Operational` maturity from CPU-only coverage.

## Maturity
- Target: `CPUContracted`; `Operational` owned by `ARCH-012`.
