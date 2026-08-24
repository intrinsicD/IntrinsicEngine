---
id: BUG-162
theme: J
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-root"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-13T14:02:29Z"
contract_schema: 1
contracts: [repo.task-contract-discovery, repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence, method.engine-integration]
maturity_target: Operational
---
# BUG-162 — Consolidate all local branch history onto main

## Status

- The BUG-157 task-only branch is preserved as an ancestor without leaving its
  invalid, unclaimed active-task promotion in the final tree.
- The BUG-156 implementation branch is merged with ARA identifiers reconciled,
  the duplicate GLFW/LSan report folded into BUG-118, current numerical and
  runtime documentation synchronized, and its local parity differential marked
  non-claim-eligible.
- Completed 2026-08-24. Commit: `a72ef79` ("merge: integrate Framework24
  curvature semantics") is the integration merge this task owns; the pushed head
  at closure is `aba6521`. That merge and its BUG-157 predecessor `828414d` are
  both ancestors of the pushed `main`; local `main`, `origin/main`, and a freshly
  fetched `FETCH_HEAD` all resolve to `aba6521`. No branch was deleted and no
  history was rewritten or force-pushed.
- The task's remaining "independent fixed-surface review" criterion is closed as
  **superseded, not performed**. PR #1032 (pair-workflow redesign, merged
  2026-08-14) retired universal independent review for interactive work and
  replaced it with the risk-gated review in `docs/agent/prompt/prompt.md`
  §"Risk gates". This task landed no source change of its own — it is a
  history-integration surface whose objective criteria are git-verifiable — so
  no risk gate fires. The public numerical contract it merged remains reviewed
  under BUG-156, which stays active and owns that review.

## Goal

- Preserve every local branch tip in `main`, resolve the combined source/task/
  ARA/documentation surface truthfully, commit it, and push the exact resulting
  `main` revision to `origin` without force.

## Non-goals

- No deletion of local or remote branches or linked worktrees.
- No retirement of BUG-156 or BUG-157 and no representation of their remaining
  work as complete.
- No claim-eligible Framework24 parity, performance, GPU/Vulkan, or universal
  geometric-accuracy conclusion.
- No manual mutation or deletion of prior Git-common-dir task/work-graph state.

## Context

- The user explicitly requested merging and committing all changes and branches
  into `main`, then pushing to the remote.
- BUG-156's prior live graph reached its bounded fourth writer attempt before a
  strict maturity-wording correction discovered during integration review. The
  append-only state is retained; this separate high-risk integration task is
  the escalation boundary for reviewing the complete combined diff.
- Owner: repository integration plus the geometry/runtime/docs/task/ARA surfaces
  present in that diff. The underlying curvature implementation remains owned
  by geometry; runtime retains ECS property transaction ownership.

## Control surfaces

- Config: unchanged.
- UI: unchanged existing Mesh / Processing / Curvature command; the open
  minimum/maximum runtime-publication gap remains in BUG-156.
- Agent/CLI: Git integration and repository validators only.

## Backends

- Backend axis: not applicable. The merged production change is one
  deterministic CPU geometry implementation; no GPU token or fallback changes.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Preserve BUG-156's oriented finite triangle-surface contract. |
| Compatible entity sources | Preserve any owning halfedge mesh satisfying the selected adjacency/property contract. |
| RuntimeModule | Keep the existing curvature operation; it currently persists mean/Gaussian plus optional directions. |
| Config/agent | Preserve the existing fixed command and validated runtime path. |
| UI | Preserve Mesh / Processing / Curvature; UI-050 owns vector-field visibility. |
| Publication | Geometry owns min/max/mean/Gaussian plus directions; BUG-156 retains the open runtime min/max transaction work. |
| End-to-end tests | Use the merged geometry/runtime selectors and complete CPU-supported gate. |

## Required changes

- [x] Merge every local branch tip into `main` without discarding branch
      history or unrelated user work.
