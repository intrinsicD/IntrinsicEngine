# GEOM-009 — Geometry benchmark manifests, fixtures, and smoke benchmark

## Goal
- Replace the placeholder geometry benchmark scaffold with a manifest-driven smoke benchmark framework for geometry algorithms and method-readiness work.

## Non-goals
- No heavy/nightly benchmark suite in this task.
- No performance win claims.
- No GPU benchmark backend.
- No new geometry algorithms beyond benchmark harness fixtures.

## Context
- Status: backlog.
- Owning subsystem/layer: `benchmarks` using public `geometry` APIs only.
- Seeded by [`docs/reviews/2026-05-12-src-geometry-gap-analysis.md`](../../../docs/reviews/2026-05-12-src-geometry-gap-analysis.md).
- `benchmarks/geometry/Bench_ExampleSmoke.cpp` is currently a placeholder. Geometry paper-method work needs stable benchmark IDs, datasets, smoke/heavy split, metrics, and machine-readable output.
- Benchmark work should follow `docs/agent/benchmark-workflow.md` and `docs/agent/benchmark-review-checklist.md`.

## Required changes
- [ ] Define a geometry benchmark manifest format with stable IDs, input fixtures, algorithm parameters, smoke/heavy classification, expected metrics, and output schema.
- [ ] Add small checked-in geometry fixtures or reference existing test assets suitable for CPU smoke benchmarks.
- [ ] Replace `benchmarks/geometry/Bench_ExampleSmoke.cpp` with a real smoke benchmark that exercises at least one existing public geometry API without requiring GPU/Vulkan.
- [ ] Add machine-readable benchmark output for smoke runs.
- [ ] Document how to add future geometry benchmark cases and how to distinguish smoke from heavy/nightly runs.
- [ ] Ensure benchmarks depend only on public method/geometry APIs allowed by repository policy.

## Tests
- [ ] Add or update benchmark smoke tests registered with documented CTest labels.
- [ ] Ensure any new labels are added to both `tests/README.md` and `tests/CMakeLists.txt` in the same change; prefer existing `benchmark`, `geometry`, and `slo` labels where sufficient.
- [ ] Run focused benchmark/test targets and structural checks.

## Docs
- [ ] Update `benchmarks/geometry/README.md` with manifest schema, fixture policy, and run commands.
- [ ] Update benchmark docs if a shared manifest convention is introduced.
- [ ] Cross-link from `docs/reviews/2026-05-12-src-geometry-gap-analysis.md` only if the review is materially revised.

## Acceptance criteria
- [ ] Geometry benchmarks have at least one real smoke benchmark instead of a placeholder translation unit.
- [ ] Benchmark cases have stable IDs and manifest-driven configuration.
- [ ] Smoke outputs are deterministic and machine-readable.
- [ ] No performance claims are made without baseline comparison.
- [ ] Relevant benchmark and docs checks pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicBenchmarks
ctest --test-dir build/ci --output-on-failure -L 'benchmark|geometry' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not add heavy datasets or slow default tests.
- Do not introduce renderer/runtime dependencies into geometry benchmarks.
- Do not claim performance improvements without baseline evidence.

