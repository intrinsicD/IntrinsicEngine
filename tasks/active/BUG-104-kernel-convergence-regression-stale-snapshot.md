---
id: BUG-104
theme: G
depends_on: []
---
# BUG-104 — Kernel-convergence regression asserts a retired snapshot

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`; PR:
  [`#1024`](https://github.com/intrinsicD/IntrinsicEngine/pull/1024).
- Next verification: push the repaired head and require its `pr-fast` job to
  pass the synchronized regression plus the unchanged strict live checker.
- Local fix complete: only the stale repository-snapshot literals changed;
  all 19 regressions and the independent strict live checker pass. Repaired
  `pr-fast` verification remains pending.

## Goal

- Restore the `pr-fast` kernel-convergence regression by making its
  repository-snapshot assertion describe the current, machine-enforced policy.

## Non-goals

- No change to `Runtime.Engine.cppm`, the convergence checker, its policy
  thresholds, domain/substrate classification, or fail-closed behavior.
- No weakening of any synthetic import/getter/debt regression.

## Context

- [`pr-fast` run 29522214300](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29522214300)
  built successfully and passed all 3,693 selected C++ tests, then failed only
  `KernelConvergenceTests.test_current_repository_snapshot_passes`.
- The stale assertion expects `plain_imports=49 domain_imports=28` and
  `owner=RUNTIME-178`. The live strict checker succeeds with
  `plain_imports=42 domain_imports=21 export_imports=2 public_getter_names=31`
  and `Temporary debt: none`, exactly matching
  `tools/repo/kernel_convergence_policy.json`.
- The Engine interface, checker, policy, and failing regression are all
  byte-identical to `origin/main` at `e890817c`, establishing a pre-existing
  test/policy synchronization defect. Commit `109af4bd` ratcheted the policy to
  42 imports / 21 domain imports / 31 getters and cleared temporary debt, but
  the repository-snapshot regression retained its earlier literals.

## Required changes

- [x] Update only the current-repository output assertions to the exact live
  snapshot and debt-free diagnostic.
- [x] Preserve every synthetic checker regression and production policy value.

## Tests

- [x] Reproduce the single deterministic pre-fix failure locally.
- [x] Pass all 19 kernel-convergence regression tests after the correction.
- [x] Pass the strict live checker independently.

## Docs

- [ ] Record root cause and verification in this task, the active/done task
  indexes, and the retirement log; no architecture doc changes are required
  because production policy does not change.

## Acceptance criteria

- [x] The repository-snapshot test asserts `42/21`, 31 public getters, and no
  temporary debt while the strict checker continues to pass.
- [ ] A repaired exact-head `pr-fast` workflow passes.

## Verification

```bash
python3 tests/regression/tooling/Test.CheckKernelConvergence.py -v
python3 tools/repo/check_kernel_convergence.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
git diff --check
```

Local results on 2026-07-16:

- Pre-fix: 18/19 passed; only
  `test_current_repository_snapshot_passes` failed on the retired `49/28` and
  `RUNTIME-178` expectations.
- Post-fix: 19/19 passed.
- Strict live checker passed at `42` plain imports, `21` domain imports, two
  export imports, 31 public getter names, and no temporary debt.
- Strict task policy/state and diff checks passed.

## Forbidden changes

- Changing `Runtime.Engine.cppm` or `kernel_convergence_policy.json` to satisfy
  the stale test.
- Removing the live snapshot assertions instead of synchronizing them.
- Weakening, skipping, or rerunning `pr-fast` without the deterministic fix.
