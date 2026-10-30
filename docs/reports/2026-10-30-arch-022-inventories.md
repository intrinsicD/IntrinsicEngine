# ARCH-022 slice 1: inventories for rules R5 and R7

Rules R5 and R7 of the architecture target ([Engine and feature modules](../architecture/engine-and-feature-modules.md)),
measured on 2026-10-30 at `b73d90b46` by reading the code: nothing was built or run. Two agents produced the tables below;
every row cites file:line, and what static reading cannot decide is marked unknown. Tracked in
[`ARCH-022`](../../tasks/active/ARCH-022-engine-and-feature-modules-target.md), slice 1.

## Summary

**R7, algorithms in the job form** (the migration list the operator asked for, Q43): 53 table rows (21 GPU, 32 CPU).
The 53 count rows, not a clean partition of algorithms: a few rows group algorithms (G12, G19) or list infrastructure (G21).
- **Step API**, counted as "a step-wise API exists in the code, whether or not the runtime drives it": 11 rows, G4–G8,
  G10, C2–C4, C10b and C24.
  - The runtime drives the steps for G4–G8, G10, C3 (CPD, with n-step, pause and cancel) and C10b (the point bilateral
    filter on the CPU LBVH).
  - The step API is unused by the runtime for C2 (ICP on the CPU), C4 (consolidation on the CPU) and C24 (point sampling).
  - Farthest-point sampling (G8) and implicit smoothing (G10) are the closest to the target form.
  - GPU k-means and resident LOP run paged state machines without an exposed step API.
- **Residency.** 4 GPU paths stay resident between host steps and read back only small metrics per step (k-means, resident
  LOP, farthest-point sampling; implicit smoothing reads back 80 bytes per chained solve, so more with several solves per
  chunk). 3 are resident only within one unrolled submission (no pause, no preview).
- 4 GPU paths move full arrays every iteration (the CPD E-step, ICP, the point bilateral filter and consolidation on
  Vulkan); the last two also rebuild and re-upload an LBVH on the CPU each iteration.
- Findings that matter beyond R7:
  - CPU work in `IsReadyToApply` runs on the main thread, among it ICP's per-iteration step and the LBVH rebuilds above.
  - Six CPU algorithms run synchronously inside their command, without a job: CPU property smoothing, the harmonic-field
    robust fit, parameterization, the Laplacian eigenbasis, geodesics and CPU point sampling.
  - Progressive Poisson never runs on the GPU: `GpuExecutionAvailable` is hard-coded false, so the runtime always computes
    the CPU reference.
  - The GPU workspace pool keeps at most one idle workspace per kind, with no size classes and no budget.
  - Only CPU k-means and GPU implicit smoothing report job progress.

**R5, editor actions and MCP**, by static reachability: a tool handler calls the same function, or the inner function the
UI's wrapper ends in. Whether a tool can pass every argument a config panel writes is not verified. The counting rule is
Appendix B, method item 4: one row per distinct action. For example, the scalar Accept and Discard buttons of four
windows are four actions, while the texture bake drawn by the Inspector and by Appearance is one. The totals depend on
this rule.
- (A) 193 action rows: 103 reached by an MCP tool, 28 partly, **62 not**. 55 of the 62 change scene, config, selection or
  history: among them the gizmo drag, transform edits, New/Close scene, texture bake, the GPU Accept, Discard and Stop
  buttons (21), the interactive CPD session, primitive selection and the render recipes.
- (B) 111 state-changing entry points the UI calls: 66 reached by a tool, **45 not**: 42 not at all, 3 only partly.

Unknowns are listed in each appendix. Main-thread stall times (R7) and timings (R6) need runs and are measured separately,
under the experiment pre-flight.

## Appendix A: R7 inventory

Repository: /home/alex/Documents/IntrinsicEngine (read-only, 2026-10-30). Target: docs/architecture/engine-and-feature-modules.md:116-201 ("Jobs, preview and observation") and :213 (R7).
Paths below are relative to the repo root. `G/` = `src/graphics/renderer/`, `M/` = `src/runtime/Modules/`, `O/` = `src/runtime/Editor/Operations/`, `S/` = `src/runtime/GeometryIntegration/Runtime.SpatialIndexCache`, `Geo/` = `src/geometry/`.

Symbols: n = input points/rows, k = clusters, m = moving source points, t = projected/target samples, K = kNN width, C = radius capacity, S = chained solves (time steps x channels), s = submissions per iteration. "up" = CPU->GPU, "down" = GPU->CPU.

### Shared facts used by many rows

- **Main-thread polling.** `JobDesc::IsReadyToApply` is consulted on the main thread (src/runtime/Kernel/Runtime.JobService.cppm:214-216). Framed GPU jobs queue and observe GPU work there (src/runtime/Editor/internal/Runtime.EditorFramedGpuJob.hpp:1-3, 38-46), and so does the GPU transaction run job (O/Runtime.GpuTransactionLifecycle.hpp:38-40; .cpp:228). CPU work placed in these callbacks therefore runs on the main thread.
- **GPU compute submission.** `SpatialIndexCache::QueueGpuCompute(readbackBytes, recorder, latency)` reads back exactly `readbackBytes` bytes. Zero bytes means a completion-only submit with no transfer (S.cppm:151-165, S.cpp:1006-1045).
- **Workspace pool.** `LeaseGpuWorkspace<T>()` returns the one retained idle workspace of kind T, or creates one. At most one idle workspace per kind is kept until device-idle shutdown. There are no size classes and no budget (S.cppm:204-218).
- **Resident inputs.** `ResolveGpuPropertyInput` binds the canonical GPU copy of a property, uploaded once per property revision. Callers count the upload bytes and cache hits (e.g. M/Clustering/Runtime.ClusteringGpuState.cpp:134-138, 166-167; O/Runtime.PointScalarTransaction.cpp:101-110).
- **Output rings and Accept.** GPU transactions hold up to 3 output rings. A ring is kept until Accept or Discard. Accept does one front readback of the whole ring for each ring marked `ReadBack` (O/Runtime.GpuTransactionLifecycle.hpp:20-30, 141-149; .cpp:92-129, 279-292). Discard frees the rings (.cpp:325-332). The scalar transaction drops its workspace back to the pool as soon as the run completes, before Accept (O/Runtime.PointScalarTransaction.cpp:182, 207-208).
- **Progress.** Only two runtime paths call `ReportProgress`: CPU k-means (M/Clustering/Runtime.ClusteringModule.cpp:444-454) and GPU implicit smoothing (O/Runtime.MeshFieldOperations.Smoothing.cpp:776-786).

### A. GPU algorithms (Vulkan unless noted)

