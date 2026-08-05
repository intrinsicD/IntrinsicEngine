# METHOD-020 v3 CPU-oracle screening

Before freezing v3 and before submitting any Vulkan request for its WLOP
fixture, the CPU reference screened a bounded radius set on the exact
`40 x 24`, `x = 2u^1.7 - 1`, target-240, seed-1902 plane. All other
parameters matched the prospective confirmation suite.

| Support radius | CPU state | Completed iterations | Empty neighborhoods | Output points |
| ---: | --- | ---: | ---: | ---: |
| 0.22 | `empty_neighborhood` | 1 | 1 | 0 |
| 0.24 | `empty_neighborhood` | 1 | 1 | 0 |
| 0.26 | `empty_neighborhood` | 2 | 1 | 0 |
| 0.28 | `empty_neighborhood` | 2 | 1 | 0 |
| 0.30 | `not_converged` | 8 | 0 | 240 |
| 0.32 | `not_converged` | 8 | 0 | 240 |
| 0.36 | `not_converged` | 8 | 0 | 240 |
| 0.40 | `not_converged` | 8 | 0 | 240 |

V3 selects `h=0.32`, the second screened usable value, rather than freezing
the first passing value at the observed `0.28`/`0.30` feasibility boundary.
This selection was made from CPU-reference behavior only. It is not a Vulkan
tolerance adjustment, and the positional parity thresholds remain unchanged.

The exact screening command used the sanitizer-backed `ci-vulkan` executable
with a CPU-only GTest filter:

```bash
LSAN_OPTIONS=detect_leaks=0 \
ASAN_OPTIONS=symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0 \
build/ci-vulkan/bin/IntrinsicRuntimePointCloudConsolidationGpuParityTests \
  --gtest_filter='PointCloudConsolidationGpuProtocolScreening.*'
```
