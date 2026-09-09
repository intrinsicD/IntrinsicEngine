# Kernel density verification, 2026-09-09

Bounded local evidence for RUNTIME-220 and C82. The [record](../diagnostics/density_vulkan_2026-09-09/record.json)
binds source hashes, toolchain, host, checks and benchmark measurements. Base
`e508e779271c96246ee5b50454678b45217f5170` plus uncommitted changes; no publication
or performance eligibility is asserted.

| Gate | Result |
| --- | --- |
| Build | Clang 23 `ci`: IntrinsicTests and ExtrinsicSandbox; final corrected UI fixture target. `ci-vulkan`: IntrinsicPointLBVHGpuTests. |
| Focused CPU | 38 passed: reference, runtime/config/history, UI routing. |
| Full CPU | 4399 selected, 4393 passed and six capability skips in sandbox (107.81 s); all five native-window cases passed with desktop access. 4398 distinct passes and one expected unsanitized leak-control skip. |
| Actual GPU | RTX 3050, driver 590.48.01; one ci-vulkan ASan+UBSan case passed in 44.76 s, zero skips. Existing `detect_leaks=0` GPU cohort setting; BUG-180 remains separate. |
| Density comparison | All eight domains; cold/warm k=8 automatic bandwidth and k=63 explicit bandwidth. Maximum absolute density error zero against CPU octree, tolerance 1e-5. |
| Edge/lifetime cases | Deleted/unrelated rows, dense coincident peers, unsupported k=64, stale input, cancellation and undo/redo assertions pass. CPU contracts also cover malformed config, numerical failure, output revisions, new-output undo, cache rebuild and scalar recipes. |
| Benchmark | Schema v2 sealed/validated `geometry.point_lbvh.density_runtime_smoke`; warm framed total 9876.23 ms versus CPU reference 118.69 ms. Tiny display-paced fixture, dirty source: no speedup or default-change claim. |
| Structural | Strict layer imports/links, task policy, test layout, doc links, method/benchmark manifests and skill mirror checks pass. Module inventory regenerated (397 modules). |

[Final CPU log](../diagnostics/density_vulkan_2026-09-09/focused-cpu.txt),
[full CPU summary](../diagnostics/density_vulkan_2026-09-09/cpu-summary.txt),
[native follow-up](../diagnostics/density_vulkan_2026-09-09/native-cpu.txt),
[Vulkan log](../diagnostics/density_vulkan_2026-09-09/vulkan.txt),
[benchmark payload](../diagnostics/density_vulkan_2026-09-09/benchmark/runtime-smoke.json).
The [initial focused log](../diagnostics/density_vulkan_2026-09-09/initial-focused.txt)
retains a test-fixture failure: density menu activation occurred after editor
shutdown. The final dedicated fixture initializes its own editor and passes.

Clean-workshop manual review: rows 1–3 pass (allowed imports/links and public
CPU value spans; runtime owns live state). Rows 4–6 are not applicable (no
renderer subsystem/pass/recipe change). Row 7 passes with bounded backend
evidence and the active publication note; row 8 passes with no new exception.
The [automated bundle](../diagnostics/density_vulkan_2026-09-09/clean-workshop.txt)
records its strict checks. No new service or scheduler; existing framed queries
and dependent CPU jobs retain buffer lifetime and prevent stale publication.