| # | algorithm | backend | entry point | iterative (what) | step form today | observation today | resident state across iterations | CPU<->GPU bytes per iteration | buffer lifetime | notes / unknowns |
|---|---|---|---|---|---|---|---|---|---|---|
| G1 | K-means (Lloyd) | Vulkan, resident paged | M/Clustering/Runtime.ClusteringGpuState.cpp:107 `Start`; recorder M/Clustering/Runtime.ClusteringGpuBackend.cpp:49 `Prepare`, :111 `Record`; advance :395 `Advance` | yes: Lloyd assign/reduce/update until converged or MaxIterations (:493-506) | Internal paged state machine (phases Initialize/Assign/Reduce/Update/Preview/Presentation, M/Clustering/Runtime.ClusteringGpuBackend.cppm:14). `Advance` is driven by `DrainCompletedTransfers` each frame (ClusteringGpuState.cpp:564-567). No exposed Init/Step API. | Metrics: diagnostics record per submission (inertia, max shift, changed flags; ClusteringGpuBackend.cppm:15-21). Preview: every `GpuPreviewInterval` iterations, GPU->GPU into the scalar ring (ClusteringGpuState.cpp:301-320, 504-506, 432-438). `GpuRun` returns iterations, submissions, previews and bytes (:253-290). Stop, Accept and Discard exist (:258-271). | Yes. Positions are a resident canonical property (:137-140). 9 workspace buffers (positions, slots, centroids, labels, previous, distances, sums, counts, stats) stay on the GPU (ClusteringGpuBackend.cpp:60-87). | Init: seeds k*12 + slot map n_slots*4 (only when slots are not the identity) (ClusteringGpuBackend.cpp:100-108), plus the property upload on a cache miss. Per iteration: 0 up; **32*s down** (diagnostics per submission, ClusteringGpuState.cpp:529-533; 32 B computed from members in ClusteringGpuBackend.cppm:15-21). s is set by the paging budget: 2^24 pairs and serial depth 2^14 per submission (M/Clustering/Runtime.KMeansPaging.TestSupport.hpp:7-13). End: centroids k*12 down (ClusteringGpuState.cpp:441-448, 529-530). Accept: labels front n_slots*4 down (O/Runtime.PointScalarTransaction.cpp:216-237). | Workspace is kept as `Idle` for the next run; one at a time (ClusteringGpuState.cpp:65-67, 89-90, 145). Label rings are kept until Accept or Discard via `EditorPointScalarTransaction` (:170-209). A failed recording is retired until shutdown (:62-64, 87-88). | This is the doc's "GPU k-means already uploads once". Convergence is decided on the host from the 32 B readback (:494-501), so detection may lag one submission. Initial centroids are computed on the CPU (`BuildInitialCentroids`, :141). |
| G2 | LOP consolidation (Strategy Lop) | Vulkan "VulkanCompute", resident paged | M/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp:622 `Start` (resident branch :657-706); :820 `RecordLopPage`; :903 `AdvanceLop` | yes: L1 attraction/repulsion iterations until no active sample or MaxIterations (:958-966) | Internal paged state machine (Upload/Initialize/Project/Grid/Publish/Reduce/Readback, :828-897). `AdvanceLop` runs from RecordFrameCommands/DrainCompletedTransfers (:1087, :1108). Stop, Accept and Discard via `GpuRun` (:770-792). | Metrics: 64 B diagnostics record (:124-143). Preview: every `GpuPreviewInterval` (default 5, M/PointCloudConsolidation/Runtime.PointCloudConsolidationConfig.cppm:80), GPU->GPU copy into the position ring (:807-818, 963-965, 1056-1063). Observation returns iterations, submissions, previews and bytes (:783-792). | Yes. Input positions are resident (:669-672). Projected A/B, weights, displacements, grids and diagnostics stay on the GPU (:195-215, 842-852). | Init: seed rows t*4 when t != n (:831-838) + `UploadInputs` (weights n*4 + t*4, displacements t*4, diagnostics and state records; :1458-1490). Per iteration: 0 up; **64 B down for the grid page + 64 B for the page that finalizes the iteration**. Other project pages are completion-only, 0 B (:1068-1072). Submissions per iteration = 1 + ceil(t / max(1, SubmissionPairs/MaxCandidates)) (:1065-1067). End: in place (t == n, same property), Accept reads back the position front n*12 (`AcceptEditorGpuPositionRun`, :749-760; O/Runtime.GeometryProcessingOperations.GpuPositions.cpp). Otherwise a final readback of t*12 (:1021-1031). | Resources are recycled into `Spare` ("kept at their capacity for the next run", :735, :1050, :1846-1847). The position ring is kept until Accept or Discard (:737). Failed recordings are retired until shutdown (:727-733). | Closest to the R7 target besides G1. |
| G3 | WLOP (isotropic) consolidation | Vulkan "VulkanCompute", producer path | Same `Start` (non-Lop branch). M/PointCloudConsolidation/Runtime.PointCloudConsolidationGpu.cpp:1493 `RecordProducerCommands` | yes: MaxIterations L1 iterations | **None.** All MaxIterations are unrolled into one frame's command recording (:1554-1640). | None during the run. Diagnostics and positions only at the end (:1648-1676). | Within the single recording, yes (all buffers on the GPU). | Init: source n*16 + projected t*16 + weights n*4 + t*4 + displacements t*4 + diagnostics 64 + state record (:1441-1490). Per iteration: 0 (no host boundary). End: positions t*16 + 64 down (:1648-1676). | Recycled into `Spare` (:1846-1847). | GPU admission: Lop, or Wlop when not anisotropic (M/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp:337-344). The size of the state record was not checked. The whole run is one GPU submission, so it cannot pause and has no preview. |
| G4 | LOP/WLOP/CLOP/EAR consolidation | "VulkanLBVH" hybrid: GPU radius neighbourhoods + CPU projection | M/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp:2537 `SubmitProjectionGpu`; GPU stage :2496 `AdvanceProjectionGpu`; CPU step `Geo/Geometry.PointCloud.Consolidation.cppm:243` `NeighborhoodProjection::Advance` (.cpp:2597) | yes: SourceDensity, Initialize, then up to MaxIterations Iterate phases (cppm:212-218) | **Yes (CPU):** `NeighborhoodProjection::Advance` per stage. The runtime chains 2 jobs per stage: neighbourhoods (main-thread `IsReadyToApply`), then the update job (:2605-2635). | None. No preview or metrics until the end (`TakeResult`, :2622). | Partial. The source LBVH is cached. The moving-sample LBVH is rebuilt every iteration: `UpdateWorkspace` does a **CPU LBVH build** and re-uploads the points (S.cpp:808-830, 517-519). This runs inside `IsReadyToApply` on the main thread (:2524-2531). | Per iteration: attraction rows (t queries on the source index): up t*12 + t*4, down t*C*8 + t*8. Repulsion: index points t*12 up + rows up t*12 + t*4, down t*C*8 + t*8 (:2489, :2516-2535; S.cpp:396-409, 555-568). C <= 1024 (:219-221). | Batches reuse `Spare` (:2485-2489). The moving workspace lives for the run (:2524-2531). | Full-array transfer every iteration in both directions. CPU LBVH rebuild on the main thread. |
| G5 | Coherent Point Drift E-step | Vulkan hybrid: device E-step, CPU M-step | M/Registration/Runtime.CoherentPointDriftGpuEStep.cpp:87 `Queue`, :163 `Pump`, :135 `Complete`; recorder G/Graphics.CoherentPointDriftEStep.cpp:82 `Record` | yes: EM iterations (driven by the CPU solver, row C3) | **Yes:** `Solver::BeginStep` / `PendingExternalRequest` / `CompleteExternalStep` (Geo/Geometry.Registration.CoherentPointDrift.cppm:247-251), driven by a coroutine job (O/Runtime.RegistrationOperations.CoherentPointDrift.cpp:468-501). | `IterationTrace` per iteration, `StageObserver`, live snapshot with trace and source preview (CPD.cpp:413-429, 331-382). Pause after n steps, and step (CPD.cpp:438-446, 971-976). | Partial. The target is uploaded once per target generation (G/Graphics.CoherentPointDriftEStep.cpp:96-109). The source is re-uploaded every iteration (:113). | Per iteration: up **m*32** (float-float pairs, Runtime.CoherentPointDriftGpuEStep.cpp:116-119) + n*4 skip flags if any (G/...EStep.cpp:114-118). Down **(2n + 4m)*8** (:77-80). Init: target n*32 (:106). | Workspace is leased from the pool, returned by `ReleaseDeviceResources` after the run (Runtime.CoherentPointDriftGpuEStep.cppm:58-62; .cpp:233-237). Host-visible buffers (G/...EStep.cpp:60-62). | Header states the design: "every iteration uploads the moved source and reads back the row and source statistics" (Runtime.CoherentPointDriftGpuEStep.cppm:8-10). Timeout of 60 s closes the broker (:169-176). |
| G6 | ICP correspondences | "VulkanLBVH" hybrid: GPU nearest + CPU `AdvanceICP` | O/Runtime.RegistrationOperations.Processing.cpp:629 `AdvanceRegistrationGpu` (as `IsReadyToApply`, :837); `S.cpp:844` `QueueGpuNearestTransformed` | yes: ICP iterations until converged or MaxIterations | **Yes (CPU):** `Geo/Geometry.Registration.cppm:136-141` `AdvanceICP` (one iteration given correspondences, .cpp:405-465). The runtime calls it per iteration **on the main thread** (Processing.cpp:673-678 inside `IsReadyToApply`). | `IterationTrace` observer (Geo/Geometry.Registration.cpp:461-462) → `Progress->Publish` (Processing.cpp:676-677). Sandbox overlays the source with the live pose (src/app/Sandbox/Editor/Sandbox.MeshProcessingPanels.cpp:3486-3514). | Partial. The target LBVH is cached. Source points "stay uploaded while exact values match" (S.cppm:145-147; S.cpp:553-557). | float64 path: up **128 B** (dmat4 pose, S.cpp:559-563); down **m*16** (neighbour 8 + header 8 per query, S.cpp:592-603). Without float64: up m*12 + m*4, down m*16 (S.cpp:555, 567; Processing.cpp:692-694). | Batch reused across iterations (`state.Batch` passed back, Processing.cpp:690-694). Released at the end (:685). | Full-array readback every iteration. CPU solve on the main thread (stall size not measured). |
| G7 | Point bilateral filter | "VulkanLBVH" hybrid: GPU kNN + CPU update | O/Runtime.PointSetOperations.Bilateral.cpp:267 `AdvanceGpu` (as `IsReadyToApply`, :553-570); CPU step :225 | yes: `Iterations` passes (Geo/Geometry.PointCloud.Utils.cppm:226) | **Yes (CPU):** `BilateralFilterStepFromNeighbors` (Geo/Geometry.PointCloud.Utils.cppm:256-259). One job pair per iteration (Bilateral.cpp:553-600). | None during the run; result at the end. | No. The moving LBVH is rebuilt each iteration on the CPU and uploaded (`UpdateWorkspace` in `IsReadyToApply`, main thread, Bilateral.cpp:281-292; S.cpp:808-830). | Per iteration: index points n*12 up (S.cpp:517-519) + queries n*12 + n*4 up (S.cpp:555, 567); down n*K*8 + n*8 (S.cpp:396-409). | Moving workspace kept for the run (Bilateral.cpp:283-288). | Full arrays both ways every iteration. |
| G8 | Farthest-point sampling (weighted, exact) | Vulkan, chunked | M/PointSampling/Runtime.PointSamplingGpu.cpp:52 ctor (`Begin`), :74 `QueueNext`, :87 `Observe`; G/Graphics.FarthestPointSampling.cppm:40-48 | yes: count greedy rounds, chunked by a pair budget (G/Graphics.FarthestPointSampling.cpp:44-45, 178-199) | **Yes:** `Begin` / `RecordNext` / `NextChunkFinishes` / `Finished` (cppm:40-48). Driven by a framed GPU job (O/Runtime.PointSamplingOperations.cpp:361-393; also the consolidation initial order, M/PointCloudConsolidation/...Module.cpp:2680-2695). | None per chunk ("intermediate chunk carries no samples", PointSamplingGpu.cpp:93-95). | Yes. Resident canonical positions and weights (FarthestPointSampling.cppm:17-25, 38-41). | **0 B per chunk** (completion only); the last chunk reads back count*(8+4) (PointSamplingGpu.cpp:79-84; cppm:36-37). | Leased workspace, returned when finished (PointSamplingGpu.cpp:57, 106). | Already meets "0 bytes per step", but has no metrics or preview. The prefix is verified against the CPU (:112-130). |
| G9 | Property smoothing, explicit (Laplacian, Taubin, bilateral, spectral heat) | Vulkan | O/Runtime.MeshFieldOperations.Smoothing.cpp:687 `QueueExplicit`; G/Graphics.PropertyFilter.cpp:222 `Record` | yes: `Iterations` passes (PropertyFilter.cpp:302-332) | **None.** All iterations go into one recording and one submission (PropertyFilter.cpp:302-332; cap 2^20 dispatches, G/Graphics.PropertyFilter.cppm:81). | No intermediate preview (single submission). Result into the ring. | Within the submission, yes. Values are gathered on the device from the resident input (PropertyFilter.cpp:283-285). | Init (per run): CPU-built graph arrays (edges, base weights, offsets, incidences, degree, fixed) uploaded (PropertyFilter.cpp:240-268). Per iteration: 0. End: 8 B completion word (Smoothing.cpp:694-697). Accept: ring front. | Leased `PropertyFilterWorkspace` (:691). Output ring kept until Accept or Discard (GPU transaction core). | Element sizes of the per-run uploads were not verified. |
| G10 | Property smoothing, implicit (backward Euler, chained CG) | Vulkan | O/Runtime.MeshFieldOperations.Smoothing.cpp:710 `QueueChunk`, :747 `Poll`; G/Graphics.SparseConjugateGradient.cpp:148 `Begin`, :191 `RecordUpload`, :207 `RecordNext`, :269 `Observe`, :139 `RecordFinal` | yes: S chained solves x CG iterations | **Yes (device):** `Begin` / `RecordNext` (bounded dispatch budget) / `Observe` / `Finished` / `RecordFinal` (SparseConjugateGradient.cpp:207-283). Stop request (Smoothing.cpp:794). | Per chunk: CG reports (status, iterations, residuals; G/Graphics.SparseConjugateGradient.cppm:18-25). Preview: each chunk stores the latest complete time step into the ring, GPU->GPU (Smoothing.cpp:734-741), published in `Poll` (:673-685, 773). Progress (:776-786). | Yes. CSR, RHS terms and work vectors stay on the GPU. Seeds come from the canonical slot (Smoothing.cpp:721-729). | Init: CSR offsets/columns/values + RhsDiagonal + RhsConstant (+ host guesses if any) (SparseConjugateGradient.cpp:100-117). Per chunk: down **S*80 B** reports only (Smoothing.cpp:714-717; cpp:134-137; `SparseCgReport` 80 B, cpp:36). 0 up. End: final store into the ring. | Leased solver and filter workspaces (:756-758). Ring kept until Accept or Discard. | Closest existing match to Init/Step/Metrics/Preview/Finish. The metric ring holds S reports, not "a few bytes" when S is large. |
| G11 | Progressive Poisson sampling | Vulkan library (**not executed at runtime**) | M/ProgressivePoisson/Runtime.ProgressivePoissonGpuBackend.cpp:1261 `RecordProgressivePoissonGpuExecution`, :1101 `RecordProgressivePoissonGpuPasses`, :1363 readback `Enqueue` | yes: levels (cpp:932, 1170) | None. All levels are recorded in one go (:1170). | None. | Within the recording. | Init: inputs uploaded (:554, :1156). End: batch readback of 3 ranges (:1363-1406). | Unknown at runtime: no runtime caller. Exercised only by tests (tests/integration/runtime/Test.ProgressivePoissonGpuResultReadbackGpuSmoke.cpp, tests/contract/runtime/Test.ProgressivePoissonGpuBackend.cpp). | `GpuExecutionAvailable` is always false (cpp:220, 1055, 1066). The runtime labels a resolved backend but always computes the CPU reference (O/Runtime.PointSetOperations.Processing.cpp:299-313, 446-447, 1461-1462). See C25. |
| G12 | Point scalar analysis (kernel density M0, point spacing M1, density weight M2) | Vulkan, one-shot | O/Runtime.PointScalarTransaction.cpp:428 `StartPointScalarGpu`, :124 `Poll`; G/Graphics.PointScalarAnalysis.cppm:38 `Record`; methods: M/KernelDensity/Runtime.KernelDensityOperation.cpp:89-92, M/PointSpacing/Runtime.PointSpacingOperation.cpp:88-91, M/DensityWeight/Runtime.DensityWeightOperation.cpp:84-92 | no | n/a (one submission) | Stats record at the end (`PointScalarGpuStats`, 112 B, G/Graphics.PointScalarAnalysis.cpp:30). Result previewed via the ring until Accept (PointScalarTransaction.cpp:200-210). | Resident positions + cached LBVH (:111-118, 142-167). | Init: property upload on a cache miss. Run: 112 B down (:142-145). Accept: front n*4 down (+ companion) (:216-237). | Workspace returned to the pool at run completion (:182). Ring kept until Accept or Discard. | Non-iterative GPU: in R7 scope ("every GPU algorithm"); a single step suffices. |
| G13 | Statistical outlier analysis | Vulkan, one-shot | M/OutlierAnalysis/Runtime.OutlierAnalysisOperation.cpp:571 `Poll` (queue :591); G/Graphics.OutlierAnalysis.cpp | no | n/a | Stats 32 B (G/Graphics.OutlierAnalysis.cpp:23). 3 rings (score, mask, presentation; :598-604). | Resident positions + LBVH. | Run: 32 B down. Accept: front readback of each ring marked ReadBack (GpuTransactionLifecycle.cpp:282-291). | Leased workspace (:584). Rings kept until Accept or Discard. | Which of the 3 rings are read back on Accept was not checked. |
| G14 | PCA point normals | Vulkan, paged | O/Runtime.NormalOperations.Processing.cpp:856 `Queue` (point branch :860-882); G/Graphics.PointNormals.cpp | no (row pages, `PointFirst`) | Pages over rows (completion-gated, :855-856). Not iterative. | Stats 32 B per page (`PointNormalsGpuStats`, G/Graphics.PointNormals.cppm:26-29). Ring preview published per page (:850-854). | Resident positions + LBVH. | Per page: 32 B down. Accept: n*12 front. | Leased workspace (:863). Ring kept until Accept or Discard. | |
| G15 | Mesh vertex normals | Vulkan, one-shot | O/Runtime.NormalOperations.Processing.cpp:886-897; G/Graphics.VertexNormals.cpp:182-210 | no | n/a | 12 B stats (G/Graphics.VertexNormals.cppm:112). | Resident positions/topology views (:893-896). | Run: 12 B down. Accept: front. The area/CSR prep is CPU-built in VertexNormals.cpp:53-69 (upload size not checked). | Leased workspace (:886). | |
| G16 | ISS-style keypoints (spacing, reduction, score, suppression) | Vulkan, paged | O/Runtime.PointAnalysisOperations.Keypoints.cpp:486-520 (framed job); G/Graphics.PointKeypoints.cppm:38-41 `RecordPage` | no (multi-phase pages with resume, cppm:24-30) | Paged phases. Not a convergence loop. | 32 B header per page (G/Graphics.PointKeypoints.cpp:31). | Resident positions + LBVH. | Per page: up 4-32 B (Keypoints.cpp:493-495), down 32 B (:496-498). Accept: score + mask fronts (:420-439). | Leased workspace (:473). Rings until Accept or Discard. | |
| G17 | GPU LBVH neighbourhood rows (kNN/radius) used by descriptors, keypoint radius rows, point construction, generic point kNN | Vulkan, one-shot batches | O/Runtime.GeometryProcessingOperations.RadiusRows.cpp:67 `AdvancePointRadiusRows`, :109 `AdvancePointKnnRows`; callers O/Runtime.PointAnalysisOperations.Descriptors.cpp:262, O/Runtime.PointAnalysisOperations.Keypoints.cpp:292, O/Runtime.PointConstructionOperations.Processing.cpp:340/405 | no | Batch pages (`AdvanceGpuRowPages`, RadiusRows.hpp:57-80) | none | Index cached. Queries uploaded per batch. | Per batch: up q*12 + q*4, down q*(K or C)*8 + q*8 (S.cpp:396-409, 555-568). | Batch objects reused within a run (`Spare`, RadiusRows.hpp:43-46). | The CPU consumes the full neighbour rows. |
| G18 | Point LBVH build and query | Vulkan (infrastructure) | G/Graphics.PointLBVH.cppm:62 `RecordBuild`, :65 `RecordQuery`; S.cpp:500-548 | no | n/a | n/a | Index kept in the spatial cache per property revision. | Build: points n*12 (unless resident) + slot map n*4 up (S.cpp:511-513). | Cached until pruned (S.cppm:175 `Prune`). | Used by G4, G6, G7, G12-G17. |
| G19 | Parallel primitives (scan, compaction, radix sort, segmented reduce) | Vulkan (infrastructure) | G/Graphics.ComputeParallelPrimitives.cppm:484-496 | no | n/a | n/a | Caller-owned | Optional count readback (G/Graphics.ComputeParallelPrimitives.cpp:1718-1726). | Caller-owned | Building blocks of G18 and G11. |
| G20 | Property texture bake | Vulkan raster (graphics pipeline) | M/TextureBake/Runtime.TextureBakeModule.cpp:3331 `Bake`; record :2818; G/Graphics.PropertyTextureBake.cpp:573 | no | n/a | none | none | Run: up texcoords V*8 + values V*16 + indices 3F*4 + charts F*4 + gutter edges (TextureBakeModule.cpp:2685-2715, `UploadStorage` :2598-2620). No readback found in the module. | Retired after a safe frame (`RetireWorkResources`/`DrainRetiredResources`, :1308-1341). Output textures persist as assets. | The coverage report source was not traced. |
| G21 | GPU Run/Accept transaction lifecycle (scalar, outlier, normal, smoothing, GPU positions) | infrastructure | O/Runtime.GpuTransactionLifecycle.hpp:1-150; O/Runtime.PointScalarTransaction.cppm:49-68; O/Runtime.GeometryProcessingOperations.GpuPositions.cpp | — | Run job (`Poll`) then ReadyToAccept then Accept job (GpuTransactionLifecycle.cpp:223-323) | Ring fronts are the preview (renderer reads the ring) | Rings resident | Accept: one full front readback per ReadBack ring (cpp:92-129). Positions: 1:1 render commit avoids re-upload (S.cppm:185-201). | Rings until Accept or Discard; `kGpuTransactionMaxDeferrals` 600 frames (hpp:18) | Already implements "kept until Accept/Reset". No cache by size class. |

