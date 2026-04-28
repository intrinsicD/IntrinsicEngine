# Backend Policy

Backends are added in a strict progression and compared against the CPU reference output.

## Allowed backend progression

1. `cpu_reference`
2. `cpu_optimized`
3. `gpu_vulkan_compute` / `gpu_vulkan_graphics` (optional)
4. Optional external baselines

## Backend requirements

- Output payload compatibility across backends.
- Diagnostics must include selected backend and any fallback behavior.
- Parity thresholds must be declared in tests/benchmarks.
- Backend availability checks must be explicit and testable.
