# METHOD-047 independent review 1 — findings

- Reviewer: Claude Opus 5.5 (`claude-opus-5-5`), independent adversarial correctness/architecture review.
- Revision under review: `947438ca9e6dd4aaac845909ed68677667b96b27` ("METHOD-047 fixed source snapshot for independent review"),
  worktree `/tmp/intrinsic-method047-review1`. Parent: `c97235568`.
- Scope: `git diff c97235568 HEAD`: 95 files, +12005/−2661. I read these in full or in targeted sections:
  - geometry: `Geometry.UvAtlas.cpp`, `UvAtlas.ChartSolve.cpp`, `UvAtlas.Validation.cpp`, `Parameterization.Optimize.cpp`, and the LSCM direct-solve change;
  - runtime: UV worker, publication and undo (`Runtime.GeometryProcessingOperations.Uv.cpp`), import materialization, the TextureBake module (Prepare/Schedule/Record/Drain/Snapshot/freshness/appearance replay), RenderExtraction gating, AssetWorkflow reconciliation, scene-rect plumbing (`Runtime.Module`, `EditorUiHost`, `Engine`, `SceneInteractionModule`), and the config codec;
  - graphics: the bake shaders and CPU oracle, UV view shaders, push constants and fill ordering, FrameRecipe/Renderer placement, and ImGui after Present;
  - app: the atlas workspace, tabs, split pane, atlas controls and shell observer;
  - docs and ADRs.
- Constraints honoured: no source edits, no builds, no test runs and no agents. Only these review files were written.
- Evidence consulted (read-only): collaboration handoffs and progress notes, plus the root logs `full-cpu-2.log`, `combined-focused-4.log` and `packing-regression-before.log`.

## Verdict

The geometry core is careful and mostly sound: the exact validator, common-density packing and truthful objective/fallback reporting hold up. The texture-bake and UV-view contracts hold up well too.

There are 3 high-severity problems:
1. Property-only UV publication corrupts polygon (quad/n-gon) meshes.
2. Import-time UV resolution regresses: it now fails hard with no xatlas fallback.
3. The current full CPU gate is red on an existing contract test because `Unknown` freshness has inconsistent semantics.

There are also 3 medium and 7 low findings. The snapshot is not merge-ready until H1–H3 are resolved.

---

## High

### H1. Polygon faces: published corner UVs and `f:atlas_*` are corrupted when a polygon's fan triangles land in different charts

- Anchors:
  - fan triangulation of the soup: `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.MeshSupport.cpp:186-191`;
  - the same fan in the corner topology: `src/runtime/GeometryIntegration/Runtime.MeshSurfaceTopology.cpp:221-247`;
  - publication back to source slots: `src/runtime/Editor/Operations/Runtime.GeometryProcessingOperations.Uv.cpp:394-405` (`(*output)[faces[face]] = labels[face]`) and `:414-431` (`(*result.CornerUvs)[halfedges[sourceCorner]] = uv[h.Index]`).
- How polygon meshes arise in-editor: Catmull–Clark subdivide publishes quads (`Runtime.MeshTopologyOperations.Topology.cpp:1527-1534`). `WalkMeshSoupFaces` accepts any valid polygon ring.
- Reasoning:
  - The atlas is solved on the fan-triangulated soup. For quad ring `[r0,r1,r2,r3]`, the triangles are `t0=(r0,r1,r2)` and `t1=(r0,r2,r3)`. They share the chartable diagonal `(r0,r2)`.
  - `GrowCharts` normal-cone limits, guide labels and `SplitChart` refinements can all cut exactly along that diagonal. With vertex guides, each triangle's feature is the mean of 3 *different* vertices, so the two triangles can get different labels.
  - Publication maps each triangle corner to the source halfedge whose to-vertex matches. The halfedges of `r0` and `r2` are written twice, and the last write wins. The quad ends up with `h(r0)=uvB(r0), h(r1)=uvA(r1), h(r2)=uvB(r2), h(r3)=uvB(r3)`.
  - Rendering and the UV view re-fan the quad from these halfedges. `t0` then spans two charts (a stretched triangle across the atlas). `f:atlas_chart`/`f:atlas_region` record only `t1`'s chart.
  - The independent validator only validated the per-triangle `SourceCornerUvs`. Nothing re-validates the polygon-level publication, so the result is reported as Success.
  - Baseline replaced the mesh with the triangulated topology, so this is new with the property-only publication.
