# Point LBVH verification

Recorded 2026-09-08T13:44:25+02:00; local dirty source based on `314db9ea5be314c732458649775103c461992fbb`.
[C76](../../logic/claims.md#c76-bounded-cpu-and-vulkan-point-lbvh-integration)
records the bounded result. [Exact source hashes and run metadata](../diagnostics/point_lbvh_2026-09-08/record.json)
bind this record; prior geodesics/selection evidence is unchanged.

| Check | Final result |
| --- | --- |
| `cmake --preset ci`; `cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke -j 4` | Passed, Clang 23 |
| Default CPU exclusion selector, timeout 60 | 4340 passed, 1 skipped, 0 failed; 107.93 s |
| `ci-vulkan` LBVH and clustering test targets | Built and passed, ASan+UBSan |
| `PointLBVHGpuSmoke` and `ClusteringServiceGpuSmoke`, timeout 120 | 2 passed, 0 skipped; RTX 3050, NVIDIA 590.48.01 |
| `cmake --build --preset ci-vulkan --target ExtrinsicSandbox -j 4` | Passed, including final diagnostic-label correction |
| Sealed native smoke outputs | 32 validated; LBVH zero index mismatches and zero distance error |
| Strict layering, test layout, task policy, explicit-file docs sync, clean-workshop bundle | Passed |

The final full CPU selector was `-LE 'gpu|vulkan|slow|flaky-quarantine'`.
Its only skip was `GlfwLifecycleLsan.EngineStaticTeardownAndLeakControl`, which
requires sanitizer instrumentation absent from `ci`. The Vulkan selector was
`-R '^(PointLBVHGpuSmoke|ClusteringServiceGpuSmoke)\.' -L gpu -L vulkan`.
[Raw GPU test output](../diagnostics/point_lbvh_2026-09-08/vulkan-test-output.txt)
contains the selected device and driver. Validation was enabled in the LBVH
fixture; its captured output contains no VUID. The normal cold-start
`BarrierValidationFailed` breadcrumb precedes first-frame promotion.

The CPU cache tests cover all eight canonical element domains, input/deletion
revision changes, unrelated-property reuse, world separation, destruction and
source replacement. GPU coverage includes 1025 points, 258 queries,
non-power-of-two padding, coincident points, nearest ties, empty/singleton
inputs, nonfinite input rejection, inclusive radius boundaries, capacities 0/3/7,
allocation reuse, original deleted-slot mapping, cache rebuilds and k-means
publication. The CPU builder also accepts a subspan of its own prior snapshot.

During fixture bring-up, two CPU tests dereferenced an uncreated world and
were corrected to call `CreateWorld`. The initial standalone GPU check ran
before normal first-frame promotion and skipped; the final fixture performs
four normal warm-up frames before checking `IsOperational`. Neither incident
changed an engine gate or removed a query assertion.

The [CPU smoke result](../diagnostics/point_lbvh_2026-09-08/cpu-smoke.json) is
`claim_eligible: false`; there is no baseline timing comparison. GPU sorting
is bitonic and bounds use sorted-range unions. Triangle/ray/k-nearest queries,
world-distance metrics under nonuniform scale and production-scale timing are
outside this cohort. See the [API contract](../../../docs/architecture/spatial-indices.md).
