# Local distance-ratio verification

Implementation commit: `0dbff4609b3de9103f5fb36c184dd225d846285e`. Source hashes, receipt bindings and limits:
[record](../diagnostics/distance_ratio_vulkan_2026-09-10/record.json).

| Evidence | Result |
| --- | --- |
| CPU reference | Three new analytic/candidate/invalid-input cases plus seven existing outlier-score cases pass. |
| Runtime/editor | 44 focused cases pass; full final CPU selection includes the expanded shared config preview/apply/run case. |
| Full CPU | 4,413 selected, zero failures, six capability skips; five native-window follow-ups pass, leaving one expected unsanitized leak-control skip. |
| Actual Vulkan | New ratio case and existing statistical/radius regression pass. Ratio masks match exactly and score error is zero under the 1e-5 bound across eight domains, k=8 and k=63, deleted slots, cache/history, stale/cancel and dense coincidence. |
| Diagnostic smoke | Warm framed Vulkan 1095.307927 ms; CPU reference 100.417883 ms. One warmup and one measured iteration; no performance conclusion. |

The named input and same-domain output contracts are documented in
[outlier analysis](../../../docs/architecture/outlier-analysis.md).
The [schema-v2 smoke](../diagnostics/distance_ratio_vulkan_2026-09-10/benchmark/runtime-smoke.json)
is dirty-source diagnostic evidence. Score reduction stays on CPU. BUG-180 owns
the GPU LeakSanitizer exclusion. This does not establish full Framework24
covariance probability, LOF/LoOP, general numerical proof or a new rendered readback.
