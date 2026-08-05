# METHOD-020 v2 screening disposition

The frozen `builtin.lop_family.gpu_vulkan.v2` confirmation suite also cannot
adjudicate isotropic WLOP parity. Before any Vulkan request was submitted, the
CPU-reference oracle returned `empty_neighborhood` on the WLOP fixture during
its first projected-density calculation, with zero completed iterations and
one empty neighborhood.

The v2 protocol incorrectly described the reused METHOD-019 WLOP fixture as
CPU-confirmed for a usable result. METHOD-019 confirmed exact parity between
two CPU implementations for an intentional `empty_neighborhood` failure; its
result report explicitly records an empty output. That evidence cannot serve
as a successful CPU oracle for GPU position parity.

The observed v2 diagnostic was:

```text
wlop_isotropic: empty_neighborhood, iterations=0, empty_neighborhoods=1
```

This rejection occurred at the CPU prerequisite and is not Vulkan execution
or Vulkan WLOP evidence. The frozen v2 fixture and thresholds remain unchanged.
A subsequent suite must CPU-screen a usable WLOP result first and then freeze
that distinct dataset and benchmark identity before Vulkan confirmation.
