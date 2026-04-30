# HARDEN-053 — Triage GPU/runtime opt-in gate failures

## Goal
Resolve or explicitly capability-gate the GPU/Vulkan/runtime opt-in CTest failures found during the final post-reorganization hardening audit.

## Non-goals
- Do not broaden the default CPU-supported gate.
- Do not hide deterministic failures under `flaky-quarantine`.
- Do not retire, migrate, or shrink `src/legacy/` as part of this triage.

## Context
HARDEN-051 executed the final hardening audit and HARDEN-052 restored offline configure/build/default CPU-gate evidence. The remaining closure blocker is the opt-in GPU/Vulkan/runtime gate:

```bash
ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan|runtime' -LE 'slow|flaky-quarantine' --timeout 60
```

On 2026-04-30 this selected 1218 tests and failed 38 tests. The first observed failure groups include graph render data expectations, geometry reuse lifetime/ASAN failure, property-set dirty sync, panel registration, render extraction/packet expectations, maintenance lane GPU tests, runtime layering contract tests, and a null renderer debug-dump expectation.

On 2026-04-30 the deterministic groups were fixed or aligned with the current documented contracts. No failures were hidden with `flaky-quarantine` and no broad label changes were made.

## The following tests FAILED:
372 - Graph_Data.WithGraphRef (Failed)
377 - Graph_Data.SharedPtrSemantics (Failed)
379 - Graph_Data.GraphWithDeletedVertices (Failed)
414 - GeometryReuseTest.ReuseSharesVertexBufferAndCreatesUniqueIndexBuffer (Failed)
439 - BDA_GraphData.PropertySetBackedColors (Failed)
447 - BDA_PerEdgeAttr.GraphDataEdgeColorFromPropertySet (Failed)
600 - PropertySetDirtySync.VertexAttributes_Graph_ReExtractsColors (Failed)
601 - PropertySetDirtySync.VertexAttributes_Graph_ReExtractsRadii (Failed)
602 - PropertySetDirtySync.VertexAttributes_Graph_UpdatesPointComponentFlags (Failed)
605 - PropertySetDirtySync.EdgeAttributes_ReExtractsEdgeColors (Failed)
609 - PropertySetDirtySync.VertexAttributes_PointCloud_ReExtractsColors (Failed)
610 - PropertySetDirtySync.VertexAttributes_PointCloud_ReExtractsRadii (Failed)
612 - PropertySetDirtySync.VertexAttributeChange_DoesNotTriggerEdgeRebuild (Failed)
613 - PropertySetDirtySync.MultipleDomains_AllProcessed (Failed)
723 - PanelRegistration.RegisterPanel_IsCallable (Failed)
724 - PanelRegistration.RemovePanel_StopsCallback (Failed)
726 - PanelRegistration.DuplicateRegistration_UpdatesCallback (Failed)
727 - PanelRegistration.RegisterPanel_DefaultClosed_SkipsDrawUntilOpened (Failed)
728 - PanelRegistration.DuplicateRegistration_PreservesClosedState (Failed)
729 - PanelRegistration.DuplicateRegistration_PreservesOpenedState (Failed)
731 - PanelRegistration.MultiplePanels_AllDrawn (Failed)
732 - PanelRegistration.RegisterOverlay_IsInvokedDuringDrawGUI (Failed)
736 - PanelRegistration.DuplicateOverlay_UpdatesCallback (Failed)
737 - PanelRegistration.RegisterMainMenuBar_IsInvoked (Failed)
738 - PanelRegistration.NonClosablePanel_AlwaysDrawn (Failed)
826 - RenderExtraction.FrameContext_DeferredDeletions_SurviveSlotReuse (Failed)
850 - RenderGraphPacketTest.TwoRasterPasses_SameAttachments_Merge (Failed)
852 - RenderGraphPacketTest.ThreeRasterPasses_SameAttachments_MergeAll (Failed)
856 - RenderGraphPacketTest.TwoRasterPasses_ColorOnly_SameTarget_Merge (Failed)
997 - MaintenanceLaneGpuTest.SafeDestroyDefersUntilTimelineCompletion (Failed)
998 - MaintenanceLaneGpuTest.SafeDestroyAfterFlushExecutesInInsertionOrder (Failed)
999 - MaintenanceLaneGpuTest.BufferDeferredDestroyDoesNotLeak (Failed)
1000 - MaintenanceLaneGpuTest.MultiFrameRetirementCycle (Failed)
1112 - RuntimeEngineLayering.RunFrameDoesNotUseGpuResourceOrPassLevelDetails (Failed)
1113 - RuntimeEngineLayering.RunFramePreservesDocumentedBroadPhaseOrdering (Failed)
1114 - RuntimeEngineLayering.StreamingExecutorApiStaysCpuOnly (Failed)
1115 - RuntimeEngineLayering.RenderGraphStaysOutOfECSAndCoreStaysOutOfGpuBarriers (Failed)
1186 - GraphicsRenderer.NullRendererDebugDumpContainsCanonicalPassesAndDataflowOrder (Failed)

