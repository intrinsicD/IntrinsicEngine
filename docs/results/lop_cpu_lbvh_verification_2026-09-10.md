# Cached CPU LOP verification

Historical result summary. Raw completed-task artifacts were retired; exact
Git links preserve their original provenance. No run was repeated for cleanup.

Implementation `020622c844d1809e4614d5ed120925e63aa3bdad`; [source hashes and receipts](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/ara/evidence/diagnostics/lop_cpu_lbvh_2026-09-10/record.json).

The 46 focused cases pass, including three moving iterations, downsampled cross-set IDs, malformed rows, tiny support, eight-domain cache reuse, stale-source rejection and publication/history. Full CPU: 4464 selected, six sandbox skips, five native follow-ups pass, giving 4463 distinct passes and one expected unsanitized leak-control skip. Two existing actual Vulkan grid cases pass under ci-vulkan ASan+UBSan with BUG-180's LeakSanitizer exclusion.

The [schema-v2 smoke](https://github.com/intrinsicD/IntrinsicEngine/blob/36a7b5062d9be3239d667efd9e41c4d4028ba249/ara/evidence/diagnostics/lop_cpu_lbvh_2026-09-10/benchmark.json) observes zero position error and no contribution-count mismatches; 6.50472ms is a dirty Debug diagnostic. No speedup, default change, original inverse-cubic formulation, general-input equivalence or Vulkan LBVH projection is claimed. RUNTIME-229 owns framed GPU integration.
