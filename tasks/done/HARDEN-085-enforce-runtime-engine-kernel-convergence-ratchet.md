---
id: HARDEN-085
theme: F
depends_on: []
maturity_target: CPUContracted
---
# HARDEN-085 — Enforce the Runtime.Engine kernel-convergence ratchet

## Status

- Completed on 2026-07-16 at `CPUContracted`.
- Implementation commit: `ea3b403e`.
- Verification: checker regressions passed `19/19`, touched-scope regressions
  `10/10`, workflow regressions `5/5`, and ccache workflow regressions `15/15`.
  The live exact policy and strict structural gates passed.

## Goal

- Implement the no-backsliding child required by `ARCH-014`: count every plain
  `Runtime.Engine.cppm` import outside an explicit kernel-substrate allowlist,
  reject any untracked public `Engine::GetX()` name or re-export, and require
  every improvement to ratchet the checked policy in the same change.

## Non-goals

- No runtime C++ behavior or Engine API changes in this slice.
- No attempt to hide today's convergence debt or to restore the historical
  import budget inside the checker task; `RUNTIME-178` owns that remediation.
- No subjective reconstruction of the historical “~13 domain facade” getter
  estimate; the guard conservatively snapshots all current public `GetX`
  names.

## Context

- Owner/layer: repository architecture tooling; authoritative contract:
  `ARCH-014` and `docs/architecture/kernel-target-state.md`.
- The fixed 2026-07-13 legacy-interim reference snapshot is 43 plain imports /
  23 domain imports. Its unanchored regex classified the then-present
  `RenderExtractionService` through the `RenderExtraction` prefix; exact v1
  classification is prospective and does not rewrite that record. This branch
  currently has 49 / 28 and 33 distinct public `GetX` names versus 32 on
  `origin/main`. The added
  `GetMaterialTextureAssetBindings` name came from `GRAPHICS-122`.
- The `+6` total-import, `+5` domain-import, and `+1` getter-name delta is
  temporary debt, not a rewritten baseline. `RUNTIME-178` owns restoring
  `<=43 / <=23` and removing the getter debt without resurrecting retired
  service BMIs.
- The interim regex admits `JobServiceGpuQueueBridge` through `JobService` and
  `ModuleSchedule` through `Module` accidentally. The authoritative policy
  must use exact module names where prefix classification is not intended.

## Right-sizing

- A standalone stdlib-only checker plus plain JSON policy is justified by the
  present `pr-fast` caller and synthetic-test seam required by `ARCH-014`.
- Do not add a parser framework, package, service, plugin, generated policy, or
  generic C++ architecture query language. A comment-aware source scan over one
  named interface is the complete required shape.

## Required changes

- [x] Add `tools/repo/check_kernel_convergence.py` and a versioned JSON policy
      that records the fixed reference snapshot, exact current snapshot,
      substrate classification, export imports, public getter names, and
      `RUNTIME-178` temporary debt.
- [x] Count domain imports as the allowlist complement; classify runtime
      substrate modules exactly and fail closed on missing/malformed policy or
      Engine class source.
- [x] Reject new and stale domain imports, export imports, and public `GetX`
      names so reductions require a same-change policy ratchet.
- [x] Wire both the live checker and synthetic regression suite into
      `.github/workflows/pr-fast.yml`.
- [x] Teach touched-scope planning to select the checker regression and live
      guard when the checker, policy, test, or `Runtime.Engine.cppm` changes.

## Tests

- [x] Synthetic fixtures cover a clean snapshot, an unknown domain import, an
      allowed substrate import, a new public getter, a removed/stale entry, a
      comment-only fake getter, a new export import, and malformed/missing
      Engine input.
- [x] Current repository snapshot passes the live guard.
- [x] Touched-scope and workflow policy regressions pass.

## Docs

- [x] Replace the interim metric with the checker command in the kernel target
      state, retain the historical reference snapshot, and record the dated
      current/debt snapshot.
- [x] Update `ARCH-014`, task indexes, and `tasks/SESSION-BRIEF.md`.

## Acceptance criteria

- [x] `pr-fast` rejects an increased allowlist-complement domain-import count,
      a new public `Engine::GetX()` name, and a new Engine re-export.
- [x] A convergence improvement cannot leave slack for later regrowth: stale
      policy entries fail until the policy is lowered in the same change.
- [x] The current 49 / 28 snapshot is green only with its explicit
      `RUNTIME-178` debt record; the 43 / 23 reference remains unchanged.

## Evidence

- The live exact-v1 scan reports 49 plain imports, 28 allowlist-complement
  domain imports, two re-exports, and 33 distinct public getter names. The
  policy keeps 43 / 23 as the labeled legacy-interim reference and
  machine-validates the `+6 / +5 / +1 getter` budget debt against the unique
  open owner `RUNTIME-178`.
- The checker fails closed on malformed policy/source/class structure,
  unsupported header-unit imports, unresolvable/retired debt owners, stale
  zero-debt records, and debt arithmetic mismatch. Exact domain/export/getter
  sets make reductions require a same-change policy update; zero excess
  requires `temporary_debt: null` so the owner can retire cleanly.
- Checker regressions passed `19/19`, including same-line multi-directive
  parsing, comment/string/private/inline-call exclusions, unknown and stale
  domain imports, substrate classification, new getters/re-exports, workflow
  wiring, and the debt lifecycle.
- Touched-scope regressions passed `10/10`; independent workflow-policy
  regressions passed `5/5`; ccache workflow regressions passed `15/15`.
  Direct plan inspection selects the live/synthetic guard for Engine/checker
  changes and independently selects workflow regression for `pr-fast` edits.
- Strict task policy and validation passed for 94 task files; documentation
  links passed for 2,720 relative links; strict layering passed across 741
  source files, 6,392 imports/includes, and 85 CMake links. Workflow naming,
  Python compilation, JSON validation, and diff checks passed. This tooling-only
  slice changes no runtime C++ behavior.

## Verification

```bash
python3 tests/regression/tooling/Test.CheckKernelConvergence.py
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tests/regression/tooling/Test.TouchedScope.py
python3 tools/ci/touched_scope.py --root . --changed-file src/runtime/Runtime.Engine.cppm --print
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tools/ci/check_workflow_names.py --root .github/workflows --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes

- Reclassifying a domain import as substrate merely to make the count pass.
- Rewriting the historical 43 / 23 reference as 49 / 28.
- Removing the temporary-debt owner without first restoring its budget.
- Changing runtime source/API behavior in this tooling slice.

## Maturity

- Target: `CPUContracted`; deterministic source/policy regressions are the
  complete contract. No Operational follow-up is owed.
