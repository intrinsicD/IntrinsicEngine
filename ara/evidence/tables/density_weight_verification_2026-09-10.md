# Compact density-weight verification

Implementation `618a8c54aa07d4754cbb6a18ab60fb248cfd8ded`. [Source hashes and receipts](../diagnostics/density_weight_vulkan_2026-09-10/record.json) bind this bounded result.

| Evidence | Result |
| --- | --- |
| Geometry/runtime | Analytic kernels, complete candidate rows, tiny-radius before/after regression, malformed rows, extreme support, projection boundary/derivative guards, eight-domain config/cache/publication/history and stale/cancel checks pass. |
| Full CPU | 4,456 selected, zero failures, six sandbox skips. Five native-window follow-ups pass: 4,455 distinct passes and one expected unsanitized leak-control skip. Shared panel/menu checks pass. |
| Actual Vulkan | Three density-weight cases execute without skips, plus two existing keypoint and three descriptor cases after shared pagination refactoring. All kernel/mode combinations compare against the reference on eight domains; observed error 0.0 is within 1e-5. Tiny pair/internal-node support and single-sample weights pass. Stale inputs, cancellation after completed support, overflow and second-stage submission rejection preserve output/history. |
| Diagnostic smoke | Warm Vulkan 5926.360469 ms; CPU reference 79.116965 ms. No performance conclusion. |

The [method contract](../../../docs/architecture/density-weights.md) preserves strict double Euclidean support with conservative float candidates. The [schema-v2 smoke](../diagnostics/density_weight_vulkan_2026-09-10/benchmark/runtime-smoke.json) records dirty-source diagnostics. BUG-180 owns the GPU LeakSanitizer exclusion.

The original tiny-radius fixture failed before correction and passes afterward. The first seven-phase GPU comparison hit the inherited 30-second CTest timeout because its 120-second framed-test registration was omitted. After that registration correction, the unchanged fixture passes under its existing 95-second internal watchdog. The seven other cases passed in the original run and were not repeated. The Vulkan density adapter deliberately excludes nonzero subnormal coordinate components; the bounded tests do not prove a general device denormal contract. Projection safeguards preserve exact support after query broadening and avoid evaluating irrelevant overflowing derivatives. No projection GPU grid replacement is included.