### B. Iterative CPU algorithms (and their runtime glue)

CPU rows have no GPU traffic during iteration unless noted. The byte column states what crosses at preview or publish.

| # | algorithm | backend | entry point | iterative (what) | step form today | observation today | resident state | CPU<->GPU bytes per iteration | buffer lifetime | notes / unknowns |
|---|---|---|---|---|---|---|---|---|---|---|
| C1 | K-means (Lloyd) | CPU | Geo/Geometry.KMeans.cpp:223 `Cluster` (cppm:102-107); loop :280; runtime job M/Clustering/Runtime.ClusteringModule.cpp:940-948 → `RunKMeansWorker` :426, call :444-454 | yes: Lloyd until tolerance or MaxIterations | Monolithic loop with observer | `IterationObserver(completed, max)`, progress only (cppm:98-101, cpp:308-309) → `ReportProgress` (ClusteringModule.cpp:450-453) | Locals of one call. Scratch passed as null (ClusteringModule.cpp:449). | 0 (publish once at the end) | freed at return | No preview, no metrics beyond progress. |
| C2 | ICP (point-to-point/plane, robust) | CPU (KD-tree or CPU LBVH query) | Geo/Geometry.Registration.cpp:495 `AlignICP`, :467 `AlignICPWithQueries` (loop :476-492); runtime CPU job O/Runtime.RegistrationOperations.Processing.cpp:824-837 → :601 | yes: ICP iterations | **Step exists:** `AdvanceICP` (cpp:405). The CPU runtime path uses the monolithic `AlignICPWithQueries` loop (Processing.cpp:145-146, 601). | `IterationTrace` observer (transform, RMSE, inliers; cppm:107-127) → progress object. Cancel via `params.Cancelled` (Processing.cpp:583; cpp:478-482). Sandbox live-pose overlay (Sandbox.MeshProcessingPanels.cpp:3486-3514). | KD-tree is built per call (cpp:502-506) | 0 | freed at return | G6 shows a step-driven variant already exists. |
| C3 | Coherent Point Drift (rigid, affine, nonrigid, Bayesian) | CPU | Geo/Geometry.Registration.CoherentPointDrift.cpp:699 `Initialize`, :881 `Step`, :890 `BeginStep`, :939 `Run`, :1040 `Register` | yes: EM iterations | **Yes:** `Solver` with `Initialize`/`Step`/`Current`/`Finished` (cppm:229-258). Runtime: n-step batches with Pause, Step, Cancel and Apply (O/Runtime.RegistrationOperations.CoherentPointDrift.cpp:438-501, 971-990) | `IterationTrace` per iteration (cppm:160-175) appended to a trace (CPD.cpp:413-429). `StageObserver` (cppm:156). Preview: full `TransformedSource` copied each iteration (CPD.cpp:331-366). Sandbox overlay of <= ~20000 points (Sandbox.MeshProcessingPanels.cpp:3919-3962). | Solver state persists in the run handle across steps | 0 compute. Preview: host copy m*12 per iteration, then the overlay upload (path not traced) | Run handle until Apply or Cancel | Model for the target form. The Vulkan E-step is G5. |
| C4 | Point consolidation LOP/WLOP/CLOP/EAR (reference) | CPU "CpuReference" | Geo/Geometry.PointCloud.Consolidation.cpp:2006/2234/2242 `Consolidate`; loop :2158-2175; runtime `RunStages` M/PointCloudConsolidation/Runtime.PointCloudConsolidationModule.cpp:1474 (call :1593-1604), job :2370-2377 | yes: MaxIterations (or normal-refinement rounds) | Monolithic | None in the loop. Cancellation checked only after the call (Module.cpp:1606) | locals | 0 | freed at return | Step APIs exist but are unused by runtime: `NeighborhoodProjection::Advance` (cppm:220-254, used by G4), `SeedLop`/`InitializeLopFromNeighbors`/`StepLopFromNeighbors` (cppm:257-269); no runtime caller found for the last three. |
| C5 | LOP with source LBVH | CPU "CpuLBVH" | Geo/Geometry.PointCloud.Consolidation.cpp:2631 `ConsolidateLopWithIndex`; loop :2653; runtime Module.cpp:1597 | yes | Monolithic | none | locals | 0 | freed | |
| C6 | Gaussian mixture EM (+ Anderson acceleration) | CPU | Geo/Geometry.GaussianMixture.cpp:619 `FitEM` (cppm:119-122); loop :702 | yes: EM until tolerance or MaxIterations | Monolithic | none (no observer in the signature) | locals | 0 | freed | Called inside C7 (Geo/Geometry.HalfedgeMesh.Segmentation.cpp:333), C8 (Patches.cpp:459) and CLOP (Consolidation.cpp:617). Anderson: Geo/Geometry.FixedPoint.Anderson.cppm:74. |
| C7 | Curvature GMM segmentation + spatial regularization | CPU | Geo/Geometry.HalfedgeMesh.Segmentation.cppm:138-160 `Segment`/`ComputeAndSegment`; regularization loop Segmentation.cpp:522-539; runtime job O/Runtime.MeshFieldOperations.Curvature.cpp:1713-1798, call :2339-2352 | yes: EM (C6) + MaxSpatialIterations | Monolithic | none | locals | 0 | freed | |
| C8 | Feature-aligned patches boundary refinement | CPU | Geo/Geometry.HalfedgeMesh.Segmentation.Patches.cpp:2649 `SegmentFeatureAlignedPatches`; :2072 `RefineBoundaries`, loop :2091 | yes: refinement sweeps (<= 8, :159) | Monolithic | none | locals | 0 | freed | runtime Curvature.cpp:2504 (same job as C7) |
| C9 | Multicut boundary partition (local moves) | CPU | Geo/Geometry.HalfedgeMesh.Segmentation.Multicut.cpp:64 `PartitionFeatureBoundaries` → src/geometry/CurvatureBoundaryGraph.cpp:605 `Solve`, sweeps :735 | yes: MaximumSweeps | Monolithic | none | locals | 0 | freed | runtime Curvature.cpp:2447 |
| C10a | Point bilateral filter | CPU "CpuOctree" | Geo/Geometry.PointCloud.Utils.cpp:561 `BilateralFilter`, loop :575; runtime O/Runtime.PointSetOperations.Bilateral.cpp:184-185 | yes: Iterations passes | Monolithic | none | locals | 0 | freed | |
| C10b | Point bilateral filter | CPU "CpuLBVH" | Bilateral.cpp:188-238 `ComputeStep` (LBVH rebuilt per iteration on CPU, :190-201); loop `Compute` :253-263; job :518-521 (also inline at :469) | yes | **Yes:** `BilateralFilterStepFromNeighbors` per iteration (Geo/Geometry.PointCloud.Utils.cppm:256-259) | none | Positions carried in `BilateralWork` | 0 | job lifetime | |
| C11 | Property smoothing (explicit iterations; implicit via CG) | CPU | Geo/Geometry.PropertySmoothing.cpp:163 `FilterProperty` (loops :212, :240, :255); runtime **inline in the command**: O/Runtime.MeshFieldOperations.Smoothing.cpp:1205 `ApplyEditorPropertySmoothingCommand` → :1265 | yes | Monolithic | none | locals | 0 | freed | Runs on the caller thread, with no job wrapper in this path (:1205-1265). Main-thread stall is likely if called from the UI; caller thread not traced. |
| C12 | Harmonic-field robust fit (proximal iterations) | CPU | Geo/Geometry.HarmonicField.cppm:102 `FitProperty`; loops Geo/Geometry.HarmonicField.Fit.cpp:329, :460; runtime inline Smoothing.cpp:1252 | yes: MaxFitIterations (Geo/Geometry.Smoothing.Types.cppm:72) | Monolithic | none | locals | 0 | freed | inline like C11 |
| C13 | Mesh bilateral denoise (face-normal filter + vertex update) | CPU | Geo/Geometry.HalfedgeMesh.Smoothing.cppm:200 `DenoiseBilateral`; loops Smoothing.cpp:262 (`FilterFaceNormalsCore`), :380 (`UpdateVertexPositions`); job O/Runtime.MeshTopologyOperations.Topology.cpp:2114-2162, call :1383 | yes: NormalIterations, VertexIterations (cppm:133-136) | Monolithic | none | locals | 0 | freed | |
| C14 | Isotropic remeshing | CPU | Geo/Geometry.HalfedgeMesh.Remeshing.cpp:156 `Remesh`, loop :187; job Topology.cpp:2188-2237, call :1461 (inline at :2890 when no job lane) | yes: Iterations (cppm:44) | Monolithic | none | locals | 0 | freed | |
| C15 | Adaptive remeshing | CPU | Geo/Geometry.HalfedgeMesh.AdaptiveRemeshing.cpp:303 `AdaptiveRemesh`, loop :345; job as C14, call :1489 | yes: Iterations (cppm:123) | Monolithic | none | locals | 0 | freed | |
| C16 | QEM simplification | CPU | Geo/Geometry.HalfedgeMesh.Simplification.cppm:254 `Simplify`; collapse loop Simplification.cpp:1415 | yes: greedy collapses until the face target | Monolithic | none | locals | 0 | freed | job Topology.cpp:2330-2388, call :1738 |
| C17 | Subdivision (Loop, Catmull-Clark, sqrt3) | CPU | Subdivision.cpp:373, CatmullClark.cpp:345, SubdivisionSqrt3.cpp:177/194 (cppm:80/80/33) | yes: levels (not convergence) | Monolithic | none | locals | 0 | freed | job Topology.cpp:2257-2309. Borderline for R7 (fixed level count). |
| C18 | Mesh parameterization (LSCM via CG, harmonic, BFF) | CPU | Geo/Geometry.HalfedgeMesh.Parameterization.cppm:153 `ParameterizeMesh` (dispatch .cpp:520/547/580; CG MaxSolverIterations :445); runtime **inline** O/Runtime.ParameterizationOperations.cpp:1162-1185 | yes: CG inner loop (Geo/Geometry.Sparse.cpp:1134/1201) | Monolithic. The external solver owns the loop, so the step boundary would be the outer call. | none | locals | 0 | freed | inline, no job in `ApplyParameterizationChecked` |
| C19 | UV atlas chart solve (SLIM-style injective optimization) | CPU | Geo/Geometry.UvAtlas.ChartSolve.cppm:76 `SolveUvChart` (loops .cpp:210, :301); `ResolveUvAtlas` Geo/Geometry.UvAtlas.cppm:262-264; runtime job O/Runtime.ParameterizationOperations.Processing.cpp:1274-1321 (call :1101); asset workflow src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp:841, :1056 | yes: MaxIterations per chart (ChartSolve.cppm:35) | Monolithic per chart | Cancel flag polled (ChartSolve.cpp:303; UvAtlas.cppm:42, 74). Iteration counts in diagnostics (UvAtlas.cpp:1612, 1631-1632). | locals | 0 | freed | Thread context of the asset-workflow callers is unknown (not traced). Also uses `MaxInjectiveStep` (Geo/Geometry.Parameterization.Optimize.cpp:1162, loop :1330). |
| C20 | Laplacian eigenbasis / modal analysis (subspace iteration) | CPU | Geo/Geometry.Sparse.Eigensolver.cpp:69 `SolveSymmetricGeneralizedEigen`, loop :151; runtime **inline** O/Runtime.MeshFieldOperations.Eigenbasis.cpp:261 → :358 | yes: MaxIterations subspace iterations (Geo/Geometry.Sparse.cppm:338) | Monolithic | none (iterations and residuals reported after, :360-363) | locals | 0 | freed | inline, no job |
| C21 | Sparse CG (Jacobi PCG) and Eigen CG variants | CPU library | Geo/Geometry.Sparse.cpp:1134 `SolveCG` (loop :1201); Eigen solvers :532, :551 | yes | Monolithic (library) | `CGResult` after the call | locals | 0 | freed | Inner loop of C11, C18 and the heat method; the CPU mirror of G10. |
| C22 | Geodesic distance (virtual-source window propagation) | CPU | Geo/Geometry.HalfedgeMesh.Geodesic.cppm:31-35 `ComputeVirtualSourceDistance`; queue loop Geo/Geometry.HalfedgeMesh.Geodesic.VirtualSource.cpp:173; runtime **inline** O/Runtime.MeshFieldOperations.Geodesics.cpp:54 → :146 | yes: propagation until the queue empties (not a convergence loop) | Monolithic | none | locals | 0 | freed | inline. Whether R7 should treat this as "iterative" is open. |
| C23 | Heat-method geodesics / vector heat / log map | CPU | Geo/Geometry.HalfedgeMesh.Geodesic.cppm:105 `ComputeDistance` (CG at .cpp:180-248); Geo/Geometry.HalfedgeMesh.VectorHeatMethod.cppm:117/136 (complex CG loop .cpp:433) | yes: CG | Monolithic | none | locals | 0 | freed | **No runtime caller found.** `VectorHeat` exists only as an enum case (O/Runtime.GeometryProcessingOperations.Mesh.cpp:210). |
| C24 | Point sampling order (FPS, greedy, MIS rounds, elimination, tournament) | CPU | Geo/Geometry.PointSampling.cppm:188-189 `Order`; MIS rounds Geo/Geometry.PointSampling.Approximate.cpp:92/192; runtime **inline** O/Runtime.PointSamplingOperations.cpp:308-313, glyph order O/Runtime.PointSamplingOperations.GlyphOrder.cpp:126; consolidation initial order | yes: count selections / MaxMisRounds (cppm:147) | Monolithic. An incremental extend API exists (`Build`/`Extend`/`ExtendRelaxed`, cppm:201-206). | none | locals | 0 | freed | CPU backend runs inline on the command path (:308-313). |
| C25 | Progressive Poisson sampling (reference) | CPU | src/geometry/ProgressivePoisson/ProgressivePoissonReference.cpp:752 `Compute`, level loop :492; job O/Runtime.PointSetOperations.Processing.cpp:1458-1470 (also inline :446-447 when no job lane) | yes: levels | Monolithic | none | locals | 0 | freed | Runs even when "Vulkan compute" is requested (G11). |
| C26 | Robust PCA (iterative) | CPU | Geo/Geometry.Linalg.cpp:587 `RobustPCA`, loop :674 | yes: MaxIterations (Geo/Geometry.Linalg.cppm:105) | Monolithic | none | locals | 0 | freed | No runtime caller found. |
| C27 | RANSAC coarse alignment | CPU | Geo/Geometry.PointCloud.Features.cpp:778 `EstimateCoarseAlignment`, loop :838 | yes: MaxIterations 4000 (cppm:149) | Monolithic | none | locals | 0 | freed | No runtime caller found. |
| C28 | Rotation averaging (Karcher mean, geodesic median, quaternion median) | CPU | Geo/Geometry.RotationAveraging.cpp:437/505/575, loops :455/:523/:599 | yes | Monolithic | none | locals | 0 | freed | No runtime caller. The gizmo uses closed-form `ChordalMean` (src/runtime/Gizmos/Runtime.GizmoInteraction.cpp:226). |
| C29 | Sphere fit (geometric refinement) | CPU | Geo/Geometry.Sphere.cppm:49 `ToSphere`; loop Geo/Geometry.Sphere.cpp:336 | yes: MaxIterations 64 (cppm:44) | Monolithic | none | locals | 0 | freed | No runtime caller found. |
| C30 | Implicit plane field projection | CPU | Geo/Geometry.ImplicitPlaneField.cpp:536 `PlaneField::Project`, loop :542 | yes: <= 4 (cppm:107) | Monolithic | none | locals | 0 | freed | No runtime caller found. Per-query, tiny. |
| C31 | Incremental convex hull | CPU | Geo/Geometry.ConvexHullBuilder.cpp:363 | construction loop (<= 2n) | Monolithic | none | locals | 0 | freed | Borderline: a construction, not convergence. Runtime caller not traced. |

