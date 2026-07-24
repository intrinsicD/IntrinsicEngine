---
id: RUNTIME-204
theme: F
depends_on: []
maturity_target: Retired
---
# RUNTIME-204 — Withdraw dormant method-figure export from runtime

## Goal

- Remove `Runtime.MethodFigureExport` from the production runtime surface and
  place any still-needed deterministic serialization/writer helpers directly
  in the concrete benchmark/tool/test owner that uses them.

## Non-goals

- No new report framework, artifact service, export registry, runtime UI, or
  general-purpose data frame.
- No change to method numerical results or benchmark result schemas.
- No preservation of a production module solely because its unit tests exist.

## Context

- `Runtime.MethodFigureExport` is imported by its unit test but has no
  production caller. It serializes copied metric/point data and writes files,
  which is tooling/benchmark behavior unless a concrete runtime workflow owns
  it.
- `RenderArtifactRegistry` and render-artifact publication have real runtime
  ownership and remain separate; this task does not fold method data into
  renderer artifacts.
- The smallest endpoint is no runtime module. A concrete benchmark/tool may
  keep private serializers; otherwise the unused code and tests are deleted.

## Required changes

- [ ] Re-run the production consumer census and document the zero-consumer
      result at implementation time.
- [ ] Move any serializer/writer required by an existing benchmark/tool into
      that target as private implementation; do not export it as an engine
      module.
- [ ] Delete `Runtime.MethodFigureExport`, its runtime CMake entries,
      production documentation, and unit tests that only validate otherwise
      unused runtime functionality.
- [ ] Preserve atomic-write/path-validation coverage only where a concrete
      remaining tool actually performs those writes.

## Tests

- [ ] Existing benchmark/tool tests cover any retained private serializer at
      its real call site.
- [ ] The default CPU gate passes after module and direct unit-test removal.
- [ ] Structural source/module inventory checks prove no production
      `MethodFigureExport` symbol remains.

## Docs

- [ ] Remove the runtime module inventory/README entry and point any real
      exporter documentation at its concrete tool.
- [ ] Regenerate the module inventory and refresh task/session records.

## Acceptance criteria

- [ ] Runtime exports no method-figure serializer/writer surface without a
      runtime consumer.
- [ ] Any retained code is private to a concrete benchmark/tool and tested
      through that workflow.
- [ ] The old module, CMake entries, and orphan tests are deleted.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Replacing the module with an equally unused export/artifact service.
- Moving runtime ownership into a lower engine layer.
- Deleting coverage for a concrete remaining tool without replacement.

## Maturity

- Target: `Retired`; closure is the absence of the dormant production surface,
  with any surviving helper localized and workflow-tested.
