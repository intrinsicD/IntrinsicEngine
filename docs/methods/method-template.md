# Method Package Template

Use `methods/_template/` as the starting point for each new method package.

## Required package files

- `README.md`: scope, backend status, and usage notes.
- `method.yaml`: machine-readable manifest.
- `paper.md`: claim summary and citation metadata.
- `tests/`: correctness and regression test notes/assets.
- `benchmarks/`: benchmark manifests and runner notes.
- `reports/`: validation/performance summaries.

## Required implementation order

1. CPU reference backend.
2. Correctness tests.
3. Benchmark harness/manifests.
4. Optimized CPU backend.
5. GPU backend (only after reference parity).

## Manifest checklist

A method manifest must follow [method-manifest-schema.md](method-manifest-schema.md) and include:

- Stable ID (`geometry.*`, `rendering.*`, `physics.*`).
- Declared inputs/outputs.
- Declared backends.
- Correctness tests and benchmarks.
- Known limitations.