- Repro sketch: Catmull–Clark a cube once (quads), then Generate atlas (FastStaged/Both, or any vertex guide with `RegionCount=2`). For every quad, compare its 4 source halfedge `h:texcoord` values with the atlas `SourceCornerUvs` of both fan triangles. Any quad whose triangles have different `SourceFaceChart` shows the mismatch. A subsequent bake reports `OverlappingUvCharts`.
- Minimal fix: in `MapUvPublishedProperties`, record written halfedges and face labels. If a source halfedge receives two different UVs, or a source face receives two different chart/region ids, return `nullopt` with an explicit diagnostic, e.g. "polygon face split across atlas charts; triangulate or regenerate". Better options:
  - keep fan triangles of one source face together in charting (an atomic-group constraint honoured by `GrowCharts`/`MergeSmallProposals`/`SplitChart`); or
  - publish triangulated topology when polygons exist.
- Add a quad-mesh regression test for this.

### H2. Import-time UV resolution regression: hard failure without xatlas fallback, including any degenerate triangle

- Anchors:
  - degenerate rejection: `src/geometry/Geometry.UvAtlas.cpp:209-228`, now in `PreflightGeneration` for both backends; baseline failed only when *all* faces were degenerate;
  - no fallback for that status: `:1966-1982` (`IsInputRejection` includes `DegenerateInput`);
  - fallback blocked for non-Angle objectives: `:2429-2436`;
  - import selects `Both` for FastStaged: `src/runtime/AssetWorkflow/Runtime.AssetWorkflowGeometryMaterialization.cpp:403-405`;
  - the Required default policy: `Runtime.AssetWorkflowGeometryMaterialization.cppm:29-30`;
  - hard error: `.cpp:826-829`;
  - model scenes propagate the error: `Runtime.AssetWorkflowModelMaterialization.cpp:548-552`;
  - direct-mesh enrichment is dropped: `Runtime.AssetWorkflowRecipePolicies.cpp:885-891` and `:973-984`.
- Reasoning:
  - At baseline, any FastStaged failure fell back to xatlas, which tolerated partially degenerate input. Import therefore produced UVs.
  - Now a single zero-area triangle makes generation fail with `DegenerateInput`, and fallback is suppressed. This happens even when authored `v:texcoord` exists: `ValidateAuthoredUvs` rejects authored UVs because of the degenerate *position* face (absolute `1e-12` area in `EvaluateParameterizationDiagnostics`), and generation then fails too.
  - Every other FastStaged failure (`UnderResolved`/`QualityLimitNotMet`/`ResourceLimitExceeded`) also has no fallback, because import requests `Both`.
  - Under `FailurePolicy::Required`:
    - model-scene materialization fails wholesale;
    - direct mesh import loses enrichment ("Direct mesh enrichment failed…").
  - The geometry handoff's own 100k "child" probe (3 sub-texel charts at 1024) is a concrete input that would now fail import.
  - The import job also ignores `JobCancellation` (`[state](const JobCancellation&)`) while running the much heavier LSCM+SLIM path.
- Minimal fix (a policy decision the root should make explicitly), for example:
  - use `Angle` for import FastStaged so the truthful xatlas fallback still applies; and/or
  - use `FailurePolicy::Optional` for enrichment, keeping geometry and normals and reporting "no UVs" with the diagnostic.
  - Degenerate faces need a defined import behaviour, e.g. exclude them from charting and report `InvalidFaces`, rather than failing the whole asset.
- Add import regressions: a mesh with 1 degenerate triangle, and a mesh FastStaged rejects at 1024.

### H3. Full CPU gate red: `AssetWorkflowModule.CallerOwnedBakeReconciliationIsAtomicAndPreservesUnrelatedChannels` fails because `Unknown` freshness semantics are inconsistent

