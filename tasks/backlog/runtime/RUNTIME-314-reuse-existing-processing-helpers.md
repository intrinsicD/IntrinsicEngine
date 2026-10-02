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

## Slice log
- Slice 1 (`11f2fdcc9`): vec2/vec3 finite-check copies replaced by `Geometry::Validation::IsFinite`; the `FinitePosition`/`IsFiniteGeometryPosition` pair in `PointProperties.cpp` and their header declarations are gone. Deliberately kept: `Runtime.VisualizationRecipes.cpp` (its `IsFinite(float)` overload also serves templated scalar sources, and `Geometry::Validation::IsFinite(double)` would change how a `double` is judged), the `glm::vec4`/`glm::quat`/`glm::mat4` checks in `VertexAttributeBinding`, `TextureBakeModule` and `AssetWorkflowModelMaterialization` (no shared overload), and the ImGui clip-rect checks (`ImVec2`/`ImVec4`).
- Slice 2 (`a46e83513`): `"v:position"` literals replaced by `GeometrySources::PropertyNames::kPosition` in runtime sources that may import it. The remaining literals are serialized config defaults in sources listed by a boundary test that forbids `Extrinsic.ECS.Components.GeometrySources` (`ProcessingCompilationLocality.ConfigPropertyTypes`, or `ConsolidationContracts` for `PointCloudConsolidationTypes`); moving the constant below that boundary is a separate decision. `RuntimeReuseDriftGuard` in `Test.QueuedEditorJobContract.cpp` prevents both patterns from coming back.
- Review fixes (slice 3): slice 2 wrongly claimed every `*Config`/`*Types` file sat inside that boundary. `Runtime.ClusteringTypes.cpp` (an implementation unit that already imports `GeometryAvailability`) now uses `kPosition`; `Runtime.GeodesicsConfig.{cppm,cpp}`, `Runtime.PointSamplingConfig.cppm` and `Runtime.CoherentPointDriftConfig.cppm` joined `ConfigPropertyTypes` deliberately, like their sibling config interfaces. The guard now uses an explicit allowlist that must be a subset of those two boundaries' `--check-source` lists and fails on stale entries. Its finite regex now matches member chains and scalar `IsFinite(v.x) && ...` wrappers, which found `Runtime.CameraFocusCommand.cpp` (`sphere.Center`, now `Geometry::Validation::IsFinite`) plus two deliberate variants (`VisualizationRecipes.cpp`, `VisualizationEditingOperations.Actions.cpp` vec4 default); every kept variant carries a one-line source reason.
- Slice 4 (items 3 and 4): reuse decision: the owner of undoable scalar publication is `PublishPointScalarField` (`...PointFields.hpp`, `...PointProperties.cpp`); it gained a span-of-`PointScalarOutput` overload (float or uint32 values per column) and the single-output form now delegates to it. Descriptors (33 columns) and Keypoints (mask + score, also the `PublishPair` sink of the RUNTIME-311 resident transaction, whose lifecycle is unchanged) publish through it: every column is prepared before history runs and every write is checked before the first, so the single history entry, its label, the input/output revision guards and the status mapping are unchanged. Recorded difference: an apply refused inside history (`InvalidCommand`, unreachable while the watches hold) now reports the method's representability message instead of "publication rejected by history checks", as the density families already do. Item 4: Descriptors and Keypoints already advanced through `AdvancePointRadiusRows` -> `AdvanceGpuRowPages` (their `AdvanceGpu` only adds the stale guard and result fields), so only Construction moved: its kNN pages now run on `GpuRowPages`, keeping its per-page layout checks, the CPU re-sort, batch counts and GPU time over consumed pages, the release of the completed batch and every diagnostic. Production lines 132 -> 154 (Construction's consume/queue lambdas re-indent the old body; the overload adds the column record). Contract tests: `DescriptorAnalysis.MultiOutputPublicationIsAtomicForPreparationAndUndoGuards`, `KeypointAnalysis.PairedPublicationUndoIsRefusedWhenEitherOutputChanged` and `QueuedEditorJobContract.VulkanConstructionPagesThroughTheSharedRowCursor` (MockRHI: 3/3/2 pages, one batch allocation, failure after the first consumed page) pass on the old and the new code, pinning the behavior; `RuntimeReuseDriftGuard` now also fails on hand-rolled scalar history publication and unpaged kNN/radius queues (the old Descriptors, Keypoints and Construction fail it). Deliberately kept: `Runtime.MeshFieldOperations.Geodesics.cpp` (double distances with infinity sentinels) and `Runtime.ScalarRidgeOperations.cpp` (vertex and edge outputs on two slot sets) publish themselves; Outliers stays with RUNTIME-311. Candidate, not migrated: `Runtime.PointCloudConsolidationModule.cpp` pages `QueueGpuRadius` with its own cursor. **Pending:** the `build/ci-vulkan` `Descriptor|Keypoint|Construction` gpu;vulkan smokes were not run (the GPU was reserved for the operator); this item stays open until they pass.
- Slice 5 (review follow-up): `AdvanceGpuRowPages` documents that `queue` never returns null (a refused page is a Failed batch with its diagnostic; `SpatialIndexCache` queues never return null), and Construction's dead "query submission rejected" branch is gone. `QueuedEditorJobContract.VulkanConstructionRefusesGridQueriesOutsideTheCoordinateRange` pins the out-of-range grid-query refusal (no submission, no consumed page). Like the slice-4 contract tests it passes on the old and the new code: these tests pin behavior, while `RuntimeReuseDriftGuard` is the detector that fails on the old hand-rolled code. RUNTIME-314 stays open for the Vulkan reruns (`PointLBVHGpuSmoke.Descriptor*`/`Keypoint*`, `PointConstructionGpuSmoke.*`), run together with RUNTIME-311's once the GPU is free.
