# Benchmark Baselines

Baselines provide comparison anchors for benchmark regressions and improvements.

## Baseline rules

- Store baselines in `benchmarks/baselines/` as machine-readable JSON.
- Tie baseline snapshots to commits or releases.
- Do not claim improvements without before/after comparison.
- Record tolerance windows for noisy metrics.

## Update policy

- Baseline updates must state reason (hardware change, algorithm change, measurement fix).
- Baseline-only updates should avoid mixing unrelated code changes.
