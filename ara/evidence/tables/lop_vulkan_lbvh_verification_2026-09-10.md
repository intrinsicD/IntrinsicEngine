# Framed Vulkan LBVH LOP verification

Implementation `caa85ce781be772faf30de920322010825413afa`; [source hashes and receipts](../diagnostics/lop_vulkan_lbvh_2026-09-10/record.json).

48 focused CPU cases pass. Full CPU: 4466 selected, 1 skips in the full run, giving 4465 distinct passes and one expected unsanitized leak-control skip. 3 actual Vulkan cases pass under ci-vulkan ASan+UBSan with BUG-180's LeakSanitizer exclusion.

The new LBVH case compares three moving iterations on all eight canonical domains with the CPU reference at 1e-6 position tolerance, cold and explicitly primed current-storage source caches, point-cloud downsampling and early convergence. Undo/redo, complete-capacity overflow, exact stale-source/cancel status and unchanged output/history on failure are checked. Partial submission has independent static ownership review only; no injected failure or all-interleaving proof is claimed. Existing grid cases retain their separate implementation.

The [schema-v2 smoke](../diagnostics/lop_vulkan_lbvh_2026-09-10/benchmark.json) is a dirty Debug diagnostic, not a performance result. GPU queries feed CPU projection arithmetic; no GPU reduction, original inverse-cubic formulation, general-input equivalence, speedup or default change is claimed. WLOP/CLOP/EAR adapters remain open.
