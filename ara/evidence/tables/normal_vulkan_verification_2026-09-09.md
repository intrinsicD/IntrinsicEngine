# Vulkan neighborhoods for normal estimation

Bounded local evidence for C80. The [record](../diagnostics/normal_vulkan_2026-09-09/record.json) binds the source hashes, toolchain and limitations.

| Scope | Result |
| --- | --- |
| CPU | [45 focused checks](../diagnostics/normal_vulkan_2026-09-09/focused-cpu.txt) passed. [Full gate](../diagnostics/normal_vulkan_2026-09-09/cpu-summary.txt): 4,376 passed, one expected unsanitized LSan-control skip, no failures. `IntrinsicTests`, Sandbox and benchmark smoke targets built with Clang 23 `ci`. |
| Actual GPU | RTX 3050, driver 590.48.01, `ci-vulkan` ASan+UBSan. [Four existing cases](../diagnostics/normal_vulkan_2026-09-09/vulkan-initial.txt) passed; the two new fixtures initially skipped before cold-start promotion. [Both corrected fixtures](../diagnostics/normal_vulkan_2026-09-09/vulkan-final.txt) then executed and passed. |
| Normal output | Eight domains with 66 slots, duplicate samples and deleted rows. Cold/warm kNN and radius output match CPU KD-tree normals within 1e-5 per component (observed maximum zero); fitting and MST orientation remain CPU. Warm index reuse, unrelated/deleted values and undo/redo are checked. |
| Rejection | Stale inputs, cancellation during chunked GPU work and radius support exceeding 1024 candidates retain previous normals. Three expected JobService prerequisite rejections appear in the negative-case log. Framed radius tests also verify total counts, exclusions, reused buffers and stale-target rejection. |
| Timing smoke | [Schema-v2 result](../diagnostics/normal_vulkan_2026-09-09/benchmark/runtime-smoke.json) passes its manifest and timing gate. Eight concurrent requests: cold kNN 9,510.77 ms, warm kNN 9,822.29 ms, radius 9,730.91 ms; separate CPU references 168.04 ms warm kNN and 267.39 ms radius. Query elapsed sums overlap and include frame waits/readback; CPU timing includes output capture/reset. |

This small frame-paced fixture favors CPU execution and does not justify a
backend-default change. Dirty timing is non-claim-eligible; no scaling or
statistical speedup claim follows. The GPU environment retains its existing
`detect_leaks=0` policy; BUG-180 remains open, and this is not whole-process leak
qualification. No GPU normal-vector visualization or full Framework24
PCA/features/saliency parity is asserted.

The implementation is local and uncommitted. Outlier adoption remains
RUNTIME-209/UI-041; no other consumer is migrated by this slice.