- Evidence: root `full-cpu-2.log` (1/4993 failed) and `combined-focused-4.log` show this existing, unmodified test failing at `tests/contract/runtime/Test.AssetWorkflowModule.cpp:1181-1191`. `Albedo`/`Normal` are unbound and the range/colormap are defaults.
- Root cause: `src/runtime/AssetWorkflow/Runtime.AssetWorkflowModule.cpp:470` treats `Freshness != Fresh` as Stale and clears the target. The test's caller-owned records (Ready, no fingerprints) have `Unknown`. Other consumers disagree about `Unknown`:
  - `IsPropertyTextureBakeFreshnessStale(Unknown) == false` (`Runtime.TextureBakeModule.cppm:192-197`);
  - RenderExtraction requires `Fresh` (`TextureBakeModule.cpp:800-809`);
  - tabs map `Unknown` to Stale (`EditorFeatureContextAdapters.cpp:2301-2310`);
  - appearance replay treats `Unknown` as *not* stale, so it neither rebakes nor binds (`TextureBakeModule.cpp:2962-2970`).
- Fix: decide one contract and apply it everywhere.
  - Either records without identity are "unverified" and never bindable: update the RUNTIME-191 test and doc, label them "unverified", and make appearance replay rebake them.
  - Or caller-owned identity-less records stay bindable: exempt `Unknown` in reconciliation and extraction.

## Medium

### M1. A shadowed ("ghost") `v:texcoord` is accepted as authored while the canonical `h:texcoord` exists, and publication then deletes `h:texcoord`

- Anchors:
  - the authored candidate is always `v:texcoord`: `Runtime.GeometryProcessingOperations.Uv.cpp:1291-1298`;
  - preservation, with weak admission (flips, overlap and out-of-bounds all pass): `Geometry.UvAtlas.cpp:2365-2378` and `:2237-2282`;
  - the no-seam publish removes `h:texcoord`: `Uv.cpp:174-188`;
  - applied to the source: `Uv.cpp:458`.
- Reasoning: the renderer's authority is corner-over-vertex. Any path that leaves both properties makes `v:texcoord` a shadow — e.g. a standalone parameterization writing `v:texcoord` with `CornerTexcoordsToRetire` null, which is the default. A UV regeneration with `PreserveValidAuthoredUvs=true, ForceRegenerate=false` then:
  - reports `AuthoredPreserved` ("preserved valid authored texcoords");
  - publishes the shadow per-vertex UVs;
  - removes the corner authority, so the visible UVs change.
- Reachable via the "Preserve valid authored" + "Force regenerate" checkboxes (`Sandbox.PanelSupport.cpp:1498-1514`) and the command API. The atlas action itself sends `false/true` and is unaffected.
- Fix: if a complete `h:texcoord` exists, do not offer `v:texcoord` as authored. Either force generation, or treat "preserve" as "keep the canonical corner UVs untouched and publish nothing". Consider running at least orientation/bounds validation before claiming preservation.

### M2. `split_ratio` float/double boundary makes a documented-valid value unusable and blocks later config applies

- Anchors:
  - the UI clamps to `0.8f`: `Sandbox.MethodPanels.cpp:3461-3465, 3495-3502`;
  - serialization writes the float: `Runtime.FeatureConfigCodecs.Detail.cpp:2621`;
  - validation is an inclusive double range: `:1944-1950`, with `ReadNumber` at `:476-485`;
  - the stored ratio is copied into every draft: `Sandbox.MethodPanels.cpp:3264-3273, 3278-3284`.
- Reasoning: `0.8f` becomes 0.800000011920929 in JSON, which is greater than 0.8, so the section is Invalid.
  - Dragging the splitter to its limit shows "Layout change rejected".
  - A config loaded with `"split_ratio": 0.8` is accepted (0.8 ≤ 0.8) but stored as `0.8f`. Every later `ApplyEditorParameterizationConfig` then fails, including `ApplyParameterizationDraft` before "Generate atlas" (`:3330-3338`) and the auto-open `Texcoords` switch.
- Fix: validate after float conversion (`static_cast<float>(v) >= 0.2f && <= 0.8f`), or keep the ratio as `double`. Add round-trip tests at both bounds.

