---
id: RUNTIME-133
theme: none
depends_on: [GEOM-036]
completed_on: 2026-06-28
---
# RUNTIME-133 — Method result & figure data-export seam (CSV/JSON)

## Goal
- Add a runtime data-export seam that writes method results and GEOM-036 quality-metric arrays (RDF, RAPS, periodogram, NN histogram, per-level counts, min-distance curves, throughput) to deterministic CSV/JSON plus a small run-manifest (dataset, N, seeds, config knobs), so a paper author can regenerate publication-quality plots with external scripts (e.g. the sibling repo's `generate_figures.py`) from reproducible numeric dumps.

## Non-goals
- No in-engine plotting/charting or image rasterization (rendered figures are GRAPHICS-109; vector plots stay in external tools).
- No new metric computation (that is GEOM-036); this task only serializes arrays it is handed.
- No change to the benchmark result-JSON schema; this is a complementary analysis-export lane.

## Context
- Status: done at `CPUContracted`. Depends on GEOM-036 (retired; the metric arrays to export).
- Commit reference: this retirement commit.
- Owning subsystem/layer: `runtime` (composition/IO wiring; CPU-only file writing). Geometry/methods stay IO-free; runtime owns serialization.
- Today only benchmark runners emit JSON (`benchmarks/runners/BenchmarkSmokeRunner.cpp`, schema in `docs/benchmarking/result-json-schema.md`); there is **no** general CSV/JSON export for metric arrays or point sets for offline plotting. This seam closes the "generate the plots I need for the paper" gap by making in-engine results reproducible inputs to existing plotting scripts.

## Slice plan
- **Slice A (this slice).** Add `Extrinsic.Runtime.MethodFigureExport`, a CPU/headless runtime-owned data-export seam with deterministic numeric formatting, metric CSV/JSON writers, a run-manifest JSON writer, and point-set CSV/PLY writers. Writers use atomic temp-file replacement where possible, fail closed with explicit diagnostics, and leave geometry/methods IO-free. Add unit tests, docs, module inventory, session-brief regeneration, and retire the task at `CPUContracted`.

## Required changes
- [x] Add a runtime export module that serializes named numeric arrays (bin centers + values) and scalar summaries to CSV and JSON with stable column ordering and fixed float formatting.
- [x] Emit a run-manifest (JSON) capturing dataset id, point count, all sampler-config knobs, seeds, backend identity, and engine/version stamp, so each figure dump is self-describing and reproducible.
- [x] Add a point-set export (positions + per-point `level`/`phase`/`splat_radius`) to CSV/PLY for external renderers and the `generate_figures.py`-style scripts.
- [x] Make all writers deterministic and fail closed on unwritable paths with explicit diagnostics.

## Tests
- [x] Add `unit;runtime` (headless) tests round-tripping a known metric set: written CSV/JSON parses back to identical values; column order and float formatting are stable.
- [x] Add a run-manifest test asserting all config knobs and seeds are captured and that two runs with identical inputs produce byte-identical manifests.
- [x] Add a failure test for unwritable output paths (explicit diagnostic, no partial/corrupt file left behind).

## Docs
- [x] Document the export formats and the run-manifest schema under `docs/methods/` or `docs/architecture/`, and note how to feed the dumps into external plotting (reference the sibling repo `FIGURES.md` metric definitions).
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] Metric arrays, scalar summaries, point sets, and a reproducible run-manifest export to deterministic CSV/JSON/PLY.
- [x] Round-trip and determinism tests pass on the default CPU/headless gate; unwritable-path failures are explicit.
- [x] Geometry/methods remain IO-free; runtime owns serialization; no GPU dependency.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'FigureExport|MetricExport|RunManifest' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Completed verification:

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicRuntimeUnitTests
ctest --test-dir build/ci --output-on-failure -R 'MethodFigureExport|MetricExport|RunManifest' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/agents/generate_session_brief.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/docs/check_docs_sync.py --root . --diff-mode --base-ref origin/main
python3 tools/agents/check_task_policy.py --root . --strict
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Mixing mechanical file moves with semantic refactors.
- Adding plotting/charting/rasterization in this task (data export only).
- Putting file IO into geometry/method packages; serialization stays in runtime.
- Introducing a GPU dependency.

## Maturity
- Target: `CPUContracted` deterministic export seam.
- No `Operational` follow-up is owed; this task has no GPU/backend seam.
