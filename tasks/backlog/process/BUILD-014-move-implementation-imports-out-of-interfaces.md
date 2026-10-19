---
id: BUILD-014
theme: H
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive session; fixed source diff, matched local measurements and verification retained
contract_schema: 1
contracts: [runtime.editor-prepared-frame-locality, runtime.engine-compilation-locality, repo.source-documentation]
---
# BUILD-014 — Move implementation-only imports out of module interfaces

## Goal
Shrink interface dependency closures and BMI write cost: an import that only
function bodies need moves from the `.cppm` interface to its implementation unit,
and `export import` remains only where importers use the re-exported names.
Start with the largest BMIs, the costliest interface compiles and the hub
interfaces with the most transitive importers.

## Context (descriptive, local, not claim-grade)
Measured 2026-10-19 at `3302d06e3`, `build/ci` Debug, Clang 23:
- 449 interface and 426 implementation units in `src/`; the longest interface
  import chain is 11 units. `build/ci` holds 554 BMIs totalling 4.7 GB (median
  7.7 MB). Largest: `Runtime.Detail.EditorFeatures` 65.4 MB,
  `Runtime.EditorFeatureInternals` 65.1 MB, `Runtime.SandboxEditorFacades`
  65.1 MB, `Runtime.Private.EditorFeatures` 62.3 MB,
  `Sandbox.Editor.PanelSupport` 59.2 MB, `Runtime.AssetImportPipeline` 43.5 MB.
- `Runtime.SceneInteractionModule.cppm` has 236 lines and 16 direct imports, and
  its compile passes 71 BMIs. It takes 16.0 s locally, 13.8 s of it `WriteAST`.
  It lies on the duration-weighted interface critical path.
- Transitive interface importers: `Core.StrongHandle` 190, `Core.Error` 172,
  `Geometry.Properties.Types` 145, `RHI.Handles` 121, `Geometry.Linalg` 116,
  `Geometry.Properties` 115. 110 interfaces use `export import`.
- Every importer needs all transitive BMIs, so a body-only import in an
  interface couples every importer to it for build order and invalidation.
- CI compiles with Clang 20 (full BMIs by default); Clang 23 already emits
  reduced BMIs (`RUNTIME-268`). Changing the BMI mode or compiler is out of scope.
- Reuse prior cuts and their guards instead of re-deriving them: `RUNTIME-265`
  to `RUNTIME-268`, `GRAPHICS-138`, `GRAPHICS-144`, the `CompilationLocality`
  CTest guards and `tools/analysis/compile_hotspots.py`. `RUNTIME-268` found that
  declaration shape and reachable types drive `WriteAST`.

## Scope
- For each selected interface, classify every import as needed by exported
  declarations (signature types, bases, inline or template bodies) or body-only.
  Move body-only imports to the implementation unit. Replace `export import` with
  `import` where no importer uses the re-exported names, and add explicit imports
  to the importers that do.
- Non-goals: no API or behavior change; no new facade or aggregate modules; no
  module merges or splits beyond the moved imports; no Pimpl or type erasure
  without a separate decision; no BMI-mode or compiler change; no template
  instantiation work (`BUILD-013`).

## Acceptance criteria
- [ ] A fresh measurement at a frozen revision (BMI size, interface `WriteAST`,
      transitive importer count) selects the candidates and their stop rule
      before any edit: at least the six largest BMIs, `SceneInteractionModule`
      and the hub interfaces above.
- [ ] Each touched interface records its import classification. Body-only
      imports are moved, unused re-exports removed, and layering checks,
      `CompilationLocality` guards and a minimum-Clang-20 build pass.
- [ ] Timed comparisons are results-bearing: each slice follows
      `experiment-preflight` through the `BUILD-009` protocol, with a frozen
      manifest and gates (clean compiler CPU sum, BMI sizes, touched-interface
      `WriteAST` and one interface-edit scenario per touched hub) set before the
      first timed sample.
- [ ] The module inventory is regenerated; changed module dependencies are
      documented; the default CPU gate and an independent review pass; results
      are recorded as bounded observations.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests --parallel 4
ctest --test-dir build/ci --output-on-failure -R 'CompilationLocality' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/analysis/compile_hotspots.py --build-dir build/ci --top 40
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/check_task_policy.py --root . --strict
```
