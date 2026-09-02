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
- BUG-156's follow-on runtime slice now publishes minimum and maximum principal
  curvature atomically with mean/Gaussian curvature and optional directions;
  its canonical CPU, sanitizer, and structural gates pass.
- Commit `a9706f332` is the final integrated source revision. It was pushed
  without force and fetched back; local `main`, `origin/main`, and `FETCH_HEAD`
  resolve identically, and all recorded pre-integration tips remain ancestors.
- BUG-162 remains active solely for label-distinct independent fixed-surface
  review. BUG-156 separately retains that review for its public numerical and
  runtime-publication contract.

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
- UI: unchanged existing Mesh / Processing / Curvature command; all four scalar
  fields publish through its existing result path.
- Agent/CLI: Git integration and repository validators only.

## Backends

- Backend axis: not applicable. The merged production change is one
  deterministic CPU geometry implementation; no GPU token or fallback changes.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Preserve BUG-156's oriented finite triangle-surface contract. |
| Compatible entity sources | Preserve any owning halfedge mesh satisfying the selected adjacency/property contract. |
| RuntimeModule | Keep the existing curvature operation; synchronous and queued paths persist all four scalar fields plus optional directions in one transaction. |
| Config/agent | Preserve the existing fixed command and validated runtime path. |
| UI | Preserve Mesh / Processing / Curvature; UI-050 owns vector-field visibility. |
| Publication | Geometry owns min/max/mean/Gaussian plus directions; runtime publishes all four scalars atomically with stale-state rejection and undo/redo. |
| End-to-end tests | Use the merged geometry/runtime selectors and complete CPU-supported gate. |

## Required changes

- [x] Merge every local branch tip into `main` without discarding branch
      history or unrelated user work.
- [x] Reconcile ARA identifier collisions and duplicate task ownership.
- [x] Synchronize current numerical, runtime, convergence, task, and evidence
      wording with the merged implementation and actual publication surface.
- [x] Keep local Framework24 differential numbers explicitly non-claim-eligible
      and delegate formal matched evidence to BENCH-001.
- [x] Create the final integration commit and push it to `origin/main` without force.

## Tests

- [x] Configure with the canonical `ci` preset and build `IntrinsicTests` with
      the selected Clang 23 toolchain.
- [x] Pass the 75-case curvature/runtime selector and all 4,257 CPU-supported
      tests under the repository exclusion policy.
- [x] Pass isolated ASan and UBSan selectors with 2,745 registrations each.
- [x] Pass strict task, ARA, layering, test-layout, root-hygiene,
      workflow-evidence, and documentation validation.
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
- [ ] Independent fixed-surface review accepts the exact high-risk integration
      surface; BUG-156 retains separate review ownership for its public contract.
- [x] Local and fetched `origin/main` resolve to the same pushed commit.

## Verification

Recorded on 2026-09-02:

- `a9706f33252a4928eb2bb8f5672279b29cf130a2` was pushed to `origin/main`
  without force and a fresh fetch resolved `HEAD`, `origin/main`, and
  `FETCH_HEAD` to that exact revision.
- `828414d`, `a72ef79`, `6783b3d`, and `0dece16` remain ancestors of `HEAD`.
- The committed strict docs-sync gate and post-commit diff checks passed.

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
- No engine-backend `Operational` follow-up is owed by this integration task.
