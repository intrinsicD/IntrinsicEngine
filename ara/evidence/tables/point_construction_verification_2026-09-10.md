# Point construction verification

C91 is bound to the [source hashes and executed receipts](../diagnostics/point_construction_2026-09-10/record.json).

| Check | Observed result and scope |
| --- | --- |
| CPU | 4500 passed, zero failed, one expected unsanitized leak-control skip; 4501 selected. Includes the new config/runtime/window tests and existing geometry tests. |
| Focused kernels | 28 passed: independent exhaustive and LBVH rows/fields, analytic plane, malformed/duplicate/tied rows, graph union/mutual/degenerate filtering and existing reconstruction cases. |
| Actual Vulkan | Both GPU/Vulkan cases passed under ci-vulkan ASan+UBSan, with BUG-180's explicit LeakSanitizer exclusion. No capability skip or fallback. |
| Output comparison | Five phases across eight domains: nearest Hoppe, weighted warmup and measurement, union graph, mutual graph. Maximum per-vertex Euclidean error is zero; tolerance 1e-4. Edge arrays and face counts match the CPU reference. |
| Generated pixels | 2274, 2304, 2304, 183 and 123 foreground pixels across the five phases; each must exceed 10. Other renderable entities are removed. |
| Failure/history | CPU property/domain/transform watches and generated-entity history guards; GPU stale source, pending gate cancellation and rejected second/third submission publish no entity. Not every cancellation interleaving is covered. |

The [schema-v2 smoke](../diagnostics/point_construction_2026-09-10/results/benchmark.json) records one weighted-Hoppe measured request-to-publication run after one warmup. Its 22175.77666 ms diagnostic includes eight domains and frame pacing; it is a dirty Debug result with `claim_eligible: false`, not a speedup comparison. CPU field/normal/graph/extraction/materialization work remains shared. Legacy octree near-tie ordering, original Hoppe centroid anchors/boundary handling, global solvers, primitive/ray traversal and large-data performance remain outside C91.

The preserved initial logs identify the mesh-payload naming defect, short-batch reuse rejection, missing explicit smoke timeout, retired-token fixture wait, construction menu-count correction and four pre-existing appearance expectations tracked by BUG-186. The final CPU and GPU logs supersede those failures. Strict root hygiene still reports local `.agents/` metadata under BUG-177; no root-policy exception was added.

| Results audit claim | Disposition |
| --- | --- |
| Construction reference comparisons | Confirmed only for the bound CPU/GPU fixtures and stated tolerance. |
| Generated output is renderable | Confirmed for the actual Vulkan point-cloud output in each of the five phases; CPU authoring/selection tests cover all domains. |
| GPU is faster | Not asserted; no matched performance qualification. |
| All stages run on GPU | Not asserted; GPU supplies neighborhoods. |
| Sanitizer/leak coverage | Narrowed to actual ci-vulkan ASan+UBSan with leaks disabled; full isolated CPU sanitizer suites were not rerun. |
