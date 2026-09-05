---
id: HARDEN-089
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts:
  - repo.source-documentation
---
# HARDEN-089 — Reconcile audit-window source comments and README prose

## Goal

- Make the source documentation introduced since the 2026-08-06 output audit
  satisfy the repository's current-state, what-and-why contract without
  changing executable behavior or public APIs.

## Non-goals

- No executable C++ change, module rename, declaration/signature change, or
  behavior/test-contract change.
- No repository-wide attempt to erase the inherited source-documentation
  inventory; this task is bounded to additions in the audited commit range.
- No automatic deletion of declaration comments or task references merely
  because the report-only scanner flags them.
- No new prose validator, warning, or CI gate.

## Context

- The 2026-09-05
  [agent-output audit](../../../docs/reports/2026-09-05-agent-output-audit.md)
  found five newly added module interfaces without mandatory leading synopses,
  six historical comment blocks in the largest touched runtime implementation,
  and current-state README sections phrased as task chronology or future work.
- The fixed audit window is
  `51e7faddad943ab7727e407d008e474ec076566d..3ee6a343d13c54dbd255e15ce254aa812d6dc194`.
  It contains 98 added lines under `src/` that mention a task ID; 32 are in
  `Runtime.GeometryProcessingOperations.Mesh.cpp`. Those lines form a bounded
  review inventory, not a mandate to remove every useful provenance link.
- `REVIEW-004` remains the standing Theme J P0 gate. On 2026-09-05 the
  operator explicitly requested that the next unattended task be a long
  cleanup/hygiene task, so this bounded Theme F exception is intentional and
  does not resume the paused research queue.

## Right-sizing

- Element: a cleanup could expand into bulk comment generation, a new linter,
  or a repository-wide README rewrite.
- Simpler alternative: correct only objective errors and manually confirmed
  current-state violations introduced in the fixed audit window.
- Blast radius: comments in five module interfaces and one runtime
  implementation, plus the relevant current-state runtime README passages and
  task/evidence records; no executable tokens.
- Reintroduction trigger: broader cleanup requires a separately measured,
  path-bounded finding. A new gate requires demonstrated recurring review
  failure and its own policy task.

## Required changes

- [ ] Add a concise leading what-and-why synopsis to:
      `Geometry.HalfedgeMesh.CurvatureSegmentation.Features.cppm`,
      `Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cppm`,
      `Geometry.HalfedgeMesh.CurvatureSegmentation.cppm`,
      `Geometry.HalfedgeMesh.Features.cppm`, and
      `Runtime.CurvatureSegmentationConfig.cppm`.
- [ ] Review all 98 audit-window source additions that mention a task ID.
      Preserve a reference only after the current invariant when the provenance
      materially helps; remove or rewrite chronology, status, and future-owner
      narration.
- [ ] Rewrite the six confirmed history-first blocks in
      `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp`
      (the topology signature, registration normals, mesh/registration/UV job
      identities, and UV publication seam) as concise present-state rationale.
- [ ] Rewrite the audit-window additions in `src/runtime/README.md` so the
      changed-count rule, curvature segmentation, ICP prerequisites, and
      per-operation diagnostic lifetime describe current behavior. Keep links
      to durable task evidence only where they add provenance after the rule.
- [ ] Review the final diff as documentation-only and confirm that no
      declaration, signature, expression, statement, include/import, literal
      consumed by code, or build input changed.

## Tests

- [ ] The focused source-documentation audit reports zero objective errors for
      the five new module interfaces.
- [ ] Report-only scans of the touched runtime implementation and README are
      manually reconciled against the fixed findings; unrelated inherited
      review prompts remain out of scope.
- [ ] The canonical Clang module build and CPU-supported CTest selector pass
      because comments in module interface units still traverse the real build
      graph.
- [ ] Touched-scope `pr-fast`, strict task/docs/layering/test-layout checks,
      and `git diff --check` pass.

## Docs

- [ ] Source comments and `src/runtime/README.md` are the documentation
      surface; do not copy their current-state contracts into architecture
      docs unless an actual architecture contract changes.
- [ ] Retire this task with standard-profile evidence, append the retirement
      narrative, and regenerate `tasks/SESSION-BRIEF.md`.

## Acceptance criteria

- [ ] All five audited module interfaces begin with a concise synopsis that
      states what the file contains and why its surface exists.
- [ ] Every one of the 98 added task-ID lines has an explicit keep/rewrite/drop
      disposition in the work record, and no retained source comment or README
      sentence substitutes task history for the current contract.
- [ ] The six confirmed runtime comment-history findings are absent while
      their load-bearing rationale remains next to the relevant implementation.
- [ ] The affected runtime README passages are factual current state; future
      work is linked as such rather than narrated as an in-progress feature.
- [ ] The final C++ diff is comment-only and all required build/test/structural
      gates pass.

## Verification

```bash
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --path src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Features.cppm --path src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.Patches.cppm --path src/geometry/Geometry.HalfedgeMesh.CurvatureSegmentation.cppm --path src/geometry/Geometry.HalfedgeMesh.Features.cppm --path src/runtime/Modules/CurvatureSegmentation/Runtime.CurvatureSegmentationConfig.cppm --summary
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --path src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp --path src/runtime/README.md --summary --no-fail
python3 tools/agents/skills/intrinsicengine-source-documentation/scripts/audit_source_documentation.py --root . --summary --no-fail
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --head-ref HEAD --preset ci-fast --preset-build-dir build/ci-fast --build-dir build/ci-fast --run
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
git diff --check
```

## Forbidden changes

- Modifying executable C++, public signatures, module/import topology, build
  files, tests, or runtime behavior.
- Bulk-generating comments or deleting human-review findings without reading
  the affected source.
- Rewriting source documentation outside the fixed audit window.
- Weakening the source-documentation policy or converting its report-only
  review prompts into a new mandatory gate.
