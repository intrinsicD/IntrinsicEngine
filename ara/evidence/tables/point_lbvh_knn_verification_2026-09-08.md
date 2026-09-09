# Shared point-LBVH kNN and PCA adapter verification

Local dirty-source implementation evidence for C78. The [record](../diagnostics/point_lbvh_knn_2026-09-08/record.json) hashes the tested source surface and distinguishes CPU, Vulkan and benchmark evidence.

| Claim under review | Evidence and disposition |
| --- | --- |
| CPU sorted kNN and original-ID exclusion | Five PointLBVH cases and two canonical cache cases pass; analytic/seeded/coincident/invalid inputs and deleted rows covered. Full ci gate: 4,351 passed, one expected unsanitized skip, zero failures. [CPU output](../diagnostics/point_lbvh_knn_2026-09-08/cpu-tests.txt). |
| Vulkan queries and existing consumers | Standalone nearest/radius, kNN/exclusion and framed ICP passed in the [first run](../diagnostics/point_lbvh_knn_2026-09-08/vulkan-first-run.txt). The framed kNN fixture initially exited during cold-start validation; after fixing that fixture wait, framed kNN and existing k-means passed in the [focused rerun](../diagnostics/point_lbvh_knn_2026-09-08/vulkan-final-focused.txt). Five current cases passed, none skipped. |
| Framed reuse and staleness | Original IDs, per-query counts, coincident peers, deleted exclusion IDs, same-batch reuse and stale-before-record rejection asserted. One CPU and one GPU target build across the reuse fixture. |
| Supplied CPU PCA | Existing KD-tree and supplied LBVH agree on kNN/radius duplicate fixtures within 1e-5 component tolerance. The smoke paraboloid has zero normal component error and zero query-ID mismatches. |
| Speed or default adoption | Not established. [Schema-v2 smoke](../diagnostics/point_lbvh_knn_2026-09-08/benchmarks/cpu-knn-pca-smoke.json) reports build, exhaustive/warm queries and complete PCA separately. Supplied PCA is slower on this small local fixture; defaults stay. Dirty source and background compilation preclude a performance claim. |

GPU evidence uses ci-vulkan ASan+UBSan with the existing leak-disabled cohort environment; see the [registry](../diagnostics/point_lbvh_knn_2026-09-08/gpu-registry.txt). It is not evidence for whole-process leak freedom or the isolated full CPU sanitizer gates. BUG-180 remains open. Structural checks pass; BUG-177's pre-existing local root entry remains.

[GEOM-077](../../../tasks/active/GEOM-077-point-lbvh-knn-exclusion.md) records the implementation scope. RUNTIME-213/UI-045 own normal canonical config/UI/publication; outlier adoption remains RUNTIME-209/UI-041. GPU PCA, arbitrary predicates and primitive/ray traversal are not provided by this slice.
