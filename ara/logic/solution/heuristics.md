# Heuristics

## H01: Robust paired timing for short comparative smokes
- **Rationale**: Individually time an odd number of interleaved baseline/probe
  pairs and gate on the median paired ratio so a minority of host-scheduling
  stalls cannot flip a strict threshold; retain every raw sample so the robust
  statistic does not hide contamination or sustained regressions.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Sensitivity**: medium
- **Code ref**: [benchmarks/geometry/Bench_UvAtlasSmoke.cpp,
  benchmarks/geometry/manifests/geometry_uv_atlas_fast_staged_promotion_smoke.yaml,
  docs/benchmarking/metrics.md]
- **From staging**: O11

## H02: Rebaseline CI cache cohorts when gate shape drifts
- **Rationale**: Keep cold and warm populations on the same workflow generation,
  commit, runner image, compiler, preset, sanitizer, selected-test count, Ninja
  edge count, and dependency-cache state. If the historical baseline differs on
  those dimensions, collect a contemporaneous cold cohort rather than attributing
  unrelated gate drift to the cache.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Sensitivity**: high
- **Code ref**: [tasks/archive/CI-007-module-safe-persistent-ccache-pilot.md,
  docs/benchmarking/ci-policy.md,
  benchmarks/baselines/ci_gate_latency_github_ubuntu_24_04_v1.json]
- **From staging**: O12

## H03: Aggregate Every Measured Comparative-Smoke Iteration
- **Rationale**: A repeated comparative correctness smoke should AND status
  across every measured iteration, report failed-iteration count and worst
  quality/error, enforce its declared runtime budget, and name diagnostics by
  the invariant actually counted. A final successful sample must not conceal
  an earlier failure or an exceeded smoke budget.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Sensitivity**: medium
- **Code ref**: [benchmarks/geometry/Bench.SimplificationQualitySmoke.hpp,
  benchmarks/geometry/Bench_SimplificationQualitySmoke.cpp,
  benchmarks/geometry/manifests/geometry_simplification_fa_qem_quality_smoke.yaml,
  tasks/done/GEOM-014-feature-aware-quadric-error-simplification.md,
  N211, N212]
- **From staging**: O29

## H04: Validate Every Persisted Strategy Record
- **Rationale**: When serialization persists all strategy parameter records,
  edit-time validation should validate inactive records as well as the selected
  one; otherwise an invalid inactive enum can silently become a fallback token
  during serialization and break faithful round trips.
- **Provenance**: ai-executed
- **Crystallized via**: artifact-commitment
- **Sensitivity**: medium
- **Code ref**: [src/core/Core.Config.EngineLoad.cpp,
  src/runtime/Runtime.EngineConfigControl.cpp,
  tests/contract/runtime/Test.ParameterizationFacade.cpp,
  tasks/done/RUNTIME-176-parameterization-runtime-config-integration.md,
  N214, N215]
- **From staging**: O35

## H05: Price hierarchy layouts by expected memory traffic before GPU adoption
- **Rationale**: Compare engine-owned BVH and cluster-hierarchy layouts using
  expected bytes or cache lines per query, not node count alone. A deterministic
  CPU packing or trace study can reject unhelpful merged-node layouts before any
  Vulkan integration, while opaque vendor acceleration structures remain out of
  scope for custom physical layouts.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Sensitivity**: high
- **Code ref**: [tasks/backlog/geometry/GEOM-067-memory-aware-bvh-merged-node-evidence.md,
  tasks/backlog/rendering/GRAPHICS-125-memory-priced-cluster-hierarchy-evidence.md]
- **From staging**: O41

## H06: Establish test truth before optimizing CI gate shape
- **Rationale**: Mechanically reconcile unique GoogleTest cases, source
  ownership, capability labels, build aggregates, and clean-configure target
  identity before touched-scope routing, slow-lane splitting, coverage
  comparison, or grouped execution. Otherwise a faster selector can preserve or
  enlarge an existing coverage hole while reporting success.
- **Provenance**: ai-suggested
- **Crystallized via**: artifact-commitment
- **Sensitivity**: high
- **Code ref**: [tasks/backlog/bugs/BUG-106-test-gate-capability-routing-drift.md,
  tasks/backlog/bugs/BUG-107-backend-target-graph-configure-history.md,
  tasks/backlog/process/CI-005-real-touched-scope-pr-fast-gate.md,
  tasks/backlog/process/CI-010-cpu-source-coverage-refactor-parity.md, N237,
  N238, N239]
- **From staging**: O48
