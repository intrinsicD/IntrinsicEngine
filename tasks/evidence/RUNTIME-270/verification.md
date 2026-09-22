# RUNTIME-270 retirement verification — 2026-09-22

Clang 23 preset builds; canonical ci is unsanitized Null/headless, ASan and UBSan
use isolated grouped CPU builds, and ci-vulkan uses promoted Vulkan plus its
configured combined sanitizers. Source corrections are independently reviewed.
This is implementation verification, not benchmark or backend-maturity evidence.

| Gate | Source | Result |
| --- | --- | --- |
| Full ci CPU selector | `7dbe421e8` | 4,937 selected: 4,936 pass, one expected GLFW/LSan skip; 157.38 s. |
| Full ASan CPU selector | `1a578a1aa` | 3,288 pass, no skips; 668.63 s. |
| Full UBSan CPU selector | `1a578a1aa` | 3,288 selected: 3,287 pass, one expected GLFW/LSan skip; 296.05 s. |
| Final material/bake/display CPU selection | `70b1d2f35` | 87 ci cases; 47 ASan and 47 UBSan cases pass. |
| Corrected UV race | `1a578a1aa` | 100 consecutive passes in each ci, ASan and UBSan build. |
| Initial full Vulkan selector | Before final material/shader/watchdog corrections | 89/95 pass; six diagnosed failures retained below. |
| Final rebuilt Vulkan selection | `7dbe421e8` | 15/15 pass, including all six corrected failures; 624.19 s. |

CPU result summaries: [ci](ci-full.log), [ASan](asan-full.log),
[UBSan](ubsan-full.log). Later material reconciliation correction and its six-kind
regression passed [ci](ci-material-final.log), [ASan](asan-material-final.log),
[UBSan](ubsan-material-final.log); the subsequent shader/watchdog changes affect
only Vulkan execution. The final ci gate includes every source correction.
[UV race repetitions](../BUG-206/ci-fixed-repeat.log) retain their complete outputs;
ASan and UBSan repetition outputs are adjacent.

The [initial Vulkan run](gpu-initial-run.log) retains every failure and passing
case, including the shutdown LeakSanitizer contract. Four failures exposed the
Float/Double-only material interpretation predicate, one exposed extreme-normal
raster interpolation overflow, and one exposed the eight-phase anisotropic
fixture's seven-phase watchdog. No numerical, pixel, history, backend or sanitizer
assertion was removed. The final subset reruns all six failures and the immediate
bake/display/analysis/clustering consumers after a complete IntrinsicTests rebuild.
The remaining broader presentation-pacing investigation is BUG-193.

[GPU state](gpu-host.csv) records the RTX 3050, driver and display/power state.
Runtime tests verify promoted-device readiness; no Null or CPU substitution is
counted as Vulkan success. Timings are diagnostic wall time, not performance claims.

Commands (full CPU/preset build commands also appear in the task):

```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci-asan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
ctest --test-dir build/ci-ubsan --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60 --parallel 1
ctest --test-dir build/ci --output-on-failure -R 'AssetWorkflowModule|TextureBakeModule|VisualizationRecipes|RenderExtraction' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci-asan --output-on-failure -R 'AssetWorkflowModule|TextureBakeModule|VisualizationRecipes' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci-ubsan --output-on-failure -R 'AssetWorkflowModule|TextureBakeModule|VisualizationRecipes' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R 'ImportedObjectSpaceNormalBake|PropertyTexture.*Bakes|SurfaceAppearanceBakes|AnalysisMaskSaliency|UnreachableVertexPreserves|UnreachableScalarRegion|ReferenceTriangleScalarField|ClusteringServiceGpuSmoke|VulkanLbvhAnisotropic' --timeout 120
```

The [final Vulkan run](gpu-final-run.log) passes every selected case. Anisotropic
WLOP completes all eight phases in 335.93 seconds under the existing eight-phase
480-second internal allowance. Across the full run and corrected regression run,
each of the 95 selected Vulkan cases has passing execution evidence.