### C. Out of scope (stated, not inventoried)

- **Rendering compute** (per-frame renderer passes, not editor algorithms): CullingSystem, HZB, LightClusters, post-process histogram, GpuWorld position gather (G/Graphics.GpuWorld.cpp:1138), RenderGraph executor.
- **Physics per-step solvers:** GJK/EPA/SDFContact (Geo/Geometry.GJK.cppm:197-339) and the PhysicsIntegration module.
- **Non-iterative CPU** algorithms (CPU normals, outlier, density, descriptors, keypoint CPU, surface reconstruction, repair, booleans, shortest path, etc.).
- **Harmonic field `Solve`/`PropagateLabels`:** a direct sparse Cholesky, not iterative (Geo/Geometry.HarmonicField.cpp:2, 296-311).

### Unknowns (what could not be determined from code)

1. **Preview overlay upload cost** (CPD C3, ICP C2/G6). `SceneInteractionModule::SetPreviewOverlay` only stores points (src/runtime/Scene/Runtime.SceneInteractionModule.cpp:1249-1266). How and when they reach the GPU was not traced.
2. **k-means submissions per iteration (s)** depend on paging budgets and serial depth (ClusteringGpuState.cpp:321-387). Only bounds are given.
3. **The 32 B size of `KMeansGpuDiagnostics`** is computed from its members. There is no static_assert.
4. **WLOP producer state record size**, and **PropertyFilter per-run upload element sizes**: not verified.
5. **Outlier (G13) and vertex-normal (G15) Accept:** which rings are read back was not checked.
6. **TextureBake coverage report:** source not traced; no readback found in the module.
7. **Asset-workflow UV atlas (C19)** thread context not traced.
8. **Caller thread of the inline commands** (C11, C12, C18, C20, C22, C24 CPU). They run synchronously inside `Apply*Command`; whether that is always the main thread was not traced.
9. **Main-thread stall sizes** were not measured. The CPU work placed in `IsReadyToApply` (ICP `AdvanceICP`, G6; LBVH rebuilds in G4 and G7) is a candidate.
10. **Geodesic virtual source (C22) and subdivision (C17):** whether R7 counts them as "iterative" is a policy question, not a code fact.

## Appendix B: R5 inventory

Rule R5 (docs/architecture/engine-and-feature-modules.md:211): "Everything the editor can do, an agent can do over MCP",
measured as (A) editor actions that are not commands reachable by a tool and (B) commands without an MCP tool.
Nothing was built or run. Every row is from static reading of the cited lines.

### Method

1. Tool side. For each of the 48 declared tools (tests/data/agent_declared_tool_schemas.json) I read the handler and noted the
   editor function it calls (table "Tool -> function" below). The 27 read-only tools change nothing and are not used.
2. UI side. In every file named in the task, every interactive widget (Button, SmallButton, MenuItem, Selectable, Checkbox,
   Combo, Drag*/Slider*/Input*/ColorEdit, key presses, mouse gestures) was followed to the function that changes state.
3. Match. A UI action counts as **yes** when a tool handler calls the same function, or calls the inner function the UI's
   wrapper ends in (cited). **partial** = same function but the tool cannot express the arguments this action passes, or a
   different function with the same outcome for a subset of the action. **no** = no tool handler reaches the function.
4. Granularity. One row = one action site: a control (or a loop/group of controls in one code block) that calls one
   state-changing function with one command kind. A window's config widgets that all funnel into one `Apply*Config` call
   are one row ("config controls"). A row is one distinct action, a control together with the state it changes:
   look-alike controls acting on different operations count separately, while one control changing the same state from
   several windows counts once. Where reachability differs by window (Geometry Visualization vs. Appearance), the
   actions differ and count separately.
5. Categories for "no" rows: S/H scene or history, Sel selection, C config, J job control, F file output, O other runtime state.

#### Excluded as view-local (not counted)

| what | where | why |
|---|---|---|
| Window open/close menu items, redirect windows | ES:302-303; MP:662-678, MP:795-810; MeP:3468, MeP:3704 | EditorWindowRegistry::SetOpen, window visibility only |
| Gizmo menu Enabled/Translate/Rotate/Scale/Pivot/Local axes, W/E/R keys | ES:2558-2572, ES:2597-2605 | panel-local GizmoUiState; affects only the next drag (comment ES:2550-2551) |
| G key (hide editor UI) | src/runtime/Editor/Runtime.EditorUiModule.cpp:267-271 | EditorUiHost::ApplyVisibilityCommand, UI visibility |
| Esc during gizmo drag | ES:2610-2613 | cancels an uncommitted drag, no net change |
| Unsaved-changes dialog Cancel | ES:2493-2494 | drops the pending request (PS:123-127) |
| Camera navigation (RMB/MMB drag, WASD, scroll) | src/runtime/Cameras/Runtime.CameraControllers.cpp:37-43, :319 | view state; outcome expressible by `set_camera pose` (AE:977), but not scene/config/selection/history |
| Window close -> quit | src/runtime/Kernel/Runtime.Engine.cpp:514 | quitting is excluded by design (docs/architecture/agent-control-lane.md:399-402) |
| Agent Connection > Disconnect agent | ES:2766-2768 | lane control; the bridge has `sandbox_disconnect` (agent-control-lane.md:33, 71-77) |
| "Dismiss" last-result buttons, "Reload active" | e.g. MP:1010, MP:1348-1354, MeP:1612-1618, MeP:1917, MeP:2991 | panel draft/result display only |
| Draft edits that apply only via a later button (pins, features, Swap source/target, Name all bins, Write to input, Use selected vertices, Add/Clear sources) | e.g. MP:1179-1183, MP:3296-3301, MeP:2736-2941 | land in that window's config row |
| Read-only queries: Property Inspector entity/property/bins/compare/values/paging/Refresh; Diagnostics filters; Properties window Property/Values combos; spectrum slider | PI:75-257; DG:23-28; DP:149-202; src/runtime/Editor/Runtime.EditorPropertyWidgets.cpp:131, :193 | no state change |
| UV pane Fit, pan, zoom; per-frame UV view submit | MeP:3198, MeP:3223-3231, MeP:3271 | view-local render of the UV pane |
| Panel shutdown discards of GPU transactions | MP:712-760 | not a user action |

