---
id: RORG-135
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive, operator-batched behavior-preserving refactor; evidence is the three commits, their review and the gates Codex runs."
owner: claude-opus-5-5
branch: codex/opus-processing-selection-simplify
worktree: /tmp/intrinsic-simplify-opus
claimed_at: "2026-09-23T00:00:00Z"
contract_schema: 1
contracts: [runtime.processing-compilation-locality, repo.source-documentation, geometry.element-domain-sources, geometry.property-coherence]
---
# RORG-135 — Consolidate density/spacing, selection-ID passes and GPU row pagination

## Goal
- Implement the first three findings of the 2026-09-23 simplification review, explicitly batched by the operator, as three separate behavior-preserving commits: (1) density/spacing share one private lifecycle, (2) the four selection-ID passes become one module, (3) Outliers/Bilateral/Normals reuse the existing GPU row-pagination owner.

## Context
- Out of scope: review finding four (GPU readback lifecycle), ProgressivePoisson GPU scaffolding, `*.Frame.cpp` and Config/Types compile-locality boundaries, SelectionOutline, public result/config types, numerical kernels.
- Right-sizing: no public lifecycle template, scheduler, method-name switch, virtual interface or policy-flag set. Justified shared mechanism only where the contract is identical; distinct neighbor/decoding contracts stay local.
- Reuse (slice 1): capture/publication reuse `CapturePointScalarField`/`PointScalarFieldCurrent`/`PublishPointScalarField` (`PointProperties.cpp`) and `AcquirePointIndex`/`AppendPointKnnRows`/`AdvancePointKnnRows` (`RadiusRows.cpp`). The duplicated capture/index/job/publication body becomes one anonymous-namespace template in `Runtime.GeometryProcessingOperations.Density.cpp`, instantiated for `KernelDensityMethod` and `PointSpacingMethod`. Each record keeps its config/result types, validator, statistics, history label, failure text and self-neighbor floor (density `min(N,max(k,2)+1)`, spacing `min(N,max(k,1)+1)`). Shared diagnostics compose the method noun and keep the previous exact strings. The k=1 width difference is already covered by the all-domain CPU LBVH/octree parity loops (k in {1,2,63}), whose neighbor-row cardinality validation rejects a wrong width.
- Reuse (slice 2): `RecordOpaqueSurfaceBucket` (`Graphics.CullingSystem`) stays the surface draw owner for entity/face IDs. One module `Extrinsic.Graphics.Pass.Selection.Id` keeps four named pass types over a non-virtual `SelectionIdPassState` (selection reference, pipeline, ready guard); edge (indexed `Lines`) and point (non-indexed `SelectionPoints`) records keep their bucket checks and draw calls and share one TU-local push-constant writer. `NullRenderer::RecordSelectionPrimitiveIdPass` replaces three identical face/edge/point guards; the entity route keeps its lease selection and per-record pipeline binding. Removed surface: the old four module names, the unused no-op `Execute(cmd, camera)` overloads and the `Selection*IdPass` type aliases (no production users; the alias assertion in `Test.SelectionSystemContracts.cpp` is dropped).
- Reuse (slice 3): the existing private owner `RadiusRows.hpp/.cpp` gains `GpuRowPages` + `AdvanceGpuRowPages`, a header template for the identical cursor (pending/failed/ready batch states, recycling on matching row count, query counts, first-to-last timing, batch drop on failure). `AdvancePointRadiusRows`/`AdvancePointKnnRows` and the outlier, bilateral and normal stages use it. Each passes one per-batch decoder and one submission lambda (compile-time, no `std::function`, no per-neighbor callback or extra allocation). Kept local: outlier radius counts with capacity 1 and slot exclusions, statistical-only exclusions and Compute-time decoding; bilateral source-row decoding in iteration 0, compact private IDs afterwards, per-iteration reset and cumulative timing/batch counts; normal CSR offsets, the 1024-candidate cap and `ActualBackend`. Only the radius owner checks the expected batch row count, so no fail-closed guard is added. `RadiusRowsState`/`KnnRowsState` become one `RowsState`.
- Slice-3 obstacle: the review estimate of 90–130 fewer lines does not hold. Each site dropped about 17 skeleton lines, but the decoders/submissions stay distinct, lambda wrappers and result mapping cost about 10 lines per site, and the template itself is about 30 lines. The gain is one cursor owner instead of five copies.

| Slice | Production files before → after | Physical lines before → after |
| --- | --- | --- |
| 1 Density/Spacing TUs | 2 → 1 | 564 → 400 (plus 2 CMake list lines removed) |
| 2 Selection-ID passes | 8 → 2 | 325 → 155; `Graphics.Renderer.cpp` −47 net, pass CMake −6 |
| 3 Outliers/Bilateral/Normals + RadiusRows owner | 5 → 5 | 1,858 → 1,846; four existing clients rename the state enum only |

## Acceptance criteria
- [ ] Density/spacing public behavior, job identity/guard, GPU prerequisite ordering, terminal delivery/cancellation, expired-attachment suppression, staleness, deleted slots and checked conversion are unchanged (focused density/spacing contracts and GPU smokes pass).
- [ ] Selection-ID passes keep pass identities, shaders/pipelines, attachments, recipe order, failure statuses, draw kinds and push-constant layout (selection contracts, frame lifecycle and Vulkan picking pass).
- [ ] Outliers/Bilateral/Normals keep radius vs kNN semantics, decoding, caps and timing; any fail-closed guard added by sharing has focused coverage (contracts and Vulkan smokes pass).
- [ ] `ProcessingCompilationLocality.*`, layering, module inventory, doc links, task policy and full CPU/ASan/UBSan/Vulkan gates pass on the combined tree.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'KernelDensity|PointSpacing|Selection|RendererFrameLifecycle|Outlier|Bilateral|NormalEstimation|DensityWeight|Keypoint|Descriptor|ProcessingCompilationLocality' --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
cmake --preset ci-vulkan && cmake --build --preset ci-vulkan --target IntrinsicTests
ctest --test-dir build/ci-vulkan --output-on-failure -L 'gpu' -L 'vulkan' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/generate_session_brief.py --check
git diff --check
```

## Log
- 2026-09-23: Opened with slice 1. C++ builds and tests are run by Codex in the main checkout; the writer worktree runs structural checks only.
