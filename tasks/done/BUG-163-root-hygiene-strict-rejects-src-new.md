---
id: BUG-163
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: >-
  Repository-root policy and CI-gate health only; no engine layer, geometry,
  method, or control-surface contract changes. If the chosen disposition adds
  a root-allowlist exception, it is the AGENTS.md §13-style documented
  temporary exception tracked by this task, not a contract change.
---
# BUG-163 — Strict root hygiene rejects `src_new/`, failing docs-validation on every run

## Status

- Resolved and retired on 2026-09-02. The repository owner implemented
  disposition **(b)** directly on `main`: commit `17d9d29a`
  ("removed src_new") deletes the entire `src_new/` experiment tree
  (3,678 deletions, no other changes), so no undocumented root entry
  remains and `tools/repo/root_allowlist.yaml` is unchanged.
- Verified on the first tree containing that commit (the PR #1034 branch
  after merging `main`): `python3 tools/repo/check_root_hygiene.py
  --root . --strict` passes and
  `python3 tests/regression/tooling/Test.RootHygiene.py` passes 12/12.
- Precision correction recorded at retirement: the `docs-validation` job
  (`ci-docs.yml`) is triggered by pull requests only, so there is no
  main-push run to observe. Gate confirmation is each PR's
  `docs-validation` lane once its merge ref contains `17d9d29a`.
- The only other in-tree `src_new` references are April-2026 archive and
  review records about an unrelated earlier reorganization; nothing
  live pointed at the removed experiment.

## Goal

- Restore a green `check_root_hygiene.py --strict` gate (the final step of the
  `docs-validation` CI job) by resolving the undocumented root entry
  `src_new/` per repository policy — either as a documented temporary
  allowlist exception with a removal condition, or by relocating the
  experiment out of the repository root.

## Non-goals

- No deletion or content change of the `src_new/` experiment itself; its
  disposition is the repository owner's decision, recorded here first.
- No weakening, warning-moding, or removal of the strict root-hygiene check.
- No broader root-allowlist rework.

## Context

- `main` commit `aba65211` ("working on a test (src_new", 2026-08-14) added
  the experimental tree `src_new/` at the repository root. It is not in
  `tools/repo/root_allowlist.yaml`, so every CI run that executes
  `python3 tools/repo/check_root_hygiene.py --root . --strict` fails:

  ```
  [check_root_hygiene] Unexpected root entries:
    - src_new/
  [check_root_hygiene] STRICT MODE: failing due to root-policy mismatch.
  ```

  Concrete instance: PR #1034 `docs-validation` job `97613350190`
  (run `32784458286`, 2026-08-24) — every earlier validator step in the job
  passes; only this final step fails. The failure is therefore pre-existing
  on `main`, independent of any PR diff that does not touch the root.
- `src_new/` is the owner's ground-up shape experiment (module skeletons for
  apps/compute/core/ecs/graphics). It is intentional in-progress work, which
  is why this task records a decision instead of prescribing removal.
- Related but distinct: the long-standing red C++ lanes on `main`
  (`full-cpu`, sanitizers, Vulkan) are the Clang-20/glm module break owned by
  `BUG-157` — do not mix that repair into this task.

## Required changes

- [x] Record the owner's disposition here: **(a)** add `src_new/` to
      `tools/repo/root_allowlist.yaml` as a documented temporary experiment
      root (this task is the tracking record; name the removal condition,
      e.g. "experiment merged into `src/` or archived to a branch"), or
      **(b)** move the experiment out of the root (e.g. under `research/` or
      onto a dedicated branch) and leave the allowlist unchanged.
      (Owner chose **(b)** by removing the tree; see Status.)
- [x] Implement the chosen disposition in a minimal slice. (Implemented by
      the owner in `main` commit `17d9d29a`, a pure deletion.)
- [x] Confirm the `docs-validation` lane is green on the next `main` push.
      (Corrected in Status: the lane is PR-triggered; the strict gate is
      verified green locally on the post-removal tree, and PR lanes confirm
      once their merge refs contain `17d9d29a`.)

## Tests

- [x] `python3 tools/repo/check_root_hygiene.py --root . --strict` passes
      locally on the resulting tree.
- [x] `python3 tests/regression/tooling/Test.RootHygiene.py` passes.

## Docs

- [x] If (a): the allowlist entry carries a short comment naming this task
      as the removal owner; no other docs owed. (N/A — (b) chosen.)
- [x] If (b): update any references to the experiment's location. (None
      existed outside this task, its index entry, and the generated session
      brief; see Status.)

## Acceptance criteria

- [x] The strict root-hygiene gate is green on the `main` head. (Verified
      by running the strict check on a tree containing the `main` head
      `17d9d29a`; the CI lane itself is PR-triggered, see Status.)
- [x] No undocumented root entry remains; if an exception exists, it names
      this task and a removal condition. (No exception was added.)

## Verification

```bash
python3 tools/repo/check_root_hygiene.py --root . --strict
python3 tests/regression/tooling/Test.RootHygiene.py
```

## Forbidden changes

- Deleting or rewriting `src_new/` content without the owner's explicit
  direction.
- Weakening strict mode, skipping the check, or ignoring the entry via
  ignore-file tricks (`src_new/` is tracked content).
- Folding the BUG-157 Clang-20/glm repair into this task.
