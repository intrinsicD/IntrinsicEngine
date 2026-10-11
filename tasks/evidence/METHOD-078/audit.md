# METHOD-078 — Nyström retry diagnosis

Question: “Dann bitte nyström Nutzung untersuchen und verbessern”.

**Do not adopt unconditional retries.** C119 records the bounded result. Both variants retain their accepted-iteration counts and identical endpoint buffers; added retries only repeat target-intermediate validity failures. The production candidate and probes have been removed; the exact experiment patch is preserved in [candidate.patch](candidate.patch).

| Two repeats, 20 iterations | Points/fixed | Box/resampled |
| --- | ---: | ---: |
| Accepted Nyström iterations, baseline → candidate | 6 → 6 | 1 → 1 |
| Attempts, baseline → candidate | 9 → 22 | 4 → 40 |
| Target-intermediate rejections | 3 → 16 | 2 → 38 |
| Cached suppressions | 13 → 0 | 18 → 0 |
| Baseline registration-call seconds | 151.221–151.399 | 201.525–202.195 |
| Candidate registration-call seconds | 159.070–160.567 | 216.216–216.727 |
| RMS / max versus Auto, world units | 5.079e-7 / 1.297e-6 | 8.098e-5 / 2.124e-4 |

Both repeats give the same counts and xyz values. The frozen screen requires slowest-candidate / fastest-baseline < 0.9 in both variants; observed ratios are 1.06180 and 1.07543. These are local diagnostic timings, not a sealed benchmark speed claim.

## What causes fallback

Points first has a rejected attempt at draw 3, then succeeds after increasing landmarks. At draw 6, 1024 landmarks fail target validity and the next doubling reaches the cost cap; the remaining 13 draws are suppressed in the baseline. Box first exceeds the sampled error limit (0.0140272 > 0.001), then succeeds at a larger count. Draw 1 fails target validity and reaches the cost cap; the remaining 18 baseline draws are suppressed. Unconditionally retrying these later draws produces only further target-validity failures. The log identifies a non-positive **or non-finite** shifted target sum, not its value or exact numerical cause. Hitting the cost limit does not prove a larger landmark set would succeed.

The sigma-only cache can suppress a viable later approximation in the separate deterministic moved-source counterexample. That counterexample does not reproduce this registration's trajectory. The actual pilot does not support removing the cache as a performance fix.

## Controls and evidence

Auto and the cost-ineligible control return identical positions and zero accepted Nyström iterations. The deliberately permissive approximation differs from Auto by RMS 0.90495855 (required >0.001). It has five accepted E-step evaluations but only four completed iterations: convergence is checked before the final M-step. Attempt counts and completed-iteration counts therefore differ legitimately.

All eleven cells completed without technical failure. Full xyz buffers independently reproduce every recorded RMS/max. All repetitions, including those beyond the first timing failure returned by `assessed`, were independently checked. The original frozen analysis and raw report remain unchanged.

- [Frozen report, curves, phase boundaries and before/after views](runs/report.html)
- [Raw-data hashes](raw-sha256.txt), SHA256 `c87f162607b11b3ff966c02263b09a7ea2e6dad637a9dddad2c50cf84546bac8`
- [Machine-readable audit](audit-record.json)
- [Preflight packet](preflight/pilot/layer-a.md), commit `0efb7e48f87ee57fd5cb34f2c5d072fb71391d3e`, digest `e75cf4c61d104b0529f5f1ba1e24a0e8eed6728d1c55053e6ef396aae2f2f229`
- [Independent preflight approvals](reviews/approvals.json)
- [Independent results audit](reviews/results-audit.md)

Scope: CPU, one synthetic fixture/seed, two variants, 20 iterations, no completed-registration accuracy or general speedup claim. The old METHOD-077 evidence is untouched. A next investigation would examine why the shifted target sums fail positivity/finiteness; neither larger landmark budgets nor altered tolerances are authorized by this packet.
