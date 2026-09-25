---
id: BUG-222
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive recording of an unrelated benchmark failure; no performance claim.
contract_schema: 1
contracts: []
contract_review: Benchmark baseline diagnosis under the existing benchmark workflow; no new engine or integration contract.
---
# BUG-222 — UV atlas smoke baseline output mismatch

## Goal

Diagnose the output-contract mismatch in
`geometry.uv_atlas.fast_staged_edge_grouping.scaling` without silently replacing
the baseline or weakening its quality gate.

## Evidence

Observed on 2026-09-25 at `6dd78b971` plus the property-smoothing and pre-existing
scalar-gradient/vector-field working changes. The all-workload
`IntrinsicBenchmarkSmoke` returned 2. Its new property-smoothing workload passed;
the atlas scaling workload failed its output signature / quality-vector contract.
The atlas kernel, workload and baseline were not changed by property smoothing.
This identifies an unrelated failing workload, not a proven root cause.

- Expected output signature: `5684639256857304174` (baseline `8ca52438`).
- Observed output signature: `13353140223134595692`.
- Quality-vector L2 difference: `0.38978686878190383`.
- Large-case topology is deterministic, finite and reports success; zero flipped
  faces. This does not satisfy the distinct baseline-output gate.
- [Sealed local result](../../evidence/BUG-222/uv-atlas-result.json.gz).
- Full local runner log: `/tmp/property-smoothing-benchmarks.log`.

## Acceptance criteria

- [ ] Reproduce on an isolated source revision and identify the first relevant atlas change.
- [ ] Decide whether the output is a regression or an intentionally changed contract, with numerical evidence.
- [ ] Repair the implementation or update the reviewed baseline protocol without weakening unrelated gates.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
python3 tools/benchmark/run_and_seal.py --executable build/ci/bin/IntrinsicBenchmarkSmoke --output /tmp/bug222-benchmarks --manifests-root benchmarks --run-id bug222-reproduction
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/bug222-benchmarks --manifests-root benchmarks --strict
```
