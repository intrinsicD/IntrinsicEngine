# IntrinsicEngine

IntrinsicEngine is a modular C++23 research and rendering engine for geometry processing, graphics experimentation, and method-driven scientific implementation.

## Final Source Layout

Canonical source roots:

- `src/legacy/` *(temporary migration area; expected to shrink)*
- `src/core/`
- `src/assets/`
- `src/ecs/`
- `src/geometry/`
- `src/graphics/rhi/`
- `src/graphics/vulkan/`
- `src/graphics/framegraph/`
- `src/graphics/renderer/`
- `src/platform/`
- `src/runtime/`
- `src/app/`

Repository architecture contract and layer invariants are defined in `AGENTS.md`.

## Build and Test Quick Start

### Configure

```bash
cmake --preset ci
```

### Build tests

```bash
cmake --build --preset ci --target IntrinsicTests
```

### Run tests

```bash
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

This is the default CPU-supported correctness gate. GPU/Vulkan, slow, and explicitly quarantined tests are opt-in through CTest labels.

For fast local iteration on small, well-scoped changes, plan or run the conservative touched-scope helper before the full gate:

```bash
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir cmake-build-debug --print
python3 tools/ci/touched_scope.py --root . --base-ref origin/main --build-dir cmake-build-debug --run
```

The helper selects affected build targets, CTest labels, and structural checks, but it does not replace the full CPU-supported PR/merge gate above.

## Agent Workflow Pointer

For authoritative agent operating rules, task workflow, and review checklist, use:

- `AGENTS.md`
- `docs/agent/contract.md`
- `docs/agent/task-format.md`
- `docs/agent/review-checklist.md`

## Documentation Pointer

Use the docs index as the canonical navigation entry:

- `docs/index.md`

This includes architecture, ADRs, migration docs, methods, benchmarking, API/generated docs, and troubleshooting references.

## Method / Paper Implementation Pointer

Method package structure, manifest expectations, and implementation workflow:

- `methods/README.md`
- `docs/methods/index.md`
- `docs/agent/method-workflow.md`

## Benchmarking Pointer

Benchmark structure, manifests, and result schema:

- `benchmarks/README.md`
- `docs/benchmarking/index.md`
- `docs/benchmarking/benchmark-manifest-schema.md`
- `docs/benchmarking/result-json-schema.md`

## CI Status and Expectations

The repository uses split workflows under `.github/workflows/` for fast PR validation, full CPU CI, sanitizers, docs/manifests, benchmark smoke, and nightly deep checks. `tools/ci/touched_scope.py` is available for local/touched-scope verification planning between full CI runs.

When changing code/docs/structure, keep touched-scope checks green and update related docs/tasks in the same PR.
