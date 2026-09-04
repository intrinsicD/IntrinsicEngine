---
id: BUG-166
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "Codex-BUG166"
branch: "codex/bug-166-historical-input-seals"
worktree: "/tmp/intrinsic-bug166"
claimed_at: "2026-09-04T23:28:23Z"
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-166 — Clean scratch experiments lose historical input seals

## Goal
- Keep a frozen non-claim scratch experiment with an exact clean source
  revision valid after later commits legitimately change its declared source
  inputs, while preserving fail-closed validation of live/dirty inputs and
  current evidence artifacts.

## Non-goals
- No regeneration, retuning, or widening of METHOD-037 or METHOD-038 evidence.
- No promotion of scratch evidence to claim eligibility and no weakening of
  task completion, bundle, audit, or artifact integrity checks.
- No engine, method, benchmark, runtime, config, UI, or backend change.

## Context
- Symptom: on 2026-09-04, repository-global workflow validation on unchanged
  `origin/main` rejected the retired METHOD-037 `run-003` and METHOD-038
  `scratch-011` custody. Their frozen input hashes still match the exact clean
  revisions recorded in each protocol/run, but later commits changed
  `CMakePresets.json` and METHOD-038's HalfedgeMesh implementation paths.
- Expected behavior: an exact clean source revision remains the authority for
  frozen protocol/run inputs even when the run is intentionally non-claim
  scratch evidence. Current post-run evidence remains current-tree-bound, and
  a missing, dirty, ambiguous, or unresolvable source identity fails closed.
- Impact: `ci-docs` fails every otherwise unrelated PR after legitimate source
  evolution, including BUG-164 PR #1035. Rewriting the frozen records to match
  current files would destroy their historical identity.
- On 2026-09-05, the operator requested a long cleanup/hygiene overnight task.
  BUG-166 was promoted first because hosted run `33928262543` reproduced the
  same METHOD-037/038 failures on BUG-165 PR #1036, making this hygiene repair
  a prerequisite for retiring the active bugs without weakening `ci-docs`.

## Observation ledger
- 2026-09-05 O1 — `experiment_custody.py validate --root .` reproduced ten
  current-tree hash errors across unchanged METHOD-037 `run-003` and METHOD-038
  `scratch-011`; this matched hosted `ci-docs` run `33928262543` exactly.
- 2026-09-05 O2 — both recorded source revisions resolve, are ancestors of
  `HEAD`, contain regular-file entries at every failing path, and reproduce all
  frozen SHA-256 values. This ruled out stale seals, missing Git objects,
  non-ancestor identities, and symlink substitution.
- 2026-09-05 O3 — the new clean non-claim scratch regression failed before the
  fix on both datasets, config, environment, implementation, and the declared
  input bundle link. The sibling claim-eligible regression already passed,
  isolating `_claim_source_revision()` as the discriminating branch.
- 2026-09-05 O4 — deriving declared-input history from a clean exact ancestor
  made the new scratch regression and unchanged METHOD-037/038 records pass;
  focused dirty-input, symlink, unresolved/non-ancestor, claim-substitution,
  and current-evidence tamper probes remained fail-closed.

## Required changes
- [x] Diagnose why clean exact-revision input fallback is gated on
      `claim_eligible: true` rather than the validity of the recorded source
      identity, and define the narrow historical rule in the canonical
      workflow-evidence policy.
- [x] Validate frozen protocol/run config, environment, dataset, and
      implementation inputs against their exact recorded clean revision for
      both claim-eligible and non-claim runs.
- [x] Retain live-worktree validation for dirty or non-exact source identities
      and current-tree validation for raw rows, results, receipts, previews,
      bundles, audits, and other post-run evidence.

## Tests
- [x] Add a hermetic regression with a frozen clean non-claim scratch run,
      commit later changes to every declared source-input class, and prove
      global plus completion validation still use the original revision.
- [x] Prove current evidence tampering, dirty-source input changes, missing or
      non-ancestor revisions, and claim-eligibility substitution still fail.

## Docs
- [x] Update `docs/agent/workflow-evidence.md` and regenerate the mirrored
      workflow skill if the historical source-input rule changes.

## Acceptance criteria
- [x] Unmodified METHOD-037 and METHOD-038 custody validates after later source
      evolution without rewriting either experiment record.
- [x] Historical lookup is limited to repository-relative regular files in an
      exact clean Git revision and does not authorize a research claim.
- [x] Experiment-custody regressions and repository-global workflow evidence
      validation pass with no integrity check skipped.

## Verification
```bash
python3 tests/regression/tooling/Test.ExperimentCustody.py
python3 tools/agents/experiment_custody.py validate --root .
python3 tools/agents/workflow_evidence.py validate --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```

## Forbidden changes
- Rewriting historical protocols, runs, bundles, audits, or source hashes to
  match today's tree.
- Treating scratch evidence as claim eligible or permitting current evidence
  artifacts to fall back silently to an old source revision.