Path abbreviations: ES = src/app/Sandbox/Editor/Sandbox.EditorShell.cpp, PS = .../Sandbox.PanelSupport.cpp,
DP = .../Sandbox.DomainPanels.cpp, MP = .../Sandbox.MeshProcessingPanels.cpp, MeP = .../Sandbox.MethodPanels.cpp,
PI = .../Sandbox.PropertyInspectorPanel.cpp, DG = .../Sandbox.DiagnosticsPanel.cpp,
AE = src/runtime/Agent/Runtime.AgentOperations.Editor.cpp, AO = src/runtime/Agent/Runtime.AgentOperations.Operations.cpp,
AC = src/runtime/Agent/Runtime.AgentOperations.Capture.cpp, SEO = src/runtime/Editor/Operations/Runtime.SceneEditingOperations.cppm,
VEO = src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.cppm,
VEA = src/runtime/Editor/Operations/Runtime.VisualizationEditingOperations.Actions.cpp.

### Tool -> function (state-changing tools)

| tool | calls | evidence / limits |
|---|---|---|
| select_entity | SelectEditorEntity | AE:114 (replace with one entity) |
| import_file | ApplyEditorFileImportCommand | AE:139, only `.Path` (no PayloadKind) |
| save_scene / load_scene | ApplyEditorSceneSaveCommand / ApplyEditorSceneLoadCommand | AE:220, AE:235 |
| set_visibility | ApplyEditorRenderHintCommand | AE:257-258 (bounds), AE:284-292 (lane enable; domains/point type copied from the model, sizes/widths never set) |
| bind_attribute | ApplyEditorAttributeBindingCommand | AE:544, AE:602-607 |
| show_property | Color binding (-> ApplyEditorVisualizationRecipeCommand, VEA:1865) or ApplyEditorVisualizationRecipeCommand | AE:850-856, AE:861-862 |
| vector_field | ApplyEditorGeometryVectorFieldCommand (Add/Update/Remove) | AE:738-740 |
| lane_style | ApplyEditorVisualizationConfigCommand | AE:797-801; target = lane (Surface/Edges/Points) only, always `EnableConfig = true` |
| set_camera | ApplyEditorCameraControllerCommand / ApplyEditorCameraPoseCommand | AE:926, AE:977 |
| undo / redo | EditorDocumentCommandSurface Undo/Redo | AE:1024 |
| config_apply | EngineConfigControl::ApplyEngineConfigHotSubset on an AppSections upsert | AE:1127-1133; only registered sections (src/app/Sandbox/Sandbox.ConfigSections.cpp:38-67) |
| jobs_cancel | CancelEditorJob (one job token) | AE:1317 |
| run_operation / run_mesh_operation | 21 rows -> ApplyEditor*Command / ApplyEditorConfigured* | AO:414-657 |
| run_registration | ApplyEditorConfiguredRegistrationCommand (icp), ApplyEditorConfiguredCoherentPointDrift (cpd, one-shot) | AO:972, AO:984 |
| run_point_sampling / run_keypoint_analysis | ApplyEditorPointSamplingCommand / ApplyEditorConfiguredKeypointAnalysis | AO:762, AO:781 |
| run_kmeans / run_point_cloud_consolidation | SubmitKMeansRun / SubmitEditorPointCloudConsolidation | AO:858, AO:933 |
| view_capture (and view_screenshot) | ViewCaptureModule::Request | AC:126; regions viewport/window AC:77 |

Shared inner paths used below:
- Every UI `ApplyEditor*Config` (24 of them) ends in ApplyEditorProcessingConfig (directly or via ApplyOperationConfig,
  src/runtime/Editor/Operations/Runtime.OperationRuntime.cppm:925-931) -> `context.ApplyEngineConfigHotSubset`
  (src/runtime/Editor/Runtime.EditorProcessing.cpp:226-257), bound to the EngineConfigControl
  (src/runtime/Editor/internal/Runtime.EditorWorkspaceSession.cpp:953-955). config_apply calls the same control (AE:1133).
- ApplyEditorVisualizationPropertyCommand ends in ApplyEditorVisualizationConfigCommand (VEA:1630-1632).
- Sandbox ApplyEditorHistoryCommand = DocumentCommands.Undo/Redo (PS:194-201).
- The "Show" button = ApplyEditorVisualizationRecipeCommand with MakeEditorPropertyVisualizationRecipe (PS:413-431),
  the same recipe command show_property reaches (AE:861; VEA:1865 for the Color path).

### (A) Editor actions, per window

#### Main menu bar and global shortcuts (ES)

| action | file:line | state-changing function | MCP? |
|---|---|---|---|
| File > Save Screenshot | ES:412-413 | ViewCaptureModule::Request (viewport) | yes: view_capture |
| File > Save Window Screenshot | ES:414-415 | ViewCaptureModule::Request (window) | yes: view_capture region=window |
| F12 | ES:2778-2780 | ViewCaptureModule::Request | yes: view_capture |
| View > Bounding boxes > All on / All off | ES:370-376 | ApplyEditorRenderHintCommand(SetBounds) per entity | yes: set_visibility lane=bounds (one call per entity) |
| Gizmo > Apply snap steps | ES:2535-2539 | ApplyGizmoSnapConfig -> ApplyEngineConfigHotSubset (src/runtime/Scene/Runtime.SceneInteractionModule.Config.cpp:109-116) | yes: config_apply (gizmo snap section, ConfigSections.cpp:39) |

#### Viewport gestures (runtime)

| action | file:line | state-changing function | MCP? |
|---|---|---|---|
| Gizmo drag translate/rotate/scale (Shift = snap), commit on release | ES:2661-2711 | SceneInteractionModule::BeginGizmoDrag / PreviewGizmoDrag / CommitGizmoDrag (src/runtime/Scene/Runtime.SceneInteractionModule.cppm:193-202) | **no** (S/H) |
| Click pick: replace / Shift add / Ctrl toggle; entity or primitive by selection target | src/runtime/Scene/Runtime.SceneInteractionModule.cpp:77-104 | SelectionController::RequestClickPick (src/runtime/Scene/Runtime.SelectionController.cppm:174) | **partial**: select_entity replaces with one entity through another function; add/toggle and primitive picks not reachable |
| Drop files on the window | src/runtime/Kernel/Runtime.Engine.cpp:1239-1266 | AssetWorkflowModule::ImportDroppedFilePaths (src/runtime/AssetWorkflow/Runtime.AssetWorkflowModule.cppm:270) | **partial**: import_file reaches a different entry point, one path per call |

#### Unsaved scene changes dialog (ES:2460-2502)

| action | file:line | state-changing function | MCP? |
|---|---|---|---|
| Save, then run the pending New/Close/Open | ES:2487-2488 -> PS:115-142 | ApplyEditorSceneSaveCommand, then PS:69-88 | **partial**: save_scene; the follow-up is reachable only for Open (load_scene) |
| Discard, then run the pending New/Close/Open | ES:2490-2491 -> PS:131-134 | ApplyEditorNewSceneCommand / ApplyEditorCloseSceneCommand / ApplyEditorSceneLoadCommand (PS:75-82) | **partial**: only Open (load_scene) |

#### Scene Hierarchy (ES:1032-1054)

| action | file:line | function | MCP? |
|---|---|---|---|
| Click a row | ES:1042-1046 | SelectEditorEntity | yes: select_entity |

#### Inspector (ES:1056-1320)

| action | file:line | function | MCP? |
|---|---|---|---|
| Local position drag | ES:1075-1086 | ApplyEditorTransformEdit (SEO:553) | **no** (S/H) |
| Local scale drag | ES:1089-1100 | ApplyEditorTransformEdit | **no** (S/H) |
| Bounding box checkbox | ES:1112-1116 | ApplyEditorRenderHintCommand(SetBounds) | yes: set_visibility lane=bounds (AE:252-258) |
| Geometry presentation: Default color | ES:1225-1241 | ApplyEditorGeometryPresentationSlotDefaultCommand (VEO:773) | **no** (S) |
| Geometry presentation: Source property | ES:1252-1288 | ApplyEditorGeometryPresentationSlotPropertyCommand (VEO:777) | **no** (S) |
| Texture bake controls | ES:1177 | see "Texture bake (shared)" | |

#### File / Scene (ES:1363-1500)

| action | file:line | function | MCP? |
|---|---|---|---|
| Undo | ES:1379-1386 | ApplyEditorHistoryCommand -> Undo (PS:194-201) | yes: undo |
| Redo | ES:1388-1395 | Redo | yes: redo |
| New scene | ES:1412-1421 | RequestSceneReplacement -> ApplyEditorNewSceneCommand (PS:75-76; SEO:548) | **no** (S) |
| Close scene | ES:1423-1432 | ApplyEditorCloseSceneCommand (PS:78-79; SEO:550) | **no** (S) |
| Save / Save As | ES:1462-1468 | ApplyEditorSceneSaveCommand | yes: save_scene |
| Open path | ES:1470-1478 | ApplyEditorSceneLoadCommand (PS:81-82) | yes: load_scene (agent skips the unsaved-changes dialog) |

#### File / Export (ES:1502-1552)

| action | file:line | function | MCP? |
|---|---|---|---|
| Export selected geometry | ES:1537-1539 | ApplyEditorGeometryExportCommand (SEO:531) | **no** (F) |

#### File / Import (ES:1554-1680, queue ES:126-254)

| action | file:line | function | MCP? |
|---|---|---|---|
| Import asset (path + payload hint) | ES:1622-1630 | ApplyEditorFileImportCommand | **partial**: import_file passes `.Path` only (AE:139), the payload hint (ES:1573-1601) is not expressible |
| AssetIO queue: Clear completed | ES:141-144 | EditorAssetImportQueueCommandSurface::ClearCompleted (SEO:267) | **no** (O) |
| AssetIO queue: Cancel import | ES:232-235 | EditorAssetImportQueueCommandSurface::Cancel (SEO:268) | **no** (J): jobs_cancel refuses imports (agent-control-lane.md:246-247; AE:1650-1651) |

#### Frame Graph (ES:1682-1948)

| action | file:line | function | MCP? |
|---|---|---|---|
| Enable GPU profiling | ES:1694-1700 | ApplyEditorGpuProfilingConfigCommand -> EngineConfig.Render.EnableGpuProfiling (src/runtime/Editor/Operations/Runtime.RenderRecipeEditingOperations.Debug.cpp:51-79) | **no** (C): config_apply writes AppSections only (AE:1127-1129) |

#### Render Recipes (ES:639-999, window ES:1950-1957)

All call ApplyEditorRenderRecipeCommand (src/runtime/Editor/Operations/Runtime.RenderRecipeEditingOperations.cppm:399); no tool calls it.

| action | file:line | MCP? |
|---|---|---|
| Update Draft | ES:724-733 | **no** (C) |
| Debounce | ES:735-745 | **no** (C) |
| Validate | ES:747-756 | **no** (C) |
| Preview | ES:759-768 | **no** (C) |
| Activate Preview | ES:771-778 | **no** (C) |
| Cancel draft | ES:781-788 | **no** (C) |
| Artifact Publish | ES:940-958 | **no** (C) |
| Artifact Apply | ES:962-981 | **no** (C) |

#### Camera / Render (ES:1959-2025)

| action | file:line | function | MCP? |
|---|---|---|---|
| Orbit | ES:1978-1985 | ApplyEditorCameraControllerCommand | yes: set_camera controller |
| Fly | ES:1987-1994 | same | yes |
| Free look | ES:1996-2003 | same | yes |
| Top down | ES:2005-2012 | same | yes |
| View presets (Front ... Isometric) | PS:2687-2702 | ApplyEditorCameraPoseCommand(Preset, selection) | yes: set_camera preset + entities |
| Focus selection | PS:2709-2714 | ApplyEditorCameraPoseCommand(Focus) | yes: set_camera focus |

#### Geometry Visualization (ES:2027-2123), target = EditorVisualizationTarget::Entity

| action | file:line | function | MCP? |
|---|---|---|---|
| Uniform color button | ES:2066-2075 | ApplyEditorVisualizationConfigCommand(Target::Entity) | **partial**: lane_style is the same function but only for lane targets (AE:769-801) |
| Clear vis | ES:2077-2099 | ApplyEditorVisualizationConfigCommand(EnableConfig=false) x4 + ApplyEditorVisualizationRecipeCommand(EnableRecipe=false) | **partial**: bind_attribute color default:true clears a lane overlay (VEA:1874-1902); the Entity target and recipe disable are not expressible |
| Uniform color edit | ES:2100-2105 -> PS:525-536 | ApplyEditorVisualizationConfigCommand(Entity) | **partial** (Entity target) |
| Scalar field controls (colormap, auto range, clamp, bins, isoline count/width/color, isovalues add/edit/remove; 10 sites) | ES:2106-2111 -> PS:572-693 | ApplyEditorVisualizationConfigCommand(Entity) via PS:542-553 | **partial** (Entity target) |
| Preset "Scalar" | ES:519-538 | ApplyEditorVisualizationPropertyCommand -> ...ConfigCommand (VEA:1630) | **partial** (Entity target) |
| Preset "Isolines" | ES:545-565 | same | **partial** (Entity target) |
| Preset "Color buffer" | ES:572-588 | same | **partial** (Entity target) |
| Preset "Vector field" | ES:594-611 | ApplyEditorGeometryVectorFieldCommand(Add) | yes: vector_field add |

