# Benchmark Review Checklist

Use this checklist when reviewing benchmark-related changes to avoid unreliable or misleading results.

## 1. Benchmark definition quality

- [ ] Dataset is explicitly declared and versioned/named.
- [ ] Parameters are explicitly declared (including defaults).
- [ ] Warmup policy is declared (iterations/time).
- [ ] Metrics are explicitly declared and meaningful for the method.
- [ ] Baseline is declared and comparable.

## 2. Output and validation quality

- [ ] Runner emits benchmark result JSON with required schema fields.
- [ ] Result JSON validates with `tools/benchmark/validate_benchmark_results.py`.
- [ ] Benchmark manifest validates with `tools/benchmark/validate_benchmark_manifests.py`.
- [ ] Result schema v2 binds the exact manifest hash, resolved params,
      config digest, warmup policy, source state, and declared thresholds.
- [ ] Metric leaves are finite numeric values; booleans and undeclared metric
      names are rejected.
- [ ] Reported status equals the recomputed execution/threshold disposition.
- [ ] Stable benchmark ID is not used as execution identity; run/attempt
      tuples are distinct and failed retries remain visible.

## 3. CI and execution policy

- [ ] PR smoke thresholds are reasonable and stable.
- [ ] Heavy/slow benchmarks are excluded from PR-fast and reserved for nightly/deep runs.
- [ ] Benchmark category labels and workflow routing are correct.

## 4. Scientific/engineering signal quality

- [ ] Quality/error metric is included when relevant (not runtime-only claims).
- [ ] Reported comparisons identify backend and dataset context.
- [ ] Any performance claim references baseline comparison and measurement conditions.
- [ ] Dirty, local, historical, or unverified source is explicitly
      non-claim-eligible.
- [ ] Claim-grade evidence has a frozen protocol, portable raw-result bundle,
      independent recomputation audit, and ARA claim row; a green smoke result
      is not promoted by itself.

## 5. Documentation and traceability

- [ ] Benchmark docs are updated (`docs/benchmarking/*`) when policy/manifests change.
- [ ] PR includes benchmark decisions in the Benchmarking section.
- [ ] Temporary benchmark shims/exceptions are tracked with removal task IDs.