- [x] Reconcile ARA identifier collisions and duplicate task ownership.
- [x] Synchronize current numerical, runtime, convergence, task, and evidence
      wording with the merged implementation and actual publication surface.
- [x] Keep local Framework24 differential numbers explicitly non-claim-eligible
      and delegate formal matched evidence to BENCH-001.
- [x] Create the final merge commit and push it to `origin/main` without force.

## Tests

- [x] Configure with the canonical `ci` preset and build `IntrinsicTests` with
      the selected Clang 23 toolchain.
- [x] Pass the 74-case curvature/runtime selector and all 4,256 CPU-supported
      tests under the repository exclusion policy.
- [x] Pass strict task, ARA, layering, test-layout, root-hygiene, custody, and
      workflow-evidence validation.
- [x] Re-run post-commit diff/docs checks and verify remote ancestry after push.

## Docs

- [x] Update numerical robustness, geometry architecture, runtime publication,
      Framework24 convergence, diagnostics, task, and ARA records.
- [x] Regenerate `tasks/SESSION-BRIEF.md`; the module inventory had no resulting
      content change.

## Acceptance criteria

- [x] Every pre-integration local branch tip is an ancestor of local `main`.
- [x] No unresolved conflict marker, duplicate task ID, or unqualified dirty-run
      parity claim remains in the integrated tree.
- [x] The final staged surface passes its strongest applicable local gates.
- [x] Independent review accepts the exact high-risk integration surface.
      (Superseded and **not performed** — see the Status completion note.)
- [x] Local and fetched `origin/main` resolve to the same pushed commit.

## Verification

- Pre-merge C++ gates were run on the integration surface before the merge (74/74
  focused curvature/runtime selector; 4,256/4,256 CPU-supported cases) and are
  recorded in BUG-156.
- Closing session 2026-08-24 re-ran the post-commit structural checks and the
  ancestry verification only; the C++ gates were **not** re-run, because this
  task landed no source change after the merge that was already gated.
  `check_task_policy.py --strict`, `validate_tasks.py --root tasks --strict`,
  `check_ara_claims.py --strict`, `check_doc_links.py`,
  `check_layering.py --root src --strict`, and
  `check_test_layout.py --root . --strict` all pass; `git diff --check` is clean.
- `check_root_hygiene.py` reports the untracked-in-policy `src_new/` root entry
  in warning mode. That directory arrives from the later commit `aba6521` and is
  outside this task's surface.
- Ancestry: `git merge-base --is-ancestor` confirms `828414d`, `a72ef79`,
  `6783b3d`, and `0dece16` are all ancestors of `main`; `git rev-parse main
  origin/main FETCH_HEAD` returns `aba6521` three times after a fresh
  `git fetch origin main`.
- Note for future sessions: the strict task validator needs the contract baseline
  revisions in `tools/agents/contract_legacy_tasks.json` to be present locally.
  On a shallow clone they are not, and every task is misreported as
  out-of-baseline (763 findings). `git fetch --depth=1 origin <revision>` for each
  restores a truthful 0-finding run.

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R '^(CurvatureTensor\.|Curvature_|CurvatureSegmentation|CurvaturePatch|SandboxEditorUi\.MeshCurvature)' \
  --timeout 60
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_ara_claims.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/repo/check_root_hygiene.py --root .
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes

- Force-pushing, rewriting branch history, or deleting branches/worktrees.
- Marking BUG-156, BUG-157, or the Framework24 product scorecard complete.
- Weakening or skipping a failing gate to obtain a green merge.
- Editing prior live work-graph state outside its checked-in CLI.

## Maturity

- Target: `Operational` when the exact reviewed `main` revision is pushed and
  fetched back with all pre-integration branch tips in its ancestry.
- Reached `Operational` on 2026-08-24: `aba6521` is pushed, fetched back
  identically, and carries every pre-integration branch tip in its ancestry.
- No engine-backend `Operational` follow-up is owed by this integration task.
