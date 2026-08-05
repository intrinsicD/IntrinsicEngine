# METHOD-020 v1 screening disposition

The frozen `builtin.lop_family.gpu_vulkan.v1` confirmation suite cannot
adjudicate isotropic WLOP parity. Before any Vulkan request was submitted, the
CPU-reference oracle returned `empty_neighborhood` on the WLOP fixture after
one projection iteration, with one empty projected-density neighborhood.

The observed command was:

```bash
LSAN_OPTIONS=detect_leaks=0 \
ASAN_OPTIONS=symbolize=1:detect_leaks=0:fast_unwind_on_malloc=0 \
build/ci-vulkan/bin/IntrinsicRuntimePointCloudConsolidationGpuParityTests \
  --gtest_filter='PointCloudConsolidationGpuParity.*'
```

The relevant diagnostic was:

```text
wlop_isotropic: empty_neighborhood, iterations=1, empty_neighborhoods=1
```

This is a protocol rejection, not a GPU failure and not evidence about Vulkan
WLOP parity. The v1 LOP oracle remained usable. In accordance with the frozen
decision rule, the v1 fixture, radius, seed, and tolerances are not retuned.
Any replacement confirmation suite uses a new dataset and benchmark identity.
