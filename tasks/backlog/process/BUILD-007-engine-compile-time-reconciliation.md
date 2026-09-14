---
id: BUILD-007
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive cleanup and follow-up tracking; no new performance or capability claim.
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUILD-007 — Measure matched engine-source compile iteration

## Goal
Measure whether the completed engine simplification reduces full and incremental
compile iteration under matched conditions. C92 remains a hypothesis; dependency
closure reductions and historical dirty single samples do not establish speedup.
This task compares engine source. BUILD-006 separately compares build backends
and cache behavior while keeping engine source fixed.

## Acceptance criteria
- [ ] Select real baseline/current source identities and preserve equivalent features.
- [ ] Reuse the existing compile-hotspot and benchmark tooling; declare stable IDs,
  manifests and explicit cache/toolchain/source identities before timed runs.
- [ ] Repeat full reconciliation and representative implementation/interface edits
  under matched conditions without competing builds or timed tests.
- [ ] Compare dominant producers, critical path, memory and wall time; retain null
  or negative results and update C92 only when eligible evidence supports it.

## Verification
```bash
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/benchmark/validate_benchmark_results.py
python3 tools/agents/check_ara_claims.py --root . --strict
```
Read the benchmark skill before designing measurements. Preserve BUG-178 cache
limitations; use a consistent supported Clang toolchain and preset identity.