#### Jobs (PS:2189-2249, registered ES:2826-2838)

| action | file:line | function | MCP? |
|---|---|---|---|
| Cancel (row) | PS:2224-2231 | CancelEditorJobRun: whole run via CancelEditorRuns (src/runtime/Editor/Runtime.EditorProcessing.cpp:128-136) | **partial**: jobs_cancel calls CancelEditorJob for one token (AE:1317; EditorProcessing.cpp:103-107); same surface, later stages of the run not covered |

#### Diagnostics / Log (DG)

| action | file:line | function | MCP? |
|---|---|---|---|
| Clear | DG:30-33 | ClearEditorDiagnosticsLog (src/runtime/Editor/Runtime.DiagnosticsStream.cppm:92) | **no** (O) |

#### Screenshot window (ES:2841-2900)

| action | file:line | function | MCP? |
|---|---|---|---|
| Save PNG (region, camera preset, legend) | ES:2867-2878 | ViewCaptureModule::Request | yes: view_capture (region/preset/fit_entity/legend_entity, AC:45-126) |

#### Property Inspector (PI)

| action | file:line | function | MCP? |
|---|---|---|---|
| Show | PI:187 -> PS:421-431 | ApplyEditorVisualizationRecipeCommand | yes: show_property |

#### Appearance (DP:1460-1515 and DP:887-1141)

| action | file:line | function | MCP? |
|---|---|---|---|
| Lane checkbox Points / Edges / Surface | DP:984-998 | ApplyEditorRenderHintCommand(Set*/Enable*) | yes: set_visibility (AE:284-292, "Exactly the panel's command") |
| Attribute source selector (per row) | PS:2588-2600 -> PS:2572 | ApplyEditorAttributeBindingCommand | yes: bind_attribute |
| Color row uniform color | DP:474-487 | ApplyEditorVisualizationConfigCommand (lane) | yes: lane_style source/color |
| Color interpretation | DP:898-903 | same | yes: lane_style color_interpretation |
| Color mapping (shared scalar controls, lane target) | DP:906-908 -> PS:572-693 | same | yes: lane_style colormap/auto_range/range/bin_count/isoline_* (src/runtime/Visualization/Runtime.LaneVisualizationStyle.cpp:81-133) |
| Uniform point size | DP:446-456 | ApplyEditorRenderHintCommand(SetUniformPointSize) | **partial**: set_visibility never sets this field |
| Uniform edge width | DP:389-399 | ApplyEditorRenderHintCommand(SetUniformEdgeWidth) | **partial** (same) |
| Settings: Surface domain | DP:409-417 | ApplyEditorRenderHintCommand(SurfaceDomain) | **partial** (domain copied from the model, AE:288) |
| Settings: Edge domain | DP:367-375 | ApplyEditorRenderHintCommand(EdgeDomain) | **partial** (AE:290) |
| Settings: Point type | DP:426-433 | ApplyEditorRenderHintCommand(PointType) | **partial** (AE:292) |
| Settings: Property > Material / default | DP:510-518 | ApplyEditorVisualizationConfigCommand | yes: lane_style source=Material |
| Settings: Property > Uniform color | DP:520-525 | same | yes: lane_style |
| Settings: Property > a property | DP:541-558 | ApplyEditorVisualizationPropertyCommand -> ConfigCommand (VEA:1630) | yes: lane_style scalar_field_name/color_buffer_name (inner command) |
| Settings: Color edit | DP:1110-1111 -> PS:525-536 | ApplyEditorVisualizationConfigCommand (lane) | yes: lane_style |
| Texture baking: Use baked texture | DP:1118-1123 | same | yes: lane_style use_baked_texture |
| Vector fields: add vec3 property | DP:836-863 | ApplyEditorGeometryVectorFieldCommand(Add) | yes: vector_field add |
| Vector fields: remove layer | DP:620-625 | ...(Remove) | yes: vector_field remove |
| Vector fields: layer style (visible, depth test, length mode/property/value, sliders, color property/color, colormap, range, arrows/stride/max) | DP:636-765 | ...(Update, UseLayerStyle) | yes: vector_field update (UseLayerStyle only matters for Add, VEA:2429) |
| Vector fields: Compute arrow order | DP:809-818 | ApplyEditorGlyphOrderCommand (src/runtime/Editor/Operations/Runtime.PointSamplingOperations.cppm:75) | **no** (S) |
| Advanced: texture bake (mesh) | DP:1055 | see "Texture bake (shared)" | |

#### Domain Properties windows (pointcloud/graph/mesh, DP:182-259)

| action | file:line | function | MCP? |
|---|---|---|---|
| "Select in viewport" (and combo/viewport pick while ticked) | DP:236-255 | ApplyEditorPrimitiveSelection (src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:186) | **no** (Sel) |

#### Domain Selection windows (DP:1171-1268)

| action | file:line | function | MCP? |
|---|---|---|---|
| Pick target / Highlight / Point radius | DP:1181-1187 | ApplyEditorSelectionInteractionConfig -> ApplyEditorProcessingConfig (src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Mesh.cpp:473-484) | yes: config_apply (selection section, ConfigSections.cpp:38) |
| Add | DP:1239-1240 | ApplyEditorPrimitiveSelection(Add) | **no** (Sel) |
| Remove | DP:1242-1243 | (Remove) | **no** (Sel) |
| Select all | DP:1245-1246 | (All) | **no** (Sel) |
| Invert | DP:1248-1249 | (Invert) | **no** (Sel) |
| Clear | DP:1251-1252 | (Clear) | **no** (Sel) |

#### Texture bake (shared; drawn by Inspector ES:1177 and Appearance DP:1055)

| action | file:line | function | MCP? |
|---|---|---|---|
| Bake | PS:1202-1240 | ApplyEditorTextureBakeCommand (VEO:786) | **no** (S) |
| Bake run Cancel | PS:1254-1266 | CancelEditorJobRun | **partial** (as Jobs > Cancel) |
| Rename > Apply | PS:1358-1365 | RenameEditorBakedTexture (VEO:790) | **no** (S) |
| Remove | PS:1373-1379 | RemoveEditorBakedTexture (VEO:795) | **no** (S) |
| Consume as ... / Render colormap | PS:1416-1500 | SetEditorBakedTextureTargets (VEO:799) | **no** (S) |
| Regenerate UVs | PS:920 -> PS:1711-1722 | ApplyEditorUvRegenerationCommand (src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cppm:272) | **no** (S) |

#### Mesh / point processing windows (MP)

Config rows: the window's widgets set `changed`, then the window calls its `ApplyEditor*Config` (cited); all reach
config_apply on a registered section. "GPU branch": for Vulkan backends the Run button calls an interactive
`Start*Transaction` instead of the one-shot command the tool calls; the one-shot command handles that backend itself for
normals (src/runtime/Editor/Operations/Runtime.NormalOperations.Processing.cpp:1315-1319) and smoothing
(src/runtime/Editor/Operations/Runtime.MeshFieldOperations.cppm:181-185); for the other four it is unknown.

| window | action | file:line | function | MCP? |
|---|---|---|---|---|
| Denoise | Denoise | MP:965-969 | ApplyEditorMeshDenoiseCommand | yes: run_operation (AO:509) |
| Curvature | config controls | MP:1057-1061 | ApplyEditorMeshCurvatureConfig | yes: config_apply |
| Curvature | Compute | MP:1063-1076 | ApplyEditorMeshCurvatureCommand | yes: run_operation (AO:441) |
| Curvature | Show | MP:1092 | Show (recipe) | yes: show_property |
| Segmentation | Apply configuration | MP:1343-1346 (MP:1329) | ApplyEditorCurvatureSegmentationConfig | yes: config_apply |
| Segmentation | Run segmentation | MP:1360-1372 | ApplyEditorConfiguredCurvatureSegmentationCommand | yes: run_operation (AO:458) |
| Segmentation | auto-visualize after run | MP:1370-1371 -> MP:241-261 | ApplyEditorRenderHintCommand (face/edge domains) + ApplyEditorVisualizationPropertyCommand x2 | **partial**: coloring via lane_style; render-hint domains not expressible |
| Segmentation | Show outputs (9) | MP:1390 | Show | yes: show_property |
| Remesh | Remesh | MP:1557-1560 | ApplyEditorMeshRemeshCommand | yes: run_operation (AO:530) |
| Subdivide | Subdivide | MP:1664-1667 | ApplyEditorMeshSubdivideCommand | yes: run_operation (AO:552) |
| Simplify | Simplify | MP:1750-1753 | ApplyEditorMeshSimplifyCommand | yes: run_operation (AO:570) |
| Normal Estimation | config controls | MP:1982-1987 | ApplyEditorNormalEstimationConfig | yes: config_apply |
| Normal Estimation | Estimate normals | MP:1995-2046 | CPU: ApplyEditorConfiguredNormalEstimation (MP:2039); GPU: StartEditorNormalEstimationTransaction (MP:2011) | **partial** (GPU branch) |
| Normal Estimation | GPU Accept | MP:4092-4095 | AcceptEditorNormalEstimation | **no** (S/H) |
| Normal Estimation | GPU Discard | MP:4103 | DiscardEditorNormalEstimation | **no** (S) |
| Normal Estimation | Show face normals | MP:2054-2071 | ApplyEditorRenderHintCommand(SurfaceDomain=Face) + ApplyEditorVisualizationPropertyCommand | **partial**: coloring via lane_style; surface domain not expressible |
| Normal Estimation | Show normals | MP:2074 | Show (normal direction) | yes: show_property normal_direction |
| Outlier Analysis | config controls | MP:2186-2191 | ApplyEditorOutlierAnalysisConfig | yes: config_apply |
| Outlier Analysis | Detect outliers | MP:2228-2244 | CPU: ApplyEditorConfiguredOutlierAnalysis (MP:2213); GPU: StartEditorOutlierAnalysisTransaction (MP:2235) | **partial** (GPU branch) |
| Outlier Analysis | GPU Accept | MP:2262-2264 | AcceptEditorOutlierAnalysis | **no** (S/H) |
| Outlier Analysis | GPU Discard | MP:2266-2267 | DiscardEditorOutlierAnalysis | **no** (S) |
| Outlier Analysis | Remove marked points | MP:2284-2285 -> MP:2199-2215 | Config(Operation=RemoveMarked) + ApplyEditorConfiguredOutlierAnalysis | yes: config_apply `operation` (src/runtime/Modules/OutlierAnalysis/Runtime.OutlierAnalysisConfig.cppm:75) + run_operation |
| Outlier Analysis | Show mask / Show score | MP:2288, MP:2291 | Show | yes: show_property |
| ISS Keypoints | config controls | MP:2399 | ApplyEditorKeypointAnalysisConfig | yes: config_apply |
| ISS Keypoints | Detect keypoints | MP:2389-2417 | CPU: ApplyEditorConfiguredKeypointAnalysis (MP:2413); GPU: StartEditorKeypointAnalysisTransaction (MP:2407) | **partial** (GPU branch) |
| ISS Keypoints | GPU Accept | MP:2388 -> MP:225 | AcceptEditorPointScalar | **no** (S/H) |
| ISS Keypoints | GPU Discard | MP:2388 -> MP:226-228 | DiscardEditorPointScalar | **no** (S) |
| ISS Keypoints | Show (2) | MP:2433, MP:2436 | Show | yes: show_property |
| FPFH Descriptors | config controls (incl. Name all 33 bins MP:2517) | MP:2563 | ApplyEditorDescriptorAnalysisConfig | yes: config_apply |
| FPFH Descriptors | Compute | MP:2553-2571 | ApplyEditorConfiguredDescriptorAnalysis (MP:2567) | yes: run_operation (AO:483) |
| FPFH Descriptors | Show | MP:2587 | Show | yes: show_property |
| Kernel Density | config controls | MP:2681 | ApplyEditorKernelDensityConfig | yes: config_apply |
| Kernel Density | Estimate density | MP:2689-2695 | one-shot (MP:2692) or StartEditorKernelDensityTransaction (MP:2689) | **partial** (GPU branch) |
| Kernel Density | GPU Accept / Discard | MP:2670 -> MP:225-228 | AcceptEditorPointScalar / DiscardEditorPointScalar | **no** x2 (S) |
| Kernel Density | Show | MP:2707 | Show | yes: show_property |
| Density Weights | config controls | MP:2810 | ApplyEditorDensityWeightConfig | yes: config_apply |
| Density Weights | Compute compact weights | MP:2818-2824 | one-shot (MP:2821) or StartEditorDensityWeightTransaction (MP:2818) | **partial** (GPU branch) |
| Density Weights | GPU Accept / Discard | MP:2799 -> MP:225-228 | point-scalar Accept/Discard | **no** x2 (S) |
| Density Weights | Show | MP:2837 | Show | yes: show_property |
| Construct from Points | config controls | MP:2968-2971 | ApplyEditorPointConstructionConfig | yes: config_apply |
| Construct from Points | Construct | MP:2983-2996 | ApplyEditorConfiguredPointConstruction | yes: run_operation (AO:495) |
| Point Spacing | config controls | MP:3093 | ApplyEditorPointSpacingConfig | yes: config_apply |
| Point Spacing | Estimate radii | MP:3101-3107 | one-shot (MP:3104) or StartEditorPointSpacingTransaction (MP:3101) | **partial** (GPU branch) |
| Point Spacing | GPU Accept / Discard | MP:3082 -> MP:225-228 | point-scalar Accept/Discard | **no** x2 (S) |
| Point Spacing | Show | MP:3119 | Show | yes: show_property |
| Bilateral Filter | config controls (incl. Write to input positions MP:3190) | MP:3226 | ApplyEditorBilateralFilterConfig | yes: config_apply |
| Bilateral Filter | Filter positions | MP:3228-3234 | ApplyEditorConfiguredBilateralFilter | yes: run_operation (AO:487) |
| ICP | config controls (incl. Swap MP:3296, Use final pose MP:3366) | MP:3371-3376 | ApplyEditorRegistrationConfig | yes: config_apply |
| ICP | Run ICP / apply trajectory step | MP:3384-3412 | ApplyEditorConfiguredRegistrationCommand | yes: run_registration icp |
| ICP | progress Cancel | MP:3478-3483 | CancelEditorRegistration (src/runtime/Editor/Operations/Runtime.RegistrationOperations.cppm:119) | **no** (J); whether jobs_cancel can reach the same run: unknown |
| CPD | config controls (incl. Swap MP:3588) | MP:3769-3773 | ApplyEditorCoherentPointDriftConfig | yes: config_apply |
| CPD | Start / Restart | MP:3793-3803 | StartEditorCoherentPointDrift (RegistrationOperations.cppm:237) | **no** (S): tool has only the one-shot ApplyEditorConfiguredCoherentPointDrift (AO:984) |
| CPD | Step / Step 10 / Run to end | MP:3815-3821 | StepEditorCoherentPointDrift (:241) | **no** (S) |
| CPD | Apply | MP:3823-3832 | ApplyEditorCoherentPointDrift (:247) | **no** (S/H) |
| CPD | Discard | MP:3834-3840 | CancelEditorCoherentPointDrift (:244) + reset | **no** (S) |
| CPD | progress Cancel | MP:3880-3888 | CancelEditorCoherentPointDrift | **no** (J) |
| CPD | Export trace (CSV) | MP:3996-4005 | panel writes `exports/` itself | **no** (F) |
| Smooth Property | config controls (incl. Overwrite input MP:4266) | MP:4339-4348 | ApplyEditorPropertySmoothingConfig | yes: config_apply |
| Smooth Property | Smooth property | MP:4359-4400 | CPU: ApplyEditorPropertySmoothingCommand (MP:4396); Vulkan: StartEditorPropertySmoothing (MP:4371) | **partial** (GPU branch; one-shot auto-accepts Vulkan, MeshFieldOperations.cppm:181-185) |
| Smooth Property | GPU Stop | MP:4467 | StopEditorPropertySmoothing (MeshFieldOperations.cppm:227) | **no** (S) |
| Smooth Property | GPU Accept | MP:4468-4469 | AcceptEditorPropertySmoothing (:232) | **no** (S/H) |
| Smooth Property | GPU Discard | MP:4476-4477 | DiscardEditorPropertySmoothing (:236) | **no** (S) |
| Smooth Property | Show | MP:4427 | Show | yes: show_property |
| Spectral Modes | config controls | MP:4635-4637 | ApplyEditorLaplacianEigenbasisConfig | yes: config_apply |
| Spectral Modes | Compute eigenbasis | MP:4644-4651 | ApplyEditorLaplacianEigenbasisCommand | yes: run_operation (AO:420) |
| Spectral Modes | Show (selected eigenvector, signature, distance) | MP:4668, MP:4679 | Show | yes: show_property |
| Harmonic Field | config controls | MP:4896-4898 | ApplyEditorHarmonicFieldConfig | yes: config_apply |
| Harmonic Field | Solve | MP:4905-4912 | ApplyEditorHarmonicFieldCommand | yes: run_operation (AO:426) |
| Harmonic Field | Show | MP:4917 | Show | yes: show_property |
| Scalar Gradient | config controls | MP:4974-4976 | ApplyEditorScalarGradientConfig | yes: config_apply |
| Scalar Gradient | Compute Gradient | MP:4983-4990 | ApplyEditorScalarGradientCommand | yes: run_operation (AO:432) |
| Scalar Gradient | Show Gradient Vectorfield | MP:5001-5016 | ApplyEditorGeometryVectorFieldCommand (Add or Update) | yes: vector_field |
| Geodesics | config controls + sources (Add picked vertex MP:5081, Use selected vertices MP:5088, Add source MP:5097, Clear sources MP:5103, Clear source property MP:5131) | MP:5057, MP:5168 | ApplyEditorGeodesicsConfig | yes: config_apply |
| Geodesics | Compute geodesics | MP:5176-5183 | ApplyEditorConfiguredGeodesicsCommand | yes: run_operation (AO:450) |
| Geodesics | Show (2) | MP:5190, MP:5192 | Show | yes: show_property |
| Scalar Ridges | Extract ridges | MP:5307-5309 | ApplyEditorScalarRidgeCommand | yes: run_operation (AO:599) |
| Scalar Ridges | Show (2) | MP:5329, MP:5332 | Show | yes: show_property |
| Point Sampling | config controls | MP:5430-5432 | ApplyEditorPointSamplingConfig | yes: config_apply |
| Point Sampling | Sample | MP:5443-5446 | ApplyEditorPointSamplingCommand | yes: run_point_sampling |

