---
id: RUNTIME-314
theme: F
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: mechanical reuse of existing helpers with no behavior change; evidence is the diff, existing tests and CI.
contract_schema: 1
contracts: [geometry.element-domain-sources, geometry.property-coherence, repo.source-documentation]
---
# RUNTIME-314 — Reuse existing runtime helpers instead of local copies

## Goal
Replace local re-implementations in `src/runtime` with the helpers that already own the
behavior, with no semantic change. Per AGENTS.md §5, no mechanical rename is mixed with
algorithm changes; each item below is its own reviewable commit.

## Context
Source: 2026-10-01 duplication/consistency audit (finding set 4), re-verified at `665c693dd`.
The UI-side items from the same audit are in [UI-072](../ui/UI-072-adopt-shared-panel-support-helpers.md).

1. `Geometry::Validation::IsFinite(vec2/vec3/dvec3)` (`src/geometry/Geometry.Validation.cppm:14-18`)
   is imported by no runtime file, while about 25 local finite-checks exist, for example
   `Runtime.GeometryProcessingOperations.PointProperties.cpp` `FinitePosition` (~164) **and**
   `IsFiniteGeometryPosition` (~596) in the same translation unit, `Runtime.ParameterizationOperations.cpp`,
   `Runtime.GeometryPlanBuilders.{Mesh,MeshPrimitiveView,PointCloud,Graph}.cpp`,
   `Runtime.AssetWorkflowGeometryMaterialization.cpp`, `...GpuPositions.cpp`, and the
   consolidation module/GPU files. Some copies deliberately differ (for example a tolerance or a
   different component set): audit each, replace the identical ones, and keep a documented local
   check only where behavior must differ. Check the layering allowlist before adding the import.
2. `"v:position"` literals: 52 in `src/runtime` against 46 uses of
   `ECS::Components::GeometrySources::PropertyNames::kPosition`
   (`src/ecs/Components/ECS.Component.GeometrySources.cppm`). About 15 are the defaults of
   `Modules/*/Runtime.*Config.cppm`, plus `MeshFieldOperations.cppm`, `Smoothing.cpp`,
   `HarmonicField.cpp`, the agent `Detail.hpp` `kPositionsDefaultProperty` and `Operations.cpp`. Config
   defaults are serialized strings: confirm each stays byte-identical in schema/golden output.
3. Multi-output scalar publication with undo is hand-rolled: `...Descriptors.cpp` (33 columns) and
   `...Keypoints.cpp` (2 columns) each build before/after `GeometryScalarPropertySnapshot`s, a
   guarded `mutate` lambda and `CommandHistory->Execute`, while `PublishPointScalarField`
   (`...PointFields.hpp`, `...PointProperties.cpp`) handles one output. Add a span-of-outputs
   overload and migrate those two. The outlier multi-output path stays with
   [RUNTIME-311](RUNTIME-311-unify-gpu-scalar-outlier-transaction-lifecycle.md).
4. `GpuRowPages`/`AdvanceGpuRowPages` (`...RadiusRows.hpp`, with the GRAPHICS-153 spare-batch
   reuse) is bypassed by `...Construction.cpp` (~319-380, own `Batch`/`NextQuery`/`GpuStarted`
   cursor) and by the near-identical `AdvanceGpu` in `Descriptors.cpp` (~195) and
   `Keypoints.cpp` (~211). RUNTIME-308 owns construction residency only; coordinate if it is
   already rewriting that cursor.

Not in scope: the per-job setup helper ([RUNTIME-313](../../done/RUNTIME-313-queued-editor-job-setup-and-completion-helper.md)),
the GPU transaction lifecycle (RUNTIME-311), and any change to what is considered finite.

## Acceptance criteria
- [ ] Identical local finite-checks are replaced by `Geometry::Validation::IsFinite`; each retained local variant has a one-line reason, and the duplicate pair in `PointProperties.cpp` is gone.
- [ ] No `"v:position"` literal remains in `src/runtime` outside tests where `kPosition` is usable; serialized config defaults and schema output are unchanged.
- [ ] Descriptors and Keypoints publish through one multi-output `PublishPointScalarField` overload with the same atomic single undo entry and guard semantics.
- [ ] Construction, Descriptors and Keypoints advance GPU rows through `GpuRowPages`/`AdvanceGpuRowPages`, or the task records why one cannot.
- [ ] Existing tests pass without edits to their expectations; layering check passes.

## Verification
```bash
cmake --build build/ci -j$(nproc)
ctest --test-dir build/ci -LE 'gpu|vulkan|slow|flaky-quarantine' --output-on-failure --timeout 60 -j$(nproc)
cmake --build build/ci-vulkan -j$(nproc)
ctest --test-dir build/ci-vulkan -R 'Descriptor|Keypoint|Construction' -L gpu -L vulkan --output-on-failure --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
```
