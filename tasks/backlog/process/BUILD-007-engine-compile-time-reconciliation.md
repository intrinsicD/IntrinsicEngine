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

## Measurement preparation — 2026-09-15
Claude and root reviewed the existing compile-hotspot reporting and benchmark
sealing/validation tools. `compile_hotspots.py` analyzes Ninja/compiler records;
it does not yet orchestrate matched repeated clean and incremental builds.
Choose the smallest runner that reuses these owners rather than another framework.

The overnight baseline `29d75ebe7` and a clean post-cleanup commit can support a
narrow comparison of this night's feature-preserving edits; they do not measure
all earlier engine simplification. Select the broader baseline separately if that
is the intended claim. Pin exact source, Clang/toolchain, preset, jobs, cache and
scenario identities in the manifest before timing, and retain negative results.

The current host has about 4.2 GiB free; existing ci/asan/ubsan/vulkan trees occupy
roughly 11/8.5/11/18 GiB. Establish a source/build-isolation plan that fits measured
headroom before allocating another tree. Existing preset dependency paths are
source-relative. Run timing without competing verification builds or tests;
ordinary cleanup build receipts and historical unmatched timings are not evidence
of improvement. No timed comparison was started during RUNTIME-255.