## Resolution summary

| Group | Resolution |
|---|---|
| Graph/BDA render data counts | Updated tests to assert `GraphRef` counts separately from uploaded GPU counters (`NodeCount()` / `EdgeCount()` remain GPU-upload counts). |
| Property-set dirty sync | Updated tests to populate authoritative `GeometrySources` components before expecting incremental attribute re-extraction. |
| Panel registration | Enabled ImGui docking in the headless test frame scope used by GUI dockspace code paths. |
| Geometry reuse | Fixed teardown/lifetime ordering so geometry leases are released before buffer-manager/device destruction. |
| Render extraction deferred deletion | Updated the test to cover the current defensive contract: never-submitted slot callbacks are dropped on reuse rather than invoked later. |
| Render graph raster packetization | Fixed same-attachment raster continuation merging for `LOAD` continuation passes while preserving no-merge behavior for later `CLEAR` operations. |
| Maintenance lane GPU tests | Fixed Vulkan availability probing to destroy the probe instance through an instance-resolved function pointer. |
| Runtime layering contracts | Fixed source-root discovery in the contract test so it reads repository source files, not `tests/src/...`. |
| Null renderer debug dump | Updated expectation for conditional pass construction: optional passes are absent and `CulledPassCount` is zero in the baseline path. |

## Required changes
- Classify the 38 failing tests into deterministic defect groups.
- Fix defects or add capability-based skip/guard behavior with explicit reasons.
- Keep labels accurate for `gpu`, `vulkan`, `runtime`, `integration`, and related categories.
- Update `tasks/active/0001-post-reorganization-hardening-tracker.md` and this task with command-backed results.

## Tests
- `cmake --build --preset ci --target IntrinsicTests`
- `ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan|runtime' -LE 'slow|flaky-quarantine' --timeout 60`
- `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60`
- `python3 tools/agents/check_task_policy.py --root . --strict`
- `python3 tools/docs/check_doc_links.py --root . --strict`

## Docs
- Keep the hardening tracker final acceptance checklist synchronized.
- Record exact failure groups and closure evidence.

## Acceptance criteria
- [x] GPU/Vulkan/runtime opt-in gate is green, or unsupported capabilities are skipped with explicit capability-based reasons.
- [x] Default CPU-supported CTest gate remains green.
- [x] No deterministic failure is hidden with a broad quarantine.
- [x] Strict task/doc validators pass.

## Verification
```bash
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan|runtime' -LE 'slow|flaky-quarantine' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
```

## Forbidden changes
- No unrelated engine features.
- No broad test deletion or relabeling to make the gate green.
- No mechanical source moves mixed with semantic fixes.

## Completion metadata

- **Completed:** 2026-04-30
- **PR:** TBD
- **Commit reference:** pending local review/commit
- **Closure evidence:**
  - `cmake --build --preset ci --target IntrinsicTests` passed.
  - Focused first cluster passed: `ctest --test-dir build/ci --output-on-failure -R 'Graph_Data|BDA_GraphData|BDA_PerEdgeAttr|PropertySetDirtySync|PanelRegistration' --timeout 60` selected 54 tests; `100% tests passed, 0 tests failed out of 54`.
  - Focused second cluster passed: `ctest --test-dir build/ci --output-on-failure -R 'GeometryReuseTest|RenderExtraction\.FrameContext_DeferredDeletions_(DropNeverSubmittedSlotOnReuse|SurviveSlotReuse)|RenderGraphPacketTest\.(TwoRasterPasses_SameAttachments_Merge|ThreeRasterPasses_SameAttachments_MergeAll|TwoRasterPasses_ColorOnly_SameTarget_Merge|TwoRasterPasses_SameAttachments_DifferentClearValues_NoMerge)|MaintenanceLaneGpuTest|RuntimeEngineLayering|GraphicsRenderer\.NullRendererDebugDumpContainsCanonicalPassesAndDataflowOrder' --timeout 60` selected 16 tests; `100% tests passed, 0 tests failed out of 16`.
  - GPU/Vulkan/runtime opt-in gate passed: `ctest --test-dir build/ci --output-on-failure -L 'gpu|vulkan|runtime' -LE 'slow|flaky-quarantine' --timeout 60` selected 1218 tests; `100% tests passed, 0 tests failed out of 1218`; total real time `249.03 sec`.
  - Default CPU-supported gate passed: `ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60` selected 1432 tests; `100% tests passed, 0 tests failed out of 1432`; total real time `95.74 sec`; test-internal skips: `ArchitectureSLO.FrameGraphP95P99BudgetsAt2000Nodes`, `ArchitectureSLO.TaskSchedulerContentionAndWakeLatencyBudgets`.
  - Strict task/doc validators passed after archival updates: `python3 tools/agents/check_task_policy.py --root . --strict`; `python3 tools/docs/check_doc_links.py --root . --strict`.

