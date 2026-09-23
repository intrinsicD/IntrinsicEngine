# METHOD-047 independent review 2 — findings

- Reviewer: Claude Opus 5.5 (`claude-opus-5-5`), numerical and systems review. Read-only: no builds, no test runs, no source edits, no agents.
- Revision under review: `d32aa779c67663910e93d929f52d1657ce7695a8` ("METHOD-047 integrated review remediation snapshot"), worktree `/tmp/intrinsic-method047-review2`.
  - Its parent is `c97235568`, the same as review1's snapshot `947438ca9`. So `git diff 947438ca9 d32aa779c` is the complete remediation delta: 28 files, +1236/−298.
  - The root's later fixture-only lifetime fix (the RenderExtractionCache scope in the direct-mesh import test) is **not** in this revision. It is not covered by this verdict.
- Read: AGENTS.md, the core, review and results-audit skills, review1-findings.md, bake-fixes-handoff.md, and the whole remediation diff.
  - Changed functions read in context: `TryPackCharts`, `PackCharts`, `AlignChartTexelCenters`, the validator region union and `CoversPixelCenter`; TextureBake `Prepare`/`Schedule`/`RejectBeforePreparation`/`ClassifyGeneratedAsset`/`HasScheduleCapacity`/`DrainCompletedTransfers`/`Remove`/`ReconcileSurfaceAppearance`, the revision token, and `MeasurePropertyTextureBakeCoverage`/`SampleTriangle`; the Uv job and undo guards; import materialization and the `Required`/`Optional` call sites; `AssetService::Reload` and the `AssetState` transitions; the UI, codec, docs and new tests.
- Logs read, but not re-run:
  - `bake-integrated-build-1.log`, `bake-integrated-focused-1.log`, `asan-geometry-test-1.log`, `full-cpu-6.log` (partial);
  - `atlas-corpus-2/summary.json` and `results/*.json`, `atlas-corpus-2-validation.log`, `child-4096-independent-audit.json`.

## Verdict

**Not ready to merge at `d32aa779c`.**

Most review1 findings are correctly fixed, and the new geometry and bake contracts are sound in their main paths. Three things block it:

1. **A new High regression.** Automatic appearance re-runs the whole bake preparation every frame, with no recorded failure, for any mesh whose source snapshot exceeds 64 MiB (N1).
2. **An unresolved H2 residue.** Surface reconstruction still requires UVs and loses its whole output when atlas admission fails (N2).
3. **Incomplete evidence at this revision.**
   - `bake-integrated-focused-1.log` records `RejectedAtlasKeepsDirectMeshRenderableAndSelectable` as a SEGFAULT. The root says the fix is fixture-only and outside this revision.
   - The full CPU run (`full-cpu-6.log`) was still in progress when I read it: 1607/5004, no failures seen yet. `full-cpu-5.log` does not exist.
   - Vulkan, full ASan and full UBSan have not run.
   - No Operational or completion claim is supportable yet.

## Review1 findings: disposition

