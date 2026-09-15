# Overnight compile measurements — 2026-09-15

The overnight cleanup reduces the measured full engine-library build modestly and makes the selected implementation rebuilds substantially cheaper. The config-interface dependency chain remains a bottleneck. These are three local measurements per source revision, with descriptive medians and observed ranges; no statistical confidence interval or cross-host guarantee is inferred. C94 records this bounded observation; C92 remains a separate hypothesis.

## Wall time

Times are seconds: median (minimum–maximum). Improvement is the reduction in the median duration, `(before − after) / before`. Configure time is excluded from clean builds; the last row explicitly includes both configuration and its resulting build.

| Scenario | Before | After | Median reduction | Compiler invocations before → after |
| --- | ---: | ---: | ---: | ---: |
| Clean engine-library build | 365.524 (365.265–365.550) | 348.332 (348.115–348.721) | 4.7% | 779 → 775 |
| Settled no-op | 0.074 (0.072–0.079) | 0.073 (0.072–0.082) | 2.2% | 0 → 0 |
| Workspace Models implementation | 13.246 (13.201–13.289) | 7.252 (7.226–7.272) | 45.2% | 1 → 1 |
| RenderExtraction Recipes implementation | 3.253 (3.252–3.260) | 1.856 (1.848–1.874) | 42.9% | 1 → 1 |
| Consolidation config implementation | 0.722 (0.716–0.732) | 0.422 (0.408–0.422) | 41.6% | 1 → 1 |
| Consolidation config interface | 37.018 (36.835–37.120) | 33.584 (33.344–33.747) | 9.3% | 19 → 13 |
| CMake reconfigure + resulting build | 4.229 (4.223–4.254) | 0.578 (0.549–0.651) | 86.3% | 1 → 0 |

No-op differences are only a few milliseconds and support no useful performance conclusion. Each implementation probe compiles exactly one source in both arms. The interface probe compiles 19 sources before and 13 after in every sample. Reconfiguration recompiles only generated `tinygltf_impl.cpp` before and none after; the change avoids rewriting identical generated contents.

Initial configure time is separate: 7.169 (6.893–7.218) s before and 7.094 (7.081–7.306) s after. These measurements reuse installed dependencies and exclude package installation.

## Dependency path, CPU work and memory

The weighted dependency path sums measured command durations along the longest path in the actual Ninja target graph, including scans and archives. It excludes scheduler/resource waiting between commands and is not a prediction of wall time at unlimited parallelism. CPU time sums user and system time for the command and children. RSS is GNU time’s maximum single-process resident set, not total concurrent build memory.

| Scenario | Dependency path seconds before → after | CPU seconds before → after | Maximum single-process MiB before → after |
| --- | ---: | ---: | ---: |
| Clean engine-library build | 72.134 → 53.242 | 1370.70 → 1342.18 | 2897.99 → 2897.90 |
| Workspace Models implementation | 13.172 → 7.175 | 13.23 → 7.24 | 2766.55 → 1699.44 |
| RenderExtraction Recipes implementation | 3.177 → 1.781 | 3.24 → 1.84 | 1305.86 → 837.90 |
| Consolidation config implementation | 0.646 → 0.344 | 0.71 → 0.41 | 417.42 → 389.52 |
| Consolidation config interface | 33.792 → 33.510 | 87.19 → 67.28 | 2895.34 → 2895.20 |

The full-build critical chain changes from renderer → render-extraction internals → extraction implementation to scene editing → workspace snapshot interface → context adapter. The config-interface chain still runs through consolidation types/module → point-cloud service operations → editor workspace session. Removing other importers reduces total work without substantially shortening this particular chain. Full-build peak single-process memory remains dominated by other files.

## Dominating compiler producers

Medians below come from the four-job clean builds; they include contention during each compiler invocation. Producers are matched by source and edge kind, not command hash or build-specific edge identity. The isolated probes above give the corresponding edit/rebuild measurements.

| Source (largest remaining producers) | Before seconds | After seconds |
| --- | ---: | ---: |
| `src/runtime/Editor/Runtime.EditorWorkspaceSnapshots.cppm` | 18.825 | 18.784 |
| `src/graphics/renderer/Graphics.Renderer.cppm` | 15.410 | 15.416 |
| `src/graphics/renderer/Graphics.Renderer.cpp` | 14.718 | 14.638 |
| `src/runtime/Editor/internal/Runtime.EditorFeatureContextAdapters.cpp` | 12.917 | 12.882 |
| `src/runtime/Rendering/Runtime.RenderExtraction.Internal.cpp` | 26.418 | 12.262 |
| `src/geometry/Geometry.HalfedgeMesh.CurvatureExtrema.cpp` | 11.967 | 11.924 |
| `src/runtime/AssetWorkflow/Runtime.AssetWorkflowModelMaterialization.cpp` | 11.117 | 11.549 |
| `src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cppm` | 11.332 | 11.407 |
| `src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp` | 10.997 | 10.819 |
| `src/runtime/Editor/Operations/Runtime.SceneEditingOperations.cppm` | 10.661 | 10.705 |

