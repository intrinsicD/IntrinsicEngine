# Vector Heat — method package structure example

This package is a **non-building example** that demonstrates how to scaffold a geometry method package before real algorithm implementation. It is **not** a real method intake.

- Its location under `methods/_examples/` indicates this is instructional only.
- No production C++ code is included.
- Use this package as a template for creating a real `METHOD-*` task.

## Purpose

This example mirrors a vector-heat-style method organization so future tasks can add:

1. CPU reference backend first.
2. Correctness tests.
3. Benchmark manifests and smoke runner integration.
4. Optimized CPU/GPU backends only after reference parity.

## How to convert into a real method package

1. Move the package out of `methods/_examples/` into its domain directory (for example `methods/geometry/<name>/`).
2. Replace `method.yaml` placeholders with real identifiers, paper metadata, datasets, and metrics.
3. Add implementation code under method-owned include/src paths (or declared backend integration).
4. Replace placeholder docs in `tests/` and `benchmarks/` with concrete files.
5. Ensure `python3 tools/agents/validate_method_manifests.py --root methods --strict` passes.
