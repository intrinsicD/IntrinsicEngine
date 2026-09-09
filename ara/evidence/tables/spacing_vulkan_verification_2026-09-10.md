# Point spacing / radius verification — 2026-09-10

C83 binds this bounded implementation result to the [source hashes and machine record](../diagnostics/spacing_vulkan_2026-09-10/record.json).

| Check | Observed result | Evidence |
| --- | --- | --- |
| Build | Clang 23 ci IntrinsicTests + ExtrinsicSandbox and ci-vulkan IntrinsicPointLBVHGpuTests succeed | [build summary](../diagnostics/spacing_vulkan_2026-09-10/build-summary.txt) |
| Analytical and legacy geometry | 11/11 focused geometry cases | [geometry](../diagnostics/spacing_vulkan_2026-09-10/geometry.txt) |
| Final focused CPU | 38/38; domains, config, cache, history, cancellation and shared window | [focused](../diagnostics/spacing_vulkan_2026-09-10/focused-cpu.txt) |
| Full CPU | 4409 selected; 4403 passes, six capability skips; 104.50 s | [summary](../diagnostics/spacing_vulkan_2026-09-10/cpu-summary.txt) |
| Native follow-up | Five window cases pass; 4408 distinct CPU passes, one expected unsanitized leak-control skip | [native](../diagnostics/spacing_vulkan_2026-09-10/native-cpu.txt) |
| Actual Vulkan | 1/1, 45.38 s, zero skips; RTX 3050 / 590.48.01; ASan+UBSan with existing leak exclusion | [Vulkan](../diagnostics/spacing_vulkan_2026-09-10/vulkan.txt) |
| Radius/spacing comparison | Maximum absolute error 0; tolerance 1e-5 | [schema-v2 smoke](../diagnostics/spacing_vulkan_2026-09-10/benchmark/runtime-smoke.json) |

The fixture has 66 stored rows per domain, one deleted row (two for halfedges),
seed 241 clustered points, coincidences and a far point. Cold/warm phases use
k=8, scale=1; a third phase uses k=63, scale=2. The dense case has 1030 stored
rows. Separate stale/cancel phases intentionally reject publication, producing
two prerequisite rejection logs. Radius and nearest-spacing aggregates are
compared with CPU octree. Scalar visualization has CPU recipe/window tests;
this is not a rendered splat-size readback.

Warm framed wall time is 9903.503186 ms versus CPU reference 95.184817 ms.
Cold time is 9439.411895 ms; scale-2/k63 time is 9850.964196 ms versus CPU
147.315945 ms. Each measured GPU phase uses 14 batches across eight requests.
Summed request neighborhood times overlap and are not wall time. This is a
dirty Debug smoke under display/frame pacing with concurrent compilation,
not a representative scaling benchmark or a performance improvement. CPU
remains the default. The schema-v2 result validates but is non-claim-eligible
for performance.

The source review removed an unnecessary generic algorithm-enum addition before
final verification; no generic enum values were changed. Initial focused/GPU
runs passed and are retained. The final CPU rebuild reflects the correction;
Vulkan reconciliation needed dependency scans only. No correctness tolerance or
benchmark threshold was relaxed.

Clean-workshop scorecard: rows 1–3 pass (imports/links, public value surfaces);
rows 4–6 N/A (no renderer member, pass or recipe dependency added); row 7 pass
(bounded runtime evidence, RUNTIME-222 explicitly owns model-space rendering);
row 8 pass (no layer exceptions). Scope, layering, tests and docs review passed.
[Automated bundle](../diagnostics/spacing_vulkan_2026-09-10/clean-workshop.txt).
Source-documentation audit has zero objective errors; advisory findings concern
existing broad interfaces/declaration comments. BUG-177 owns local .agents root
metadata, BUG-178 the ccache workaround, and BUG-180 leak-enabled GPU evidence.
No claim of whole-process leak freedom, splat coverage or full Framework24
parity is made. Work remains uncommitted.
