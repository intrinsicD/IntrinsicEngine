# Bilateral point-filter verification

Implementation commit: `15983b2d4d97cb6f8352a6236eae8b3c3aad36fe`. [Source hashes and receipts](../diagnostics/bilateral_vulkan_2026-09-10/record.json) bind the tested scope.

| Evidence | Result |
| --- | --- |
| Geometry | Four analytic/supplied-neighbor/invalid-input cases and nine legacy bilateral cases pass. |
| Runtime and editor | Ten focused cases pass, including all eight domains, copy/in-place history, stale/cancelled inputs, shared config and private-index leases. |
| Full CPU | 4,424 selected, zero final failures, six sandbox skips. Five native-window follow-ups pass; one expected unsanitized leak-control skip remains. |
| Actual Vulkan | Both final cases execute without skips. Three moving-position passes match the reference on all eight domains at k=8 and k=63; observed position error is 0.0 under the 1e-5 bound. Separate stale, intermediate cancellation and dense-coincidence assertions pass. |
| Diagnostic smoke | Warm Vulkan 18797.801831 ms and CPU reference 199.8132 ms. No performance conclusion. |

The [architecture contract](../../../docs/architecture/bilateral-point-filter.md) specifies fixed normals and atomic publication. The [schema-v2 smoke](../diagnostics/bilateral_vulkan_2026-09-10/benchmark/runtime-smoke.json) is dirty-source diagnostic evidence. BUG-180 owns the GPU LeakSanitizer exclusion; BUG-183 owns the shared scheduler issue, independently of this operation's abandonment guard.

Earlier attempts retained in the task evidence include the obsolete menu-count assertion and the oversized combined Vulkan fixture. A timeout probe measured only 1,991.63 ms in the stale phase at a 95-second global deadline, identifying accumulated comparison work as the fixture-budget problem. The final two cases preserve every assertion and the original per-case timeout. No numerical mismatch was observed in completed comparisons.