### M3. Automatic appearance bakes run at a fixed 1024², now reject the whole bake on any unresolved chart, and never retry

- Anchors:
  - the request has no extent, so it defaults to 1024: `Runtime.TextureBakeModule.cpp:2972-2984`;
  - whole-bake `UnderresolvedAtlas`: `:1696-1708`;
  - on failure, fingerprints are cleared and freshness becomes `Unknown`, so it is never retried: `:3004-3011` and `:2962-2970`.
- Reasoning: this breaks in 2 cases.
  - The atlas UI advises "increase the atlas resolution" for `UnderResolved` atlases. An atlas accepted at 2048/4096 can then have charts with no texel centre at 1024, so the appearance bake fails permanently until the config changes.
  - Authored/imported UVs with one tiny island now lose the whole appearance texture (baseline rendered with a hole).
- Fix: bake appearance at the atlas extent. Record it when publishing, or reuse the extent adoption the asset workflow already has (runtime.md:859-861). Or retry at a larger extent, or allow documented partial coverage for appearance bakes.

## Low

- **L1: tab selection resets on state changes.** Labels are `name + " (state)" + "##tab<idx>"` (`Sandbox.MethodPanels.cpp:3613-3617`). ImGui hashes the full string, so the ID changes on pending→ready, ready→stale and so on, and the selected tab falls back to "Atlas". Use `"###"` plus a stable key (the output name).
- **L2: accepted atlases can be un-bakeable.** The bake rejects UV triangles with `|2A| <= 1e-10` in normalized UV units (`TextureBakeModule.cpp:67, 1401-1414`). The atlas validator accepts any positive exact float orientation (`Validation.cpp:420-433`), so fine or sliver regions of a valid atlas can fail the bake wholesale. Reject only non-positive exact orientation in the bake, reusing `ExactUvOrientation`.
- **L3: raw vector/normal tabs clamp negative components.** Prepare forces `RangeMin/Max = 0/1` for non-scalars (`TextureBakeModule.cpp:1844-1848`). `VectorRange` maps each component through that range (`EditorFeatureContextAdapters.cpp:1624-1648`, `background.frag:64-72, 94-95`). Signed vectors, e.g. raw `v:normal`, therefore show negatives as 0, while the legend says "range [0, 1]". Use a symmetric data range, or [-1, 1] for Normal.
- **L4: authored-preserved results show distortion "0 / 0".** The validator is not evaluated for them (`Uv.cpp:718-721`, `Sandbox.PanelSupport.cpp:1452-1454`), so the zeros read as perfect. Show "not evaluated" when `Validation.Evaluated` is false.
- **L5: main/coverage asset creation is not atomic.** In `Schedule`, the main asset is created or reloaded before the coverage asset (`TextureBakeModule.cpp:2016-2046`). If coverage creation fails: a new main asset leaks, or the replaced record's asset has already been reloaded with the new metadata. A coverage-only completion failure is reported as "completion became stale" (`:2526-2531`).
- **L6: doc drift.** These still describe the removed alpha dilation or renderer-residency bake path and "at most one bake per frame" (the code now records up to 4 and owns its own buffers):
  - `docs/architecture/graphics.md:260`;
  - `docs/architecture/runtime.md:862-898`;
  - `src/runtime/README.md:67` ("dilation").

  AGENTS.md §9 requires current-state docs.
- **L7 (nit): region-component definitions differ.** The validator unites each face only with the *first* face on an edge (`Validation.cpp:477-496`). `AnalyzeTopology` unites all pairs (`UvAtlas.cpp:358-366`). On non-manifold edges with mixed labels, `report.RegionComponentCount` and `Diagnostics.RegionComponentCount` / `f:atlas_region` can disagree. Generated atlases are unaffected thanks to the cut-partner rule. Also a nit: `IsAvailableBackgroundTexture`'s comment says slot 0 is invalid, but the check permits 0 (`Graphics.UvView.cpp:27-35`).

## Optional improvements (not defects)