| # | Disposition | Evidence at `d32aa779c` |
|---|---|---|
| H1 polygon corner corruption | **Resolved.** Rejected before queueing. | `MeshSupport.cpp:184` makes `WalkMeshSoupFaces(requireTriangles)` reject any ring of size ≠ 3. It is called from the shared preflight at `Uv.cpp:1254` with the "triangulate" diagnostic. Test `ParameterizationOperations.WrongTypedUvAndNonTriangleFacesFailClosed` checks that Preview is disabled, Apply returns `InvalidProcessingParameters`, `v:texcoord` is absent and the undo count is 0. The docs and task declare indexed-triangle input. |
| H2 import hard failure | **Partially resolved.** | Direct-mesh import (`RecipePolicies.cpp:891`) and model import (`ModelMaterialization.cpp:551`) now use `Optional`: geometry, normals, render surface and selection survive, and no UVs are invented (`GeometryMaterialization.cpp:833-866`). Both regression tests use a valid under-resolved two-island fixture. **Unresolved:** surface reconstruction (N2). **Deferred** (a policy the root accepted; record it): no xatlas fallback, because import still requests FastStaged `Both`; any exactly degenerate face still rejects the whole atlas; `DirectMeshPostProcess` still ignores `JobCancellation` (`RecipePolicies.cpp:883`). |
| H3 `Unknown` freshness | **Resolved.** | `IsPropertyTextureBakeFreshnessStale` is removed. Reconciliation (`AssetWorkflowModule.cpp:472`), extraction and `IsPropertyTextureBakeRecordBindable` (Fresh plus a matching token) all refuse anything but Fresh. Appearance now rebakes non-Fresh records. The reconciliation test uses real `Bake` identities, not fabricated records; it mutates and restores `v:heat` to prove the stale→fresh round trip. **Contract change:** a Pending output now unbinds the previous texture (`AssetWorkflowModule.cpp:462-465`). That is correct given the in-place reload, but it is a visible change (a flash to fallback during a rebake); the docs should state it. |
| M1 ghost `v:texcoord` | **Resolved as specified.** | `Uv.cpp:1310` never offers `v:texcoord` when `h:texcoord` exists. Test `UvRegenerationNeverPreservesShadowVertexUvsOverCornerAuthority` covers it. Residual semantics: see N7. |
| M2 split 0.8f | **Resolved.** | The codec upper bound is `double(0.8f)` (`FeatureConfigCodecs.Detail.cpp:1944`). The lower bound `0.2f` ≥ 0.2 is already safe. No other `SplitRatio` validator exists. The test round-trips both bounds through the document. |
| M3 appearance extent and retry | **Resolved, with new defects.** | Adaptive extent (`TextureBakeModule.cpp:1688-1719`) and the token gate (`:3230-3238`, `:3295-3298`) are correct for their tested cases. The new defects are N1 (High), N3 (Medium) and N5 (Low). |
| L1 tab identity | **Resolved.** | `"###bake" + packed(generation, index)` at `MethodPanels.cpp:3616`. Nit: records whose value asset was destroyed by a partial Remove all pack to the same id. Using `"###bake" + OutputName` (unique per entity) avoids that. |
| L2 bake degeneracy | **Resolved.** | Exact `ExactUvOrientation == 0` at `TextureBakeModule.cpp:1418`. Test `TinyPositiveUvTrianglesReportResolutionInsteadOfDegeneracy` covers it. |
| L3 raw vector range | **Resolved** for `AutoFinite`. | The component-wise range over the active components applies to `RawFloat` (`:1846-1862`). Test `RawVectorDisplayRangeIncludesNegativeComponents` covers it. |
| L4 "0 / 0" distortion | **Resolved.** | `PanelSupport.cpp:1452` shows "not evaluated". |
| L5 pair atomicity | **Resolved, with caveat N4.** | Classify-then-create-then-reload ordering, rollback of created assets, partial-reload invalidation, both-asset Remove and specific completion diagnostics. Tests cover the busy, reload, remove and coverage-creation-failure cases. The partial-reload and partial-destroy branches are only reachable by reading, as the handoff states. |
| L6 doc drift | **Mostly resolved.** | `graphics.md:260` and `runtime.md:864-891` are updated. The remainder is N8. |
| L7 region components | **Resolved.** | The validator unites the first face per (edge, label) (`Validation.cpp:477-495`). The fin test asserts both counts are 2. Performance note: N9. The UvView comment nit is fixed. |

## New findings

### N1 (High, new in this delta). An oversized source snapshot makes automatic appearance re-run the whole Prepare every frame, and no failure is ever recorded

- **Anchors:**
  - `TextureBakeModule.cpp:1173-1176`: `HasScheduleCapacity` returns false whenever `SourceByteCount(prepared) > 64 MiB`, **even with an empty queue**;
  - `:2086-2098`: `Schedule` maps that to `JobSubmitFailed` after a full `Prepare`;
  - `:3263`: appearance treats every `JobSubmitFailed` as transient, `continue`s, and records nothing.
- **Reasoning:**
  - For a single request larger than the cap, this rejection is permanent, not transient.
  - Each frame, appearance then repeats the full preparation: snapshot, content fingerprints, chart union-find, gutter edges, and coverage measurement at up to four extents from 1024 to 8192².
  - Nothing is written to the record, so the UI shows no diagnostic.
  - Before this delta, that failure was recorded once and not retried.
