---
id: RORG-134
theme: none
depends_on: []
workflow_schema: 1
workflow_profile: high-risk
evidence: required
owner: "codex-reuse"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-09-11T00:55:25Z"
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence]
---
# RORG-134 — Complete the bounded code-reuse audit run

## Goal
Resolve remaining items in [the reuse audit](../../docs/reports/2026-09-08-code-reuse-audit.md) through behavior-preserving consolidation or evidence-backed decisions to retain distinct implementations, stopping at 08:00 Europe/Berlin on 2026-09-11 (06:00 UTC).

## Non-goals
- Feature additions, changed layer policy, algorithm changes, performance claims, or automatic publication.

## Context
- Status: complete. Implementation committed; source and follow-up integration reviewed with Claude Code CLI.
- The operator explicitly directs this cleanup outside the standing convergence work-selection default and authorizes sending cleanup diffs to Claude.
- The existing uncommitted cleanup is expected and must be preserved. One writer owns this checkout and build tree.
- R01–R38 are resolved. R06 retains distinct snapshots and R24 retains distinct lifecycle branches after sharing their common policies. No numbered item remains.
- Verification: ci and ci-vulkan IntrinsicTests builds; final CPU gate 4,508 selected with zero failures and one expected skip; all 84 Vulkan tests passed; 116 workflow regressions passed. Per-batch receipts and the audit record preserve earlier findings and corrections.

## Required changes
- [x] Inspect and resolve R06, R15, R17–R27 and R29–R38 in bounded batches; keep an explicit disposition for unfinished items at the time limit.
- [x] Ask Claude Code CLI to review each fixed batch and fix verified findings before proceeding.
- [x] Reuse existing ownership and files wherever practical; preserve diagnostics, ordering, binary layouts and test oracles.

## Slice plan
One bounded implementation cycle contains independently reviewed batches: geometry parsing/numerics and fingerprints; runtime/upload helpers; renderer/backend lifecycle; shader common code; benchmark/test/tooling helpers. Each batch is frozen during compilation and CLI review. The final graph freeze and evidence bind the combined tree. Stop starting risky work early enough to complete final verification before the deadline.

## Tests
- [x] Run relevant existing tests and add contract regressions only for meaningful uncovered behavior.
- [x] Build IntrinsicCpuTests with ci and run the CPU selector after each reviewed batch; retain the completed IntrinsicTests checkpoints.
- [x] Run shader compilation and applicable Vulkan coverage when shader or backend paths change; do not infer GPU behavior from CPU tests.

## Docs
- [x] Record each resolved audit item, reviewed digest, findings, fixes and verification in the existing audit report.
- [x] Refresh affected architecture documentation and module inventory when surfaces change.
- [x] Record a final status with any remaining work and the actual stopping condition.

## Acceptance criteria
- [x] The run ends because the audit list is resolved or the specified deadline has arrived; remaining items are explicitly listed.
- [x] Completed batches preserve existing behavior, pass applicable checks and have Claude review findings resolved.
- [x] Existing uncommitted changes remain intact and the final worktree has a documented verification state.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicCpuTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/workflow_evidence.py validate --root .
git diff --check
```

## Forbidden changes
- Discarding existing work, overlapping writers, mixing unrelated features, or weakening tests/CI to obtain green results.
- Merging semantically different algorithms or state lifecycles solely because their syntax resembles one another.

## Stopping condition
All 38 numbered audit items were resolved and verified before 08:00
Europe/Berlin on 2026-09-11. No implementation item remains. The operator subsequently authorized commit, integration into main and push.
Existing conditional audit candidates retain their explicit scope decisions.

## Completion
Completed 2026-09-11. Implementation commit: `dd18432a8b3b5690d5f8bb36e1881b995634a378`.
The committed cleanup resolves all 38 numbered items; no new backend maturity
claim is made. Final retirement evidence is bound by the report and historical
seal under `tasks/evidence/RORG-134/`.