- **Segmentation fragmentation.** Guide segmentation disables small-region cleanup (`MinimumRegionFaces = 1`, `Uv.cpp:931`; the kernel default is 2). Noisy guides then force single-face regions, and therefore charts, which tends to end in `UnderResolved`. `MaxEmIterations` is also tied to the optimizer budget (`:932`). Consider separate knobs and attributing the failure to the guide.
- **Float precision.** Slivers above the `1e-12` relative degeneracy threshold, and tiny faces near the distortion limit, pass the chart gate and then fail final float validation for the whole atlas. The fixed `kChartLimitMargin=1e-3` is smaller than the quantization error of sub-texel faces. A preflight float-representability check or a quantization-aware gate would make these failures actionable earlier.
- **Optimizer reporting.** `RunSlim`/`RunAreaPriority` count solver or line-search failure after `Ran` as "unconverged", like iteration exhaustion. Distinguish them in diagnostics.
- **Cancellation gaps.** `SegmentFaceFeatures`, packing and validation are not cancellable. Only chart solves and optimizer iterations poll the flag.
- **Extent mismatch.** The atlas allows up to 16384, but bakes cap at 8192, so an atlas needing 16384 cannot be baked at native resolution.
- **Bake-catalog persistence.** Bake catalogs remain runtime-only, as at baseline; the README documents this. The baking plan mentioned persisting records, so confirm that is intentionally out of scope.

## Checked, no defect found

- Exact binary32 orientation via products that are exact in double, with a Shewchuk grow-expansion; the sign is taken from the top nonzero component.
- SAT "supporting edge line weakly separates" is exact for open interiors.
- The BVH query clears its output.
- Density equivalence: unit-density charts plus one common pack scale equals the validator's global density.
- Packing: rotations are proper (det +1), and zero-padding neighbours stay non-overlapping under monotone rounding.
- Corner-fan local vertices remove bowties, so the Euler/disk test is meaningful.
- Split pieces are connected, strictly smaller, terminating and budgeted.
- The area-priority gradient checks out: d ln detJ/dJ = J⁻ᵀ; d(‖J‖²/2detJ)/dJ = J/detJ − M·J⁻ᵀ; r = M + √(M² − 1).
- SimplicialLDLT uses AMD ordering.
- xatlas transpose-mirroring is avoided. The fallback runs only for Angle and is reported as requested/actual/fallback. `FinalizeGenerated` enforces objective identity.
- The uint64 guide exactness check holds, and guide slots map correctly for vertex and face domains.
- Undo snapshots restore raw property vectors.
- `f:atlas_*` scene round-trip.
- Revision tokens are globally monotonic and rebased on move.
- Bake and display orientation: the RHI negative-height viewport makes `ndc.y = 1 − 2v` give row 0 at v = 0. This is consistent across the bake, CPU oracle, the gutter `gl_FragCoord` and the UV-view `texelFetch`.
- Coverage is independent of value alpha. Zero and negative raw values are preserved.
- Gutter depth orders interior before nearest-edge gutter.
- Both GPU generations must be Ready before the record becomes Ready. Failure fails both.
- The fill pass is skipped in baked mode.
- The 64-byte push constants match the GLSL block byte-for-byte.
- Present clears the whole backbuffer and then draws the scene rect.
- The ImGui-after-Present pipeline uses the backbuffer-format variant and a top-left scissor. The colour path is unchanged from baseline.
- Pick, gizmo and camera use rect-local HiDPI-scaled pixels, and a hidden editor claims no rect.
- `TextureTabs` is guarded after detach.
- Tab gating compares `{MeshVertex, v:texcoord, Vec2}` refs consistently.
- No world-space ImGui overlays depend on the full window.

## Residual limits and verification not available

- I did not build or run anything, per instructions. The Vulkan, ASan and UBSan gate results were unavailable to me.
- The root's latest full CPU log shows the H3 failure. Other gates may still expose issues.
- Acknowledged root in-flight work, not re-reported here:
  - the corner-UV async job guard (queued for review2);
  - the thin-chart packing texel-centre regression (`packing-regression-before.log`, "1 charts contain no texel center").
- No claim is made about universal mesh reliability or speed. Numerical conclusions above come from code analysis, not new measurements.