#### Method windows (MeP)

| window | action | file:line | function | MCP? |
|---|---|---|---|---|
| K-Means | Apply configuration | MeP:1904-1915 | ApplyEditorClusteringConfig | yes: config_apply |
| K-Means | Run K-Means | MeP:1961-1972 | ApplyEditorClusteringConfig + SubmitKMeansRun | yes: run_kmeans |
| K-Means | GPU Stop | MeP:1947 | Clustering GpuRun(Stop) (src/runtime/Modules/Clustering/Runtime.ClusteringTypes.cppm:194) | **no** (S) |
| K-Means | GPU Accept | MeP:1948 | GpuRun(Accept) | **no** (S/H) |
| K-Means | GPU Discard | MeP:1949-1951 | GpuRun(Discard) | **no** (S) |
| K-Means | Show (labels, colors, scalar labels) | MeP:1976-1979 | Show | yes: show_property |
| Consolidation | Apply configuration | MeP:1597-1609 | ApplyEditorPointCloudConsolidationConfig | yes: config_apply |
| Consolidation | Consolidate selected property set | MeP:1652-1669 -> MeP:139-165 | Config + SubmitEditorPointCloudConsolidation | yes: run_point_cloud_consolidation |
| Consolidation | GPU Stop | MeP:1637-1638 | Consolidation GpuRun(Stop) (src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationTypes.cppm:208) | **no** (S) |
| Consolidation | GPU Accept | MeP:1640-1641 | GpuRun(Accept) | **no** (S/H) |
| Consolidation | GPU Discard | MeP:1643-1645 | GpuRun(Discard) | **no** (S) |
| Consolidation | Show (positions, normals) | MeP:1688, MeP:1691 | Show | yes: show_property |
| Consolidation | Undo consolidation | MeP:1705-1710 | Undo | yes: undo (no label guard) |
| Consolidation | Redo consolidation | MeP:1713-1718 | Redo | yes: redo |
| Progressive Poisson | config controls (on edit) | MeP:2391-2395, MeP:2418-2421 | ApplyEditorProgressivePoissonConfig | yes: config_apply |
| Progressive Poisson | Run | MeP:2439-2440 -> MeP:2398-2416 | ApplyEditorProgressivePoissonCommand | yes: run_operation (AO:640) |
| Progressive Poisson | auto-run after edit | MeP:2445 | same | yes: run_operation |
| Progressive Poisson | Show | MeP:2467 | Show | yes: show_property |
| Parameterize (UV) | Apply configuration (pins, boundary values, strategy) | MeP:2988-2989 -> MeP:3401-3408 | ApplyEditorParameterizationConfig | yes: config_apply |
| Parameterize (UV) | Parameterize selected mesh | MeP:3006-3012 -> PS:2836-2853 | Config + ApplyEditorConfiguredParameterizationCommand | yes: run_operation (AO:657) |
| Parameterize (UV) | Show | MeP:3023 | Show | yes: show_property |
| Parameterize (UV) | Undo UV writeback | MeP:3037-3042 | Undo | yes: undo |
| Parameterize (UV) | Redo UV writeback | MeP:3045-3050 | Redo | yes: redo |
| Parameterize (UV) | Generate atlas for selected mesh | MeP:3454-3463 -> MeP:3416-3425 | ApplyEditorUvRegenerationCommand | **no** (S) |
| UV Atlas Workspace | Regenerate | MeP:3698-3699 | ApplyEditorUvRegenerationCommand | **no** (S) |
| UV Atlas Workspace | layout: splitter, Mesh/Atlas left, Open on new UVs | MeP:3620-3628, MeP:3682-3683, MeP:3686-3687 -> MeP:3560-3569 | ApplyEditorParameterizationConfig (view sub-config) | yes: config_apply (persisted layout) |

Not a user action but a config write the UI makes on its own: after an atlas result, MeP:3540-3553 rewrites the
parameterization `Texcoords` field (ApplyEditorParameterizationConfig).

### Totals (A)

193 action rows (method above). yes 103, **partial 28, no 62**.

| window group | yes | partial | no |
|---|---|---|---|
| Main menu / shortcuts | 5 | 0 | 0 |
| Viewport gestures | 0 | 2 | 1 |
| Unsaved-changes dialog | 0 | 2 | 0 |
| Scene Hierarchy | 1 | 0 | 0 |
| Inspector | 1 | 0 | 4 |
| File / Scene | 4 | 0 | 2 |
| File / Export | 0 | 0 | 1 |
| File / Import + queue | 0 | 1 | 2 |
| Frame Graph | 0 | 0 | 1 |
| Render Recipes | 0 | 0 | 8 |
| Camera / Render | 6 | 0 | 0 |
| Geometry Visualization | 1 | 7 | 0 |
| Jobs | 0 | 1 | 0 |
| Diagnostics / Log | 0 | 0 | 1 |
| Screenshot | 1 | 0 | 0 |
| Property Inspector | 1 | 0 | 0 |
| Appearance | 13 | 5 | 1 |
| Domain Properties | 0 | 0 | 1 |
| Domain Selection | 1 | 0 | 5 |
| Texture bake (shared) | 0 | 1 | 5 |
| Mesh/point processing (MP) | 51 | 9 | 22 |
| Method windows (MeP) | 18 | 0 | 8 |
| **sum** | **103** | **28** | **62** |

The 62 "no" rows by category: S/H 40 (gizmo drag, transform edits 2, presentation slot 2, New/Close scene 2, arrow order,
texture bake/rename/remove/targets/UV regeneration 5, atlas generation 2, GPU Accept 9, GPU Discard 9, GPU Stop 3,
CPD Start/Step/Apply/Discard 4), Sel 6 (primitive selection), C 9 (render recipes 8, GPU profiling 1), J 3 (import cancel,
ICP cancel, CPD cancel), F 2 (geometry export, CPD trace CSV), O 2 (import queue clear, log clear).
Restricted to scene/config/selection/history (S/H + Sel + C): **55 no**, 28 partial.

The 28 "partial" rows: Entity-target visualization in Geometry Visualization 7; render-hint fields (point size, edge width,
surface/edge domain, point type) 5; GPU-interactive branch of a Run button 7; segmentation auto-visualize and Show face
normals 2; run-level job cancel 2; import payload hint 1; unsaved-changes dialog 2; click pick 1; file drop 1.

### (B) State-changing editor entry points the Sandbox UI calls

#### Reached: a tool calls the same function (40)