- **Reachability:** from `SourceByteCount`, roughly 28-90 bytes per triangle depending on indexing. A mesh of about 0.75-2.4 M triangles with baked surface appearance enabled triggers it.
- **Minimal fix:** make "single request exceeds snapshot cap" a non-transient outcome. Options:
  - a distinct status (update the exhaustive switch at `VisualizationEditingOperations.Actions.cpp:2419`);
  - an existing non-transient status with a clear diagnostic;
  - estimating the byte size from element counts inside `RejectBeforePreparation`.

  Keep `continue` only for the pre-preparation queue-count and busy-asset rejections.
- **Test:** appearance on a mesh whose snapshot exceeds a test-lowered cap records Failed once, and `Stats().BakeRequests` stays constant over several frames.

### N2 (Medium, unresolved H2 residue). Surface reconstruction still requires UV success

- **Anchor:** `GeometryProcessingOperations.Construction.cpp:452` calls `BuildRuntimeHalfedgeMeshMaterialization(payload)` with the default `FailurePolicy::Required` and the FastStaged default.
- **Reasoning:**
  - Marching cubes produces coincident edge vertices, and therefore exactly zero-area triangles, whenever a grid sample equals the isovalue (`MarchingCubes.cpp:~590`, interpolation parameter 0 or 1).
  - Noisy point clouds produce tiny islands that are `UnderResolved` at 1024.
  - Either case now fails the whole reconstruction with "Generated mesh normal/UV materialization failed." The baseline fell back to xatlas.
  - This is the same regression class as H2, on a caller the fix did not touch.
- **Minimal fix:** pass `.UvResolution = {.FailurePolicy = RuntimeMeshUvFailurePolicy::Optional}` there, and surface the atlas diagnostic in the reconstruction result.
- **Test:** a regression with a reconstruction that yields a tiny disconnected island, or an iso-exact grid sample.

### N3 (Medium). Automatic appearance can escalate to 8192² unasked, and adaptive extents do not follow the atlas grid

- **Anchors:** `TextureBakeModule.cpp:3256` (`MaxAdaptiveExtent = kPropertyTextureBakeMaxExtent`) and the doubling loop at `:1690-1719`.
- **Resource cost:**
  - One sub-texel island promotes a heat-appearance bake to 8192². That keeps about 256 MiB RGBA8 value plus 256 MiB R32F coverage resident, plus transient depth, per entity, with no user action.
  - Every later property edit re-measures coverage from 1024 upward: about 88 M texel-centre tests on the main thread before the GPU bake.
- **Grid mismatch:**
  - The packer anchors chart samples to the atlas's own texel grid R.
  - Appearance tries only 1024·2^k. For power-of-two R < 1024, every anchor lies on a texel *boundary* at each tried extent: (j+½)/R·E is an integer for E = 2^m·R.
  - For other R (resolution is any integer ≤ 16384), there is no relation at all.
  - So a valid 512 atlas can be baked only by escalating toward 8192, or fail. A 1536 atlas has no anchoring guarantee at all.
- **Minimal fix:**
  - Publish the generated atlas extent per entity. The handoff already recommends this.
  - Start appearance at that extent. Keep adaptive growth as a bounded safety net: a lower automatic cap such as 4096, and/or a total generated-texture byte budget.
  - Start re-bakes from the record's last adopted extent when the UV identity is unchanged.

### N4 (Low). An asset in `Failed` state is classified as Busy, which wedges rebakes indefinitely

- **Anchor:** `TextureBakeModule.cpp:1955`, where every non-Ready state maps to `Busy`.
- **Reasoning:**
  - `AssetState::Failed` is reachable. `AssetLoadPipeline.cpp:217, 240, 307, 395` mark it on pipeline transition failure, and `AssetService.LoaderFailurePropagatesErrorAndMarksFailed` shows loaders mark it too.
  - Such a record then returns `JobSubmitFailed` ("still loading…") on every manual and automatic bake, forever. Appearance polls it every frame, which is cheap.