This points to shared editor workspace types/context adapters and renderer surfaces as remaining costs. The experiment did not compare alternative splits, Pimpl, or a different build system; it does not establish which redesign would improve them.

The table also retains increases: model materialization, for example, takes 11.117 → 11.549 seconds. That source and its compiler command are unchanged; an isolated probe would be needed to attribute the increase rather than infer its cause from parallel-build timings.

## Scope and source custody

- Before: `29d75ebe7a9aa427d58c00764b3ba284f4dab0e3`; after: `07a8b29147ccd642fcf827c6a6361ba3e1c29f13`. This compares the completed overnight changes only, not the earlier processing-family migration.
- Clang 23, CMake 3.28.3, Ninja 1.11.1, Intel i9-11900KF, four jobs, Debug, `ci` preset with explicit Null/headless overrides, no sanitizers or compiler launcher. `CCACHE_DISABLE=1`; identical preinstalled vcpkg dependency contents are fingerprinted per sample.
- All 775 shared sources have byte-identical compiler commands across both arms and all repetitions. The four removed compiler entries are exactly the four deleted source files; no new producer is added.
- Target: `ExtrinsicRuntime` and its dependency closure. Tests, Sandbox and promoted Vulkan are outside this timed scope. No engine runtime or GPU-performance conclusion follows.
- One detached source worktree and one freshly recreated tmpfs build directory at identical paths for both arms. Tracked source bytes remain at exact clean commits. Incremental probes change mtimes only; they measure recompilation, not a semantic API alteration.
- Order: before, after, after, before, before, after. One discarded baseline pilot established resource headroom and probe fan-out; identical input pre-reading precedes each retained sample. Cold build artifacts, warm OS cache. No competing builds or tests ran during the retained cohort.
- The compared production/build diff removes 550 physical lines and four files. Existing overnight correctness evidence is separate from timings: [reconciliation](../../../tasks/evidence/RUNTIME-263/overnight-reconciliation.json) and [review](../../../tasks/evidence/RUNTIME-263/overnight-review.txt). Timing itself is not a feature-parity test.
- All six results retain `claim_eligible: false`. They are source-bound local descriptive observations, not publication-qualified or universal performance results. The broader C92 and BUILD-006 build-backend/cache comparison remain outside this experiment.

## Rejected attempt and metadata correction

The first population stopped after successful engine/probe builds because the new harness wrongly treated CMake’s legitimate Ninja log recompaction during configuration as a contaminated compiler window. Fix `dffa9bcd5` restricts compiler-log windows to build commands. The entire incomplete population was excluded for that harness failure before restarting the same six-sample protocol; its original evidence is retained.

The frozen runner also used a noncanonical backend label. Metadata-only successor records use `external_baseline`, following the existing external build-timing convention, and retain the original compiler label in diagnostics; the frozen protocol records the exact compiler version. Each successor identifies the original attempt, preserves every metric, source and manifest field, and explicitly records that no new execution occurred. Both sets are retained; only the repaired records are the schema-valid canonical results. The subsequent runner fix validates sealed records before writing.

## Evidence and reproduction

- [Frozen manifest](../../../benchmarks/ci/manifests/engine_compile_iteration_overnight.yaml)
- [Protocol and host](../diagnostics/build007_20260915/protocol.json)
- [All samples, medians, ranges and matched producers](../diagnostics/build007_20260915/summary.json)
- [Evidence inventory and hashes](../diagnostics/build007_20260915/evidence-index.json)
- [Raw evidence archive](../diagnostics/build007_20260915/raw-evidence.tar.gz): complete retained/rejected populations, original and repaired results, compiler logs/graphs/commands, timing receipts, pilot, review files and the summary recalculation script. `python3 summarize.py cohort-20260915-r2` regenerates the comparison from the original measurement values.
- The archive preserves the measured runner at `dffa9bcd5`; its exact SHA-256 is recorded in `protocol.json`. Later metadata-validation changes are not represented as the measured runner.

## Review and verification

Final results review and post-measurement verification are recorded in [verification.json](../diagnostics/build007_20260915/verification.json).