| entry point | declared | tool | note |
|---|---|---|---|
| SelectEditorEntity | SEO:534 | select_entity | |
| ApplyEditorFileImportCommand | SEO:536 | import_file | PayloadKind not exposed |
| ApplyEditorSceneSaveCommand | SEO:540 | save_scene | |
| ApplyEditorSceneLoadCommand | SEO:544 | load_scene | |
| ApplyEditorCameraControllerCommand | SEO:558 | set_camera | |
| ApplyEditorCameraPoseCommand | SEO:564 | set_camera | |
| ApplyEditorRenderHintCommand | VEO:749 | set_visibility | only enable/bounds fields |
| ApplyEditorVisualizationConfigCommand | VEO:753 | lane_style, bind_attribute | lane targets only; Entity target not |
| ApplyEditorVisualizationRecipeCommand | VEO:766 | show_property | enable only |
| ApplyEditorAttributeBindingCommand | VEO:770 | bind_attribute, show_property | |
| ApplyEditorGeometryVectorFieldCommand | VEO:781 | vector_field | |
| ViewCaptureModule::Request | src/runtime/Rendering/Runtime.ViewCapture.cppm:151 | view_capture, view_screenshot | |
| EditorDocumentCommandSurface::Undo/Redo (via Sandbox ApplyEditorHistoryCommand, src/app/Sandbox/Editor/Sandbox.PanelSupport.hpp:52) | SEO:479-482 | undo, redo | |
| 13 configured runs: ApplyEditorConfigured{BilateralFilter, CurvatureSegmentationCommand, DensityWeight, DescriptorAnalysis, GeodesicsCommand, KernelDensity, KeypointAnalysis, NormalEstimation, OutlierAnalysis, ParameterizationCommand, PointConstruction, PointSpacing, RegistrationCommand} | lines in the declaration scan, e.g. src/runtime/Editor/Operations/Runtime.NormalOperations.cppm:66 | run_operation / run_keypoint_analysis / run_registration | AO:450-495, 657, 781, 972 |
| 12 direct runs: ApplyEditor{HarmonicField, LaplacianEigenbasis, MeshCurvature, MeshDenoise, MeshRemesh, MeshSimplify, MeshSubdivide, PointSampling, ProgressivePoisson, PropertySmoothing, ScalarGradient, ScalarRidge}Command | e.g. src/runtime/Editor/Operations/Runtime.MeshFieldOperations.cppm:185 | run_operation / run_mesh_operation / run_point_sampling | AO:414-640, 762 |
| SubmitKMeansRun, SubmitEditorPointCloudConsolidation | src/runtime/Editor/Operations/Runtime.PointCloudServiceOperations.cppm:67, :69 | run_kmeans, run_point_cloud_consolidation | AO:858, AO:933 |

#### Reached through the same inner command (26)

| entry point | inner path | tool |
|---|---|---|
| ApplyEditorVisualizationPropertyCommand (VEO:757) | -> ApplyEditorVisualizationConfigCommand (VEA:1630) | lane_style (partial: Entity target not) |
| 24 x ApplyEditor*Config: BilateralFilter, Clustering, CoherentPointDrift, CurvatureSegmentation, DensityWeight, DescriptorAnalysis, Geodesics, HarmonicField, KernelDensity, KeypointAnalysis, LaplacianEigenbasis, MeshCurvature, NormalEstimation, OutlierAnalysis, Parameterization, PointCloudConsolidation, PointConstruction, PointSampling, PointSpacing, ProgressivePoisson, PropertySmoothing, Registration, ScalarGradient, SelectionInteraction | -> ApplyEditorProcessingConfig / ApplyOperationConfig -> ApplyEngineConfigHotSubset (verified per function by reading each definition) | config_apply |
| ApplyGizmoSnapConfig (src/runtime/Scene/Runtime.SceneInteractionModule.cppm:80) | -> ApplyEngineConfigHotSubset (SceneInteractionModule.Config.cpp:115) | config_apply |

#### Not reached by any tool (45: 42 no, 3 partial)

| entry point | declared | UI caller |
|---|---|---|
| ApplyEditorTransformEdit | SEO:553 | ES:1079, ES:1093 |
| ApplyEditorNewSceneCommand | SEO:548 | PS:76 |
| ApplyEditorCloseSceneCommand | SEO:550 | PS:79 |
| ApplyEditorGeometryExportCommand | SEO:531 | ES:1538 |
| EditorAssetImportQueueCommandSurface::Cancel | SEO:268 | ES:234 |
| EditorAssetImportQueueCommandSurface::ClearCompleted | SEO:267 | ES:143 |
| ApplyEditorGeometryPresentationSlotDefaultCommand | VEO:773 | ES:1231 |
| ApplyEditorGeometryPresentationSlotPropertyCommand | VEO:777 | ES:1268 |
| ApplyEditorTextureBakeCommand | VEO:786 | PS:1212 |
| RenameEditorBakedTexture | VEO:790 | PS:1361 |
| RemoveEditorBakedTexture | VEO:795 | PS:1376 |
| SetEditorBakedTextureTargets | VEO:799 | PS:1494 |
| ApplyEditorUvRegenerationCommand | src/runtime/Editor/Operations/Runtime.ParameterizationOperations.cppm:272 | PS:1719, MeP:3425 |
| ApplyEditorGlyphOrderCommand | src/runtime/Editor/Operations/Runtime.PointSamplingOperations.cppm:75 | DP:811 |
| ApplyEditorPrimitiveSelection | src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.cppm:186 | DP:242, DP:1231 |
| ApplyEditorRenderRecipeCommand | src/runtime/Editor/Operations/Runtime.RenderRecipeEditingOperations.cppm:399 | ES:726-973 (8 sites) |
| ApplyEditorGpuProfilingConfigCommand | src/runtime/Editor/Operations/Runtime.RenderRecipeEditingOperations.cppm:392 | ES:1699 |
| StartEditorNormalEstimationTransaction | src/runtime/Editor/Operations/Runtime.NormalOperations.cppm:92 | MP:2011 |
| StartEditorOutlierAnalysisTransaction | src/runtime/Modules/OutlierAnalysis/Runtime.OutlierAnalysisOperation.cppm:57 | MP:2235 |
| StartEditorKeypointAnalysisTransaction | src/runtime/Editor/Operations/Runtime.PointAnalysisOperations.cppm:85 | MP:2407 |
| StartEditorKernelDensityTransaction | src/runtime/Modules/KernelDensity/Runtime.KernelDensityConfig.cppm:137 | MP:2689 |
| StartEditorDensityWeightTransaction | src/runtime/Modules/DensityWeight/Runtime.DensityWeightOperation.cppm:52 | MP:2818 |
| StartEditorPointSpacingTransaction | src/runtime/Modules/PointSpacing/Runtime.PointSpacingConfig.cppm:140 | MP:3101 |
| StartEditorPropertySmoothing | src/runtime/Editor/Operations/Runtime.MeshFieldOperations.cppm:222 | MP:4371 |
| StopEditorPropertySmoothing | src/runtime/Editor/Operations/Runtime.MeshFieldOperations.cppm:227 | MP:4467 |
| AcceptEditorNormalEstimation | src/runtime/Editor/Operations/Runtime.NormalOperations.cppm:98 | MP:4095 |
| AcceptEditorOutlierAnalysis | src/runtime/Modules/OutlierAnalysis/Runtime.OutlierAnalysisOperation.cppm:61 | MP:2264 |
| AcceptEditorPointScalar | src/runtime/Editor/Operations/Runtime.PointScalarTransaction.cppm:65 | MP:225 |
| AcceptEditorPropertySmoothing | src/runtime/Editor/Operations/Runtime.MeshFieldOperations.cppm:232 | MP:4469 |
| DiscardEditorNormalEstimation | src/runtime/Editor/Operations/Runtime.NormalOperations.cppm:102 | MP:4103 |
| DiscardEditorOutlierAnalysis | src/runtime/Modules/OutlierAnalysis/Runtime.OutlierAnalysisOperation.cppm:64 | MP:2267 |
| DiscardEditorPointScalar | src/runtime/Editor/Operations/Runtime.PointScalarTransaction.cppm:68 | MP:227 |
| DiscardEditorPropertySmoothing | src/runtime/Editor/Operations/Runtime.MeshFieldOperations.cppm:236 | MP:4477 |
| ClusteringModule GpuRun(Stop/Accept/Discard) | src/runtime/Modules/Clustering/Runtime.ClusteringTypes.cppm:194 | MeP:1947-1951 |
| PointCloudConsolidationModule GpuRun(Stop/Accept/Discard) | src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationTypes.cppm:208 | MeP:1637-1645 |
| StartEditorCoherentPointDrift | src/runtime/Editor/Operations/Runtime.RegistrationOperations.cppm:237 | MP:3798 |
| StepEditorCoherentPointDrift | :241 | MP:3810 |
| ApplyEditorCoherentPointDrift | :247 | MP:3826 |
| CancelEditorCoherentPointDrift | :244 | MP:3837, MP:3887 |
| CancelEditorRegistration | :119 | MP:3483 |
| SceneInteractionModule Begin/Preview/Commit/CancelGizmoDrag (one session) | src/runtime/Scene/Runtime.SceneInteractionModule.cppm:193-202 | ES:2673, ES:2691, ES:2707, ES:2612 |
| ClearEditorDiagnosticsLog | src/runtime/Editor/Runtime.DiagnosticsStream.cppm:92 | DG:33 |
| *partial* CancelEditorJobRun | src/runtime/Editor/Runtime.EditorProcessing.cppm:150 | PS:807, PS:1265, PS:2228 (tool: CancelEditorJob, one job) |
| *partial* SelectionController::RequestClickPick | src/runtime/Scene/Runtime.SelectionController.cppm:174 | SceneInteractionModule.cpp:101 (outcome subset: select_entity) |
| *partial* AssetWorkflowModule::ImportDroppedFilePaths | src/runtime/AssetWorkflow/Runtime.AssetWorkflowModule.cppm:270 | Engine.cpp:1264 (outcome: import_file per path) |

#### Outside the UI-called set (for completeness)

- Exported, no UI caller and no tool: ApplyEditorPrimitiveViewCommand (SEO:569-572; callers only in its own module and 2 test files).
- 15 explicit-config `ApplyEditor*Command` variants (Bilateral, CPD, CurvatureSegmentation, DensityWeight, Descriptor, Geodesics,
  KernelDensity, Keypoint, NormalEstimation, Outlier, Parameterization, PointConstruction, PointSpacing, Registration;
  plus ApplyEditorCoherentPointDriftCommand) have no UI caller; the configured variants the tools call wrap them
  (e.g. NormalOperations.Processing.cpp:1375-1382, RegistrationOperations.Processing.cpp:1204-1217), so they are reached indirectly.
- Tool-only entry points (no UI site): ApplyEditorConfiguredCoherentPointDrift (RegistrationOperations.cppm:254), CancelEditorJob
  (EditorProcessing.cppm:139), CancelEditorRunJobs (:152).
- Excluded from (B) as view/lane control: EditorUiHost::ApplyVisibilityCommand (src/runtime/Editor/Runtime.EditorUiHost.cppm:122),
  EditorWindowRegistry::SetOpen, SubmitEditorParameterizationUvView, AgentServerModule::DisconnectClient
  (src/runtime/Agent/Runtime.AgentServer.cppm:131).
- Kernel command bus (src/runtime/Kernel/Runtime.CommandBus.cppm): the UI enqueues nothing directly; K-Means and consolidation
  submissions enqueue through their services (src/runtime/Modules/Clustering/Runtime.ClusteringTypes.cpp:224,
  src/runtime/Modules/PointCloudConsolidation/Runtime.PointCloudConsolidationTypes.cpp:147), both reached by tools.

**(B) totals:** of 111 entry points the UI calls, 66 are reached (40 same function, 26 via the same inner path; 4 of them only
for some arguments: file import, render hints, visualization config, visualization property), 45 are not (42 no, 3 partial).

### Limitations of the method

1. Function identity is not behavioral equivalence. A tool reaching the inner function skips the UI's wrapper checks
   (e.g. ApplyEditorVisualizationPropertyCommand's property validation, VEA:1526-1620) and may set different defaults
   (vector_field Add without UseLayerStyle, VEA:2429). Static reading cannot show the results match.
2. Argument coverage was judged from the tool handlers (which fields they set), not by enumerating every field of every
   command. In particular, whether each config section's schema exposes every field a panel writes was checked only for
   the outlier `operation` field; the other 24 sections are assumed complete because panel and tool share one validator and
   one section registration. That assumption is unverified.
3. `std::function` command surfaces (DocumentCommands.Undo/Redo, context.ApplyEngineConfigHotSubset, AssetImportCommands) were
   resolved from one binding site each (e.g. EditorWorkspaceSession.cpp:953); which binding is live at runtime depends on the
   attachment and was not executed.
4. GPU branches: auto-accept of the one-shot command was seen for normals and smoothing only; for outliers, keypoints, kernel
   density, density weights and spacing it is unknown whether the tool's one-shot path covers every backend the panel's
   interactive branch offers.
5. Unknown: whether jobs_cancel can cancel an ICP or interactive-CPD run (their panels cancel through progress handles), and
   whether the render-recipe draft/artifact state counts as engine config in the R5 sense.
6. Granularity is a choice: rows group controls that call one function with one command kind (e.g. 10 scalar-field controls
   = 1 row). Counting each ImGui widget instead would raise all numbers; the categories would not change.
7. Coverage of UI files: all ImGui controls under src/app/Sandbox/Editor and the runtime input sites in SceneInteractionModule,
   EditorUiModule, CameraControllers and Engine (drop/quit) were read. A search of src/runtime, src/graphics and src/app for
   ImGui controls found only src/runtime/Editor/Runtime.EditorPropertyWidgets.cpp (view-local sliders). Windows registered by
   code outside these trees at runtime would be missed.
8. Exclusion of camera navigation and of window/layout actions is a judgment; the parameterization workspace layout writes
   persisted config and was counted (as reachable).
9. No build, no run, no trace: all "reached" claims are static call-path claims.