- **Minimal fix:** treat `Failed` as replaceable: destroy and create, or return `AssetLoadFailed` with a specific diagnostic. Keep `Busy` for `Unloaded`/`QueuedIO`/`LoadedCPU`/`QueuedGPU`.

### N5 (Low; a verification need). The anchored sample's margin is not quantization-aware, so GPU coverage parity is unproven

- **Anchors:** `Geometry.UvAtlas.cpp:1019-1044` anchors the largest-area triangle's **centroid**. The CPU coverage predicates (`Validation.cpp:45-80`, strict interior; `PropertyTextureBake.cpp:148-186`, top-left rule) use exact doubles.
- **Reasoning:**
  - For a thin triangle, the centroid's edge distance is about one third of its altitude. In the new unit test that is roughly 0.04 texel.
  - Final float rounding adds up to about 1e-3 texel at 16384. The validator re-checks it, which is correctly truthful.
  - The GPU rasterizer also snaps vertices to 2^-subPixelPrecisionBits: 1/256 is typical; Vulkan's minimum is 1/16.
  - Because anchoring deliberately creates single, marginal samples, "every chart covers ≥1 texel" is a CPU prediction that the GPU may not honour.
  - Gutter texels would usually still carry the value, but the coverage mask would miss that chart.
- **Improvement:** anchor at the incenter of the triangle with the largest inradius. Require a sample margin of at least 1/16 texel, or the device's subpixel step, in both CPU predicates.
- **Verification:** add a Vulkan readback of the thin-chart fixture checking coverage > 0 for every chart.

### N6 (Low; a quality note). The texel reservation costs density on fragmented atlases

- `TryPackCharts` adds one texel per axis to **every** cell, and `upper` subtracts one texel (`Geometry.UvAtlas.cpp:895-897, 968`).
- The requested gutters are preserved: the shift lies in [0, 1) texel, and each side keeps ≥ padding. Bounds stay within `1 − kPackMargin`. Monotone rounding keeps neighbours from overlapping. The anchoring logic itself is correct.
- But with thousands of small charts (for example single-triangle fallbacks), the overhead approaches 1/(chart side + 2·padding) per axis.
- The doc's "preserves … the common density" is true only in the uniform-density sense; absolute texels-per-unit decreases.
- **Option:** reserve only for charts whose largest-inradius triangle is below about 0.71 texel, since larger ones always contain a centre.

### N7 (Low; semantics). "Preserve valid authored UVs" never preserves corner-authored UVs

- After M1, when `h:texcoord` exists (OBJ imports publish authored UVs there), `PreserveValidAuthoredUvs=true` with `ForceRegenerate=false` silently regenerates and replaces the artist's corner UVs. Undo can recover them.
- **Minimal fix:** in that case, validate `h:texcoord` with the existing corner validator. If valid, report `AuthoredPreserved` and publish nothing; otherwise, say so in the diagnostic.

### N8 (Low; docs). `src/runtime/README.md:1767-1782` still describes the removed path

- Stale statements:
  - "two-texel dilation";
  - "raw-float bakes keep padding at zero";
  - GpuGeometryResidencyView revalidation;
  - "extent-keyed dilation scratch … serializes one submission per frame".
- `runtime.md:908` still mentions "dilation" state.
- Add the adaptive-extent, rejected-retry and pending-unbind behaviour there.

### N9 (Low; performance). The validator region union allocates a hash map per edge

- `Validation.cpp:477`: `unordered_map<uint64, unordered_map<uint32, uint32>>` means at least two heap allocations per source edge. That is millions of allocations for 1 M-face atlases, on every generation.
- Use a flat map keyed by `(edgeKey, label)` or a sorted vector of tuples. The semantics stay the same.

### N10 (Low; evidence binding). The corpus results are not bound to `d32aa779c`

