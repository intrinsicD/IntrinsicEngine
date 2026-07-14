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
