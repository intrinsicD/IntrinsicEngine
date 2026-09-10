# FPFH descriptor verification

Implementation `2e6353df1ca9c2500bc6509ab93a71476399eef1`. [Source hashes and receipts](../diagnostics/descriptor_vulkan_2026-09-10/record.json) bind this bounded result.

| Evidence | Result |
| --- | --- |
| Geometry/runtime | Focused reference, complete/prefix support, malformed normals/rows, all-domain config/publication/history and existing keypoint/features checks pass. |
| Full CPU | 4,444 selected, zero failures, six sandbox skips. Five native-window follow-ups pass: 4,443 distinct passes and one expected unsanitized leak-control skip. Shared descriptor panel/menu checks pass. |
| Actual Vulkan | Three cases execute without skips. All 33 histogram columns compare against the reference on eight domains at explicit and automatic radii; observed error 0.0 is within 1e-5. Dense cap=1 support above 1,024 neighbors also matches within tolerance. Stale normals, cancelled/reaped scale dependency, uncapped overflow and second/third-stage submission rejection preserve outputs/history. |
| Diagnostic smoke | Warm Vulkan 6452.327235 ms; CPU reference 475.866538 ms. No performance conclusion. |

The [method contract](../../../docs/architecture/descriptor-analysis.md) preserves query-normal Darboux histograms, source-ID caps and Euclidean inverse-distance weighting. The [schema-v2 smoke](../diagnostics/descriptor_vulkan_2026-09-10/benchmark/runtime-smoke.json) records dirty-source diagnostics. BUG-180 owns the GPU LeakSanitizer exclusion.

Direct inspection corrected the planning assumption about radius result order: both LBVH implementations retain lowest IDs. FPFH now consumes that exact required prefix when capped; uncapped overflow still fails. The first dense GPU harness timed out after publication because it queried already-reaped successful ancestors; the corrected harness waits for accepted jobs only in partial-submission rejection phases. The historical geometry assertion failure concerned duplicate-only empty support and has a corrected explicit expectation and passing replacement gates.