- The `atlas-corpus-2` results record `source.revision = c97235568`, `state = dirty_worktree`, `diff_sha256 = 01f9d0b…`.
- The summary was written at 01:22; the snapshot was committed at 01:31.
- `check_property_atlas.py:111` hashes `git diff HEAD --binary`, which **omits untracked files**. The METHOD-047 sources `Geometry.UvAtlas.ChartSolve.{cpp,cppm}` and `Geometry.UvAtlas.Validation.{cpp,cppm}` are additions relative to `c97235568`, so unless they were staged, the diff hash does not cover them.
- `runner_sha256` binds the binary but not its source.
- Neither `git diff c97235568 d32aa779c` (`dfc327…`) nor its `src/geometry` subset (`4165ce…`) matches `01f9d0b…`.
- Results: 35/35 cells pass, strict validation passes, and every result has `claim_eligible: false`. These are consistent with "local correctness diagnostics", but not with evidence *for this revision*.
- **Fix:** hash tracked plus untracked content (for example `git add -N` first, or hash `git ls-files -o --exclude-standard` contents), or re-run from the clean commit.
- **Recalculation:** `child-4096-independent-audit.json` independently recomputes conformal (max 1.578, mean 1.014), area (max 1.373) and region crossings (0) on 100 000 faces. Overlap and texel-centre checks come from the native validator, as the file states. It is also dirty-worktree evidence.

## Verified with no defect found

- **Anchoring geometry:**
  - The shift `ceil(p−½)+½−p` lies in [0, 1) texel.
  - Origin = cursor + padding, so the chart occupies [cell + p + s, cell + p + s + e·scale], with s < texel. Both gutters stay ≥ p and the shelf limits hold.
  - Rotation uses the same `ChartLocalUv` as `PlaceUv`.
  - The validator independently re-tests strict texel-centre coverage on the final float UVs, so an anchoring failure is reported as `UnderResolved`, not hidden.
  - The regression test's thin chart is about 0.12 texel tall with about 6.6 texel² area, which exercises exactly the pre-fix failure.
- **Adaptive loop:**
  - The termination bound `W > Max/2` gives 1024→8192 in three doublings, and the aspect ratio is preserved.
  - Overlap fails at any extent.
  - The adopted extent flows to the metadata (`:2147`), the record (`:2280`) and `Work` (`:2319`). Every later GPU allocation uses `work.Width/Height`; `Work.Request` keeps the original request, and only identity uses it.
- **Token gate:**
  - It watches both texcoord domains when unnamed, plus positions, the halfedge/face topology arrays, the edge endpoints for the edge domain, and the source property.
  - Config changes go through `matchesConfig`.
  - The failure branch sets `Source` and `Texcoords={}` before computing the token, so the gate is consistent.
  - A successful schedule constructs a fresh record, which resets `RejectedRevisionToken` to 0.
  - `Unknown` never binds.
- **Corner stale-job and undo guards:**
  - `BitEqual` compares float bit patterns, so NaN boundary slots compare equal and ±0 compare unequal.
  - Apply now compares against the captured *source* property sets, not the converted `BeforeMesh`.
  - The test covers both edit→`StaleDiscarded` and unchanged-NaN→undo restoration.
- **Import Optional paths:**
  - Failure keeps the source topology and invents no UVs.
  - `ResolvedTexcoordsValid` reflects only payload-authored UVs.
  - The model path still records UV diagnostics.
- **UI and caps:**
  - The bake UI clamps to `kPropertyTextureBakeMaxExtent`.
  - An explicit warning appears when a successful atlas exceeds the cap.
- **Unchanged since review1 and not re-reviewed:** coverage independent of value alpha, and no fill pass in baked mode. Review1 verified both.

## Remaining verification the root owns (none run by me)

1. The fixture lifetime fix (not in `d32aa779c`), then a re-run of `bake-integrated-focused` and the complete `full-cpu` gate at the final revision.
2. Full ASan and UBSan over runtime and contract (only `IntrinsicGeometryTests.Grouped` under ASan is evidenced, 44.01 s).
3. An opt-in Vulkan run of the bake readbacks, including a thin-chart coverage readback (N5), an 8192 adaptive allocation (N3), and the pair-reload completion diagnostics.
4. Regressions for N1, N2 and N4.
5. Corpus evidence re-bound to the final revision (N10).

Until these run, keep the maturity at CPU-contracted. Do not claim performance, universal mesh reliability, or Operational GPU behaviour.
