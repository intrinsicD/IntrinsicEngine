---
id: BUG-126
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "Codex-GeometryE2E"
branch: "feature/lop-consolidation-e2e"
worktree: "/tmp/intrinsic-geometry-e2e.GJlhXS"
claimed_at: "2026-08-01T15:37:57Z"
---
# BUG-126 — Claim custody validates historical source seals against the current worktree

## Goal
- Keep a clean claim-eligible experiment auditable after a later commit changes
  one of its sealed source/config/dataset/implementation files, while retaining
  current-tree tamper detection for run-time evidence artifacts.

## Non-goals
- Do not loosen protocol digests, task hashes, run bindings, result hashes,
  bundle summaries/gates, smoke receipts, previews, or audit identity.
- Do not rewrite or reseal historical METHOD-016/METHOD-017 evidence.
- Do not add a general artifact store or copy source inputs into evidence roots.

## Context
- Extending the shared consolidation source for METHOD-017 makes the global
  custody gate reject METHOD-016 even though METHOD-016 binds an exact clean
  40-hex source revision and its sealed blobs remain present at that revision.
- `validate_protocol_data`, `_validate_run_bindings`, and bundle-link validation
  currently hash repository paths only in the current worktree. Only the
  implementation-specific secondary check uses `sha256_at_revision`.
- Claim-grade policy treats the exact clean source revision as the immutable
  identity for declared datasets, config, environment, and implementation.
  Evidence produced after that revision remains current-tree state and must not
  receive a historical fallback.

## Required changes
- [ ] Resolve the exact historical source revision only for clean,
      claim-eligible protocol/run input seals.
- [ ] Validate protocol datasets, config, environment, and implementation at
      that revision using lexical Git-tree paths.
- [ ] For bundle links, accept the current file when its hash matches; otherwise
      accept a matching source-revision blob only for a clean claim-eligible
      run. Keep previews and other post-run evidence current-only.
- [ ] Preserve current-tree validation for non-claim-eligible and dirty-source
      workflows, and preserve all task/protocol/run/result/audit bindings.

## Tests
- [ ] Add a regression that completes and audits a clean claim-grade run,
      changes/commits its sealed source inputs, and still validates the
      historical run and mixed source/evidence bundle links.
- [ ] Keep current-tree seal drift detectable for non-claim-eligible runs and
      keep evidence-link tampering detectable for claim-eligible runs.
- [ ] Run the complete experiment-custody and workflow-evidence regression
      suites.

## Docs
- [ ] Clarify historical input-seal versus current evidence-artifact validation
      in the canonical workflow-evidence policy and regenerate its skill mirror.
- [ ] Record the observed METHOD-016 reproduction and exact fixed gates here;
      update the bug index/session brief/retirement log on closure.

## Acceptance criteria
- [ ] Global `experiment_custody.py validate` accepts METHOD-016 and METHOD-017
      after METHOD-017 extends their shared source files.
- [ ] A changed historical source declaration or changed current evidence
      artifact still fails closed with an actionable diagnostic.
- [ ] Strict task/docs/layering checks remain green.

## Verification
```bash
python3 tests/regression/tooling/Test.ExperimentCustody.py
python3 tests/regression/tooling/Test.WorkflowEvidence.py
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Falling back to an arbitrary current file when an exact historical source
  revision is declared.
- Applying source-revision fallback to raw rows, results, receipts, previews,
  audits, handoffs, reviews, or other evidence created after the source commit.
- Silencing the global gate or deleting the older valid claim-grade run.
