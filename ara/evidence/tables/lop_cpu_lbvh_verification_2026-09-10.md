# Cached CPU LOP verification

Implementation `020622c844d1809e4614d5ed120925e63aa3bdad`; [source hashes and receipts](../diagnostics/lop_cpu_lbvh_2026-09-10/record.json).

The 46 focused cases pass, including three moving iterations, downsampled cross-set IDs, malformed rows, tiny support, eight-domain cache reuse, stale-source rejection and publication/history. Full CPU: 4464 selected, six sandbox skips, five native follow-ups pass, giving 4463 distinct passes and one expected unsanitized leak-control skip. Two existing actual Vulkan grid cases pass under ci-vulkan ASan+UBSan with BUG-180's LeakSanitizer exclusion.

The [schema-v2 smoke](../diagnostics/lop_cpu_lbvh_2026-09-10/benchmark.json) observes zero position error and no contribution-count mismatches; 6.50472ms is a dirty Debug diagnostic. No speedup, default change, original inverse-cubic formulation, general-input equivalence or Vulkan LBVH projection is claimed. RUNTIME-229 owns framed GPU integration.
