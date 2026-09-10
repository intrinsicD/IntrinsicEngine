# Keypoint analysis verification

Implementation `aa59947a42e5db293d053e0381487300f6e66f32`. [Source hashes and receipts](../diagnostics/keypoint_vulkan_2026-09-10/record.json) bind this bounded result.

| Evidence | Result |
| --- | --- |
| Geometry | Four new analytic/supplied-support/deleted-spacing cases and 17 existing feature cases pass. |
| Runtime | Sixteen focused cases pass, including keypoint config/publication and the existing outlier/bilateral helper consumers. |
| Full CPU | 4,435 selected, zero failures, six sandbox skips. Five native-window follow-ups pass: 4,434 distinct passes and one expected unsanitized leak-control skip. The new three-menu alias and shared keypoint window checks pass. |
| Actual Vulkan | Both cases execute without skips (24.74 and 16.72 seconds). Eight-domain masks match at explicit and automatic radii; saliency error 0.0 is within 1e-5. Stale input, cancelled/reaped scale dependency, support above 1,024 neighbors and second/third-stage submission rejection preserve outputs/history. |
| Diagnostic smoke | Warm Vulkan 6844.736167 ms; CPU reference 157.588551 ms. No performance conclusion. |

The [method contract](../../../docs/architecture/keypoint-analysis.md) specifies centroid covariance, original-index ties and complete support. The [schema-v2 smoke](../diagnostics/keypoint_vulkan_2026-09-10/benchmark/runtime-smoke.json) records dirty-source diagnostics. BUG-180 owns the GPU LeakSanitizer exclusion. The nearest-live spacing fix also changes automatic descriptor radii on clouds where the old eight-candidate cutoff hid live neighbors.

Preliminary review caught a post-submission result-copy race. Failure returns now use an immutable pending snapshot; two actual Vulkan submission-rejection phases exercise the corrected lifetime. Earlier menu-array compilation and task-field validation errors remain in immutable receipts and have passing replacement gates.
