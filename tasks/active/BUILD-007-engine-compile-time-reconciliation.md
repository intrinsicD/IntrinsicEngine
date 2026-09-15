---
id: BUILD-007
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive measurement; benchmark manifests/results carry source identity, without unattended workflow custody.
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

## Selected comparison — 2026-09-15
The user requests measurement of the completed overnight improvements. Compare
`29d75ebe7` with `07a8b2914`, not all earlier simplification. Keep C92's broader
processing-family hypothesis separate. The frozen manifest is
`benchmarks/ci/manifests/engine_compile_iteration_overnight.yaml`.

Reuse `compile_hotspots` for source/physical-compiler attribution and the existing
schema-v2 sealer. A small serial driver owns only isolated source checkout,
command timing, log windows and sample orchestration. Its two focused regression
methods guard log contamination and parallel dependency-path accounting.

Use one detached source worktree and one disposable RAM-backed build tree;
existing checkout/build trees are untouched. Supported Clang23, Debug, Null/
headless, no sanitizers or compiler launcher, four jobs, identical preinstalled
vcpkg packages. Measure the ExtrinsicRuntime dependency closure (engine libraries),
not tests, Sandbox or Vulkan execution. One discarded baseline pilot establishes
resource footprint and probe validity; six samples use before/after/after/before/
before/after order, with identical input pre-reading. Cold build artifacts and
warm OS caches are distinct; no cold-filesystem claim.

Scenarios: clean build, settled no-op, timestamp-only invalidation of workspace,
recipe and config implementations, config-interface invalidation with measured
importer fan-out, and reconfigure plus resulting build. Source bytes stay at the
exact clean commit. Configure cost is separate. Record wall time, CPU time,
maximum single-process RSS (not simultaneous memory), physical compiler counts,
dominant sources, and the weighted Ninja dependency path including scans/archives.
Reject contaminated log windows or unmatched non-meta dependency outputs.

Report all three samples, medians/ranges and null/negative differences. Results
are descriptive local measurements, not publication-grade performance claims;
`claim_eligible` remains false. Claude reviews the protocol and fixed driver and
will review the resulting comparison. No overnight claim/work-graph machinery.

### Harness correction before the retained cohort
The first attempt passed all engine build/probe phases, then its log-window
check rejected CMake's legitimate Ninja log recompaction during reconfiguration.
This is a harness error, not an engine failure or a timing-based rejection.
The incomplete population remains under `/tmp/intrinsic-build007-measure/cohort-20260915`;
it contributes no retained comparison sample. Restrict compiler log windows to
build commands, preserve configure timing separately, and start a fresh six-sample
population with the same frozen manifest/order. Original logs and the corrected
runner revisions remain available; no failed attempt is overwritten.
